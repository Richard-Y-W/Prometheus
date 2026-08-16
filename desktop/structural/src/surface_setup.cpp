#include "prometheus/structural/surface_setup.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace prometheus::structural {
namespace {

using Vector3 = std::array<double, 3>;

Vector3 subtract(const Vector3 &left, const Vector3 &right) {
  return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

Vector3 cross(const Vector3 &left, const Vector3 &right) {
  return {left[1] * right[2] - left[2] * right[1],
          left[2] * right[0] - left[0] * right[2],
          left[0] * right[1] - left[1] * right[0]};
}

double magnitude(const Vector3 &value) {
  return std::sqrt(value[0] * value[0] + value[1] * value[1] +
                   value[2] * value[2]);
}

std::vector<const SurfaceGroup *> select_groups(
    const VolumeMesh &mesh, const std::vector<std::string> &names,
    const std::string_view role) {
  if (names.empty())
    throw std::invalid_argument("At least one " + std::string(role) +
                                " surface group is required");
  std::set<std::string> selectedNames;
  std::vector<const SurfaceGroup *> selected;
  selected.reserve(names.size());
  for (const auto &name : names) {
    if (!selectedNames.insert(name).second)
      throw std::invalid_argument(std::string(role) +
                                  " surface group was selected more than once");
    const auto group =
        std::ranges::find(mesh.surface_groups, name, &SurfaceGroup::name);
    if (group == mesh.surface_groups.end())
      throw std::invalid_argument("Unknown " + std::string(role) +
                                  " surface group: " + name);
    selected.push_back(&*group);
  }
  return selected;
}

} // namespace

CompiledSurfaceSetup compile_surface_setup(
    const VolumeMesh &mesh,
    const std::vector<std::string> &restraintGroups,
    const std::vector<std::string> &loadGroups, const double magnitudeN,
    const std::array<double, 3> &direction) {
  if (!std::isfinite(magnitudeN) || magnitudeN <= 0.0)
    throw std::invalid_argument(
        "Reviewed force magnitude must be finite and positive");
  if (!std::ranges::all_of(direction, [](const double value) {
        return std::isfinite(value);
      }))
    throw std::invalid_argument(
        "Reviewed force direction must be a finite nonzero vector");
  const double directionMagnitude = magnitude(direction);
  if (!std::isfinite(directionMagnitude) || directionMagnitude == 0.0)
    throw std::invalid_argument(
        "Reviewed force direction must be a finite nonzero vector");
  Vector3 unitDirection{};
  for (std::size_t axis = 0; axis < 3U; ++axis)
    unitDirection[axis] = direction[axis] / directionMagnitude;

  const auto restraints = select_groups(mesh, restraintGroups, "restraint");
  const auto loads = select_groups(mesh, loadGroups, "load");
  const std::set<std::string> restraintNames(restraintGroups.begin(),
                                              restraintGroups.end());
  if (std::ranges::any_of(loadGroups, [&](const std::string &name) {
        return restraintNames.contains(name);
      }))
    throw std::invalid_argument(
        "A surface group cannot be both restrained and loaded");

  std::map<int, const Node *> nodeById;
  for (const auto &node : mesh.nodes)
    if (node.id <= 0 || !nodeById.emplace(node.id, &node).second)
      throw std::invalid_argument(
          "Surface setup mesh node IDs must be unique and positive");

  std::set<int> fixedNodeIds;
  for (const auto *group : restraints)
    for (const auto &triangle : group->triangles)
      for (const int nodeId : triangle.node_ids) {
        if (!nodeById.contains(nodeId))
          throw std::invalid_argument(
              "Restraint surface references a missing mesh node");
        fixedNodeIds.insert(nodeId);
      }

  struct LoadedTriangle final {
    const SurfaceTriangle *triangle{};
    double area_m2{};
  };
  std::vector<LoadedTriangle> loadedTriangles;
  double selectedArea = 0.0;
  for (const auto *group : loads)
    for (const auto &triangle : group->triangles) {
      std::array<const Node *, 3> triangleNodes{};
      for (std::size_t index = 0; index < 3U; ++index) {
        const auto node = nodeById.find(triangle.node_ids[index]);
        if (node == nodeById.end())
          throw std::invalid_argument(
              "Load surface references a missing mesh node");
        triangleNodes[index] = node->second;
      }
      const double area =
          magnitude(cross(subtract(triangleNodes[1]->position_m,
                                   triangleNodes[0]->position_m),
                          subtract(triangleNodes[2]->position_m,
                                   triangleNodes[0]->position_m))) /
          2.0;
      if (!std::isfinite(area) || area <= 0.0)
        throw std::invalid_argument(
            "Selected load surface contains a zero-area triangle");
      selectedArea += area;
      loadedTriangles.push_back({&triangle, area});
    }
  if (!std::isfinite(selectedArea) || selectedArea <= 0.0)
    throw std::invalid_argument(
        "Selected load surface area must be finite and positive");

  std::map<int, Vector3> forceByNode;
  for (const auto &loaded : loadedTriangles) {
    const double nodalMagnitude =
        magnitudeN * loaded.area_m2 / selectedArea / 3.0;
    for (const int nodeId : loaded.triangle->node_ids) {
      auto &force = forceByNode[nodeId];
      for (std::size_t axis = 0; axis < 3U; ++axis)
        force[axis] += nodalMagnitude * unitDirection[axis];
    }
  }

  CompiledSurfaceSetup result;
  result.fully_fixed_node_ids.assign(fixedNodeIds.begin(), fixedNodeIds.end());
  result.selected_load_area_m2 = selectedArea;
  for (const auto &[nodeId, force] : forceByNode) {
    result.loaded_node_ids.push_back(nodeId);
    result.nodal_forces.push_back({nodeId, force});
    for (std::size_t axis = 0; axis < 3U; ++axis)
      result.resultant_force_n[axis] += force[axis];
  }
  return result;
}

} // namespace prometheus::structural
