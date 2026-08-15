#pragma once

#include "prometheus/structural/types.hpp"

namespace prometheus::structural {

[[nodiscard]] std::vector<ValidationIssue>
validate_request(const StructuralRequest &request);

} // namespace prometheus::structural
