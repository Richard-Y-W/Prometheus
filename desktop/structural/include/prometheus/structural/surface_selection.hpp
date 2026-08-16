#pragma once

#include "prometheus/structural/surface_groups.hpp"
#include "prometheus/structural/types.hpp"

#include <array>
#include <string>
#include <vector>

namespace prometheus::structural {

struct BoundarySelection final {
  std::string label;
  std::vector<std::array<int, 3>> face_node_ids;
  std::vector<int> node_ids;
  double area_m2{};
};

// Resolves transient visual patch IDs into durable exact topology identities.
[[nodiscard]] BoundarySelection resolve_boundary_selection(
    std::string label, const std::vector<SurfacePatch> &patches,
    const std::vector<int> &selected_patch_ids);

// Applies one reviewed total force uniformly as traction over the selected
// triangles, yielding consistent first-order triangular nodal forces.
[[nodiscard]] std::vector<NodalForce> distribute_surface_total_force(
    const BoundarySelection &selection,
    const std::array<double, 3> &total_force_n,
    const std::vector<BoundaryFace> &boundary_faces);

} // namespace prometheus::structural
