#include "prometheus/structural/smoke_case.hpp"

namespace prometheus::structural {

StructuralRequest structural_smoke_request() {
  return {
      .analysis_id = "analytic-tetra-smoke-v1",
      .component_name = "analytic tetrahedron (not YUBI evidence)",
      .geometry_sha256 =
          "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      .nodes = {{1, {0.0, 0.0, 0.0}}, {2, {1.0, 0.0, 0.0}},
                {3, {0.0, 1.0, 0.0}}, {4, {0.0, 0.0, 1.0}}},
      .elements = {{1, {1, 2, 3, 4}}},
      .youngs_modulus_pa = 2.0e11,
      .poisson_ratio = 0.3,
      .fully_fixed_node_ids = {1, 2, 3},
      .nodal_forces = {{4, {0.0, 0.0, -1000.0}}},
      .displacement_limit_m = 1.0e-6,
      .von_mises_limit_pa = 1.0e7,
      .material_reviewed = true,
      .loads_reviewed = true,
      .restraints_reviewed = true,
      .requirements_reviewed = true,
      .scenario_confirmed = true,
      .material_designation = "synthetic linear-elastic fixture",
      .material_temper = "not applicable",
      .material_product_form = "analytic tetrahedron",
      .material_applicability = "assumed",
      .material_evidence_sha256 =
          "sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
      .mesh_sha256 =
          "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
      .restraint_surface_groups = {"analytic-fixed-face"},
      .load_surface_groups = {"analytic-load-node"},
      .reviewed_force_magnitude_n = 1000.0,
      .reviewed_force_direction = {0.0, 0.0, -1.0},
      .selected_load_area_m2 = 0.5,
      .mesh_target_size_m = 1.0,
      .minimum_mean_ratio_threshold = 0.01,
      .observed_minimum_mean_ratio = 0.8399473666,
      .displacement_limit_basis = "synthetic smoke threshold",
      .von_mises_limit_basis = "synthetic smoke threshold",
      .mesh_reviewed = true,
  };
}

} // namespace prometheus::structural
