#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace prometheus::structural {

struct PreparedMeshIdentity final {
  std::string source_sha256;
  double coordinate_scale_to_m{};
  std::string parser_version;
  std::string validation_version;
};

struct PreparedMesh final {
  VolumeMesh mesh;
  std::vector<BoundaryFace> boundary_faces;
  std::vector<SourceSurfaceGroup> source_surface_groups;
  MeshDiagnostics diagnostics;
  PreparedMeshIdentity identity;
};

[[nodiscard]] PreparedMesh prepare_gmsh_abaqus_mesh(
    std::string_view source_bytes, double coordinate_scale_to_m);

} // namespace prometheus::structural
