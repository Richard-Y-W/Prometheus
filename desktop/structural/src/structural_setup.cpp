#include "prometheus/structural/structural_setup.hpp"

#include "prometheus/structural/structural_request.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace prometheus::structural {
namespace {

bool sha256(const std::string &value) {
  if (value.size() != 71 || !value.starts_with("sha256:")) return false;
  return std::ranges::all_of(value.substr(7), [](const unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

void issue(std::vector<ValidationIssue> &issues, std::string code,
           std::string message) {
  issues.push_back({std::move(code), std::move(message)});
}

std::array<int, 3> canonical(std::array<int, 3> nodes) {
  std::ranges::sort(nodes);
  return nodes;
}

bool valid_selection(const BoundarySelection &selection,
                     const std::vector<BoundaryFace> &boundary) {
  if (selection.label.empty() || selection.face_node_ids.empty() ||
      selection.node_ids.empty() || !std::isfinite(selection.area_m2) ||
      selection.area_m2 <= 0.0)
    return false;
  std::map<std::array<int, 3>, double> available;
  for (const auto &face : boundary)
    if (!available.emplace(canonical(face.node_ids), face.area_m2).second)
      return false;
  std::set<std::array<int, 3>> selectedFaces;
  std::set<int> selectedNodes;
  double selectedArea = 0.0;
  for (const auto &face : selection.face_node_ids) {
    const auto key = canonical(face);
    const auto found = available.find(key);
    if (found == available.end() || !selectedFaces.insert(key).second) return false;
    selectedArea += found->second;
    selectedNodes.insert(key.begin(), key.end());
  }
  const std::vector<int> exactNodes(selectedNodes.begin(), selectedNodes.end());
  const double areaTolerance = std::max(1e-15, selection.area_m2 * 1e-12);
  return exactNodes == selection.node_ids &&
         std::abs(selectedArea - selection.area_m2) <= areaTolerance;
}

} // namespace

std::string_view to_string(const RequirementQuantity value) {
  switch (value) {
  case RequirementQuantity::displacement: return "displacement";
  case RequirementQuantity::von_mises_stress: return "von_mises_stress";
  case RequirementQuantity::other: return "other";
  }
  return "other";
}

std::string_view to_string(const RequirementComparator value) {
  switch (value) {
  case RequirementComparator::less_or_equal: return "less_or_equal";
  }
  return "less_or_equal";
}

std::string_view to_string(const RequirementCriticality value) {
  switch (value) {
  case RequirementCriticality::informational: return "informational";
  case RequirementCriticality::advisory: return "advisory";
  case RequirementCriticality::critical: return "critical";
  }
  return "advisory";
}

std::vector<ValidationIssue> validate_setup(const StructuralSetup &setup) {
  std::vector<ValidationIssue> issues;
  if (setup.analysis_id.empty())
    issue(issues, "missing_analysis_id", "Analysis identity is required.");
  if (setup.component_name.empty())
    issue(issues, "missing_component", "A component is required.");
  if (!sha256(setup.geometry_sha256))
    issue(issues, "invalid_geometry_identity", "Exact lowercase geometry SHA-256 is required.");
  if (setup.mesh.nodes.empty() || setup.mesh.elements.empty() ||
      setup.boundary_faces.empty())
    issue(issues, "missing_mesh", "A volume mesh and exterior boundary are required.");
  if (!std::isfinite(setup.selection_patch_angle_degrees) ||
      setup.selection_patch_angle_degrees <= 0.0 ||
      setup.selection_patch_angle_degrees > 180.0)
    issue(issues, "selection_patch_angle_invalid",
          "Surface selection patch angle must be in (0, 180] degrees.");

  if (!setup.material.reviewed)
    issue(issues, "material_unreviewed", "Material applicability and properties require review.");
  if (setup.material.designation.empty() || setup.material.applicability.empty() ||
      !sha256(setup.material.source_sha256))
    issue(issues, "material_provenance_missing",
          "Material designation, applicability, and exact source identity are required.");
  if (!std::isfinite(setup.material.youngs_modulus_pa) ||
      setup.material.youngs_modulus_pa <= 0.0 ||
      !std::isfinite(setup.material.poisson_ratio) ||
      setup.material.poisson_ratio <= -1.0 || setup.material.poisson_ratio >= 0.5)
    issue(issues, "material_properties_invalid", "Reviewed elastic properties are invalid.");

  if (!setup.load.reviewed)
    issue(issues, "load_unreviewed", "Surface load selection and vector require review.");
  if (!valid_selection(setup.load.selection, setup.boundary_faces))
    issue(issues, "load_selection_invalid", "Load faces must resolve to the exact mesh boundary.");
  double loadMagnitudeSquared = 0.0;
  for (const double component : setup.load.total_force_n) {
    if (!std::isfinite(component)) loadMagnitudeSquared = -1.0;
    else if (loadMagnitudeSquared >= 0.0) loadMagnitudeSquared += component * component;
  }
  if (loadMagnitudeSquared <= 0.0)
    issue(issues, "load_vector_invalid", "Reviewed total surface force must be finite and nonzero.");

  if (!setup.restraint.reviewed)
    issue(issues, "restraint_unreviewed", "Fixed surface selection requires review.");
  if (!valid_selection(setup.restraint.selection, setup.boundary_faces))
    issue(issues, "restraint_selection_invalid",
          "Restraint faces must resolve to the exact mesh boundary.");
  std::set<std::array<int, 3>> restraintFaces;
  for (const auto &face : setup.restraint.selection.face_node_ids)
    restraintFaces.insert(canonical(face));
  if (std::ranges::any_of(setup.load.selection.face_node_ids, [&](const auto &face) {
        return restraintFaces.contains(canonical(face));
      }))
    issue(issues, "load_restraint_overlap",
          "The bounded model does not permit the same face to be loaded and fully fixed.");

  if (setup.requirements.empty())
    issue(issues, "requirement_missing", "At least one reviewed requirement is required.");
  bool sawDisplacement = false;
  bool sawVonMises = false;
  bool anySupportedAndPositive = false;
  for (const auto &requirement : setup.requirements) {
    if (!requirement.reviewed)
      issue(issues, "requirement_unreviewed", "All structural requirements require review.");
    if (requirement.source_or_exploratory_rationale.empty())
      issue(issues, "requirement_provenance_missing",
            "A requirement source or explicit exploratory rationale is required.");
    if (requirement.unit.empty())
      issue(issues, "requirement_unit_missing", "A requirement unit is required.");
    if (requirement.quantity == RequirementQuantity::other) {
      if (requirement.other_quantity_description.empty())
        issue(issues, "requirement_description_missing",
              "An 'other' requirement needs an explicit description of what it asks.");
      continue;
    }
    if (requirement.quantity == RequirementQuantity::displacement) {
      if (sawDisplacement)
        issue(issues, "requirement_duplicate_quantity",
              "Only one displacement requirement is supported.");
      sawDisplacement = true;
    } else {
      if (sawVonMises)
        issue(issues, "requirement_duplicate_quantity",
              "Only one von Mises stress requirement is supported.");
      sawVonMises = true;
    }
    if (std::isfinite(requirement.limit_value) && requirement.limit_value > 0.0)
      anySupportedAndPositive = true;
    else
      issue(issues, "requirement_limit_invalid",
            "A positive reviewed limit is required for a supported requirement quantity.");
  }
  if (!setup.requirements.empty() && !anySupportedAndPositive)
    issue(issues, "requirement_unsupported_only",
          "At least one positive reviewed displacement or von Mises stress limit is required.");

  if (!setup.mesh_controls.reviewed)
    issue(issues, "mesh_controls_unreviewed", "Mesh controls require review.");
  if (!std::isfinite(setup.mesh_controls.minimum_size_m) ||
      !std::isfinite(setup.mesh_controls.maximum_size_m) ||
      setup.mesh_controls.minimum_size_m <= 0.0 ||
      setup.mesh_controls.maximum_size_m < setup.mesh_controls.minimum_size_m ||
      setup.mesh_controls.mesher_identity.empty())
    issue(issues, "mesh_controls_invalid", "Reviewed SI mesh controls and mesher identity are required.");

  if (!setup.scenario_confirmed || setup.scenario_description.empty())
    issue(issues, "scenario_unconfirmed", "The described structural scenario requires confirmation.");
  return issues;
}

StructuralRequest compile_structural_request(const StructuralSetup &setup) {
  const auto issues = validate_setup(setup);
  if (!issues.empty())
    throw std::invalid_argument(issues.front().code + ": " + issues.front().message);
  std::optional<double> displacementLimit;
  std::optional<double> vonMisesLimit;
  for (const auto &requirement : setup.requirements) {
    if (requirement.quantity == RequirementQuantity::displacement)
      displacementLimit = requirement.limit_value;
    else if (requirement.quantity == RequirementQuantity::von_mises_stress)
      vonMisesLimit = requirement.limit_value;
  }
  StructuralRequest request{
      .analysis_id = setup.analysis_id,
      .component_name = setup.component_name,
      .geometry_sha256 = setup.geometry_sha256,
      .nodes = setup.mesh.nodes,
      .elements = setup.mesh.elements,
      .youngs_modulus_pa = setup.material.youngs_modulus_pa,
      .poisson_ratio = setup.material.poisson_ratio,
      .fully_fixed_node_ids = setup.restraint.selection.node_ids,
      .nodal_forces = distribute_surface_total_force(
          setup.load.selection, setup.load.total_force_n, setup.boundary_faces),
      .displacement_limit_m = displacementLimit,
      .von_mises_limit_pa = vonMisesLimit,
      .material_reviewed = setup.material.reviewed,
      .loads_reviewed = setup.load.reviewed,
      .restraints_reviewed = setup.restraint.reviewed,
      .requirements_reviewed = std::ranges::all_of(
          setup.requirements, &ReviewedRequirement::reviewed),
      .scenario_confirmed = setup.scenario_confirmed};
  const auto requestIssues = validate_request(request);
  if (!requestIssues.empty())
    throw std::invalid_argument(requestIssues.front().code + ": " +
                                requestIssues.front().message);
  return request;
}

} // namespace prometheus::structural
