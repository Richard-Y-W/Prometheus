#pragma once

#include "prometheus/structural/types.hpp"

namespace prometheus::structural {

struct ValidatedMeshTopology final {
  std::vector<SurfaceGroup> surface_groups;
  MeshDiagnostics diagnostics;
};

[[nodiscard]] ValidatedMeshTopology validate_and_measure_mesh(
    const std::vector<Node> &nodes, const std::vector<Tetrahedron> &elements,
    std::vector<SurfaceGroup> surface_groups);

} // namespace prometheus::structural
