#include "structural_controller.hpp"
#include "project_controller.hpp"

#include "prometheus/structural/structural_setup.hpp"
#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_archive.hpp"
#include "prometheus/decision/project_summary.hpp"
#include "prometheus/run_store/run_store.hpp"
#include "prometheus/run_store/object_store.hpp"
#include "prometheus/run_store/structural_archive_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <unordered_map>
#include <limits>

#include <QVector3D>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>
#include <QUuid>

namespace ps = prometheus::structural;
namespace decision = prometheus::decision;

namespace {

ps::RequirementCriticality parseCriticality(const QString &value) {
  if (value == "critical") return ps::RequirementCriticality::critical;
  if (value == "informational") return ps::RequirementCriticality::informational;
  return ps::RequirementCriticality::advisory;
}

QString criticalityLabel(const ps::RequirementCriticality value) {
  const auto label = ps::to_string(value);
  return QString::fromUtf8(label.data(), static_cast<qsizetype>(label.size()));
}

QString dispositionLabel(const ps::StructuralFindingDisposition value) {
  switch (value) {
  case ps::StructuralFindingDisposition::violated: return "violated";
  case ps::StructuralFindingDisposition::cannot_answer: return "cannot_answer";
  case ps::StructuralFindingDisposition::no_violation_detected_within_scope:
    return "no_violation_detected_within_scope";
  }
  return "no_violation_detected_within_scope";
}

QString verdictLabel(const decision::Verdict value) {
  switch (value) {
  case decision::Verdict::satisfied_within_scope: return "satisfied_within_scope";
  case decision::Verdict::requirements_violated: return "requirements_violated";
  case decision::Verdict::indeterminate: return "indeterminate";
  }
  return "indeterminate";
}

QString coverageLabel(const decision::Coverage value) {
  switch (value) {
  case decision::Coverage::sufficient: return "sufficient";
  case decision::Coverage::insufficient: return "insufficient";
  case decision::Coverage::not_assessed: return "not_assessed";
  }
  return "not_assessed";
}

QString executionStateLabel(const decision::ExecutionState value) {
  switch (value) {
  case decision::ExecutionState::not_started: return "not_started";
  case decision::ExecutionState::ready: return "ready";
  case decision::ExecutionState::running: return "running";
  case decision::ExecutionState::blocked: return "blocked";
  case decision::ExecutionState::completed: return "completed";
  case decision::ExecutionState::completed_with_blocked_work:
    return "completed_with_blocked_work";
  case decision::ExecutionState::failed: return "failed";
  case decision::ExecutionState::cancelled: return "cancelled";
  }
  return "failed";
}

struct DesktopRunResult final {
  ps::SolverRunResult run;
  ps::StructuralEvaluation evaluation;
  QString output_directory;
  std::optional<ps::StructuralArchive> archive;
  std::string archive_error;
};

struct DesktopStructuralCommitResult final {
  std::optional<prometheus::run_store::ProjectV2> project;
  bool already_committed{};
  std::string manifest_hash;
  std::string error;
};

struct DesktopStructuralRestoreResult final {
  ps::VolumeMesh mesh;
  std::vector<ps::BoundaryFace> boundary;
  std::optional<ps::CalculixMetrics> metrics;
  QString manifest_path;
  QString output_directory;
  std::string setup_bytes;
  std::string archive_bytes;
  std::string error;
};

QString runStatus(const ps::SolverRunStatus status) {
  switch (status) {
  case ps::SolverRunStatus::completed: return "completed";
  case ps::SolverRunStatus::launch_failed: return "launch_failed";
  case ps::SolverRunStatus::timed_out: return "timed_out";
  case ps::SolverRunStatus::nonzero_exit: return "nonzero_exit";
  case ps::SolverRunStatus::output_conflict: return "output_conflict";
  case ps::SolverRunStatus::output_missing: return "output_missing";
  case ps::SolverRunStatus::result_invalid: return "result_invalid";
  }
  return "unknown";
}

QVariantList ids(const std::vector<int> &values) {
  QVariantList result;
  for (const int value : values) result.append(value);
  return result;
}

std::vector<int> sortedUnique(std::vector<int> values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

std::optional<double> positiveOptional(const QVariantMap &draft,
                                       const char *key) {
  const double value = draft.value(key).toDouble();
  return value > 0.0 ? std::optional<double>(value) : std::nullopt;
}

std::optional<prometheus::run_store::StoredObjectReference>
storedReference(const QJsonObject &object) {
  const auto hash = object.value("object_hash").toString();
  const auto media = object.value("media_type").toString();
  const auto schema = object.value("schema_id").toString();
  const auto version = object.value("schema_version").toString();
  const auto length = object.value("byte_length").toDouble(-1.0);
  if (hash.isEmpty() || media.isEmpty() || schema.isEmpty() || version.isEmpty() ||
      length <= 0.0 || length > 9007199254740991.0)
    return std::nullopt;
  return prometheus::run_store::StoredObjectReference{
      hash.toStdString(), static_cast<std::uint64_t>(length),
      media.toStdString(), schema.toStdString(), version.toStdString()};
}

} // namespace

StructuralResultGeometry::StructuralResultGeometry(
    const ps::VolumeMesh &mesh,
    const std::vector<ps::BoundaryFace> &boundary,
    const ps::CalculixMetrics &fields, const double deformationScale,
    QObject *parent)
    : QQuick3DGeometry(nullptr) {
  setParent(parent);
  std::unordered_map<int, ps::Node> nodes;
  for (const auto &node : mesh.nodes) nodes.emplace(node.id, node);
  std::unordered_map<int, ps::NodalDisplacement> displacement;
  for (const auto &row : fields.displacements)
    displacement.emplace(row.node_id, row);
  std::unordered_map<int, double> stress;
  for (const auto &row : fields.stresses) {
    auto [entry, inserted] = stress.emplace(row.element_id, row.von_mises_pa);
    if (!inserted) entry->second = std::max(entry->second, row.von_mises_pa);
  }
  const auto color = [&](const double value) {
    const auto ratio = fields.maximum_von_mises_pa > 0.0
        ? std::clamp(value / fields.maximum_von_mises_pa, 0.0, 1.0) : 0.0;
    // Perceptually ordered blue -> cyan -> yellow -> red ramp.
    if (ratio < 0.333333) {
      const auto t = ratio * 3.0;
      return std::array<float, 4>{0.05F, static_cast<float>(0.25 + 0.7 * t),
                                  static_cast<float>(0.85 + 0.15 * t), 1.0F};
    }
    if (ratio < 0.666667) {
      const auto t = (ratio - 0.333333) * 3.0;
      return std::array<float, 4>{static_cast<float>(0.05 + 0.9 * t), 0.95F,
                                  static_cast<float>(1.0 - 0.9 * t), 1.0F};
    }
    const auto t = (ratio - 0.666667) * 3.0;
    return std::array<float, 4>{0.95F, static_cast<float>(0.95 - 0.85 * t),
                                0.1F, 1.0F};
  };
  std::vector<float> vertices;
  std::vector<std::uint32_t> indices;
  vertices.reserve(boundary.size() * 3U * 10U);
  indices.reserve(boundary.size() * 3U);
  QVector3D minimum(std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
  QVector3D maximum(std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest());
  for (const auto &face : boundary) {
    std::array<QVector3D, 3> positions;
    for (std::size_t index = 0; index < 3U; ++index) {
      const auto node = nodes.find(face.node_ids[index]);
      if (node == nodes.end())
        throw std::runtime_error("result face references an unknown mesh node");
      auto position = node->second.position_m;
      if (const auto moved = displacement.find(node->first);
          moved != displacement.end()) {
        position[0] += deformationScale * moved->second.x_m;
        position[1] += deformationScale * moved->second.y_m;
        position[2] += deformationScale * moved->second.z_m;
      }
      positions[index] = QVector3D(static_cast<float>(position[0] * 1000.0),
                                   static_cast<float>(position[1] * 1000.0),
                                   static_cast<float>(position[2] * 1000.0));
      minimum.setX(std::min(minimum.x(), positions[index].x()));
      minimum.setY(std::min(minimum.y(), positions[index].y()));
      minimum.setZ(std::min(minimum.z(), positions[index].z()));
      maximum.setX(std::max(maximum.x(), positions[index].x()));
      maximum.setY(std::max(maximum.y(), positions[index].y()));
      maximum.setZ(std::max(maximum.z(), positions[index].z()));
    }
    const auto normal = QVector3D::crossProduct(positions[1] - positions[0],
                                                 positions[2] - positions[0])
                            .normalized();
    const auto stressValue = stress.contains(face.source_element_id)
        ? stress.at(face.source_element_id) : 0.0;
    const auto rgba = color(stressValue);
    for (const auto &position : positions) {
      vertices.insert(vertices.end(),
          {position.x(), position.y(), position.z(), normal.x(), normal.y(),
           normal.z(), rgba[0], rgba[1], rgba[2], rgba[3]});
      indices.push_back(static_cast<std::uint32_t>(indices.size()));
    }
  }
  clear();
  setStride(10 * sizeof(float));
  setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
  addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
               QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float),
               QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, 6 * sizeof(float),
               QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
               QQuick3DGeometry::Attribute::U32Type);
  setVertexData(QByteArray(reinterpret_cast<const char *>(vertices.data()),
                           static_cast<qsizetype>(vertices.size() * sizeof(float))));
  setIndexData(QByteArray(reinterpret_cast<const char *>(indices.data()),
                          static_cast<qsizetype>(indices.size() * sizeof(std::uint32_t))));
  if (!vertices.empty()) setBounds(minimum, maximum);
}

