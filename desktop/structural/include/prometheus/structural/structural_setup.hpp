#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/surface_selection.hpp"
#include "prometheus/structural/types.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
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

enum class RequirementQuantity { displacement, von_mises_stress, other };

enum class RequirementComparator { less_or_equal };

enum class RequirementCriticality { informational, advisory, critical };

// A requirement targets `displacement`/`von_mises_stress` (the only
// quantities the CalculiX linear-static capability can evaluate) or
// `other`, which records a real reviewed requirement this capability
// cannot answer instead of making it unrepresentable.
struct ReviewedRequirement final {
  RequirementQuantity quantity{RequirementQuantity::other};
  std::string other_quantity_description;
  RequirementComparator comparator{RequirementComparator::less_or_equal};
  double limit_value{};
  std::string unit;
  std::string applicability;
  RequirementCriticality criticality{RequirementCriticality::advisory};
  std::string source_or_exploratory_rationale;
  bool reviewed{};
};

[[nodiscard]] std::string_view to_string(RequirementQuantity value);
[[nodiscard]] std::string_view to_string(RequirementComparator value);
[[nodiscard]] std::string_view to_string(RequirementCriticality value);

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
  std::vector<ReviewedRequirement> requirements;
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
