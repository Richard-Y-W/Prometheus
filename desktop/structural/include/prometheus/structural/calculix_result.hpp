#pragma once

#include "prometheus/structural/types.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace prometheus::structural {

struct NodalDisplacement final {
  int node_id{};
  double x_m{};
  double y_m{};
  double z_m{};
  double magnitude_m{};
};

struct ElementStress final {
  int element_id{};
  int integration_point{};
  double xx_pa{};
  double yy_pa{};
  double zz_pa{};
  double xy_pa{};
  double xz_pa{};
  double yz_pa{};
  double von_mises_pa{};
};

struct CalculixMetrics final {
  double maximum_displacement_m{};
  double maximum_von_mises_pa{};
  std::size_t displacement_rows{};
  std::size_t stress_rows{};
  std::vector<NodalDisplacement> displacements;
  std::vector<ElementStress> stresses;
};

[[nodiscard]] CalculixMetrics parse_calculix_dat(std::string_view rawDat);
[[nodiscard]] std::vector<ValidationIssue> validate_calculix_result_binding(
    const StructuralRequest &request, const CalculixMetrics &result);

} // namespace prometheus::structural
