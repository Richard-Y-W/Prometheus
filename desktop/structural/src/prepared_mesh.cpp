#include "prometheus/structural/prepared_mesh.hpp"

#include "prometheus/structural/mesh_validation.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <utility>

namespace prometheus::structural {

PreparedMesh prepare_gmsh_abaqus_mesh(
    const std::string_view sourceBytes, const double coordinateScaleToM) {
  auto parsed = parse_gmsh_abaqus_source(sourceBytes, coordinateScaleToM);
  auto validated = validate_and_measure_mesh(
      parsed.mesh, std::move(parsed.source_surface_candidates));
  return {.mesh = std::move(parsed.mesh),
          .boundary_faces = std::move(validated.boundary_faces),
          .source_surface_groups =
              std::move(validated.source_surface_groups),
          .diagnostics = validated.diagnostics,
          .identity =
              {.source_sha256 = integrity::sha256_bytes(sourceBytes),
               .coordinate_scale_to_m = coordinateScaleToM,
               .parser_version = "gmsh-abaqus-c3d4-v2",
               .validation_version = "tetra-topology-v2"}};
}

} // namespace prometheus::structural
