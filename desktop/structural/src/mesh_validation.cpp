#include "prometheus/structural/mesh_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>

namespace prometheus::structural {
namespace {

using Face = std::array<int, 3>;
using Vector3 = std::array<double, 3>;

struct FaceRecord final {
  std::size_t count{};
  std::size_t first_element_index{};
  int opposite_node_id{};
};

Face sorted_face(Face face) {
  std::ranges::sort(face);
  return face;
}

Vector3 subtract(const Vector3 &left, const Vector3 &right) {
  return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

Vector3 cross(const Vector3 &left, const Vector3 &right) {
  return {left[1] * right[2] - left[2] * right[1],
          left[2] * right[0] - left[0] * right[2],
          left[0] * right[1] - left[1] * right[0]};
}

double dot(const Vector3 &left, const Vector3 &right) {
  return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

double magnitude(const Vector3 &value) { return std::sqrt(dot(value, value)); }

double signed_determinant(const Vector3 &a, const Vector3 &b,
                          const Vector3 &c, const Vector3 &d) {
  return dot(subtract(b, a), cross(subtract(c, a), subtract(d, a)));
}

} // namespace

ValidatedMeshTopology validate_and_measure_mesh(
    const std::vector<Node> &nodes, const std::vector<Tetrahedron> &elements,
    std::vector<SurfaceGroup> surfaceGroups) {
  if (nodes.size() < 4U || elements.empty())
    throw std::runtime_error(
        "Mesh requires at least four nodes and one tetrahedron");

  std::map<int, const Node *> nodeById;
  Vector3 minimum{std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::max()};
  Vector3 maximum{std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::lowest()};
  for (const auto &node : nodes) {
    if (node.id <= 0 || !nodeById.emplace(node.id, &node).second)
      throw std::runtime_error("Mesh node IDs must be unique and positive");
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      if (!std::isfinite(node.position_m[axis]))
        throw std::runtime_error("Mesh node coordinates must be finite");
      minimum[axis] = std::min(minimum[axis], node.position_m[axis]);
      maximum[axis] = std::max(maximum[axis], node.position_m[axis]);
    }
  }

  const double lengthScale = magnitude(subtract(maximum, minimum));
  const double volumeFloor =
      std::max(1.0e-30, std::pow(lengthScale, 3) * 1.0e-15);
  const double areaFloor =
      std::max(1.0e-30, std::pow(lengthScale, 2) * 1.0e-15);

  std::set<int> elementIds;
  std::map<Face, FaceRecord> faceRecords;
  std::vector<std::vector<std::size_t>> adjacency(elements.size());
  double minimumMeanRatio = std::numeric_limits<double>::max();
  double maximumMeanRatio = 0.0;

  for (std::size_t elementIndex = 0; elementIndex < elements.size();
       ++elementIndex) {
    const auto &element = elements[elementIndex];
    if (element.id <= 0 || !elementIds.insert(element.id).second)
      throw std::runtime_error(
          "Mesh tetrahedron IDs must be unique and positive");
    std::set<int> localNodeIds;
    std::array<const Node *, 4> elementNodes{};
    for (std::size_t localIndex = 0; localIndex < 4U; ++localIndex) {
      const int nodeId = element.node_ids[localIndex];
      const auto node = nodeById.find(nodeId);
      if (node == nodeById.end())
        throw std::runtime_error("Mesh tetrahedron references a missing node");
      if (!localNodeIds.insert(nodeId).second)
        throw std::runtime_error(
            "Mesh tetrahedron requires four distinct nodes");
      elementNodes[localIndex] = node->second;
    }

    const double determinant =
        signed_determinant(elementNodes[0]->position_m,
                           elementNodes[1]->position_m,
                           elementNodes[2]->position_m,
                           elementNodes[3]->position_m);
    if (!std::isfinite(determinant) || determinant < 0.0)
      throw std::runtime_error("Mesh contains an inverted tetrahedron");
    const double volume = determinant / 6.0;
    if (volume <= volumeFloor)
      throw std::runtime_error("Mesh contains a degenerate tetrahedron");

    double squaredEdgeLengthSum = 0.0;
    for (std::size_t first = 0; first < 4U; ++first)
      for (std::size_t second = first + 1U; second < 4U; ++second) {
        const auto edge = subtract(elementNodes[first]->position_m,
                                   elementNodes[second]->position_m);
        squaredEdgeLengthSum += dot(edge, edge);
      }
    const double meanRatio =
        12.0 * std::pow(3.0 * volume, 2.0 / 3.0) / squaredEdgeLengthSum;
    if (!std::isfinite(meanRatio) || meanRatio <= 0.0)
      throw std::runtime_error("Mesh tetrahedron quality is not finite");
    minimumMeanRatio = std::min(minimumMeanRatio, meanRatio);
    maximumMeanRatio = std::max(maximumMeanRatio, meanRatio);

    const std::array<std::pair<Face, int>, 4> faces{{
        {{element.node_ids[0], element.node_ids[1], element.node_ids[2]},
         element.node_ids[3]},
        {{element.node_ids[0], element.node_ids[1], element.node_ids[3]},
         element.node_ids[2]},
        {{element.node_ids[0], element.node_ids[2], element.node_ids[3]},
         element.node_ids[1]},
        {{element.node_ids[1], element.node_ids[2], element.node_ids[3]},
         element.node_ids[0]},
    }};
    for (const auto &[unsortedFace, oppositeNodeId] : faces) {
      const auto face = sorted_face(unsortedFace);
      auto [record, inserted] = faceRecords.emplace(
          face, FaceRecord{1U, elementIndex, oppositeNodeId});
      if (inserted)
        continue;
      if (record->second.count != 1U)
        throw std::runtime_error(
            "Mesh contains a non-manifold tetrahedral face");
      adjacency[record->second.first_element_index].push_back(elementIndex);
      adjacency[elementIndex].push_back(record->second.first_element_index);
      record->second.count = 2U;
    }
  }

  std::vector<bool> visited(elements.size());
  std::size_t connectedComponents = 0U;
  for (std::size_t start = 0; start < elements.size(); ++start) {
    if (visited[start])
      continue;
    ++connectedComponents;
    std::queue<std::size_t> pending;
    pending.push(start);
    visited[start] = true;
    while (!pending.empty()) {
      const auto current = pending.front();
      pending.pop();
      for (const auto neighbor : adjacency[current]) {
        if (visited[neighbor])
          continue;
        visited[neighbor] = true;
        pending.push(neighbor);
      }
    }
  }
  if (connectedComponents != 1U)
    throw std::runtime_error(
        "Mesh must contain exactly one face-connected volume component");

  if (surfaceGroups.empty())
    throw std::runtime_error("Mesh has no selectable boundary surface groups");

  std::size_t boundaryFaceCount = 0U;
  for (const auto &[face, record] : faceRecords) {
    (void)face;
    if (record.count == 1U)
      ++boundaryFaceCount;
  }

  std::set<int> triangleIds;
  std::set<Face> representedBoundaryFaces;
  for (auto &group : surfaceGroups) {
    if (group.name.empty() || group.triangles.empty())
      throw std::runtime_error(
          "Surface groups require a name and at least one triangle");
    group.node_ids.clear();
    group.area_m2 = 0.0;
    group.centroid_m = {};
    group.representative_normal = {};
    group.representative_normal_defined = false;
    std::set<int> groupNodeIds;
    Vector3 areaVector{};

    for (const auto &triangle : group.triangles) {
      if (triangle.id <= 0 || !triangleIds.insert(triangle.id).second)
        throw std::runtime_error(
            "Surface triangle IDs must be unique and positive");
      std::set<int> localNodeIds;
      std::array<const Node *, 3> triangleNodes{};
      for (std::size_t localIndex = 0; localIndex < 3U; ++localIndex) {
        const int nodeId = triangle.node_ids[localIndex];
        const auto node = nodeById.find(nodeId);
        if (node == nodeById.end())
          throw std::runtime_error("Surface triangle references a missing node");
        if (!localNodeIds.insert(nodeId).second)
          throw std::runtime_error(
              "Surface triangle requires three distinct nodes");
        triangleNodes[localIndex] = node->second;
        groupNodeIds.insert(nodeId);
      }

      const auto face = sorted_face(triangle.node_ids);
      const auto record = faceRecords.find(face);
      if (record == faceRecords.end() || record->second.count != 1U)
        throw std::runtime_error(
            "Surface triangle is not a volume boundary face");
      if (!representedBoundaryFaces.insert(face).second)
        throw std::runtime_error(
            "A volume boundary face appears more than once in surface groups");

      const auto firstEdge = subtract(triangleNodes[1]->position_m,
                                      triangleNodes[0]->position_m);
      const auto secondEdge = subtract(triangleNodes[2]->position_m,
                                       triangleNodes[0]->position_m);
      auto twiceAreaVector = cross(firstEdge, secondEdge);
      const double twiceArea = magnitude(twiceAreaVector);
      const double triangleArea = twiceArea / 2.0;
      if (!std::isfinite(triangleArea) || triangleArea <= areaFloor)
        throw std::runtime_error("Surface triangle has zero or unresolved area");

      const auto opposite = nodeById.at(record->second.opposite_node_id);
      if (dot(twiceAreaVector,
              subtract(opposite->position_m,
                       triangleNodes[0]->position_m)) > 0.0)
        for (auto &component : twiceAreaVector)
          component = -component;

      group.area_m2 += triangleArea;
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        const double triangleCentroid =
            (triangleNodes[0]->position_m[axis] +
             triangleNodes[1]->position_m[axis] +
             triangleNodes[2]->position_m[axis]) /
            3.0;
        group.centroid_m[axis] += triangleArea * triangleCentroid;
        areaVector[axis] += twiceAreaVector[axis] / 2.0;
      }
    }

    group.node_ids.assign(groupNodeIds.begin(), groupNodeIds.end());
    for (auto &coordinate : group.centroid_m)
      coordinate /= group.area_m2;
    const double areaVectorMagnitude = magnitude(areaVector);
    if (areaVectorMagnitude > areaFloor) {
      for (std::size_t axis = 0; axis < 3U; ++axis)
        group.representative_normal[axis] =
            areaVector[axis] / areaVectorMagnitude;
      group.representative_normal_defined = true;
    }
  }

  if (representedBoundaryFaces.size() != boundaryFaceCount)
    throw std::runtime_error(
        "Mesh boundary is not completely represented by surface groups");

  return {
      .surface_groups = std::move(surfaceGroups),
      .diagnostics =
          {.connected_components = connectedComponents,
           .minimum_mean_ratio = std::min(1.0, minimumMeanRatio),
           .maximum_mean_ratio = std::min(1.0, maximumMeanRatio)},
  };
}

} // namespace prometheus::structural
