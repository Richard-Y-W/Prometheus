#pragma once

#include "prometheus/structural/types.hpp"

#include <array>
#include <string_view>

namespace prometheus::structural {

struct VolumeMesh final {
  std::vector<Node> nodes;
  std::vector<Tetrahedron> elements;
};

struct BoundaryFace final {
  int source_element_id{};
  std::array<int, 3> node_ids{};
  std::array<double, 3> centroid_m{};
  std::array<double, 3> outward_unit_normal{};
  double area_m2{};
};

[[nodiscard]] VolumeMesh parse_gmsh_abaqus_mesh(
    std::string_view rawMesh, double coordinate_scale_to_m);

// Extracts only the exterior triangular faces. Face node order is oriented so
// its normal points away from the owning tetrahedron. Non-manifold faces fail
// closed because they cannot support unambiguous reviewed boundary selection.
[[nodiscard]] std::vector<BoundaryFace> extract_boundary_faces(
    const VolumeMesh &mesh);

} // namespace prometheus::structural
