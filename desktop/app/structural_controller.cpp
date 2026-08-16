#include "structural_controller.hpp"
#include "project_controller.hpp"

#include "prometheus/integrity/canonical_json.hpp"
#include "prometheus/run_store/object_store.hpp"
#include "prometheus/run_store/run_store.hpp"
#include "prometheus/run_store/structural_archive_store.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QUuid>
#include <QVector3D>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ps = prometheus::structural;
namespace run_store = prometheus::run_store;

namespace {

constexpr qint64 maximumMeshBytes = 512LL * 1024LL * 1024LL;
constexpr qint64 maximumMaterialEvidenceBytes = 4LL * 1024LL * 1024LL;
constexpr qsizetype maximumMaterialCandidates = 4096;

struct DesktopRunCompletion final {
  DesktopStructuralRun result;
  QString output_directory;
};

struct DesktopStructuralCommitResult final {
  std::optional<run_store::ProjectV2> project;
  bool already_committed{};
  std::string manifest_hash;
  std::string error;
};

struct DesktopStructuralRestoreResult final {
  ps::StructuralArchiveVerification verification;
  QString manifest_path;
  QString output_directory;
  std::string error;
};

std::filesystem::path native_path(const QString &path) {
#ifdef Q_OS_WIN
  return std::filesystem::path(path.toStdWString());
#else
  const auto bytes = path.toUtf8();
  return std::filesystem::path(
      std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
#endif
}

QString qt_path(const std::filesystem::path &path) {
#ifdef Q_OS_WIN
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native().c_str());
#endif
}

std::string read_regular_file(const QString &path, const qint64 maximumBytes) {
  const QFileInfo information(path);
  if (!information.isAbsolute() || !information.exists() ||
      !information.isFile() || information.isSymbolicLink() ||
      information.size() < 0 || information.size() > maximumBytes)
    throw std::invalid_argument(
        "input must be an absolute bounded regular non-symlink file");
  QFile input(information.absoluteFilePath());
  if (!input.open(QIODevice::ReadOnly))
    throw std::runtime_error("input file could not be opened");
  const auto bytes = input.readAll();
  if (input.error() != QFileDevice::NoError ||
      bytes.size() != information.size())
    throw std::runtime_error("input file could not be read exactly");
  return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QString run_status(const ps::SolverRunStatus status) {
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

std::vector<int> sorted_unique(std::vector<int> values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

std::array<int, 3> canonical_face(std::array<int, 3> face) {
  std::ranges::sort(face);
  return face;
}

std::optional<double> positive_optional(const QVariantMap &draft,
                                        const char *key) {
  const double value = draft.value(key).toDouble();
  return std::isfinite(value) && value > 0.0
             ? std::optional<double>(value)
             : std::nullopt;
}

std::optional<run_store::StoredObjectReference>
stored_reference(const QJsonObject &object) {
  const auto hash = object.value("object_hash").toString();
  const auto media = object.value("media_type").toString();
  const auto schema = object.value("schema_id").toString();
  const auto version = object.value("schema_version").toString();
  const auto length = object.value("byte_length").toDouble(-1.0);
  if (hash.isEmpty() || media.isEmpty() || schema.isEmpty() ||
      version.isEmpty() || length <= 0.0 || length > 9007199254740991.0)
    return std::nullopt;
  return run_store::StoredObjectReference{
      hash.toStdString(), static_cast<std::uint64_t>(length),
      media.toStdString(), schema.toStdString(), version.toStdString()};
}

QVariantMap patch_map(const ps::SurfacePatch &patch) {
  return {{"id", patch.id},
          {"face_count", static_cast<qlonglong>(patch.face_node_ids.size())},
          {"node_count", static_cast<qlonglong>(patch.node_ids.size())},
          {"area_m2", patch.area_m2},
          {"centroid_x_m", patch.area_weighted_centroid_m[0]},
          {"centroid_y_m", patch.area_weighted_centroid_m[1]},
          {"centroid_z_m", patch.area_weighted_centroid_m[2]},
          {"normal_x", patch.representative_unit_normal[0]},
          {"normal_y", patch.representative_unit_normal[1]},
          {"normal_z", patch.representative_unit_normal[2]}};
}

struct MeshExtents final {
  double minimum_x{};
  double minimum_y{};
  double minimum_z{};
  double maximum_x{};
  double maximum_y{};
  double maximum_z{};
  double diagonal{};
};

MeshExtents mesh_extents(const ps::VolumeMesh &mesh) {
  MeshExtents result{
      .minimum_x = std::numeric_limits<double>::max(),
      .minimum_y = std::numeric_limits<double>::max(),
      .minimum_z = std::numeric_limits<double>::max(),
      .maximum_x = std::numeric_limits<double>::lowest(),
      .maximum_y = std::numeric_limits<double>::lowest(),
      .maximum_z = std::numeric_limits<double>::lowest()};
  for (const auto &node : mesh.nodes) {
    result.minimum_x = std::min(result.minimum_x, node.position_m[0]);
    result.minimum_y = std::min(result.minimum_y, node.position_m[1]);
    result.minimum_z = std::min(result.minimum_z, node.position_m[2]);
    result.maximum_x = std::max(result.maximum_x, node.position_m[0]);
    result.maximum_y = std::max(result.maximum_y, node.position_m[1]);
    result.maximum_z = std::max(result.maximum_z, node.position_m[2]);
  }
  result.diagonal = std::hypot(result.maximum_x - result.minimum_x,
                               result.maximum_y - result.minimum_y,
                               result.maximum_z - result.minimum_z);
  return result;
}

QVariantList string_list(const std::vector<std::string> &values) {
  QVariantList result;
  for (const auto &value : values)
    result.append(QString::fromStdString(value));
  return result;
}

QVariantList decode_material_candidates(const std::string_view bytes,
                                        const QString &evidenceHash) {
  const auto canonical = prometheus::integrity::canonicalize_json_bytes(bytes);
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(
      QByteArray::fromStdString(canonical), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    throw std::invalid_argument("material evidence must be one JSON object");
  const auto root = document.object();
  if (root.keys().size() != 3 ||
      root.value("$schema").toString() !=
          "urn:prometheus:material-candidate-evidence:0.1.0" ||
      !root.value("candidates").isArray() ||
      !root.value("sources").isArray())
    throw std::invalid_argument("material evidence contract is unsupported");
  const auto candidates = root.value("candidates").toArray();
  if (candidates.isEmpty() || candidates.size() > maximumMaterialCandidates)
    throw std::invalid_argument("material candidate count is invalid");
  QSet<QString> identities;
  QVariantList result;
  for (const auto &value : candidates) {
    if (!value.isObject())
      throw std::invalid_argument("material candidate must be an object");
    const auto object = value.toObject();
    const auto identity = object.value("candidate_id").toString();
    const auto designation = object.value("designation").toString();
    if (identity.isEmpty() || designation.isEmpty() ||
        identities.contains(identity) || object.contains("yield_strength_pa") ||
        object.contains("allowable_stress_pa") ||
        object.contains("von_mises_limit_pa"))
      throw std::invalid_argument("material candidate identity or scope is invalid");
    identities.insert(identity);
    auto candidate = object.toVariantMap();
    candidate["applicability"] = "unresolved";
    candidate["evidence_sha256"] = evidenceHash;
    result.append(candidate);
  }
  return result;
}

void append_finding(QVariantList &target, const ps::StructuralFinding &finding) {
  target.append(QVariantMap{
      {"obligation", QString::fromStdString(finding.obligation)},
      {"disposition",
       finding.disposition == ps::StructuralFindingDisposition::violated
           ? "violated"
           : "no_violation_detected_within_scope"},
      {"measured", finding.measured_value},
      {"limit", finding.limit_value},
      {"margin", finding.margin_to_limit},
      {"unit", QString::fromStdString(finding.unit)},
      {"scope", QString::fromStdString(finding.scope)},
      {"evidence_sha256", string_list(finding.evidence_sha256)},
      {"assumptions", string_list(finding.assumptions)}});
}

template <typename Geometry>
void clear_geometry(Geometry *&geometry) {
  if (geometry) geometry->deleteLater();
  geometry = nullptr;
}

} // namespace

StructuralMeshGeometry::StructuralMeshGeometry(
    const ps::VolumeMesh &mesh, const std::vector<ps::BoundaryFace> &boundary,
    const std::vector<std::array<int, 3>> &highlightedFaces, QObject *parent)
    : QQuick3DGeometry(nullptr) {
  setParent(parent);
  std::map<int, const ps::Node *> nodes;
  for (const auto &node : mesh.nodes) nodes.emplace(node.id, &node);
  std::set<std::array<int, 3>> selected;
  for (auto face : highlightedFaces)
    selected.insert(canonical_face(face));
  std::vector<float> vertices;
  std::vector<std::uint32_t> indices;
  QVector3D minimum(std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
  QVector3D maximum(std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest());
  for (const auto &face : boundary) {
    if (!selected.empty() && !selected.contains(canonical_face(face.node_ids)))
      continue;
    for (const int nodeId : face.node_ids) {
      const auto node = nodes.find(nodeId);
      if (node == nodes.end())
        throw std::runtime_error("display face references an unknown node");
      const QVector3D position(
          static_cast<float>(node->second->position_m[0] * 1000.0),
          static_cast<float>(node->second->position_m[1] * 1000.0),
          static_cast<float>(node->second->position_m[2] * 1000.0));
      vertices.insert(vertices.end(),
                      {position.x(), position.y(), position.z(),
                       static_cast<float>(face.outward_unit_normal[0]),
                       static_cast<float>(face.outward_unit_normal[1]),
                       static_cast<float>(face.outward_unit_normal[2])});
      indices.push_back(static_cast<std::uint32_t>(indices.size()));
      minimum.setX(std::min(minimum.x(), position.x()));
      minimum.setY(std::min(minimum.y(), position.y()));
      minimum.setZ(std::min(minimum.z(), position.z()));
      maximum.setX(std::max(maximum.x(), position.x()));
      maximum.setY(std::max(maximum.y(), position.y()));
      maximum.setZ(std::max(maximum.z(), position.z()));
    }
  }
  clear();
  setStride(6 * sizeof(float));
  setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
  addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
               QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float),
               QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
               QQuick3DGeometry::Attribute::U32Type);
  setVertexData(QByteArray(reinterpret_cast<const char *>(vertices.data()),
                           static_cast<qsizetype>(vertices.size() * sizeof(float))));
  setIndexData(QByteArray(reinterpret_cast<const char *>(indices.data()),
                          static_cast<qsizetype>(indices.size() * sizeof(std::uint32_t))));
  if (!vertices.empty()) setBounds(minimum, maximum);
  update();
}

StructuralResultGeometry::StructuralResultGeometry(
    const ps::VolumeMesh &mesh,
    const std::vector<ps::BoundaryFace> &boundary,
    const ps::CalculixDat &normalized, const ps::CalculixMetrics &metrics,
    const double deformationScale, QObject *parent)
    : QQuick3DGeometry(nullptr) {
  setParent(parent);
  std::unordered_map<int, ps::Node> nodes;
  for (const auto &node : mesh.nodes) nodes.emplace(node.id, node);
  std::unordered_map<int, ps::NodalDisplacement> displacement;
  for (const auto &row : normalized.displacements)
    displacement.emplace(row.node_id, row);
  std::unordered_map<int, double> stress;
  for (const auto &row : normalized.stresses) {
    auto [entry, inserted] = stress.emplace(row.element_id, row.von_mises_pa);
    if (!inserted) entry->second = std::max(entry->second, row.von_mises_pa);
  }
  const auto color = [&](const double value) {
    const auto ratio = metrics.maximum_von_mises_pa > 0.0
                           ? std::clamp(value / metrics.maximum_von_mises_pa,
                                        0.0, 1.0)
                           : 0.0;
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
                                 ? stress.at(face.source_element_id)
                                 : 0.0;
    const auto rgba = color(stressValue);
    for (const auto &position : positions) {
      vertices.insert(vertices.end(),
                      {position.x(), position.y(), position.z(), normal.x(),
                       normal.y(), normal.z(), rgba[0], rgba[1], rgba[2],
                       rgba[3]});
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
  update();
}

StructuralController::StructuralController(
    ProjectController *project, QObject *parent,
    std::shared_ptr<const StructuralBackend> backend)
    : QObject(parent), backend_(std::move(backend)), project_(project) {
  if (!backend_) backend_ = makeLocalStructuralBackend();
  if (project_) {
    connect(project_, &ProjectController::projectOpened,
            this, &StructuralController::reloadProject);
    connect(project_, &ProjectController::projectSaved,
            this, &StructuralController::reloadProject);
    connect(project_, &ProjectController::assemblyArtifactInvalidated, this,
            [this] {
      compiled_setup_.reset();
      completed_run_.reset();
      can_run_ = false;
      const auto present = std::ranges::any_of(
          blockers_, [](const QVariant &value) {
            return value.toMap().value("code") == "source_artifact_changed";
          });
      if (!present)
        blockers_.append(QVariantMap{
            {"code", "source_artifact_changed"},
            {"message", "The accounted CAD bytes changed. Historical results remain viewable, but geometry and selected surfaces require review before rerun."}});
      emit changed();
    });
  }
  rebuildPreview();
  reloadProject();
}

void StructuralController::clearCompletedRun() {
  completed_run_.reset();
  restored_verification_.reset();
  last_run_.clear();
  findings_.clear();
  clear_geometry(result_geometry_);
  result_view_.clear();
}

void StructuralController::invalidateRefinementEvidence() {
  for (const auto *key : {"refinement_complete",
                          "refinement_criteria_satisfied",
                          "refinement_change_fraction",
                          "refinement_maximum_allowed_change_fraction",
                          "refinement_result_sha256"})
    draft_.remove(QString::fromLatin1(key));
}

QVariantList StructuralController::selectedLoadPatchIds() const {
  return ids(load_patch_ids_);
}

QVariantList StructuralController::selectedRestraintPatchIds() const {
  return ids(restraint_patch_ids_);
}

void StructuralController::rebuildPatchPresentation() {
  surface_patches_.clear();
  active_surface_patch_.clear();
  active_patch_id_.reset();
  clear_geometry(highlight_geometry_);
  if (!prepared_mesh_) {
    mesh_summary_.clear();
    return;
  }
  double exteriorArea = 0.0;
  for (const auto &face : prepared_mesh_->boundary_faces)
    exteriorArea += face.area_m2;
  const auto extents = mesh_extents(prepared_mesh_->mesh);
  const auto angle = mesh_summary_.value("patch_angle_degrees", 15.0).toDouble();
  mesh_summary_ = {
      {"nodes", static_cast<qlonglong>(prepared_mesh_->mesh.nodes.size())},
      {"elements", static_cast<qlonglong>(prepared_mesh_->mesh.elements.size())},
      {"exterior_faces",
       static_cast<qlonglong>(prepared_mesh_->boundary_faces.size())},
      {"surface_patches", static_cast<qlonglong>(patches_.size())},
      {"patch_angle_degrees", angle},
      {"exterior_area_m2", exteriorArea},
      {"coordinate_scale_to_m",
       prepared_mesh_->identity.coordinate_scale_to_m},
      {"mesh_sha256",
       QString::fromStdString(prepared_mesh_->identity.source_sha256)},
      {"parser_version",
       QString::fromStdString(prepared_mesh_->identity.parser_version)},
      {"validation_version",
       QString::fromStdString(prepared_mesh_->identity.validation_version)},
      {"connected_components",
       static_cast<qlonglong>(prepared_mesh_->diagnostics.connected_components)},
      {"minimum_mean_ratio", prepared_mesh_->diagnostics.minimum_mean_ratio},
      {"maximum_mean_ratio", prepared_mesh_->diagnostics.maximum_mean_ratio}};
  mesh_summary_["center_x_mm"] =
      500.0 * (extents.minimum_x + extents.maximum_x);
  mesh_summary_["center_y_mm"] =
      500.0 * (extents.minimum_y + extents.maximum_y);
  mesh_summary_["center_z_mm"] =
      500.0 * (extents.minimum_z + extents.maximum_z);
  mesh_summary_["radius_mm"] =
      std::max(1.0, 0.55 * extents.diagonal * 1000.0);
  for (const auto &patch : patches_)
    surface_patches_.append(patch_map(patch));
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
  try {
    if (!url.isLocalFile())
      throw std::invalid_argument("a local mesh file is required");
    const auto local = url.toLocalFile();
    const auto bytes = read_regular_file(local, maximumMeshBytes);
    auto prepared = backend_->prepareMesh(bytes, coordinateScaleToM);
    auto patches = backend_->groupPatches(prepared, patchAngleDegrees);
    prepared_mesh_ = std::move(prepared);
    patches_ = std::move(patches);
    mesh_summary_["patch_angle_degrees"] = patchAngleDegrees;
    mesh_geometry_ = new StructuralMeshGeometry(
        prepared_mesh_->mesh, prepared_mesh_->boundary_faces, {}, this);
    rebuildPatchPresentation();
    status_ = "setup_blocked";
    error_.clear();
    rebuildPreview();
  } catch (const std::exception &error) {
    status_ = "mesh_failed";
    error_ = QString::fromUtf8(error.what());
  }
  emit changed();
}

void StructuralController::setPatchAngle(const double patchAngleDegrees) {
  if (busy_ || !prepared_mesh_) return;
  try {
    patches_ = backend_->groupPatches(*prepared_mesh_, patchAngleDegrees);
    mesh_summary_["patch_angle_degrees"] = patchAngleDegrees;
    load_patch_ids_.clear();
    restraint_patch_ids_.clear();
    draft_["load_reviewed"] = false;
    draft_["restraint_reviewed"] = false;
    draft_["scenario_confirmed"] = false;
    invalidateRefinementEvidence();
    compiled_setup_.reset();
    clearCompletedRun();
    rebuildPatchPresentation();
    rebuildPreview();
    error_.clear();
  } catch (const std::exception &error) {
    error_ = QString::fromUtf8(error.what());
  }
  emit changed();
}

void StructuralController::setActiveSurfacePatch(const int patchId) {
  if (!prepared_mesh_) return;
  clear_geometry(highlight_geometry_);
  active_surface_patch_.clear();
  active_patch_id_.reset();
  if (patchId <= 0) {
    emit changed();
    return;
  }
  const auto found = std::ranges::find(patches_, patchId, &ps::SurfacePatch::id);
  if (found == patches_.end()) {
    error_ = "The selected structural surface patch does not exist.";
    emit changed();
    return;
  }
  highlight_geometry_ = new StructuralMeshGeometry(
      prepared_mesh_->mesh, prepared_mesh_->boundary_faces,
      found->face_node_ids, this);
  active_patch_id_ = patchId;
  active_surface_patch_ = patch_map(*found);
  error_.clear();
  emit changed();
}

void StructuralController::setPatchSelected(const int patchId,
                                            const QString &role,
                                            const bool selected) {
  if (busy_) return;
  if (std::ranges::none_of(patches_,
                           [&](const auto &patch) { return patch.id == patchId; })) {
    error_ = "The selected structural surface patch does not exist.";
    emit changed();
    return;
  }
  auto *target = role == "load"          ? &load_patch_ids_
                 : role == "restraint" ? &restraint_patch_ids_
                                        : nullptr;
  if (!target) {
    error_ = "Structural patch role must be load or restraint.";
    emit changed();
    return;
  }
  if (selected)
    target->push_back(patchId);
  else
    target->erase(std::remove(target->begin(), target->end(), patchId),
                  target->end());
  *target = sorted_unique(std::move(*target));
  draft_[role == "load" ? "load_reviewed" : "restraint_reviewed"] = false;
  draft_["scenario_confirmed"] = false;
  invalidateRefinementEvidence();
  compiled_setup_.reset();
  clearCompletedRun();
  error_.clear();
  rebuildPreview();
  emit changed();
}

bool StructuralController::loadMaterialEvidence(const QUrl &source) {
  if (busy_) return false;
  try {
    if (!source.isLocalFile())
      throw std::invalid_argument("material evidence must be a local file");
    const auto path = source.toLocalFile();
    const auto bytes = read_regular_file(path, maximumMaterialEvidenceBytes);
    const auto hash = QString::fromStdString(
        prometheus::integrity::sha256_bytes(bytes));
    material_candidates_ = decode_material_candidates(bytes, hash);
    material_evidence_path_ = QFileInfo(path).absoluteFilePath();
    material_evidence_sha256_ = hash;
    error_.clear();
    emit changed();
    return true;
  } catch (const std::exception &error) {
    error_ = QString::fromUtf8(error.what());
    emit changed();
    return false;
  }
}

void StructuralController::selectMaterialCandidate(
    const QString &candidateId, const QString &applicability) {
  if (busy_) return;
  const auto normalizedApplicability =
      applicability == "known" || applicability == "assumed"
          ? applicability
          : QStringLiteral("unresolved");
  for (const auto &value : material_candidates_) {
    const auto candidate = value.toMap();
    if (candidate.value("candidate_id").toString() != candidateId) continue;
    draft_["material_designation"] = candidate.value("designation");
    draft_["material_temper"] = candidate.value("temper");
    draft_["material_product_form"] = candidate.value("product_form");
    draft_["material_source_sha256"] = material_evidence_sha256_;
    draft_["material_applicability"] = normalizedApplicability;
    draft_["youngs_modulus_pa"] = candidate.value("youngs_modulus_pa");
    draft_["poisson_ratio"] = candidate.value("poisson_ratio");
    draft_["material_reviewed"] = false;
    draft_["scenario_confirmed"] = false;
    invalidateRefinementEvidence();
    compiled_setup_.reset();
    clearCompletedRun();
    rebuildPreview();
    error_.clear();
    emit changed();
    return;
  }
  error_ = "The selected material candidate is unavailable.";
  emit changed();
}

void StructuralController::reviewSetup(const QVariantMap &draft) {
  if (busy_) return;
  draft_ = draft;
  invalidateRefinementEvidence();
  compiled_setup_.reset();
  clearCompletedRun();
  error_.clear();
  rebuildPreview();
  emit changed();
}

void StructuralController::applyCompiledPreview(
    const ps::StructuralSetup &setup) {
  if (!compiled_setup_) return;
  const auto &request = compiled_setup_->request;
  std::array<double, 3> resultant{};
  for (const auto &force : request.nodal_forces)
    for (std::size_t axis = 0; axis < resultant.size(); ++axis)
      resultant[axis] += force.force_n[axis];
  request_preview_ = {
      {"analysis_id", QString::fromStdString(request.analysis_id)},
      {"component_name", QString::fromStdString(request.component_name)},
      {"nodes", static_cast<qlonglong>(request.nodes.size())},
      {"elements", static_cast<qlonglong>(request.elements.size())},
      {"fixed_nodes",
       static_cast<qlonglong>(request.fully_fixed_node_ids.size())},
      {"loaded_nodes", static_cast<qlonglong>(request.nodal_forces.size())},
      {"load_faces",
       static_cast<qlonglong>(setup.load.selection.face_node_ids.size())},
      {"restraint_faces",
       static_cast<qlonglong>(setup.restraint.selection.face_node_ids.size())},
      {"selected_load_area_m2", setup.load.selection.area_m2},
      {"resultant_force_x_n", resultant[0]},
      {"resultant_force_y_n", resultant[1]},
      {"resultant_force_z_n", resultant[2]},
      {"mesh_sha256", QString::fromStdString(request.mesh_sha256)},
      {"geometry_sha256", QString::fromStdString(request.geometry_sha256)},
      {"minimum_mean_ratio", request.observed_minimum_mean_ratio},
      {"minimum_mean_ratio_threshold", request.minimum_mean_ratio_threshold}};
  can_run_ = true;
  status_ = "ready_for_execution";
}

void StructuralController::rebuildPreview() {
  blockers_.clear();
  request_preview_.clear();
  can_run_ = false;
  compiled_setup_.reset();
  if (!prepared_mesh_ || patches_.empty()) {
    blockers_.append(QVariantMap{{"code", "mesh_required"},
                                 {"message", "Load a structural volume mesh first."}});
    status_ = "mesh_required";
    return;
  }
  ps::BoundarySelection load;
  ps::BoundarySelection restraint;
  try {
    load = ps::resolve_boundary_selection("reviewed load surface", patches_,
                                          load_patch_ids_);
  } catch (const std::exception &error) {
    blockers_.append(QVariantMap{{"code", "load_selection_invalid"},
                                 {"message", QString::fromUtf8(error.what())}});
  }
  try {
    restraint = ps::resolve_boundary_selection(
        "reviewed fixed surface", patches_, restraint_patch_ids_);
  } catch (const std::exception &error) {
    blockers_.append(QVariantMap{{"code", "restraint_selection_invalid"},
                                 {"message", QString::fromUtf8(error.what())}});
  }
  ps::StructuralSetup setup{
      .analysis_id = draft_.value("analysis_id").toString().toStdString(),
      .component_name = draft_.value("component_name").toString().toStdString(),
      .geometry_sha256 =
          draft_.value("geometry_sha256").toString().toStdString(),
      .mesh = prepared_mesh_->mesh,
      .boundary_faces = prepared_mesh_->boundary_faces,
      .material =
          {.designation =
               draft_.value("material_designation").toString().toStdString(),
           .source_sha256 =
               draft_.value("material_source_sha256").toString().toStdString(),
           .applicability =
               draft_.value("material_applicability").toString().toStdString(),
           .youngs_modulus_pa = draft_.value("youngs_modulus_pa").toDouble(),
           .poisson_ratio = draft_.value("poisson_ratio").toDouble(),
           .reviewed = draft_.value("material_reviewed").toBool(),
           .temper = draft_.value("material_temper").toString().toStdString(),
           .product_form =
               draft_.value("material_product_form").toString().toStdString()},
      .load = {.selection = std::move(load),
               .total_force_n =
                   {draft_.value("force_x_n").toDouble(),
                    draft_.value("force_y_n").toDouble(),
                    draft_.value("force_z_n").toDouble()},
               .reviewed = draft_.value("load_reviewed").toBool()},
      .restraint = {.selection = std::move(restraint),
                    .reviewed =
                        draft_.value("restraint_reviewed").toBool()},
      .requirement =
          {.displacement_limit_m =
               positive_optional(draft_, "displacement_limit_m"),
           .von_mises_limit_pa =
               positive_optional(draft_, "von_mises_limit_pa"),
           .source_or_exploratory_rationale =
               draft_.value("requirement_rationale").toString().toStdString(),
           .reviewed = draft_.value("requirement_reviewed").toBool(),
           .displacement_limit_basis =
               draft_.value("displacement_limit_basis").toString().toStdString(),
           .von_mises_limit_basis =
               draft_.value("von_mises_limit_basis").toString().toStdString()},
      .mesh_controls =
          {.minimum_size_m = draft_.value("mesh_minimum_size_m").toDouble(),
           .maximum_size_m = draft_.value("mesh_maximum_size_m").toDouble(),
           .mesher_identity =
               draft_.value("mesher_identity").toString().toStdString(),
           .reviewed = draft_.value("mesh_controls_reviewed").toBool(),
           .mesh_sha256 = prepared_mesh_->identity.source_sha256,
           .coordinate_scale_to_m =
               prepared_mesh_->identity.coordinate_scale_to_m,
           .target_size_m = draft_.value("mesh_target_size_m").toDouble(),
           .minimum_mean_ratio_threshold =
               draft_.value("minimum_mean_ratio_threshold").toDouble(),
           .observed_minimum_mean_ratio =
               prepared_mesh_->diagnostics.minimum_mean_ratio},
      .scenario_description =
          draft_.value("scenario_description").toString().toStdString(),
      .scenario_confirmed = draft_.value("scenario_confirmed").toBool(),
      .selection_patch_angle_degrees =
          mesh_summary_.value("patch_angle_degrees", 15.0).toDouble()};
  if (project_ && project_->project() &&
      setup.geometry_sha256 != project_->project()->assembly_artifact_hash)
    blockers_.append(QVariantMap{
        {"code", "structural_geometry_binding_mismatch"},
        {"message",
         "Reviewed structural geometry must match the loaded project assembly identity."}});
  for (const auto &issue : ps::validate_setup(setup))
    blockers_.append(QVariantMap{
        {"code", QString::fromStdString(issue.code)},
        {"message", QString::fromStdString(issue.message)}});
  QVariantList unique;
  QSet<QString> codes;
  for (const auto &value : blockers_) {
    const auto blocker = value.toMap();
    if (!codes.contains(blocker.value("code").toString())) {
      codes.insert(blocker.value("code").toString());
      unique.append(blocker);
    }
  }
  blockers_ = std::move(unique);
  if (!blockers_.isEmpty()) {
    status_ = "setup_blocked";
    return;
  }
  try {
    compiled_setup_ = backend_->compileSetup(setup);
    applyCompiledPreview(setup);
  } catch (const std::exception &error) {
    blockers_.append(QVariantMap{{"code", "request_invalid"},
                                 {"message", QString::fromUtf8(error.what())}});
    status_ = "setup_blocked";
  }
}

void StructuralController::runAnalysis(const QUrl &calculixExecutable,
                                       const QUrl &outputRoot) {
  if (busy_) return;
  if (!can_run_ || !compiled_setup_) {
    error_ = "The reviewed structural setup is not ready for execution.";
    emit changed();
    return;
  }
  if (!calculixExecutable.isLocalFile() || !outputRoot.isLocalFile()) {
    error_ = "Select a local CalculiX executable and output directory.";
    emit changed();
    return;
  }
  const auto executablePath = calculixExecutable.toLocalFile();
  const auto rootPath = outputRoot.toLocalFile();
  const QFileInfo executable(executablePath);
  if (!executable.exists() || !executable.isFile() ||
      executable.isSymbolicLink()) {
    error_ = "Select a regular local CalculiX executable.";
    emit changed();
    return;
  }
  QDir root(rootPath);
  if (!root.exists() && !QDir().mkpath(rootPath)) {
    error_ = "The structural output directory could not be created.";
    emit changed();
    return;
  }
  const QString runName =
      "structural-" +
      QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz-") +
      QUuid::createUuid().toString(QUuid::Id128).left(8);
  if (!root.mkdir(runName)) {
    error_ = "A unique structural run directory could not be created.";
    emit changed();
    return;
  }
  const auto runDirectory = root.filePath(runName);
  const auto options = ps::SolverRunOptions{
      native_path(executablePath), native_path(runDirectory),
      "prometheus_structural_run", std::chrono::minutes(5)};
  const auto setup = *compiled_setup_;
  const auto backend = backend_;
  clearCompletedRun();
  error_.clear();
  busy_ = true;
  status_ = "executing";
  emit changed();
  auto *watcher = new QFutureWatcher<DesktopRunCompletion>(this);
  connect(watcher, &QFutureWatcher<DesktopRunCompletion>::finished, this,
          [this, watcher] {
    auto completed = watcher->result();
    watcher->deleteLater();
    busy_ = false;
    completed_run_ = std::move(completed.result);
    const auto &run = completed_run_->run;
    last_run_ = {
        {"status", run_status(run.status)},
        {"exit_code", run.exit_code},
        {"elapsed_ms", static_cast<qlonglong>(run.elapsed.count())},
        {"detail", QString::fromStdString(run.detail)},
        {"stdout", QString::fromStdString(run.standard_output)},
        {"stderr", QString::fromStdString(run.standard_error)},
        {"output_directory", completed.output_directory},
        {"archived", completed_run_->archive.has_value()},
        {"declared_obligations",
         completed_run_->evaluation.declared_obligations},
        {"evaluated_obligations",
         completed_run_->evaluation.evaluated_obligations},
        {"limitation",
         QString::fromStdString(completed_run_->evaluation.limitation)}};
    if (completed_run_->archive) {
      last_run_["archive_manifest"] =
          qt_path(completed_run_->archive->manifest_path);
      last_run_["archive_sha256"] = QString::fromStdString(
          completed_run_->archive->manifest_sha256);
      last_run_["archive_schema_version"] = QString::fromStdString(
          completed_run_->archive->schema_version);
      last_run_["validated_result_identity"] = QString::fromStdString(
          completed_run_->archive->validated_result_identity);
    } else if (!completed_run_->archive_error.empty()) {
      last_run_["archive_error"] =
          QString::fromStdString(completed_run_->archive_error);
    }
    findings_.clear();
    for (const auto &finding : completed_run_->evaluation.findings)
      append_finding(findings_, finding);
    if (run.validated_result && run.validated_result->metrics && prepared_mesh_) {
      const auto &validated = *run.validated_result;
      const auto &metrics = *validated.metrics;
      const auto maximumDisplacement = std::ranges::max_element(
          validated.normalized.displacements, {},
          &ps::NodalDisplacement::magnitude_m);
      const auto maximumStress = std::ranges::max_element(
          validated.normalized.stresses, {}, &ps::ElementStress::von_mises_pa);
      last_run_["maximum_displacement_m"] = metrics.maximum_displacement_m;
      last_run_["maximum_von_mises_pa"] = metrics.maximum_von_mises_pa;
      last_run_["displacement_rows"] =
          static_cast<qlonglong>(validated.normalized.displacements.size());
      last_run_["stress_rows"] =
          static_cast<qlonglong>(validated.normalized.stresses.size());
      if (maximumDisplacement != validated.normalized.displacements.end()) {
        last_run_["maximum_displacement_node_id"] =
            maximumDisplacement->node_id;
        last_run_["maximum_displacement_x_m"] = maximumDisplacement->x_m;
        last_run_["maximum_displacement_y_m"] = maximumDisplacement->y_m;
        last_run_["maximum_displacement_z_m"] = maximumDisplacement->z_m;
      }
      if (maximumStress != validated.normalized.stresses.end()) {
        last_run_["maximum_stress_element_id"] = maximumStress->element_id;
        last_run_["maximum_stress_integration_point"] =
            maximumStress->integration_point;
      }
      const auto extents = mesh_extents(prepared_mesh_->mesh);
      const auto scale = metrics.maximum_displacement_m > 0.0
                             ? std::clamp(0.1 * extents.diagonal /
                                              metrics.maximum_displacement_m,
                                          1.0, 1.0e6)
                             : 1.0;
      clear_geometry(result_geometry_);
      result_geometry_ = new StructuralResultGeometry(
          prepared_mesh_->mesh, prepared_mesh_->boundary_faces,
          validated.normalized, metrics, scale, this);
      result_view_ = {
          {"center_x_mm", 500.0 * (extents.minimum_x + extents.maximum_x)},
          {"center_y_mm", 500.0 * (extents.minimum_y + extents.maximum_y)},
          {"center_z_mm", 500.0 * (extents.minimum_z + extents.maximum_z)},
          {"radius_mm", std::max(1.0, 0.55 * extents.diagonal * 1000.0)},
          {"deformation_scale", scale},
          {"color_min_pa", 0.0},
          {"color_max_pa", metrics.maximum_von_mises_pa}};
    }
    status_ = run.status == ps::SolverRunStatus::completed
                  ? "execution_completed"
                  : "execution_failed";
    emit changed();
    emit runFinished();
  });
  watcher->setFuture(QtConcurrent::run(
      [backend, options, setup,
       runDirectory]() mutable -> DesktopRunCompletion {
        return {backend->execute(options, setup), runDirectory};
      }));
}

void StructuralController::commitLastRun() {
  if (busy_) return;
  if (!project_ || !project_->project() ||
      project_->currentProjectPath().isEmpty()) {
    error_ = "Open or save a Prometheus project before committing this run.";
    emit changed();
    return;
  }
  if (!completed_run_ || !completed_run_->archive) {
    error_ = "A completed active structural archive is required.";
    emit changed();
    return;
  }
  const auto trustedArchive = *completed_run_->archive;
  if (last_run_.value("archive_manifest").toString() !=
      qt_path(trustedArchive.manifest_path)) {
    error_ = "The active archive handle no longer matches the displayed run.";
    emit changed();
    return;
  }
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
      [trustedArchive, projectPath, assemblyHash] {
        auto objects = run_store::build_structural_archive_objects(
            trustedArchive.manifest_path, assemblyHash,
            trustedArchive.manifest_sha256);
        if (!objects.has_value())
          return DesktopStructuralCommitResult{
              std::nullopt, false, {}, objects.diagnostic().code + ": " +
                                           objects.diagnostic().message};
        const auto manifestHash =
            objects.value().project_manifest.reference.object_hash;
        auto published = run_store::publish_structural_archive(
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
    if (reference.schema_id == run_store::structural_manifest_schema_id_v1 ||
        reference.schema_id == run_store::structural_manifest_schema_id_v2) {
      stored_runs_.append(QVariantMap{
          {"project_manifest_hash", QString::fromStdString(reference.object_hash)},
          {"status", "legacy_manifest_only"},
          {"restorable", false}});
      continue;
    }
    if (reference.schema_id != run_store::structural_project_run_schema_id)
      continue;
    QVariantMap display{
        {"project_manifest_hash", QString::fromStdString(reference.object_hash)},
        {"status", "embedded"},
        {"restorable", true}};
    const auto projectManifest =
        run_store::read_object(project_->projectPath(), reference);
    if (!projectManifest.has_value()) {
      display["status"] = "unavailable";
      display["restorable"] = false;
      display["error"] =
          QString::fromStdString(projectManifest.diagnostic().message);
      stored_runs_.append(display);
      continue;
    }
    QJsonParseError parseError;
    const auto projectDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(projectManifest.value()), &parseError);
    const auto archiveReference =
        projectDocument.isObject()
            ? stored_reference(projectDocument.object()
                                   .value("archive_manifest")
                                   .toObject())
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
        boundAssembly.toStdString() ==
        project_->project()->assembly_artifact_hash;
    display["assembly_artifact_hash"] = boundAssembly;
    display["source_current"] = sourceCurrent;
    if (!sourceCurrent) display["status"] = "stale_source_changed";
    const auto archive =
        run_store::read_object(project_->projectPath(), *archiveReference);
    if (archive.has_value()) {
      const auto root = QJsonDocument::fromJson(
                            QByteArray::fromStdString(archive.value()))
                            .object();
      display["analysis_id"] = root.value("analysis_id").toString();
      display["component_name"] = root.value("component_name").toString();
      display["maximum_displacement_m"] =
          root.value("metrics")
              .toObject()
              .value("maximum_displacement_m")
              .toDouble();
      display["maximum_von_mises_pa"] =
          root.value("metrics")
              .toObject()
              .value("maximum_von_mises_pa")
              .toDouble();
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
  if (!selected.value("restorable").toBool() || !outputRoot.isLocalFile()) {
    error_ = "Select a restorable run and a local output folder.";
    emit changed();
    return;
  }
  const auto rootPath = outputRoot.toLocalFile();
  QDir root(rootPath);
  if (!root.exists() && !QDir().mkpath(rootPath)) {
    error_ = "The structural restore directory could not be created.";
    emit changed();
    return;
  }
  const auto hash =
      selected.value("project_manifest_hash").toString().toStdString();
  const auto reference = std::ranges::find_if(
      project_->project()->execution.committed_runs,
      [&](const auto &candidate) { return candidate.object_hash == hash; });
  if (reference == project_->project()->execution.committed_runs.end()) {
    error_ = "The selected structural history reference is unavailable.";
    emit changed();
    return;
  }
  const auto destination =
      root.filePath("restored-structural-" +
                    QDateTime::currentDateTimeUtc().toString(
                        "yyyyMMdd-HHmmss-zzz-") +
                    QUuid::createUuid().toString(QUuid::Id128).left(8));
  const auto projectPath = project_->projectPath();
  const auto stored = *reference;
  const auto sourceCurrent = selected.value("source_current", true).toBool();
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
    if (!restored.error.empty() || !restored.verification.valid ||
        !restored.verification.metrics || !restored.verification.normalized ||
        !restored.verification.reviewed_setup ||
        !restored.verification.compiled_setup ||
        !restored.verification.evaluation) {
      error_ = QString::fromStdString(
          restored.error.empty() ? "restored v2 evidence is incomplete"
                                 : restored.error);
      status_ = "structural_archive_restore_failed";
      emit changed();
      return;
    }
    const auto reviewed = *restored.verification.reviewed_setup;
    prepared_mesh_ = ps::PreparedMesh{
        .mesh = reviewed.mesh,
        .boundary_faces = reviewed.boundary_faces,
        .diagnostics =
            {.connected_components = 1U,
             .minimum_mean_ratio =
                 reviewed.mesh_controls.observed_minimum_mean_ratio,
             .maximum_mean_ratio =
                 reviewed.mesh_controls.observed_minimum_mean_ratio},
        .identity =
            {.source_sha256 = reviewed.mesh_controls.mesh_sha256,
             .coordinate_scale_to_m =
                 reviewed.mesh_controls.coordinate_scale_to_m,
             .parser_version = "restored-verified-v2",
             .validation_version = "restored-verified-v2"}};
    patches_ = backend_->groupPatches(*prepared_mesh_,
                                      reviewed.selection_patch_angle_degrees);
    mesh_summary_["patch_angle_degrees"] =
        reviewed.selection_patch_angle_degrees;
    clear_geometry(mesh_geometry_);
    mesh_geometry_ = new StructuralMeshGeometry(
        prepared_mesh_->mesh, prepared_mesh_->boundary_faces, {}, this);
    rebuildPatchPresentation();
    const auto patch_ids_for = [&](const ps::BoundarySelection &selection) {
      std::set<std::array<int, 3>> selectedFaces;
      for (auto face : selection.face_node_ids)
        selectedFaces.insert(canonical_face(face));
      std::vector<int> result;
      std::size_t covered = 0U;
      for (const auto &patch : patches_) {
        const bool contained = std::ranges::all_of(
            patch.face_node_ids, [&](auto face) {
              return selectedFaces.contains(canonical_face(face));
            });
        if (contained) {
          result.push_back(patch.id);
          covered += patch.face_node_ids.size();
        }
      }
      return covered == selectedFaces.size() ? result : std::vector<int>{};
    };
    load_patch_ids_ = patch_ids_for(reviewed.load.selection);
    restraint_patch_ids_ = patch_ids_for(reviewed.restraint.selection);
    if (load_patch_ids_.empty() || restraint_patch_ids_.empty()) {
      error_ = "Stored reviewed surfaces do not map to restored patches.";
      status_ = "structural_archive_restore_failed";
      emit changed();
      return;
    }
    draft_ = {
        {"restored", true},
        {"analysis_id", QString::fromStdString(reviewed.analysis_id)},
        {"component_name", QString::fromStdString(reviewed.component_name)},
        {"geometry_sha256",
         QString::fromStdString(reviewed.geometry_sha256)},
        {"material_designation",
         QString::fromStdString(reviewed.material.designation)},
        {"material_temper", QString::fromStdString(reviewed.material.temper)},
        {"material_product_form",
         QString::fromStdString(reviewed.material.product_form)},
        {"material_source_sha256",
         QString::fromStdString(reviewed.material.source_sha256)},
        {"material_applicability",
         QString::fromStdString(reviewed.material.applicability)},
        {"youngs_modulus_pa", reviewed.material.youngs_modulus_pa},
        {"poisson_ratio", reviewed.material.poisson_ratio},
        {"material_reviewed", reviewed.material.reviewed},
        {"force_x_n", reviewed.load.total_force_n[0]},
        {"force_y_n", reviewed.load.total_force_n[1]},
        {"force_z_n", reviewed.load.total_force_n[2]},
        {"load_reviewed", reviewed.load.reviewed},
        {"restraint_reviewed", reviewed.restraint.reviewed},
        {"displacement_limit_m",
         reviewed.requirement.displacement_limit_m.value_or(0.0)},
        {"von_mises_limit_pa",
         reviewed.requirement.von_mises_limit_pa.value_or(0.0)},
        {"requirement_rationale",
         QString::fromStdString(
             reviewed.requirement.source_or_exploratory_rationale)},
        {"displacement_limit_basis",
         QString::fromStdString(reviewed.requirement.displacement_limit_basis)},
        {"von_mises_limit_basis",
         QString::fromStdString(reviewed.requirement.von_mises_limit_basis)},
        {"requirement_reviewed", reviewed.requirement.reviewed},
        {"mesh_minimum_size_m", reviewed.mesh_controls.minimum_size_m},
        {"mesh_maximum_size_m", reviewed.mesh_controls.maximum_size_m},
        {"mesh_target_size_m", reviewed.mesh_controls.target_size_m},
        {"minimum_mean_ratio_threshold",
         reviewed.mesh_controls.minimum_mean_ratio_threshold},
        {"mesher_identity",
         QString::fromStdString(reviewed.mesh_controls.mesher_identity)},
        {"mesh_controls_reviewed", reviewed.mesh_controls.reviewed},
        {"scenario_description",
         QString::fromStdString(reviewed.scenario_description)},
        {"scenario_confirmed", reviewed.scenario_confirmed}};
    const auto &evaluation = *restored.verification.evaluation;
    compiled_setup_ = *restored.verification.compiled_setup;
    blockers_.clear();
    request_preview_.clear();
    applyCompiledPreview(reviewed);
    if (!sourceCurrent) {
      compiled_setup_.reset();
      can_run_ = false;
      blockers_.append(QVariantMap{
          {"code", "source_artifact_changed"},
          {"message", "The project assembly identity changed after this run. Historical evidence remains viewable, but rerun requires review."}});
    }
    const auto &metrics = *restored.verification.metrics;
    const auto &normalized = *restored.verification.normalized;
    const auto extents = mesh_extents(prepared_mesh_->mesh);
    const auto scale = metrics.maximum_displacement_m > 0.0
                           ? std::clamp(0.1 * extents.diagonal /
                                            metrics.maximum_displacement_m,
                                        1.0, 1.0e6)
                           : 1.0;
    clear_geometry(result_geometry_);
    result_geometry_ = new StructuralResultGeometry(
        prepared_mesh_->mesh, prepared_mesh_->boundary_faces, normalized,
        metrics, scale, this);
    result_view_ = {
        {"center_x_mm", 500.0 * (extents.minimum_x + extents.maximum_x)},
        {"center_y_mm", 500.0 * (extents.minimum_y + extents.maximum_y)},
        {"center_z_mm", 500.0 * (extents.minimum_z + extents.maximum_z)},
        {"radius_mm", std::max(1.0, 0.55 * extents.diagonal * 1000.0)},
        {"deformation_scale", scale},
        {"color_min_pa", 0.0},
        {"color_max_pa", metrics.maximum_von_mises_pa}};
    findings_.clear();
    for (const auto &finding : evaluation.findings)
      append_finding(findings_, finding);
    const auto maximumDisplacement = std::ranges::max_element(
        normalized.displacements, {}, &ps::NodalDisplacement::magnitude_m);
    const auto maximumStress = std::ranges::max_element(
        normalized.stresses, {}, &ps::ElementStress::von_mises_pa);
    last_run_ = {
        {"status", "restored_verified"},
        {"archive_manifest", restored.manifest_path},
        {"output_directory", restored.output_directory},
        {"project_anchored", true},
        {"project_artifacts_embedded", true},
        {"maximum_displacement_m", metrics.maximum_displacement_m},
        {"maximum_von_mises_pa", metrics.maximum_von_mises_pa},
        {"displacement_rows",
         static_cast<qlonglong>(normalized.displacements.size())},
        {"stress_rows", static_cast<qlonglong>(normalized.stresses.size())},
        {"declared_obligations", evaluation.declared_obligations},
        {"evaluated_obligations", evaluation.evaluated_obligations},
        {"limitation", QString::fromStdString(evaluation.limitation)}};
    if (maximumDisplacement != normalized.displacements.end()) {
      last_run_["maximum_displacement_node_id"] =
          maximumDisplacement->node_id;
      last_run_["maximum_displacement_x_m"] = maximumDisplacement->x_m;
      last_run_["maximum_displacement_y_m"] = maximumDisplacement->y_m;
      last_run_["maximum_displacement_z_m"] = maximumDisplacement->z_m;
    }
    if (maximumStress != normalized.stresses.end()) {
      last_run_["maximum_stress_element_id"] = maximumStress->element_id;
      last_run_["maximum_stress_integration_point"] =
          maximumStress->integration_point;
    }
    completed_run_.reset();
    restored_verification_ = std::move(restored.verification);
    status_ = sourceCurrent ? "structural_archive_restored"
                            : "structural_archive_restored_stale";
    error_.clear();
    emit changed();
  });
  watcher->setFuture(QtConcurrent::run(
      [projectPath, stored, destination]() -> DesktopStructuralRestoreResult {
        try {
          const auto restored = run_store::reconstruct_structural_archive(
              projectPath, stored, native_path(destination));
          if (!restored.has_value())
            return {.output_directory = destination,
                    .error = restored.diagnostic().code + ": " +
                             restored.diagnostic().message};
          auto verification = ps::verify_structural_archive(restored.value());
          if (!verification.valid)
            return {.output_directory = destination,
                    .error = verification.code + ": " +
                             verification.detail};
          return {.verification = std::move(verification),
                  .manifest_path = qt_path(restored.value()),
                  .output_directory = destination};
        } catch (const std::exception &error) {
          return {.output_directory = destination, .error = error.what()};
        }
      }));
}

void StructuralController::reset() {
  if (busy_) return;
  status_ = "mesh_required";
  error_.clear();
  mesh_summary_.clear();
  surface_patches_.clear();
  active_surface_patch_.clear();
  draft_.clear();
  material_candidates_.clear();
  material_evidence_path_.clear();
  material_evidence_sha256_.clear();
  blockers_.clear();
  request_preview_.clear();
  can_run_ = false;
  last_run_.clear();
  findings_.clear();
  prepared_mesh_.reset();
  patches_.clear();
  compiled_setup_.reset();
  completed_run_.reset();
  restored_verification_.reset();
  load_patch_ids_.clear();
  restraint_patch_ids_.clear();
  active_patch_id_.reset();
  clear_geometry(mesh_geometry_);
  clear_geometry(highlight_geometry_);
  clear_geometry(result_geometry_);
  result_view_.clear();
  rebuildPreview();
  emit changed();
}
