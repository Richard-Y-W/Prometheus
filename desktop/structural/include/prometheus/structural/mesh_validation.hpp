#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"

namespace prometheus::structural {

struct ValidatedMeshTopology final {
  std::vector<BoundaryFace> boundary_faces;
  std::vector<SourceSurfaceGroup> source_surface_groups;
  MeshDiagnostics diagnostics;
};

[[nodiscard]] ValidatedMeshTopology validate_and_measure_mesh(
    const VolumeMesh &mesh,
    std::vector<SourceSurfaceCandidate> source_surface_candidates);

} // namespace prometheus::structural
