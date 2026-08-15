#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"

#include <array>
#include <vector>

namespace prometheus::structural {

struct SurfacePatch final {
  int id{};
  std::vector<std::array<int, 3>> face_node_ids;
  std::vector<int> node_ids;
  std::array<double, 3> area_weighted_centroid_m{};
  std::array<double, 3> representative_unit_normal{};
  double area_m2{};
};

// Builds deterministic connected geometric patches. Two exterior triangles
// may join only when they share an edge and their normals differ by no more
// than max_normal_angle_degrees. These are selection aids, not inferred
// engineering meanings; callers must retain explicit human review.
[[nodiscard]] std::vector<SurfacePatch> group_boundary_faces(
    const std::vector<BoundaryFace> &faces,
    double max_normal_angle_degrees);

} // namespace prometheus::structural
