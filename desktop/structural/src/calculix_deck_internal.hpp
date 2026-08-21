#pragma once

#include "prometheus/structural/types.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace prometheus::structural::detail {

// CalculiX input uses ten-digit scientific tokens. Geometry reconstructed
// from those authoritative deck coordinates can accumulate a few round-off
// units in derived areas and loads.
inline constexpr double calculixDeckRoundTripRelativeTolerance = 1.0e-9;
inline constexpr double calculixDeckRoundTripAbsoluteTolerance = 1.0e-15;

[[nodiscard]] inline bool calculix_deck_round_trip_number_equivalent(
    const double stored, const double replayed) noexcept {
  if (!std::isfinite(stored) || !std::isfinite(replayed))
    return false;
  if (stored == replayed)
    return true;
  const double scale = std::max(std::abs(stored), std::abs(replayed));
  return std::abs(stored - replayed) <=
         std::max(calculixDeckRoundTripAbsoluteTolerance,
                  calculixDeckRoundTripRelativeTolerance * scale);
}

[[nodiscard]] std::string generate_validated_calculix_deck(
    const StructuralRequest &request);

[[nodiscard]] bool calculix_decks_round_trip_equivalent(
    const std::string &stored, const std::string &replayed);

} // namespace prometheus::structural::detail
