#include "structural_controller.hpp"

#include "prometheus/structural/structural_setup.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>

namespace ps = prometheus::structural;

namespace {

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
  rebuildPreview();
  emit changed();
}

void StructuralController::rebuildPreview() {
  blockers_.clear();
  request_preview_.clear();
  can_run_ = false;
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

void StructuralController::reset() {
  status_ = "mesh_required";
  error_.clear();
  mesh_summary_.clear();
  surface_patches_.clear();
  draft_.clear();
  blockers_.clear();
  request_preview_.clear();
  can_run_ = false;
  mesh_ = {};
  boundary_.clear();
  patches_.clear();
  load_patch_ids_.clear();
  restraint_patch_ids_.clear();
  rebuildPreview();
  emit changed();
}
