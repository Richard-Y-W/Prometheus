#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace prometheus::structural {

// The two validated CalculiX capabilities a reviewed setup's requirements
// can resolve to (structural_setup.cpp's recommend_capability). A setup
// that reviews requirements for both at once is rejected
// (requirement_capability_ambiguous) rather than silently answering only
// one side -- this architecture compiles one request per review.
enum class StructuralCapability { linear_static, modal_frequency };

struct Node final {
  int id{};
  std::array<double, 3> position_m{};
};

struct Tetrahedron final {
  int id{};
  std::array<int, 4> node_ids{};
};

struct NodalForce final {
  int node_id{};
  std::array<double, 3> force_n{};
};

struct StructuralRequest final {
  std::string schema{"urn:prometheus:calculix-structural-request:0.2.0"};
  std::string analysis_id;
  std::string component_name;
  std::string geometry_sha256;
  std::vector<Node> nodes;
  std::vector<Tetrahedron> elements;
  double youngs_modulus_pa{};
  double poisson_ratio{};
  std::vector<int> fully_fixed_node_ids;
  std::vector<NodalForce> nodal_forces;
  std::optional<double> displacement_limit_m;
  std::optional<double> von_mises_limit_pa;
  bool material_reviewed{};
  bool loads_reviewed{};
  bool restraints_reviewed{};
  bool requirements_reviewed{};
  bool scenario_confirmed{};
  std::string material_designation;
  std::string material_temper;
  std::string material_product_form;
  std::string material_applicability;
  std::string material_evidence_sha256;
  std::string mesh_sha256;
  double mesh_coordinate_scale_to_m{1.0};
  double reviewed_force_magnitude_n{};
  std::array<double, 3> reviewed_force_direction{};
  double selected_load_area_m2{};
  double mesh_target_size_m{};
  double minimum_mean_ratio_threshold{};
  double observed_minimum_mean_ratio{};
  std::string displacement_limit_basis;
  std::string von_mises_limit_basis;
  bool mesh_reviewed{};
  StructuralCapability capability{StructuralCapability::linear_static};
  // Required (and positive) only when capability == modal_frequency.
  std::optional<double> density_kg_m3;
  std::optional<double> minimum_natural_frequency_hz;
  std::string minimum_natural_frequency_basis;
};

struct ValidationIssue final {
  std::string code;
  std::string message;
};

} // namespace prometheus::structural
