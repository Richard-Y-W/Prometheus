#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace prometheus::structural {

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
  std::string schema{"urn:prometheus:calculix-linear-static-request:0.1.0"};
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
};

struct ValidationIssue final {
  std::string code;
  std::string message;
};

} // namespace prometheus::structural
