#include "prometheus/structural/mesh_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>

namespace prometheus::structural {
namespace {

using Face = std::array<int, 3>;
using Vector3 = std::array<double, 3>;

struct FaceRecord final {
  std::size_t count{};
  std::size_t first_element_index{};
  int source_element_id{};
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

BoundaryFace measure_boundary_face(
    Face face, const FaceRecord &record,
    const std::map<int, const Node *> &nodeById, const double areaFloor) {
  const auto &a = nodeById.at(face[0])->position_m;
  const auto &b = nodeById.at(face[1])->position_m;
  const auto &c = nodeById.at(face[2])->position_m;
  const auto &opposite = nodeById.at(record.opposite_node_id)->position_m;
  auto twiceAreaVector = cross(subtract(b, a), subtract(c, a));
  const double twiceArea = magnitude(twiceAreaVector);
  if (!std::isfinite(twiceArea) || twiceArea / 2.0 <= areaFloor)
    throw std::runtime_error("Mesh contains a zero-area tetrahedral face");
  const double orientation = dot(twiceAreaVector, subtract(opposite, a));
  if (!std::isfinite(orientation) || orientation == 0.0)
    throw std::runtime_error("Mesh contains a degenerate tetrahedron");
  if (orientation > 0.0) {
    std::swap(face[1], face[2]);
    for (auto &component : twiceAreaVector)
      component = -component;
  }
  for (auto &component : twiceAreaVector)
    component /= twiceArea;
  return {.source_element_id = record.source_element_id,
          .node_ids = face,
          .centroid_m = {(a[0] + b[0] + c[0]) / 3.0,
                         (a[1] + b[1] + c[1]) / 3.0,
                         (a[2] + b[2] + c[2]) / 3.0},
          .outward_unit_normal = twiceAreaVector,
          .area_m2 = twiceArea / 2.0};
}

} // namespace

ValidatedMeshTopology validate_and_measure_mesh(
    const VolumeMesh &mesh,
    std::vector<SourceSurfaceCandidate> sourceSurfaceCandidates) {
  if (mesh.nodes.size() < 4U || mesh.elements.empty())
    throw std::runtime_error(
        "Mesh requires at least four nodes and one tetrahedron");

  std::map<int, const Node *> nodeById;
  Vector3 minimum{std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::max()};
  Vector3 maximum{std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::lowest()};
  for (const auto &node : mesh.nodes) {
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
  std::vector<std::vector<std::size_t>> adjacency(mesh.elements.size());
  double minimumMeanRatio = std::numeric_limits<double>::max();
  double maximumMeanRatio = 0.0;

  for (std::size_t elementIndex = 0; elementIndex < mesh.elements.size();
       ++elementIndex) {
    const auto &element = mesh.elements[elementIndex];
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
    if (!std::isfinite(determinant))
      throw std::runtime_error("Mesh tetrahedron volume is not finite");
    if (determinant < 0.0)
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
        12.0 * std::pow(3.0 * volume, 2.0 / 3.0) /
        squaredEdgeLengthSum;
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
          face, FaceRecord{1U, elementIndex, element.id, oppositeNodeId});
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

  std::vector<bool> visited(mesh.elements.size());
  std::size_t connectedComponents = 0U;
  for (std::size_t start = 0; start < mesh.elements.size(); ++start) {
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

  std::vector<BoundaryFace> boundaryFaces;
  std::map<Face, std::size_t> boundaryIndices;
  for (const auto &[face, record] : faceRecords) {
    if (record.count != 1U)
      continue;
    boundaryIndices.emplace(face, boundaryFaces.size());
    boundaryFaces.push_back(
        measure_boundary_face(face, record, nodeById, areaFloor));
  }

  std::set<std::string> sourceGroupNames;
  std::set<int> sourceTriangleIds;
  std::set<Face> representedBoundaryFaces;
  std::vector<SourceSurfaceGroup> sourceSurfaceGroups;
  sourceSurfaceGroups.reserve(sourceSurfaceCandidates.size());
  for (auto &candidate : sourceSurfaceCandidates) {
    if (candidate.name.empty() || candidate.triangles.empty() ||
        !sourceGroupNames.insert(candidate.name).second)
      throw std::runtime_error(
          "Source surface groups require a unique name and at least one triangle");
    SourceSurfaceGroup group{.name = std::move(candidate.name)};
    std::set<int> groupNodeIds;
    Vector3 areaVector{};
    for (const auto &triangle : candidate.triangles) {
      if (triangle.source_element_id <= 0 ||
          !sourceTriangleIds.insert(triangle.source_element_id).second)
        throw std::runtime_error(
            "Source surface triangle IDs must be unique and positive");
      std::set<int> localNodeIds;
      for (const int nodeId : triangle.node_ids) {
        if (!nodeById.contains(nodeId))
          throw std::runtime_error(
              "Source surface triangle references a missing node");
        if (!localNodeIds.insert(nodeId).second)
          throw std::runtime_error(
              "Source surface triangle requires three distinct nodes");
        groupNodeIds.insert(nodeId);
      }
      const auto face = sorted_face(triangle.node_ids);
      const auto boundary = boundaryIndices.find(face);
      if (boundary == boundaryIndices.end())
        throw std::runtime_error(
            "Source surface triangle is not a volume boundary face");
      if (!representedBoundaryFaces.insert(face).second)
        throw std::runtime_error(
            "A volume boundary face appears more than once in source surface groups");
      const auto &measured = boundaryFaces.at(boundary->second);
      group.face_node_ids.push_back(face);
      group.area_m2 += measured.area_m2;
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        group.centroid_m[axis] +=
            measured.area_m2 * measured.centroid_m[axis];
        areaVector[axis] +=
            measured.area_m2 * measured.outward_unit_normal[axis];
      }
    }
    std::ranges::sort(group.face_node_ids);
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
    sourceSurfaceGroups.push_back(std::move(group));
  }

  return {.boundary_faces = std::move(boundaryFaces),
          .source_surface_groups = std::move(sourceSurfaceGroups),
          .diagnostics =
              {.connected_components = connectedComponents,
               .minimum_mean_ratio = std::min(1.0, minimumMeanRatio),
               .maximum_mean_ratio = std::min(1.0, maximumMeanRatio)}};
}

} // namespace prometheus::structural
