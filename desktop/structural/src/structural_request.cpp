#include "prometheus/structural/structural_request.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string_view>

namespace prometheus::structural {
namespace {

bool finite(const double value) { return std::isfinite(value); }

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool safe_text(const std::string_view value) {
  return !value.empty() &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return character >= 0x20U && character != 0x7fU;
         });
}

double magnitude(const std::array<double, 3> &value) {
  return std::sqrt(value[0] * value[0] + value[1] * value[1] +
                   value[2] * value[2]);
}

void issue(std::vector<ValidationIssue> &issues, std::string code,
           std::string message) {
  issues.push_back({std::move(code), std::move(message)});
}

} // namespace

std::vector<ValidationIssue>
validate_request(const StructuralRequest &request) {
  std::vector<ValidationIssue> issues;
  if (request.schema !=
      "urn:prometheus:calculix-linear-static-request:0.1.0")
    issue(issues, "unsupported_schema", "Structural request schema is unsupported.");
  if (request.analysis_id.empty())
    issue(issues, "missing_analysis_id", "Analysis identity is required.");
  else if (!safe_text(request.analysis_id))
    issue(issues, "unsafe_heading_text",
          "Analysis identity contains unsafe CalculiX heading text.");
  if (request.component_name.empty())
    issue(issues, "missing_component", "A selected component is required.");
  else if (!safe_text(request.component_name))
    issue(issues, "unsafe_heading_text",
          "Component name contains unsafe CalculiX heading text.");
  if (!strict_sha256(request.geometry_sha256))
    issue(issues, "invalid_geometry_identity",
          "Exact component geometry SHA-256 is required.");
  if (!strict_sha256(request.mesh_sha256))
    issue(issues, "invalid_mesh_identity", "Exact mesh SHA-256 is required.");
  if (!strict_sha256(request.material_evidence_sha256))
    issue(issues, "invalid_material_evidence_identity",
          "Exact material-evidence SHA-256 is required.");

  if (!request.material_reviewed)
    issue(issues, "material_unreviewed", "Material inputs require explicit review.");
  if (!request.loads_reviewed)
    issue(issues, "loads_unreviewed", "Loads require explicit review.");
  if (!request.restraints_reviewed)
    issue(issues, "restraints_unreviewed", "Restraints require explicit review.");
  if (!request.requirements_reviewed)
    issue(issues, "requirements_unreviewed",
          "Stress or displacement requirements require explicit review.");
  if (!request.mesh_reviewed)
    issue(issues, "mesh_unreviewed", "Mesh controls require explicit review.");
  if (!request.scenario_confirmed)
    issue(issues, "scenario_unconfirmed", "The complete scenario is not confirmed.");

  if (!safe_text(request.material_designation) ||
      !safe_text(request.material_temper) ||
      !safe_text(request.material_product_form))
    issue(issues, "incomplete_material_identity",
          "Material designation, temper, and product form are required.");
  if (request.material_applicability != "known" &&
      request.material_applicability != "assumed")
    issue(issues, "material_applicability_unresolved",
          "Material applicability must be explicitly known or assumed.");

  const auto validGroups = [&](const std::vector<std::string> &groups,
                               const std::string_view code,
                               const std::string_view message) {
    if (groups.empty() ||
        !std::ranges::all_of(groups, [](const std::string &group) {
          return safe_text(group);
        }))
      issue(issues, std::string(code), std::string(message));
  };
  validGroups(request.restraint_surface_groups,
              "missing_restraint_surface_group",
              "At least one safe restraint surface group is required.");
  validGroups(request.load_surface_groups, "missing_load_surface_group",
              "At least one safe load surface group is required.");

  if (!finite(request.youngs_modulus_pa) || request.youngs_modulus_pa <= 0.0)
    issue(issues, "invalid_youngs_modulus", "Young's modulus must be finite and positive.");
  if (!finite(request.poisson_ratio) || request.poisson_ratio <= -1.0 ||
      request.poisson_ratio >= 0.5)
    issue(issues, "invalid_poisson_ratio",
          "Poisson ratio must be finite and between -1 and 0.5.");
  if (!finite(request.selected_load_area_m2) ||
      request.selected_load_area_m2 <= 0.0)
    issue(issues, "invalid_selected_load_area",
          "Selected load area must be finite and positive.");
  if (!finite(request.mesh_target_size_m) || request.mesh_target_size_m <= 0.0)
    issue(issues, "invalid_mesh_target_size",
          "Mesh target size must be finite and positive.");
  if (!finite(request.minimum_mean_ratio_threshold) ||
      request.minimum_mean_ratio_threshold <= 0.0 ||
      request.minimum_mean_ratio_threshold > 1.0)
    issue(issues, "invalid_mesh_quality_threshold",
          "Minimum mean-ratio threshold must be in (0, 1].");
  if (!finite(request.observed_minimum_mean_ratio) ||
      request.observed_minimum_mean_ratio <= 0.0 ||
      request.observed_minimum_mean_ratio > 1.0)
    issue(issues, "invalid_observed_mesh_quality",
          "Observed minimum mean ratio must be in (0, 1].");
  else if (finite(request.minimum_mean_ratio_threshold) &&
           request.observed_minimum_mean_ratio + 1.0e-15 <
               request.minimum_mean_ratio_threshold)
    issue(issues, "mesh_quality_below_limit",
          "Observed mesh quality is below the reviewed threshold.");

  std::set<int> nodeIds;
  for (const auto &node : request.nodes) {
    if (node.id <= 0 || !nodeIds.insert(node.id).second)
      issue(issues, "invalid_node_identity", "Mesh node IDs must be unique and positive.");
    if (!std::ranges::all_of(node.position_m, finite))
      issue(issues, "invalid_node_coordinate", "Mesh coordinates must be finite SI values.");
  }
  if (request.nodes.size() < 4)
    issue(issues, "insufficient_mesh", "At least four mesh nodes are required.");

  std::set<int> elementIds;
  for (const auto &element : request.elements) {
    if (element.id <= 0 || !elementIds.insert(element.id).second)
      issue(issues, "invalid_element_identity",
            "Element IDs must be unique and positive.");
    std::set<int> local;
    for (const int nodeId : element.node_ids) {
      if (!nodeIds.contains(nodeId))
        issue(issues, "element_node_missing", "An element references a missing node.");
      local.insert(nodeId);
    }
    if (local.size() != 4)
      issue(issues, "degenerate_element",
            "A first-order tetrahedron requires four distinct nodes.");
    if (local.size() == 4 &&
        std::ranges::all_of(element.node_ids,
                           [&](const int id) { return nodeIds.contains(id); })) {
      const auto position = [&](const int id) -> const std::array<double, 3> & {
        return std::ranges::find(request.nodes, id, &Node::id)->position_m;
      };
      const auto &a = position(element.node_ids[0]);
      const auto &b = position(element.node_ids[1]);
      const auto &c = position(element.node_ids[2]);
      const auto &d = position(element.node_ids[3]);
      const std::array<double, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
      const std::array<double, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
      const std::array<double, 3> ad{d[0] - a[0], d[1] - a[1], d[2] - a[2]};
      const double determinant =
          ab[0] * (ac[1] * ad[2] - ac[2] * ad[1]) -
          ab[1] * (ac[0] * ad[2] - ac[2] * ad[0]) +
          ab[2] * (ac[0] * ad[1] - ac[1] * ad[0]);
      if (finite(determinant) && determinant < 0.0)
        issue(issues, "inverted_element",
              "A tetrahedral element has inverted orientation.");
      else if (!finite(determinant) || determinant <= 1e-24)
        issue(issues, "zero_volume_element",
              "A tetrahedral element has zero or unresolved volume.");
    }
  }
  if (request.elements.empty())
    issue(issues, "missing_elements", "A tetrahedral mesh is required.");

  std::set<int> fixed;
  for (const int nodeId : request.fully_fixed_node_ids) {
    if (!nodeIds.contains(nodeId))
      issue(issues, "restraint_node_missing", "A restraint references a missing node.");
    fixed.insert(nodeId);
  }
  bool nonCollinearRestraint = false;
  if (fixed.size() >= 3) {
    std::vector<const Node *> fixedNodes;
    for (const int id : fixed) {
      const auto found = std::ranges::find(request.nodes, id, &Node::id);
      if (found != request.nodes.end()) fixedNodes.push_back(&*found);
    }
    for (std::size_t i = 0; i < fixedNodes.size() && !nonCollinearRestraint; ++i)
      for (std::size_t j = i + 1; j < fixedNodes.size() && !nonCollinearRestraint; ++j)
        for (std::size_t k = j + 1; k < fixedNodes.size(); ++k) {
          const auto &a = fixedNodes[i]->position_m;
          const auto &b = fixedNodes[j]->position_m;
          const auto &c = fixedNodes[k]->position_m;
          const std::array<double, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
          const std::array<double, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
          const std::array<double, 3> cross{
              ab[1] * ac[2] - ab[2] * ac[1],
              ab[2] * ac[0] - ab[0] * ac[2],
              ab[0] * ac[1] - ab[1] * ac[0]};
          nonCollinearRestraint =
              cross[0] * cross[0] + cross[1] * cross[1] +
                  cross[2] * cross[2] >
              1e-24;
        }
  }
  if (!nonCollinearRestraint)
    issue(issues, "inadequate_restraints",
          "At least three non-collinear fully fixed nodes are required by this bounded model.");

  if (request.nodal_forces.empty())
    issue(issues, "missing_load", "At least one nodal force is required.");
  std::set<int> loadedNodeIds;
  std::array<double, 3> resultant{};
  bool finiteLoads = true;
  for (const auto &load : request.nodal_forces) {
    if (!nodeIds.contains(load.node_id))
      issue(issues, "load_node_missing", "A load references a missing node.");
    if (!loadedNodeIds.insert(load.node_id).second)
      issue(issues, "duplicate_load_node",
            "Nodal force IDs must be unique after load compilation.");
    if (!std::ranges::all_of(load.force_n, finite)) {
      issue(issues, "invalid_load", "Nodal forces must be finite SI values.");
      finiteLoads = false;
      continue;
    }
    for (std::size_t direction = 0; direction < resultant.size(); ++direction)
      resultant[direction] += load.force_n[direction];
  }
  if (finiteLoads && !request.nodal_forces.empty()) {
    const auto resultantMagnitude = magnitude(resultant);
    if (resultantMagnitude == 0.0)
      issue(issues, "zero_resultant_load",
            "Compiled nodal forces must have a nonzero resultant.");

    const bool reviewedMagnitudeValid =
        finite(request.reviewed_force_magnitude_n) &&
        request.reviewed_force_magnitude_n > 0.0;
    if (!reviewedMagnitudeValid)
      issue(issues, "invalid_reviewed_force",
            "Reviewed force magnitude must be finite and positive.");

    const bool directionFinite =
        std::ranges::all_of(request.reviewed_force_direction, finite);
    const auto directionMagnitude =
        directionFinite ? magnitude(request.reviewed_force_direction) : 0.0;
    if (!directionFinite || std::abs(directionMagnitude - 1.0) > 1.0e-12)
      issue(issues, "invalid_reviewed_force_direction",
            "Reviewed force direction must be a finite unit vector.");

    if (reviewedMagnitudeValid && directionFinite &&
        std::abs(directionMagnitude - 1.0) <= 1.0e-12) {
      std::array<double, 3> difference{};
      for (std::size_t direction = 0; direction < difference.size(); ++direction)
        difference[direction] =
            resultant[direction] - request.reviewed_force_magnitude_n *
                                       request.reviewed_force_direction[direction];
      const auto tolerance =
          std::max(1.0, request.reviewed_force_magnitude_n) * 1.0e-10;
      if (!finite(magnitude(difference)) || magnitude(difference) > tolerance)
        issue(issues, "compiled_load_mismatch",
              "Compiled nodal forces do not reproduce the reviewed force.");
    }
  }

  const bool displacementValid = request.displacement_limit_m.has_value() &&
      finite(*request.displacement_limit_m) && *request.displacement_limit_m > 0.0;
  const bool stressValid = request.von_mises_limit_pa.has_value() &&
      finite(*request.von_mises_limit_pa) && *request.von_mises_limit_pa > 0.0;
  if (!displacementValid && !stressValid)
    issue(issues, "missing_requirement",
          "A positive reviewed displacement or von Mises stress limit is required.");
  if (request.displacement_limit_m.has_value() &&
      !safe_text(request.displacement_limit_basis))
    issue(issues, "missing_displacement_limit_basis",
          "A displacement limit requires a reviewed basis.");
  if (request.von_mises_limit_pa.has_value() &&
      !safe_text(request.von_mises_limit_basis))
    issue(issues, "missing_von_mises_limit_basis",
          "A von Mises limit requires a reviewed basis.");
  return issues;
}

} // namespace prometheus::structural