StructuralController::StructuralController(ProjectController *project,
                                           QObject *parent)
    : QObject(parent), project_(project) {
  if (project_) {
    connect(project_, &ProjectController::projectOpened,
            this, &StructuralController::reloadProject);
    connect(project_, &ProjectController::projectSaved,
            this, &StructuralController::reloadProject);
    connect(project_, &ProjectController::assemblyArtifactInvalidated, this,
            [this] {
      compiled_request_.reset();
      compiled_requirements_.clear();
      compiled_setup_evidence_.clear();
      can_run_ = false;
      const auto present = std::any_of(
          blockers_.cbegin(), blockers_.cend(), [](const QVariant &value) {
            return value.toMap().value("code").toString() ==
                   "source_artifact_changed";
          });
      if (!present)
        blockers_.append(QVariantMap{
            {"code", "source_artifact_changed"},
            {"message", "The accounted CAD bytes changed. Historical results remain viewable, but geometry and selected load/restraint surfaces must be reviewed before rerun."}});
      emit changed();
    });
  }
  rebuildPreview();
  reloadProject();
}

void StructuralController::commitLastRun() {
  if (busy_) return;
  if (!project_ || project_->currentProjectPath().isEmpty()) {
    error_ = "Open or save a Prometheus project before committing this run.";
    emit changed();
    return;
  }
  const auto manifestText = last_run_.value("archive_manifest").toString();
  if (manifestText.isEmpty()) {
    error_ = "A completed verified structural archive is required.";
    emit changed();
    return;
  }
  const auto manifestPath = std::filesystem::path(manifestText.toStdWString());
  const auto projectPath = project_->projectPath();
  const auto assemblyHash = project_->project()->assembly_artifact_hash;
  busy_ = true;
  status_ = "publishing_structural_archive";
  error_.clear();
  emit changed();
  auto *watcher = new QFutureWatcher<DesktopStructuralCommitResult>(this);
  connect(watcher, &QFutureWatcher<DesktopStructuralCommitResult>::finished,
          this, [this, watcher] {
    const auto completed = watcher->result();
    watcher->deleteLater();
    busy_ = false;
    if (!completed.project) {
      error_ = QString::fromStdString(completed.error);
      last_run_["project_anchored"] = false;
      status_ = "structural_archive_publication_failed";
    } else {
      project_->acceptProject(*completed.project);
      last_run_["project_anchored"] = true;
      last_run_["project_artifacts_embedded"] = true;
      last_run_["project_manifest_hash"] =
          QString::fromStdString(completed.manifest_hash);
      last_run_["project_already_committed"] = completed.already_committed;
      status_ = "structural_archive_published";
      reloadProject();
    }
    emit changed();
  });
  watcher->setFuture(QtConcurrent::run(
      [manifestPath, projectPath, assemblyHash] {
        const auto verified = ps::verify_structural_archive(manifestPath);
        if (!verified.valid)
          return DesktopStructuralCommitResult{
              std::nullopt, false, {}, verified.code + ": " + verified.detail};
        auto objects = prometheus::run_store::build_structural_archive_objects(
            manifestPath, assemblyHash);
        if (!objects.has_value())
          return DesktopStructuralCommitResult{
              std::nullopt, false, {}, objects.diagnostic().code + ": " +
                  objects.diagnostic().message};
        const auto manifestHash =
            objects.value().project_manifest.reference.object_hash;
        auto published = prometheus::run_store::publish_structural_archive(
            projectPath, objects.value());
        if (!published.has_value())
          return DesktopStructuralCommitResult{
              std::nullopt, false, {}, published.diagnostic().code + ": " +
                  published.diagnostic().message};
        return DesktopStructuralCommitResult{
            published.value().project, published.value().already_committed,
            manifestHash, {}};
      }));
}

