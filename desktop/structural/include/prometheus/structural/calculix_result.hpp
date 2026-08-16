#pragma once

#include "prometheus/structural/types.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prometheus::structural {

struct DisplacementRow final {
  int node_id{};
  double time{};
  std::array<double, 3> displacement_m{};
};

struct StressRow final {
  int element_id{};
  int integration_point{};
  double time{};
  std::array<double, 6> stress_pa{};
};

struct CalculixDat final {
  std::vector<DisplacementRow> displacements;
  std::vector<StressRow> stresses;
};

struct CalculixMetrics final {
  double maximum_displacement_m{};
  double maximum_von_mises_pa{};
  std::size_t displacement_rows{};
  std::size_t stress_rows{};
};

struct CalculixRunEvidence final {
  int process_exit_code{};
  std::string solver_executable_sha256;
  std::string solver_version;
  std::string deck_bytes;
  std::string standard_output;
  std::string standard_error;
  std::string status_bytes;
  std::string data_bytes;
};

struct CalculixResultIssue final {
  std::string code;
  std::string message;
};

struct CalculixArtifactHashes final {
  std::string deck_sha256;
  std::string status_sha256;
  std::string data_sha256;
  std::string standard_output_sha256;
  std::string standard_error_sha256;
};

struct CalculixBackendIdentity final {
  std::string executable_sha256;
  std::string version;
};

struct CalculixConvergenceEvidence final {
  int step{};
  int increment{};
  int attempt{};
  int iterations{};
  double total_time{};
  double step_time{};
  double increment_time{};
};

struct CompiledCalculixResult final {
  std::optional<CalculixMetrics> metrics;
  CalculixDat normalized;
  std::vector<CalculixResultIssue> issues;
  CalculixArtifactHashes artifacts;
  CalculixBackendIdentity backend;
  std::optional<CalculixConvergenceEvidence> convergence;

  [[nodiscard]] bool complete() const {
    return metrics.has_value() && issues.empty();
  }
};

[[nodiscard]] CalculixDat parse_calculix_dat(std::string_view raw_dat);

[[nodiscard]] CompiledCalculixResult compile_calculix_result(
    const StructuralRequest &request, const CalculixRunEvidence &evidence);

} // namespace prometheus::structural
