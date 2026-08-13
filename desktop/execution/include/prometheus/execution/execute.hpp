#pragma once

#include <prometheus/execution/contracts.hpp>
#include <prometheus/execution/diagnostic.hpp>

#include <string>
#include <string_view>
#include <variant>

namespace prometheus::execution {

inline constexpr std::string_view analysis_result_schema_id =
    "urn:prometheus:schema:analysis-result:1.0.0";
inline constexpr std::string_view analysis_result_media_type =
    "application/vnd.prometheus.analysis-result+json;version=1.0.0";
inline constexpr std::string_view run_manifest_schema_id =
    "urn:prometheus:schema:run-manifest:1.0.0";
inline constexpr std::string_view run_manifest_media_type =
    "application/vnd.prometheus.run-manifest+json;version=1.0.0";

struct ExecutionInput final {
  std::string package_bytes;
  std::string expected_package_hash;
  std::string scenario_bytes;
  std::string expected_scenario_hash;
  std::string request_bytes;
  std::string expected_request_hash;
};

struct CompletedExecution final {
  CanonicalObject result;
  CanonicalObject manifest;
};

using ExecutionOutcome = std::variant<CompletedExecution, ExecutionFailure>;

[[nodiscard]] ExecutionOutcome execute(const ExecutionInput &input) noexcept;

} // namespace prometheus::execution
