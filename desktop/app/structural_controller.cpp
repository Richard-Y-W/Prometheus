#include "structural_controller.hpp"

#include "prometheus/structural/structural_setup.hpp"
#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_archive.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QUuid>

namespace ps = prometheus::structural;

namespace {

struct DesktopRunResult final {
  ps::SolverRunResult run;
  ps::StructuralEvaluation evaluation;
  QString output_directory;
  std::optional<ps::StructuralArchive> archive;
  std::string archive_error;
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

} // namespace

StructuralController::StructuralController(QObject *parent) : QObject(parent) {
  rebuildPreview();
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
  draft_ = draft;
  last_run_.clear();
  findings_.clear();
  rebuildPreview();
  emit changed();
}

void StructuralController::rebuildPreview() {
  blockers_.clear();
  request_preview_.clear();
  can_run_ = false;
  compiled_request_.reset();
  compiled_setup_evidence_.clear();
  if (mesh_.nodes.empty() || patches_.empty()) {
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
      .requirement = {positiveOptional(draft_, "displacement_limit_m"),
                      positiveOptional(draft_, "von_mises_limit_pa"),
                      draft_.value("requirement_rationale").toString().toStdString(),
                      draft_.value("requirement_reviewed").toBool()},
      .mesh_controls = {draft_.value("mesh_minimum_size_m").toDouble(),
                        draft_.value("mesh_maximum_size_m").toDouble(),
                        draft_.value("mesher_identity").toString().toStdString(),
                        draft_.value("mesh_controls_reviewed").toBool()},
      .scenario_description = draft_.value("scenario_description").toString().toStdString(),
      .scenario_confirmed = draft_.value("scenario_confirmed").toBool()};
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
    }
    for (const auto &finding : completed.evaluation.findings) {
      findings_.append(QVariantMap{
          {"obligation", QString::fromStdString(finding.obligation)},
          {"disposition", finding.disposition ==
                  ps::StructuralFindingDisposition::violated
              ? "violated" : "no_violation_detected_within_scope"},
          {"measured", finding.measured_value},
          {"limit", finding.limit_value},
          {"margin", finding.margin_to_limit},
          {"unit", QString::fromStdString(finding.unit)},
          {"scope", QString::fromStdString(finding.scope)}});
    }
    status_ = completed.run.status == ps::SolverRunStatus::completed
        ? "execution_completed" : "execution_failed";
    emit changed();
    emit runFinished();
  });
  watcher->setFuture(QtConcurrent::run(
      [request, setupEvidence, executablePath, runDirectory]() -> DesktopRunResult {
        const auto run = ps::run_calculix({
            std::filesystem::path(executablePath.toStdWString()),
            std::filesystem::path(runDirectory.toStdWString()),
            "prometheus_structural_run", std::chrono::minutes(5)});
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
  compiled_setup_evidence_.clear();
  mesh_ = {};
  boundary_.clear();
  patches_.clear();
  load_patch_ids_.clear();
  restraint_patch_ids_.clear();
  rebuildPreview();
  emit changed();
}
