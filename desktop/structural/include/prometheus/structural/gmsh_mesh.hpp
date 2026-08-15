#pragma once

#include "prometheus/structural/types.hpp"

#include <string_view>

namespace prometheus::structural {

struct VolumeMesh final {
  std::vector<Node> nodes;
  std::vector<Tetrahedron> elements;
};

[[nodiscard]] VolumeMesh parse_gmsh_abaqus_mesh(
    std::string_view rawMesh, double coordinate_scale_to_m);

} // namespace prometheus::structural
