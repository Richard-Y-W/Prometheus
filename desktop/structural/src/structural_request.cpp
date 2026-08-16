#include "prometheus/structural/structural_request.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace prometheus::structural {
namespace {

bool finite(const double value) { return std::isfinite(value); }

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
  if (request.component_name.empty())
    issue(issues, "missing_component", "A selected component is required.");
  if (!request.geometry_sha256.starts_with("sha256:") ||
      request.geometry_sha256.size() != 71)
    issue(issues, "invalid_geometry_identity",
          "Exact component geometry SHA-256 is required.");

  if (!request.material_reviewed)
    issue(issues, "material_unreviewed", "Material inputs require explicit review.");
  if (!request.loads_reviewed)
    issue(issues, "loads_unreviewed", "Loads require explicit review.");
  if (!request.restraints_reviewed)
    issue(issues, "restraints_unreviewed", "Restraints require explicit review.");
  if (!request.requirements_reviewed)
    issue(issues, "requirements_unreviewed",
          "Stress or displacement requirements require explicit review.");
  if (!request.scenario_confirmed)
    issue(issues, "scenario_unconfirmed", "The complete scenario is not confirmed.");

  if (!finite(request.youngs_modulus_pa) || request.youngs_modulus_pa <= 0.0)
    issue(issues, "invalid_youngs_modulus", "Young's modulus must be finite and positive.");
  if (!finite(request.poisson_ratio) || request.poisson_ratio <= -1.0 ||
      request.poisson_ratio >= 0.5)
    issue(issues, "invalid_poisson_ratio",
          "Poisson ratio must be finite and between -1 and 0.5.");

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
      if (!finite(determinant) || std::abs(determinant) <= 1e-24)
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
  for (const auto &load : request.nodal_forces) {
    if (!nodeIds.contains(load.node_id))
      issue(issues, "load_node_missing", "A load references a missing node.");
    if (!std::ranges::all_of(load.force_n, finite))
      issue(issues, "invalid_load", "Nodal forces must be finite SI values.");
  }

  const bool displacementValid = request.displacement_limit_m.has_value() &&
      finite(*request.displacement_limit_m) && *request.displacement_limit_m > 0.0;
  const bool stressValid = request.von_mises_limit_pa.has_value() &&
      finite(*request.von_mises_limit_pa) && *request.von_mises_limit_pa > 0.0;
  if (!displacementValid && !stressValid)
    issue(issues, "missing_requirement",
          "A positive reviewed displacement or von Mises stress limit is required.");
  return issues;
}

} // namespace prometheus::structural
