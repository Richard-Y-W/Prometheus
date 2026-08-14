#pragma once

#include <prometheus/execution/numeric_profile.hpp>

#include <optional>

namespace prometheus::execution::detail {

struct PlatformRuntimeIdentity final {
  PlatformIdentity operating_system;
  ToolIdentity standard_library;
  ToolIdentity math_runtime;
};

[[nodiscard]] std::optional<PlatformRuntimeIdentity>
platform_runtime_identity();

} // namespace prometheus::execution::detail
