#include "prometheus/structural/surface_groups.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <queue>
#include <set>
#include <stdexcept>

namespace prometheus::structural {
namespace {

std::array<int, 3> face_key(std::array<int, 3> nodes) {
  std::ranges::sort(nodes);
  return nodes;
}

double dot(const std::array<double, 3> &a,
           const std::array<double, 3> &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace

std::vector<SurfacePatch> group_boundary_faces(
    const std::vector<BoundaryFace> &faces,
    const double maxNormalAngleDegrees) {
  if (!std::isfinite(maxNormalAngleDegrees) || maxNormalAngleDegrees < 0.0 ||
      maxNormalAngleDegrees > 180.0)
    throw std::invalid_argument("Surface patch angle must be within [0, 180] degrees");
  if (faces.empty()) return {};

  std::vector<std::size_t> order(faces.size());
  for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
  std::ranges::sort(order, [&](const auto left, const auto right) {
    return face_key(faces[left].node_ids) < face_key(faces[right].node_ids);
  });

  std::set<std::array<int, 3>> uniqueFaces;
  std::map<std::array<int, 2>, std::vector<std::size_t>> edgeOwners;
  for (std::size_t index = 0; index < faces.size(); ++index) {
    const auto &face = faces[index];
    auto key = face_key(face.node_ids);
    if (key[0] <= 0 || key[0] == key[1] || key[1] == key[2] ||
        !uniqueFaces.insert(key).second)
      throw std::invalid_argument("Boundary faces must have unique positive node triples");
    if (!std::isfinite(face.area_m2) || face.area_m2 <= 0.0)
      throw std::invalid_argument("Boundary face area must be finite and positive");
    const double normalMagnitude = std::sqrt(dot(face.outward_unit_normal,
                                                  face.outward_unit_normal));
    if (!std::isfinite(normalMagnitude) || std::abs(normalMagnitude - 1.0) > 1e-9)
      throw std::invalid_argument("Boundary face normal must be a finite unit vector");
    for (const double coordinate : face.centroid_m)
      if (!std::isfinite(coordinate))
        throw std::invalid_argument("Boundary face centroid must be finite");
    constexpr std::array<std::array<int, 2>, 3> edges{{
        {{0, 1}}, {{1, 2}}, {{2, 0}}}};
    for (const auto edgeIndices : edges) {
      std::array<int, 2> edge{face.node_ids[edgeIndices[0]],
                              face.node_ids[edgeIndices[1]]};
      std::ranges::sort(edge);
      auto &owners = edgeOwners[edge];
      owners.push_back(index);
      if (owners.size() > 2)
        throw std::invalid_argument("Boundary surface contains a non-manifold edge");
    }
  }

  std::vector<std::vector<std::size_t>> adjacent(faces.size());
  const double minimumDot = std::cos(maxNormalAngleDegrees *
                                     std::numbers::pi / 180.0);
  for (const auto &[edge, owners] : edgeOwners) {
    (void)edge;
    if (owners.size() != 2) continue;
    const auto left = owners[0];
    const auto right = owners[1];
    if (dot(faces[left].outward_unit_normal,
            faces[right].outward_unit_normal) + 1e-15 >= minimumDot) {
      adjacent[left].push_back(right);
      adjacent[right].push_back(left);
    }
  }

  std::vector<bool> visited(faces.size());
  std::vector<SurfacePatch> result;
  for (const auto seed : order) {
    if (visited[seed]) continue;
    SurfacePatch patch;
    patch.id = static_cast<int>(result.size()) + 1;
    std::queue<std::size_t> pending;
    pending.push(seed);
    visited[seed] = true;
    std::set<int> nodeIds;
    std::array<double, 3> weightedNormal{};
    while (!pending.empty()) {
      const auto index = pending.front();
      pending.pop();
      const auto &face = faces[index];
      patch.face_node_ids.push_back(face_key(face.node_ids));
      patch.area_m2 += face.area_m2;
      for (int axis = 0; axis < 3; ++axis) {
        patch.area_weighted_centroid_m[axis] +=
            face.centroid_m[axis] * face.area_m2;
        weightedNormal[axis] += face.outward_unit_normal[axis] * face.area_m2;
      }
      nodeIds.insert(face.node_ids.begin(), face.node_ids.end());
      for (const auto neighbor : adjacent[index])
        if (!visited[neighbor]) {
          visited[neighbor] = true;
          pending.push(neighbor);
        }
    }
    std::ranges::sort(patch.face_node_ids);
    patch.node_ids.assign(nodeIds.begin(), nodeIds.end());
    for (double &coordinate : patch.area_weighted_centroid_m)
      coordinate /= patch.area_m2;
    const double normalMagnitude = std::sqrt(dot(weightedNormal, weightedNormal));
    if (!std::isfinite(normalMagnitude) || normalMagnitude <= 1e-15)
      throw std::invalid_argument("Surface patch has no representative normal");
    for (int axis = 0; axis < 3; ++axis)
      patch.representative_unit_normal[axis] = weightedNormal[axis] / normalMagnitude;
    result.push_back(std::move(patch));
  }
  return result;
}

} // namespace prometheus::structural