void StructuralController::reloadProject() {
  stored_runs_.clear();
  if (!project_ || !project_->project()) {
    emit changed();
    return;
  }
  for (const auto &reference : project_->project()->execution.committed_runs) {
    if (reference.schema_id ==
        prometheus::run_store::structural_manifest_schema_id) {
      stored_runs_.append(QVariantMap{
          {"project_manifest_hash", QString::fromStdString(reference.object_hash)},
          {"status", "legacy_manifest_only"},
          {"restorable", false}});
      continue;
    }
    if (reference.schema_id !=
        prometheus::run_store::structural_project_run_schema_id)
      continue;
    QVariantMap display{
        {"project_manifest_hash", QString::fromStdString(reference.object_hash)},
        {"status", "embedded"}, {"restorable", true}};
    const auto projectManifest = prometheus::run_store::read_object(
        project_->projectPath(), reference);
    if (!projectManifest.has_value()) {
      display["status"] = "unavailable";
      display["restorable"] = false;
      display["error"] = QString::fromStdString(projectManifest.diagnostic().message);
      stored_runs_.append(display);
      continue;
    }
    QJsonParseError parseError;
    const auto projectDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(projectManifest.value()), &parseError);
    const auto archiveReference = projectDocument.isObject()
        ? storedReference(projectDocument.object().value("archive_manifest").toObject())
        : std::nullopt;
    if (parseError.error != QJsonParseError::NoError || !archiveReference) {
      display["status"] = "unavailable";
      display["restorable"] = false;
      display["error"] = "Embedded structural project manifest is invalid.";
      stored_runs_.append(display);
      continue;
    }
    const auto boundAssembly =
        projectDocument.object().value("assembly_artifact_hash").toString();
    const auto sourceCurrent =
        boundAssembly.toStdString() == project_->project()->assembly_artifact_hash;
    display["assembly_artifact_hash"] = boundAssembly;
    display["source_current"] = sourceCurrent;
    if (!sourceCurrent) display["status"] = "stale_source_changed";
    const auto archive = prometheus::run_store::read_object(
        project_->projectPath(), *archiveReference);
    if (archive.has_value()) {
      const auto archiveDocument = QJsonDocument::fromJson(
          QByteArray::fromStdString(archive.value()));
      const auto root = archiveDocument.object();
      display["analysis_id"] = root.value("analysis_id").toString();
      display["component_name"] = root.value("component_name").toString();
      display["maximum_displacement_m"] =
          root.value("metrics").toObject().value("maximum_displacement_m").toDouble();
      display["maximum_von_mises_pa"] =
          root.value("metrics").toObject().value("maximum_von_mises_pa").toDouble();
    }
    stored_runs_.append(display);
  }
  emit changed();
}

