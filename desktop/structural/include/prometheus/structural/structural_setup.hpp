#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/surface_selection.hpp"
#include "prometheus/structural/types.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace prometheus::structural {

struct ReviewedMaterial final {
  std::string designation;
  std::string source_sha256;
  std::string applicability;
  double youngs_modulus_pa{};
  double poisson_ratio{};
  bool reviewed{};
};

struct ReviewedSurfaceLoad final {
  BoundarySelection selection;
  std::array<double, 3> total_force_n{};
  bool reviewed{};
};

struct ReviewedFixedRestraint final {
  BoundarySelection selection;
  bool reviewed{};
};

struct ReviewedStructuralRequirement final {
  std::optional<double> displacement_limit_m;
  std::optional<double> von_mises_limit_pa;
  std::string source_or_exploratory_rationale;
  bool reviewed{};
};

struct ReviewedMeshControls final {
  double minimum_size_m{};
  double maximum_size_m{};
  std::string mesher_identity;
  bool reviewed{};
};

struct StructuralSetup final {
  std::string analysis_id;
  std::string component_name;
  std::string geometry_sha256;
  VolumeMesh mesh;
  std::vector<BoundaryFace> boundary_faces;
  ReviewedMaterial material;
  ReviewedSurfaceLoad load;
  ReviewedFixedRestraint restraint;
  ReviewedStructuralRequirement requirement;
  ReviewedMeshControls mesh_controls;
  std::string scenario_description;
  bool scenario_confirmed{};
  double selection_patch_angle_degrees{15.0};
};

[[nodiscard]] std::vector<ValidationIssue> validate_setup(
    const StructuralSetup &setup);

[[nodiscard]] StructuralRequest compile_structural_request(
    const StructuralSetup &setup);

} // namespace prometheus::structural
