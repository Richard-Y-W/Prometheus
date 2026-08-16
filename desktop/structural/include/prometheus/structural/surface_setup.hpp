#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"

namespace prometheus::structural {

struct CompiledSurfaceSetup final {
  std::vector<int> fully_fixed_node_ids;
  std::vector<int> loaded_node_ids;
  std::vector<NodalForce> nodal_forces;
  double selected_load_area_m2{};
  std::array<double, 3> resultant_force_n{};
};

[[nodiscard]] CompiledSurfaceSetup compile_surface_setup(
    const VolumeMesh &mesh,
    const std::vector<std::string> &restraint_groups,
    const std::vector<std::string> &load_groups, double magnitude_n,
    const std::array<double, 3> &direction);

} // namespace prometheus::structural
