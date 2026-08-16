#include "prometheus/structural/surface_selection.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace prometheus::structural {
namespace {

std::array<int, 3> canonical(std::array<int, 3> nodes) {
  std::ranges::sort(nodes);
  return nodes;
}

} // namespace

BoundarySelection resolve_boundary_selection(
    std::string label, const std::vector<SurfacePatch> &patches,
    const std::vector<int> &selectedPatchIds) {
  if (label.empty()) throw std::invalid_argument("Boundary selection label is required");
  if (selectedPatchIds.empty())
    throw std::invalid_argument("At least one surface patch must be selected");
  std::map<int, const SurfacePatch *> byId;
  for (const auto &patch : patches)
    if (patch.id <= 0 || !byId.emplace(patch.id, &patch).second)
      throw std::invalid_argument("Surface patch IDs must be unique and positive");

  std::set<int> requested;
  std::set<std::array<int, 3>> faces;
  std::set<int> nodes;
  BoundarySelection result;
  result.label = std::move(label);
  for (const int patchId : selectedPatchIds) {
    if (!requested.insert(patchId).second)
      throw std::invalid_argument("A surface patch cannot be selected twice");
    const auto found = byId.find(patchId);
    if (found == byId.end())
      throw std::invalid_argument("Selected surface patch does not exist");
    const auto &patch = *found->second;
    if (!std::isfinite(patch.area_m2) || patch.area_m2 <= 0.0)
      throw std::invalid_argument("Selected surface patch has invalid area");
    result.area_m2 += patch.area_m2;
    for (const auto &face : patch.face_node_ids) {
      const auto key = canonical(face);
      if (!faces.insert(key).second)
        throw std::invalid_argument("Selected surface patches overlap");
      nodes.insert(key.begin(), key.end());
    }
  }
  result.face_node_ids.assign(faces.begin(), faces.end());
  result.node_ids.assign(nodes.begin(), nodes.end());
  return result;
}

std::vector<NodalForce> distribute_surface_total_force(
    const BoundarySelection &selection,
    const std::array<double, 3> &totalForceN,
    const std::vector<BoundaryFace> &boundaryFaces) {
  if (selection.face_node_ids.empty() || !std::isfinite(selection.area_m2) ||
      selection.area_m2 <= 0.0)
    throw std::invalid_argument("A non-empty boundary selection with positive area is required");
  double forceMagnitudeSquared = 0.0;
  for (const double component : totalForceN) {
    if (!std::isfinite(component))
      throw std::invalid_argument("Surface force components must be finite");
    forceMagnitudeSquared += component * component;
  }
  if (forceMagnitudeSquared == 0.0)
    throw std::invalid_argument("Surface total force must be nonzero");

  std::map<std::array<int, 3>, const BoundaryFace *> boundaryByFace;
  for (const auto &face : boundaryFaces)
    if (!boundaryByFace.emplace(canonical(face.node_ids), &face).second)
      throw std::invalid_argument("Boundary faces must have unique node triples");

  std::map<int, std::array<double, 3>> forceByNode;
  double resolvedArea = 0.0;
  for (const auto &selectedFace : selection.face_node_ids) {
    const auto found = boundaryByFace.find(canonical(selectedFace));
    if (found == boundaryByFace.end())
      throw std::invalid_argument("Selected face is not present in the boundary mesh");
    const auto &face = *found->second;
    if (!std::isfinite(face.area_m2) || face.area_m2 <= 0.0)
      throw std::invalid_argument("Selected boundary face has invalid area");
    resolvedArea += face.area_m2;
  }
  const double areaTolerance = std::max(1e-15, selection.area_m2 * 1e-12);
  if (std::abs(resolvedArea - selection.area_m2) > areaTolerance)
    throw std::invalid_argument("Boundary selection area does not match its exact faces");

  for (const auto &selectedFace : selection.face_node_ids) {
    const auto &face = *boundaryByFace.at(canonical(selectedFace));
    const double nodalWeight = face.area_m2 / selection.area_m2 / 3.0;
    for (const int nodeId : face.node_ids) {
      auto &force = forceByNode[nodeId];
      for (int axis = 0; axis < 3; ++axis)
        force[axis] += totalForceN[axis] * nodalWeight;
    }
  }
  std::vector<NodalForce> result;
  for (const auto &[nodeId, force] : forceByNode)
    result.push_back({nodeId, force});
  return result;
}

} // namespace prometheus::structural