void StructuralController::restoreStoredRun(const int index,
                                            const QUrl &outputRoot) {
  if (busy_ || !project_ || !project_->project() || index < 0 ||
      index >= stored_runs_.size())
    return;
  const auto selected = stored_runs_.at(index).toMap();
  if (!selected.value("restorable").toBool()) {
    error_ = "The selected structural history entry has no embedded artifacts.";
    emit changed();
    return;
  }
  const auto rootPath = outputRoot.toLocalFile();
  const auto sourceCurrent = selected.value("source_current", true).toBool();
  if (rootPath.isEmpty()) {
    error_ = "Select an output folder for the reconstructed archive.";
    emit changed();
    return;
  }
  QDir root(rootPath);
  if (!root.exists() && !QDir().mkpath(rootPath)) {
    error_ = "The structural restore directory could not be created.";
    emit changed();
    return;
  }
  const auto hash = selected.value("project_manifest_hash").toString().toStdString();
  const auto reference = std::ranges::find_if(
      project_->project()->execution.committed_runs,
      [&](const auto &candidate) { return candidate.object_hash == hash; });
  if (reference == project_->project()->execution.committed_runs.end()) {
    error_ = "The selected structural history reference is no longer current.";
    emit changed();
    return;
  }
  const auto destination = root.filePath(
      "restored-structural-" + QDateTime::currentDateTimeUtc().toString(
          "yyyyMMdd-HHmmss-zzz-") + QUuid::createUuid().toString(QUuid::Id128).left(8));
  const auto projectPath = project_->projectPath();
  const auto stored = *reference;
  busy_ = true;
  status_ = "restoring_structural_archive";
  error_.clear();
  emit changed();
  auto *watcher = new QFutureWatcher<DesktopStructuralRestoreResult>(this);
  connect(watcher, &QFutureWatcher<DesktopStructuralRestoreResult>::finished,
          this, [this, watcher, sourceCurrent] {
    auto restored = watcher->result();
    watcher->deleteLater();
    busy_ = false;
    if (!restored.error.empty() || !restored.metrics) {
      error_ = QString::fromStdString(restored.error);
      status_ = "structural_archive_restore_failed";
      emit changed();
      return;
    }
    mesh_ = std::move(restored.mesh);
    boundary_ = std::move(restored.boundary);
    const auto setup = QJsonDocument::fromJson(
        QByteArray::fromStdString(restored.setup_bytes)).object();
    const auto patchAngle = setup.value("selection_patch_angle_degrees").toDouble(15.0);
    patches_ = ps::group_boundary_faces(boundary_, patchAngle);
    const auto selectedFaces = [&](const char *role) {
      std::set<std::array<int, 3>> result;
      const auto faces = setup.value(role).toObject().value("selection")
                             .toObject().value("face_node_ids").toArray();
      for (const auto &value : faces) {
        const auto row = value.toArray();
        if (row.size() != 3) continue;
        std::array<int, 3> face{row[0].toInt(), row[1].toInt(), row[2].toInt()};
        std::ranges::sort(face);
        result.insert(face);
      }
      return result;
    };
    const auto loadFaces = selectedFaces("load");
    const auto restraintFaces = selectedFaces("restraint");
    const auto selectedPatchIds = [&](const std::set<std::array<int, 3>> &faces) {
      std::vector<int> ids;
      std::size_t covered = 0U;
      for (const auto &patch : patches_) {
        const auto contained = std::ranges::all_of(
            patch.face_node_ids, [&](auto face) {
              std::ranges::sort(face);
              return faces.contains(face);
            });
        if (contained) {
          ids.push_back(patch.id);
          covered += patch.face_node_ids.size();
        }
      }
      if (covered != faces.size()) return std::vector<int>{};
      return ids;
    };
    load_patch_ids_ = selectedPatchIds(loadFaces);
    restraint_patch_ids_ = selectedPatchIds(restraintFaces);
    if (load_patch_ids_.empty() || restraint_patch_ids_.empty()) {
      error_ = "Stored reviewed surface selections do not map to restored patches.";
      status_ = "structural_archive_restore_failed";
      emit changed();
      return;
    }
    mesh_summary_.clear();
    surface_patches_.clear();
    double exteriorArea = 0.0;
    for (const auto &face : boundary_) exteriorArea += face.area_m2;
    mesh_summary_ = {{"nodes", static_cast<qlonglong>(mesh_.nodes.size())},
                     {"elements", static_cast<qlonglong>(mesh_.elements.size())},
                     {"exterior_faces", static_cast<qlonglong>(boundary_.size())},
                     {"surface_patches", static_cast<qlonglong>(patches_.size())},
                     {"patch_angle_degrees", patchAngle},
                     {"exterior_area_m2", exteriorArea},
                     {"coordinate_scale_to_m", 1.0}};
    for (const auto &patch : patches_) {
      surface_patches_.append(QVariantMap{
          {"id", patch.id},
          {"face_count", static_cast<qlonglong>(patch.face_node_ids.size())},
          {"node_count", static_cast<qlonglong>(patch.node_ids.size())},
          {"area_m2", patch.area_m2},
          {"centroid_x_m", patch.area_weighted_centroid_m[0]},
          {"centroid_y_m", patch.area_weighted_centroid_m[1]},
          {"centroid_z_m", patch.area_weighted_centroid_m[2]},
          {"normal_x", patch.representative_unit_normal[0]},
          {"normal_y", patch.representative_unit_normal[1]},
          {"normal_z", patch.representative_unit_normal[2]}});
    }
    const auto material = setup.value("material").toObject();
    const auto load = setup.value("load").toObject();
    const auto restraint = setup.value("restraint").toObject();
    const auto meshControls = setup.value("mesh_controls").toObject();
    const auto scenario = setup.value("scenario").toObject();
    const auto force = load.value("total_force_n").toArray();
    double displacementLimit = 0.0;
    double vonMisesLimit = 0.0;
    QString requirementRationale;
    bool requirementReviewed = false;
    QString requirementApplicability;
    QString requirementCriticality = "advisory";
    QString otherRequirementDescription;
    QString otherRequirementUnit;
    double otherRequirementLimit = 0.0;
    // uncovered_requirements_ is rebuilt by rebuildPreview() below from these
    // draft_ fields, mirroring how live review derives it.
    for (const auto &value : setup.value("requirements").toArray()) {
      const auto entry = value.toObject();
      const auto quantity = entry.value("quantity").toString();
      requirementRationale = entry.value("source_or_exploratory_rationale").toString();
      requirementReviewed = entry.value("reviewed").toBool();
      requirementApplicability = entry.value("applicability").toString();
      requirementCriticality = entry.value("criticality").toString();
      if (quantity == "displacement") {
        displacementLimit = entry.value("limit_value").toDouble();
      } else if (quantity == "von_mises_stress") {
        vonMisesLimit = entry.value("limit_value").toDouble();
      } else {
        otherRequirementDescription = entry.value("other_quantity_description").toString();
        otherRequirementUnit = entry.value("unit").toString();
        otherRequirementLimit = entry.value("limit_value").toDouble();
      }
    }
    draft_ = {
        {"restored", true}, {"analysis_id", setup.value("analysis_id").toString()},
        {"component_name", setup.value("component_name").toString()},
        {"geometry_sha256", setup.value("geometry_sha256").toString()},
        {"material_designation", material.value("designation").toString()},
        {"material_source_sha256", material.value("source_sha256").toString()},
        {"material_applicability", material.value("applicability").toString()},
        {"youngs_modulus_pa", material.value("youngs_modulus_pa").toDouble()},
        {"poisson_ratio", material.value("poisson_ratio").toDouble()},
        {"material_reviewed", material.value("reviewed").toBool()},
        {"force_x_n", force.at(0).toDouble()},
        {"force_y_n", force.at(1).toDouble()},
        {"force_z_n", force.at(2).toDouble()},
        {"load_reviewed", load.value("reviewed").toBool()},
        {"restraint_reviewed", restraint.value("reviewed").toBool()},
        {"displacement_limit_m", displacementLimit},
        {"von_mises_limit_pa", vonMisesLimit},
        {"requirement_rationale", requirementRationale},
        {"requirement_reviewed", requirementReviewed},
        {"requirement_applicability", requirementApplicability},
        {"requirement_criticality", requirementCriticality},
        {"other_requirement_description", otherRequirementDescription},
        {"other_requirement_unit", otherRequirementUnit},
        {"other_requirement_limit_value", otherRequirementLimit},
        {"mesh_minimum_size_m", meshControls.value("minimum_size_m").toDouble()},
        {"mesh_maximum_size_m", meshControls.value("maximum_size_m").toDouble()},
        {"mesher_identity", meshControls.value("mesher_identity").toString()},
        {"mesh_controls_reviewed", meshControls.value("reviewed").toBool()},
        {"scenario_description", scenario.value("description").toString()},
        {"scenario_confirmed", scenario.value("confirmed").toBool()}};
    rebuildPreview();
    if (!sourceCurrent) {
      compiled_request_.reset();
      compiled_requirements_.clear();
      compiled_setup_evidence_.clear();
      can_run_ = false;
      blockers_.append(QVariantMap{
          {"code", "source_artifact_changed"},
          {"message", "The project assembly identity changed after this structural run. Historical evidence remains viewable, but rerun is blocked until geometry and selections are reviewed again."}});
    }
    if (result_geometry_) result_geometry_->deleteLater();
    double minimumX = std::numeric_limits<double>::max();
    double minimumY = minimumX, minimumZ = minimumX;
    double maximumX = std::numeric_limits<double>::lowest();
    double maximumY = maximumX, maximumZ = maximumX;
    for (const auto &node : mesh_.nodes) {
      minimumX = std::min(minimumX, node.position_m[0]);
      minimumY = std::min(minimumY, node.position_m[1]);
      minimumZ = std::min(minimumZ, node.position_m[2]);
      maximumX = std::max(maximumX, node.position_m[0]);
      maximumY = std::max(maximumY, node.position_m[1]);
      maximumZ = std::max(maximumZ, node.position_m[2]);
    }
    const auto diagonal = std::hypot(maximumX - minimumX,
                                     maximumY - minimumY,
                                     maximumZ - minimumZ);
    const auto scale = restored.metrics->maximum_displacement_m > 0.0
        ? std::clamp(0.1 * diagonal /
                         restored.metrics->maximum_displacement_m,
                     1.0, 1.0e6)
        : 1.0;
    result_geometry_ = new StructuralResultGeometry(
        mesh_, boundary_, *restored.metrics, scale, this);
    result_view_ = {{"center_x_mm", 500.0 * (minimumX + maximumX)},
                    {"center_y_mm", 500.0 * (minimumY + maximumY)},
                    {"center_z_mm", 500.0 * (minimumZ + maximumZ)},
                    {"radius_mm", std::max(1.0, 0.55 * diagonal * 1000.0)},
                    {"deformation_scale", scale}, {"color_min_pa", 0.0},
                    {"color_max_pa", restored.metrics->maximum_von_mises_pa}};
    findings_.clear();
    const auto archive = QJsonDocument::fromJson(
        QByteArray::fromStdString(restored.archive_bytes)).object();
    for (const auto &value : archive.value("findings").toArray()) {
      const auto finding = value.toObject();
      findings_.append(QVariantMap{
          {"obligation", finding.value("obligation").toString()},
          {"disposition", finding.value("disposition").toString()},
          {"measured", finding.value("measured").toDouble()},
          {"limit", finding.value("limit").toDouble()},
          {"margin", finding.value("margin").toDouble()},
          {"unit", finding.value("unit").toString()},
          {"scope", finding.value("scope").toString()}});
    }
    const auto maximumDisplacement = std::ranges::max_element(
        restored.metrics->displacements, {}, &ps::NodalDisplacement::magnitude_m);
    const auto maximumStress = std::ranges::max_element(
        restored.metrics->stresses, {}, &ps::ElementStress::von_mises_pa);
    last_run_ = {{"status", "restored_verified"},
                 {"archive_manifest", restored.manifest_path},
                 {"output_directory", restored.output_directory},
                 {"project_anchored", true},
                 {"project_artifacts_embedded", true},
                 {"maximum_displacement_m",
                  restored.metrics->maximum_displacement_m},
                 {"maximum_von_mises_pa",
                  restored.metrics->maximum_von_mises_pa},
                 {"displacement_rows",
                  static_cast<qlonglong>(restored.metrics->displacements.size())},
                 {"stress_rows",
                  static_cast<qlonglong>(restored.metrics->stresses.size())}};
    if (maximumDisplacement != restored.metrics->displacements.end()) {
      last_run_["maximum_displacement_node_id"] = maximumDisplacement->node_id;
      last_run_["maximum_displacement_x_m"] = maximumDisplacement->x_m;
      last_run_["maximum_displacement_y_m"] = maximumDisplacement->y_m;
      last_run_["maximum_displacement_z_m"] = maximumDisplacement->z_m;
    }
    if (maximumStress != restored.metrics->stresses.end()) {
      last_run_["maximum_stress_element_id"] = maximumStress->element_id;
      last_run_["maximum_stress_integration_point"] =
          maximumStress->integration_point;
    }
    const auto coverage = archive.value("coverage").toObject();
    last_run_["declared_obligations"] =
        coverage.value("declared_obligations").toInt();
    last_run_["evaluated_obligations"] =
        coverage.value("evaluated_obligations").toInt();
    last_run_["limitation"] = archive.value("limitation").toString();
    status_ = sourceCurrent ? "structural_archive_restored"
                            : "structural_archive_restored_stale";
    error_.clear();
    emit changed();
  });
  watcher->setFuture(QtConcurrent::run(
      [projectPath, stored, destination] {
        try {
        const auto restored = prometheus::run_store::reconstruct_structural_archive(
            projectPath, stored,
            std::filesystem::path(destination.toStdWString()));
        if (!restored.has_value())
          return DesktopStructuralRestoreResult{
              {}, {}, std::nullopt, {}, destination, {}, {},
              restored.diagnostic().code + ": " + restored.diagnostic().message};
        const auto verification = ps::verify_structural_archive(restored.value());
        if (!verification.valid)
          return DesktopStructuralRestoreResult{
              {}, {}, std::nullopt, {}, destination, {}, {},
              verification.code + ": " + verification.detail};
        const auto manifestBytes = [&] {
          std::ifstream input(restored.value(), std::ios::binary);
          return std::string{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
        }();
        const auto manifest = QJsonDocument::fromJson(
            QByteArray::fromStdString(manifestBytes)).object();
        const auto artifact = [&](const char *role) {
          return manifest.value("artifacts").toObject().value(role).toObject()
              .value("file").toString();
        };
        const auto directory = restored.value().parent_path();
        std::ifstream deck(directory /
                           std::filesystem::path(artifact("deck").toStdWString()),
                           std::ios::binary);
        const std::string deckBytes{std::istreambuf_iterator<char>(deck),
                                    std::istreambuf_iterator<char>()};
        std::ifstream dat(directory /
                          std::filesystem::path(artifact("dat").toStdWString()),
                          std::ios::binary);
        const std::string datBytes{std::istreambuf_iterator<char>(dat),
                                   std::istreambuf_iterator<char>()};
        std::ifstream setup(directory /
                            std::filesystem::path(artifact("setup").toStdWString()),
                            std::ios::binary);
        const std::string setupBytes{std::istreambuf_iterator<char>(setup),
                                     std::istreambuf_iterator<char>()};
        auto mesh = ps::parse_gmsh_abaqus_mesh(deckBytes, 1.0);
        auto boundary = ps::extract_boundary_faces(mesh);
        auto metrics = ps::parse_calculix_dat(datBytes);
        return DesktopStructuralRestoreResult{
            std::move(mesh), std::move(boundary), std::move(metrics),
            QString::fromStdWString(restored.value().wstring()), destination,
            setupBytes, manifestBytes, {}};
        } catch (const std::exception &exception) {
          return DesktopStructuralRestoreResult{
              {}, {}, std::nullopt, {}, destination, {}, {}, exception.what()};
        } catch (...) {
          return DesktopStructuralRestoreResult{
              {}, {}, std::nullopt, {}, destination, {}, {},
              "unknown structural restore failure"};
        }
      }));
}

QVariantList StructuralController::selectedLoadPatchIds() const {
  return ids(load_patch_ids_);
}

QVariantList StructuralController::selectedRestraintPatchIds() const {
  return ids(restraint_patch_ids_);
}

void StructuralController::loadMesh(const QUrl &url,
                                    const double coordinateScaleToM,
                                    const double patchAngleDegrees) {
  if (busy_) {
    error_ = "Wait for the active structural execution before replacing its mesh.";
    emit changed();
    return;
  }
  reset();
  const auto local = url.toLocalFile();
  if (local.isEmpty()) {
    error_ = "A local Gmsh/Abaqus mesh path is required.";
    status_ = "mesh_failed";
    emit changed();
    return;
  }
  try {
    std::ifstream input(std::filesystem::path(local.toStdWString()), std::ios::binary);
    if (!input) throw std::runtime_error("The selected mesh could not be opened.");
    const std::string bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    mesh_ = ps::parse_gmsh_abaqus_mesh(bytes, coordinateScaleToM);
    boundary_ = ps::extract_boundary_faces(mesh_);
    patches_ = ps::group_boundary_faces(boundary_, patchAngleDegrees);
    double exteriorArea = 0.0;
    for (const auto &face : boundary_) exteriorArea += face.area_m2;
    mesh_summary_ = {{"nodes", static_cast<qlonglong>(mesh_.nodes.size())},
                     {"elements", static_cast<qlonglong>(mesh_.elements.size())},
                     {"exterior_faces", static_cast<qlonglong>(boundary_.size())},
                     {"surface_patches", static_cast<qlonglong>(patches_.size())},
                     {"patch_angle_degrees", patchAngleDegrees},
                     {"exterior_area_m2", exteriorArea},
                     {"coordinate_scale_to_m", coordinateScaleToM}};
    for (const auto &patch : patches_) {
      surface_patches_.append(QVariantMap{
          {"id", patch.id},
          {"face_count", static_cast<qlonglong>(patch.face_node_ids.size())},
          {"node_count", static_cast<qlonglong>(patch.node_ids.size())},
          {"area_m2", patch.area_m2},
          {"centroid_x_m", patch.area_weighted_centroid_m[0]},
          {"centroid_y_m", patch.area_weighted_centroid_m[1]},
          {"centroid_z_m", patch.area_weighted_centroid_m[2]},
          {"normal_x", patch.representative_unit_normal[0]},
          {"normal_y", patch.representative_unit_normal[1]},
          {"normal_z", patch.representative_unit_normal[2]}});
    }
    status_ = "setup_blocked";
    rebuildPreview();
  } catch (const std::exception &exception) {
    error_ = QString::fromUtf8(exception.what());
    status_ = "mesh_failed";
  }
  emit changed();
}

void StructuralController::setPatchSelected(const int patchId,
                                            const QString &role,
                                            const bool selected) {
  if (busy_) {
    error_ = "Wait for the active structural execution before changing its setup.";
    emit changed();
    return;
  }
  if (std::ranges::none_of(patches_, [&](const auto &patch) {
        return patch.id == patchId;
      })) {
    error_ = "The selected structural surface patch does not exist.";
    emit changed();
    return;
  }
  auto *target = role == "load" ? &load_patch_ids_
               : role == "restraint" ? &restraint_patch_ids_ : nullptr;
  if (!target) {
    error_ = "Structural patch role must be load or restraint.";
    emit changed();
    return;
  }
  if (selected) target->push_back(patchId);
  else target->erase(std::remove(target->begin(), target->end(), patchId), target->end());
  *target = sortedUnique(std::move(*target));
  error_.clear();
  rebuildPreview();
  emit changed();
}

void StructuralController::reviewSetup(const QVariantMap &draft) {
  if (busy_) {
    error_ = "Wait for the active structural execution before changing its setup.";
    emit changed();
    return;
  }
  draft_ = draft;
  last_run_.clear();
  findings_.clear();
  if (result_geometry_) result_geometry_->deleteLater();
  result_geometry_ = nullptr;
  result_view_.clear();
  rebuildPreview();
  persistRequirementBindingEdges();
  emit changed();
}

void StructuralController::persistRequirementBindingEdges() {
  // Phase 6 checkpoint 3: promotes each reviewed requirement from transient
  // display state into a real, persisted, append-only graph edge -- the
  // RequirementBinding analogue of EngineeringController::
  // persistJointBindingEdge. Best-effort and silent by the same contract:
  // a project that is not open or not writable must never discard the
  // reviewed requirements this session already established in draft_, and
  // must never surface an error of its own. Triggered only from the
  // explicit "review this setup" action, not from rebuildPreview() itself,
  // which also runs from patch-selection toggles, mesh loads, and history
  // restores -- none of which are a deliberate requirement review.
  if (!can_run_ || !compiled_request_ || project_ == nullptr ||
      !project_->project().has_value() || project_->saveAsRequired() ||
      !project_->executionStoreAvailable()) {
    return;
  }
  const auto geometry = compiled_request_->geometry_sha256;
  const auto analysisId = compiled_request_->analysis_id;
  // reviewSetup resubmits the whole reviewed-requirement set on every
  // "Validate and preview" click, not just the fields the user actually
  // changed -- unlike a joint or CAD binding, which is confirmed one
  // relationship at a time. Appending unconditionally here would spam the
  // graph with a redundant revision per requirement on every click, even
  // when nothing changed. A snapshot (not a reference: acceptProject below
  // replaces the live project state mid-loop) of the currently active
  // bindings lets each requirement supersede its prior revision only when
  // its reviewed content actually differs.
  const auto priorBindings =
      project_->project()->execution.requirement_bindings;
  std::set<std::uint64_t> superseded;
  for (const auto &binding : priorBindings) {
    if (binding.supersedes_binding_revision.has_value()) {
      superseded.insert(*binding.supersedes_binding_revision);
    }
  }
  for (const auto &requirement : compiled_requirements_) {
    const auto quantity = std::string(ps::to_string(requirement.quantity));
    const auto comparator = std::string(ps::to_string(requirement.comparator));
    const auto criticality = std::string(ps::to_string(requirement.criticality));
    const prometheus::run_store::RequirementBinding *active = nullptr;
    for (auto iterator = priorBindings.rbegin();
         iterator != priorBindings.rend(); ++iterator) {
      if (iterator->geometry_sha256 == geometry &&
          iterator->quantity == quantity &&
          iterator->other_quantity_description ==
              requirement.other_quantity_description &&
          !superseded.contains(iterator->binding_revision)) {
        active = &*iterator;
        break;
      }
    }
    if (active != nullptr && active->analysis_id == analysisId &&
        active->comparator == comparator &&
        active->limit_value == requirement.limit_value &&
        active->unit == requirement.unit &&
        active->applicability == requirement.applicability &&
        active->criticality == criticality &&
        active->source_or_exploratory_rationale ==
            requirement.source_or_exploratory_rationale) {
      continue;
    }
    const auto installed = prometheus::run_store::install_requirement_binding(
        project_->projectPath(),
        prometheus::run_store::RequirementBindingInput{
            geometry, analysisId, quantity,
            requirement.other_quantity_description, comparator,
            requirement.limit_value, requirement.unit,
            requirement.applicability, criticality,
            requirement.source_or_exploratory_rationale});
    if (installed.has_value()) {
      project_->acceptProject(installed.value());
    }
  }
}

void StructuralController::rebuildPreview() {
  blockers_.clear();
  request_preview_.clear();
  can_run_ = false;
  compiled_request_.reset();
  compiled_requirements_.clear();
  compiled_setup_evidence_.clear();
  uncovered_requirements_.clear();
  if (mesh_.nodes.empty() || patches_.empty()) {
    blockers_.append(QVariantMap{{"code", "mesh_required"},
                                 {"message", "Load a structural volume mesh first."}});
    status_ = "mesh_required";
    return;
  }
  std::vector<ps::ReviewedRequirement> requirements;
  {
    const auto applicability =
        draft_.value("requirement_applicability").toString().toStdString();
    const auto criticality =
        parseCriticality(draft_.value("requirement_criticality").toString());
    const auto rationale =
        draft_.value("requirement_rationale").toString().toStdString();
    const auto reviewed = draft_.value("requirement_reviewed").toBool();
    if (const auto limit = positiveOptional(draft_, "displacement_limit_m"))
      requirements.push_back({ps::RequirementQuantity::displacement, "",
                              ps::RequirementComparator::less_or_equal, *limit,
                              "m", applicability, criticality, rationale, reviewed});
    if (const auto limit = positiveOptional(draft_, "von_mises_limit_pa"))
      requirements.push_back({ps::RequirementQuantity::von_mises_stress, "",
                              ps::RequirementComparator::less_or_equal, *limit,
                              "Pa", applicability, criticality, rationale, reviewed});
    const auto otherDescription =
        draft_.value("other_requirement_description").toString().toStdString();
    if (!otherDescription.empty())
      requirements.push_back(
          {ps::RequirementQuantity::other, otherDescription,
           ps::RequirementComparator::less_or_equal,
           draft_.value("other_requirement_limit_value").toDouble(),
           draft_.value("other_requirement_unit").toString().toStdString(),
           applicability, criticality, rationale, reviewed});
  }
  for (const auto &requirement : requirements) {
    if (requirement.quantity != ps::RequirementQuantity::other) continue;
    uncovered_requirements_.append(QVariantMap{
        {"description", QString::fromStdString(requirement.other_quantity_description)},
        {"unit", QString::fromStdString(requirement.unit)},
        {"limit_value", requirement.limit_value},
        {"applicability", QString::fromStdString(requirement.applicability)},
        {"criticality", criticalityLabel(requirement.criticality)},
        {"rationale", QString::fromStdString(requirement.source_or_exploratory_rationale)},
        {"reviewed", requirement.reviewed}});
  }
  ps::BoundarySelection load;
  ps::BoundarySelection restraint;
  try {
    load = ps::resolve_boundary_selection("reviewed load surface", patches_,
                                          load_patch_ids_);
  } catch (const std::exception &exception) {
    blockers_.append(QVariantMap{{"code", "load_selection_invalid"},
                                 {"message", QString::fromUtf8(exception.what())}});
  }
  try {
    restraint = ps::resolve_boundary_selection("reviewed fixed surface", patches_,
                                               restraint_patch_ids_);
  } catch (const std::exception &exception) {
    blockers_.append(QVariantMap{{"code", "restraint_selection_invalid"},
                                 {"message", QString::fromUtf8(exception.what())}});
  }
  ps::StructuralSetup setup{
      .analysis_id = draft_.value("analysis_id").toString().toStdString(),
      .component_name = draft_.value("component_name").toString().toStdString(),
      .geometry_sha256 = draft_.value("geometry_sha256").toString().toStdString(),
      .mesh = mesh_,
      .boundary_faces = boundary_,
      .material = {draft_.value("material_designation").toString().toStdString(),
                   draft_.value("material_source_sha256").toString().toStdString(),
                   draft_.value("material_applicability").toString().toStdString(),
                   draft_.value("youngs_modulus_pa").toDouble(),
                   draft_.value("poisson_ratio").toDouble(),
                   draft_.value("material_reviewed").toBool()},
      .load = {std::move(load),
               {draft_.value("force_x_n").toDouble(),
                draft_.value("force_y_n").toDouble(),
                draft_.value("force_z_n").toDouble()},
               draft_.value("load_reviewed").toBool()},
      .restraint = {std::move(restraint),
                    draft_.value("restraint_reviewed").toBool()},
      .requirements = requirements,
      .mesh_controls = {draft_.value("mesh_minimum_size_m").toDouble(),
                        draft_.value("mesh_maximum_size_m").toDouble(),
                        draft_.value("mesher_identity").toString().toStdString(),
                        draft_.value("mesh_controls_reviewed").toBool()},
      .scenario_description = draft_.value("scenario_description").toString().toStdString(),
      .scenario_confirmed = draft_.value("scenario_confirmed").toBool()};
  setup.selection_patch_angle_degrees =
      mesh_summary_.value("patch_angle_degrees", 15.0).toDouble();
  for (const auto &issue : ps::validate_setup(setup))
    blockers_.append(QVariantMap{{"code", QString::fromStdString(issue.code)},
                                 {"message", QString::fromStdString(issue.message)}});
  QVariantList uniqueBlockers;
  std::set<QString> blockerCodes;
  for (const auto &value : blockers_) {
    const auto blocker = value.toMap();
    if (blockerCodes.insert(blocker.value("code").toString()).second)
      uniqueBlockers.append(blocker);
  }
  blockers_ = std::move(uniqueBlockers);
  if (!blockers_.isEmpty()) {
    status_ = "setup_blocked";
    return;
  }
  try {
    const auto request = ps::compile_structural_request(setup);
    compiled_request_ = request;
    compiled_requirements_ = requirements;
    compiled_setup_evidence_ = ps::serialize_structural_setup_evidence(setup);
    request_preview_ = {{"analysis_id", QString::fromStdString(request.analysis_id)},
                        {"component_name", QString::fromStdString(request.component_name)},
                        {"nodes", static_cast<qlonglong>(request.nodes.size())},
                        {"elements", static_cast<qlonglong>(request.elements.size())},
                        {"fixed_nodes", static_cast<qlonglong>(request.fully_fixed_node_ids.size())},
                        {"loaded_nodes", static_cast<qlonglong>(request.nodal_forces.size())},
                        {"load_faces", static_cast<qlonglong>(setup.load.selection.face_node_ids.size())},
                        {"restraint_faces", static_cast<qlonglong>(setup.restraint.selection.face_node_ids.size())}};
    can_run_ = true;
    status_ = "ready_for_execution";
  } catch (const std::exception &exception) {
    blockers_.append(QVariantMap{{"code", "request_invalid"},
                                 {"message", QString::fromUtf8(exception.what())}});
    status_ = "setup_blocked";
  }
}

void StructuralController::runAnalysis(const QUrl &calculixExecutable,
                                       const QUrl &outputRoot) {
  if (busy_) return;
  if (!can_run_ || !compiled_request_) {
    error_ = "The reviewed structural setup is not ready for execution.";
    emit changed();
    return;
  }
  const QString executablePath = calculixExecutable.toLocalFile();
  const QString rootPath = outputRoot.toLocalFile();
  if (executablePath.isEmpty() || !QFileInfo::exists(executablePath) ||
      rootPath.isEmpty()) {
    error_ = "Select a local CalculiX executable and output directory.";
    emit changed();
    return;
  }
  QDir root(rootPath);
  if (!root.exists() && !QDir().mkpath(rootPath)) {
    error_ = "The structural output directory could not be created.";
    emit changed();
    return;
  }
  const QString runName = "structural-" +
      QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz-") +
      QUuid::createUuid().toString(QUuid::Id128).left(8);
  if (!root.mkdir(runName)) {
    error_ = "A unique structural run directory could not be created.";
    emit changed();
    return;
  }
  const QString runDirectory = root.filePath(runName);
  constexpr auto jobName = "prometheus_structural_run";
  std::ofstream deck(std::filesystem::path(runDirectory.toStdWString()) /
                         (std::string(jobName) + ".inp"),
                     std::ios::binary);
  deck << ps::generate_calculix_deck(*compiled_request_);
  deck.close();
  if (!deck) {
    error_ = "The exact structural solver deck could not be written.";
    emit changed();
    return;
  }
  const auto request = *compiled_request_;
  const auto setupEvidence = compiled_setup_evidence_;
  error_.clear();
  last_run_.clear();
  findings_.clear();
  busy_ = true;
  status_ = "executing";
  emit changed();

  auto *watcher = new QFutureWatcher<DesktopRunResult>(this);
  connect(watcher, &QFutureWatcher<DesktopRunResult>::finished, this,
          [this, watcher] {
    const auto completed = watcher->result();
    watcher->deleteLater();
    busy_ = false;
    last_run_ = {{"status", runStatus(completed.run.status)},
                 {"exit_code", completed.run.exit_code},
                 {"elapsed_ms", static_cast<qlonglong>(completed.run.elapsed.count())},
                 {"detail", QString::fromStdString(completed.run.detail)},
                 {"stdout", QString::fromStdString(completed.run.standard_output)},
                 {"stderr", QString::fromStdString(completed.run.standard_error)},
                 {"output_directory", completed.output_directory},
                 {"archived", completed.archive.has_value()},
                 {"declared_obligations", completed.evaluation.declared_obligations},
                 {"evaluated_obligations", completed.evaluation.evaluated_obligations},
                 {"limitation", QString::fromStdString(completed.evaluation.limitation)}};
    if (completed.archive) {
      last_run_["archive_manifest"] = QString::fromStdWString(
          completed.archive->manifest_path.wstring());
      last_run_["archive_sha256"] =
          QString::fromStdString(completed.archive->manifest_sha256);
    } else if (!completed.archive_error.empty()) {
      last_run_["archive_error"] =
          QString::fromStdString(completed.archive_error);
    }
    if (completed.run.metrics) {
      const auto &displacements = completed.run.metrics->displacements;
      const auto maximumDisplacement = std::ranges::max_element(
          displacements, {}, &ps::NodalDisplacement::magnitude_m);
      const auto &stresses = completed.run.metrics->stresses;
      const auto maximumStress = std::ranges::max_element(
          stresses, {}, &ps::ElementStress::von_mises_pa);
      last_run_["maximum_displacement_m"] =
          completed.run.metrics->maximum_displacement_m;
      last_run_["maximum_von_mises_pa"] =
          completed.run.metrics->maximum_von_mises_pa;
      last_run_["displacement_rows"] = static_cast<qlonglong>(displacements.size());
      last_run_["stress_rows"] = static_cast<qlonglong>(stresses.size());
      if (maximumDisplacement != displacements.end()) {
        last_run_["maximum_displacement_node_id"] = maximumDisplacement->node_id;
        last_run_["maximum_displacement_x_m"] = maximumDisplacement->x_m;
        last_run_["maximum_displacement_y_m"] = maximumDisplacement->y_m;
        last_run_["maximum_displacement_z_m"] = maximumDisplacement->z_m;
      }
      if (maximumStress != stresses.end()) {
        last_run_["maximum_stress_element_id"] = maximumStress->element_id;
        last_run_["maximum_stress_integration_point"] = maximumStress->integration_point;
      }
      double minimumX = std::numeric_limits<double>::max();
      double minimumY = std::numeric_limits<double>::max();
      double minimumZ = std::numeric_limits<double>::max();
      double maximumX = std::numeric_limits<double>::lowest();
      double maximumY = std::numeric_limits<double>::lowest();
      double maximumZ = std::numeric_limits<double>::lowest();
      for (const auto &node : mesh_.nodes) {
        minimumX = std::min(minimumX, node.position_m[0]);
        minimumY = std::min(minimumY, node.position_m[1]);
        minimumZ = std::min(minimumZ, node.position_m[2]);
        maximumX = std::max(maximumX, node.position_m[0]);
        maximumY = std::max(maximumY, node.position_m[1]);
        maximumZ = std::max(maximumZ, node.position_m[2]);
      }
      const auto diagonal = std::hypot(maximumX - minimumX,
                                       maximumY - minimumY,
                                       maximumZ - minimumZ);
      const auto deformationScale = completed.run.metrics->maximum_displacement_m > 0.0
          ? std::clamp(0.1 * diagonal /
                           completed.run.metrics->maximum_displacement_m,
                       1.0, 1.0e6)
          : 1.0;
      if (result_geometry_) result_geometry_->deleteLater();
      result_geometry_ = new StructuralResultGeometry(
          mesh_, boundary_, *completed.run.metrics, deformationScale, this);
      const auto radiusMm = std::max(1.0, 0.55 * diagonal * 1000.0);
      result_view_ = {
          {"center_x_mm", 500.0 * (minimumX + maximumX)},
          {"center_y_mm", 500.0 * (minimumY + maximumY)},
          {"center_z_mm", 500.0 * (minimumZ + maximumZ)},
          {"radius_mm", radiusMm},
          {"deformation_scale", deformationScale},
          {"color_min_pa", 0.0},
          {"color_max_pa", completed.run.metrics->maximum_von_mises_pa}};
    }
    for (const auto &finding : completed.evaluation.findings) {
      findings_.append(QVariantMap{
          {"obligation", QString::fromStdString(finding.obligation)},
          {"disposition", dispositionLabel(finding.disposition)},
          {"measured", finding.measured_value},
          {"limit", finding.limit_value},
          {"margin", finding.margin_to_limit},
          {"unit", QString::fromStdString(finding.unit)},
          {"scope", QString::fromStdString(finding.scope)}});
    }
    if (compiled_request_) {
      decision::Counts counts;
      for (const auto &finding : completed.evaluation.findings) {
        switch (finding.disposition) {
        case ps::StructuralFindingDisposition::no_violation_detected_within_scope:
          ++counts.satisfied_within_scope; break;
        case ps::StructuralFindingDisposition::violated:
          ++counts.violated; break;
        case ps::StructuralFindingDisposition::cannot_answer:
          ++counts.not_evaluated; break;
        }
      }
      counts.not_applicable =
          static_cast<std::uint64_t>(uncovered_requirements_.size());
      const auto executionState = completed.run.status != ps::SolverRunStatus::completed
          ? decision::ExecutionState::failed
          : uncovered_requirements_.isEmpty()
                ? decision::ExecutionState::completed
                : decision::ExecutionState::completed_with_blocked_work;
      const auto total = counts.satisfied_within_scope + counts.violated +
          counts.indeterminate + counts.not_applicable + counts.not_evaluated;
      if (total > 0) {
        const auto summary = decision::summarize(
            counts, executionState, total, compiled_request_->geometry_sha256);
        last_run_["assessment"] = QVariantMap{
            {"verdict", verdictLabel(summary.verdict)},
            {"coverage", coverageLabel(summary.coverage)},
            {"execution_state", executionStateLabel(summary.execution_state)}};
      }
    }
    status_ = completed.run.status == ps::SolverRunStatus::completed
        ? "execution_completed" : "execution_failed";
    emit changed();
    emit runFinished();
  });
  watcher->setFuture(QtConcurrent::run(
      [request, setupEvidence, executablePath, runDirectory]() -> DesktopRunResult {
        auto run = ps::run_calculix({
            std::filesystem::path(executablePath.toStdWString()),
            std::filesystem::path(runDirectory.toStdWString()),
            "prometheus_structural_run", std::chrono::minutes(5)});
        if (run.status == ps::SolverRunStatus::completed && run.metrics) {
          const auto binding =
              ps::validate_calculix_result_binding(request, *run.metrics);
          if (!binding.empty()) {
            run.status = ps::SolverRunStatus::result_invalid;
            run.detail = binding.front().code + ": " + binding.front().message;
            run.metrics.reset();
          }
        }
        const auto evaluation = ps::compile_structural_findings(request, run);
        std::optional<ps::StructuralArchive> archive;
        std::string archiveError;
        if (run.status == ps::SolverRunStatus::completed) {
          try {
            archive = ps::write_structural_archive(
                std::filesystem::path(runDirectory.toStdWString()),
                "prometheus_structural_run", executablePath.toStdString(),
                setupEvidence, request, run, evaluation);
          } catch (const std::exception &error) {
            archiveError = error.what();
          }
        }
        return {run, evaluation, runDirectory, std::move(archive),
                std::move(archiveError)};
      }));
}

void StructuralController::reset() {
  if (busy_) {
    error_ = "Wait for the active structural execution before resetting its setup.";
    emit changed();
    return;
  }
  status_ = "mesh_required";
  error_.clear();
  mesh_summary_.clear();
  surface_patches_.clear();
  draft_.clear();
  blockers_.clear();
  request_preview_.clear();
  can_run_ = false;
  busy_ = false;
  last_run_.clear();
  findings_.clear();
  compiled_request_.reset();
  compiled_requirements_.clear();
  compiled_setup_evidence_.clear();
  uncovered_requirements_.clear();
  if (result_geometry_) result_geometry_->deleteLater();
  result_geometry_ = nullptr;
  result_view_.clear();
  mesh_ = {};
  boundary_.clear();
  patches_.clear();
  load_patch_ids_.clear();
  restraint_patch_ids_.clear();
  rebuildPreview();
  emit changed();
}
