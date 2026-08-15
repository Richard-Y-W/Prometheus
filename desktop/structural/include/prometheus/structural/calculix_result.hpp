#pragma once

#include <cstddef>
#include <string_view>

namespace prometheus::structural {

struct CalculixMetrics final {
  double maximum_displacement_m{};
  double maximum_von_mises_pa{};
  std::size_t displacement_rows{};
  std::size_t stress_rows{};
};

[[nodiscard]] CalculixMetrics parse_calculix_dat(std::string_view rawDat);

} // namespace prometheus::structural
