#pragma once

#include "prometheus/structural/types.hpp"

#include <string>

namespace prometheus::structural {

[[nodiscard]] std::string
generate_calculix_deck(const StructuralRequest &request);

} // namespace prometheus::structural
