#pragma once

#include "prometheus/structural/types.hpp"

#include <string_view>

namespace prometheus::structural {

inline constexpr std::string_view calculix_smoke_job_name =
    "prometheus_tetra_smoke";

[[nodiscard]] StructuralRequest structural_smoke_request();

} // namespace prometheus::structural
