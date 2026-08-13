#include <prometheus/execution/numeric_profile.hpp>

#include "numeric_profile_internal.hpp"

#include <cfenv>
#include <string>

#ifndef PROMETHEUS_BACKEND_BUILD_FINGERPRINT
#error "backend build fingerprint must be supplied by CMake"
#endif
#ifndef PROMETHEUS_COMPILER_ID
#error "compiler ID must be supplied by CMake"
#endif
#ifndef PROMETHEUS_COMPILER_VERSION
#error "compiler version must be supplied by CMake"
#endif

namespace prometheus::execution {
namespace {

Result<NumericProfile> unavailable(const std::string &message) {
  return Result<NumericProfile>::failure(
      Diagnostic{"numeric_profile", "unsupported_numeric_profile", message,
                 std::nullopt, std::nullopt});
}

bool complete(const detail::PlatformRuntimeIdentity &identity) {
  return !identity.operating_system.name.empty() &&
         !identity.operating_system.release.empty() &&
         !identity.operating_system.architecture.empty() &&
         !identity.standard_library.id.empty() &&
         !identity.standard_library.version.empty() &&
         !identity.math_runtime.id.empty() &&
         !identity.math_runtime.version.empty();
}

} // namespace

Result<NumericProfile> collect_numeric_profile() {
  if (std::fegetround() != FE_TONEAREST) {
    return unavailable("floating-point rounding mode is not to-nearest");
  }
  const auto platform = detail::platform_runtime_identity();
  if (!platform.has_value() || !complete(*platform)) {
    return unavailable("platform numeric execution identity is unavailable");
  }
  const std::string compiler_id{PROMETHEUS_COMPILER_ID};
  const std::string compiler_version{PROMETHEUS_COMPILER_VERSION};
  const std::string build_fingerprint{PROMETHEUS_BACKEND_BUILD_FINGERPRINT};
  if (compiler_id.empty() || compiler_version.empty() ||
      build_fingerprint.size() != 71U ||
      !build_fingerprint.starts_with("sha256:")) {
    return unavailable("compiled numeric execution identity is invalid");
  }
  return Result<NumericProfile>::success(NumericProfile{
      platform->operating_system,
      ToolIdentity{compiler_id, compiler_version},
      platform->standard_library,
      platform->math_runtime,
      build_fingerprint,
      FloatingPointPolicy{"disabled", false, "to_nearest"},
      "1.0.0",
  });
}

} // namespace prometheus::execution
