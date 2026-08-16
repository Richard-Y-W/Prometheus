#pragma once

#include "prometheus/structural/types.hpp"

#include <string>
#include <string_view>

namespace prometheus::structural {

inline constexpr std::string_view structural_case_schema =
    "urn:prometheus:structural-case:0.1.0";

struct CanonicalStructuralCase final {
  StructuralRequest request;
  std::string bytes;
  std::string object_hash;
};

[[nodiscard]] CanonicalStructuralCase
build_structural_case(const StructuralRequest &request);

[[nodiscard]] CanonicalStructuralCase
parse_structural_case(std::string_view canonical_bytes);

} // namespace prometheus::structural
