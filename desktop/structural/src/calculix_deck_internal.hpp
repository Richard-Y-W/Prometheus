#pragma once

#include "prometheus/structural/types.hpp"

#include <string>

namespace prometheus::structural::detail {

[[nodiscard]] std::string generate_validated_calculix_deck(
    const StructuralRequest &request);

} // namespace prometheus::structural::detail
