#pragma once

#include <prometheus/execution/diagnostic.hpp>

#include <string>

namespace prometheus::execution {

struct PlatformIdentity final {
  std::string name;
  std::string release;
  std::string architecture;
  bool operator==(const PlatformIdentity &) const = default;
};

struct ToolIdentity final {
  std::string id;
  std::string version;
  bool operator==(const ToolIdentity &) const = default;
};

struct FloatingPointPolicy final {
  std::string contraction;
  bool fast_math;
  std::string rounding_mode;
  bool operator==(const FloatingPointPolicy &) const = default;
};

struct NumericProfile final {
  PlatformIdentity operating_system;
  ToolIdentity compiler;
  ToolIdentity standard_library;
  ToolIdentity math_runtime;
  std::string backend_build_fingerprint;
  FloatingPointPolicy floating_point;
  std::string numeric_serialization_version;
  bool operator==(const NumericProfile &) const = default;
};

[[nodiscard]] Result<NumericProfile> collect_numeric_profile();

} // namespace prometheus::execution
