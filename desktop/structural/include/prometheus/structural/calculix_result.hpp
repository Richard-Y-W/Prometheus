#pragma once

#include "prometheus/structural/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prometheus::structural {

struct CompiledStructuralSetup;

struct NodalDisplacement final {
  int node_id{};
  double x_m{};
  double y_m{};
  double z_m{};
  double magnitude_m{};
  double time{};
};

struct ElementStress final {
  int element_id{};
  int integration_point{};
  double xx_pa{};
  double yy_pa{};
  double zz_pa{};
  double xy_pa{};
  double xz_pa{};
  double yz_pa{};
  double von_mises_pa{};
  double time{};
};

struct CalculixDat final {
  std::vector<NodalDisplacement> displacements;
  std::vector<ElementStress> stresses;
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
  std::string frd_sha256;
  std::uintmax_t frd_byte_length{};
};

struct CalculixResultIssue final {
  std::string code;
  std::string message;
};

struct CalculixArtifactIdentity final {
  std::string sha256;
  std::uintmax_t byte_length{};

  bool operator==(const CalculixArtifactIdentity &) const = default;
};

struct CalculixArtifactIdentities final {
  CalculixArtifactIdentity deck;
  CalculixArtifactIdentity sta;
  CalculixArtifactIdentity dat;
  CalculixArtifactIdentity frd;
  CalculixArtifactIdentity standard_output;
  CalculixArtifactIdentity standard_error;
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
  CalculixArtifactIdentities artifacts;
  CalculixBackendIdentity backend;
  std::optional<CalculixConvergenceEvidence> convergence;
  std::string compiled_setup_identity;
  std::string identity;

  [[nodiscard]] bool complete() const {
    return metrics.has_value() && issues.empty() && convergence.has_value() &&
           !identity.empty();
  }
};

[[nodiscard]] CalculixDat parse_calculix_dat(std::string_view raw_dat);

[[nodiscard]] CalculixMetrics summarize_calculix_dat(
    const CalculixDat &normalized);

// Replays or independently verifies evidence against a validated request.
[[nodiscard]] CompiledCalculixResult compile_calculix_result(
    const StructuralRequest &request, const CalculixRunEvidence &evidence);

// Active execution overload: consumes the already compiled deck and does not
// regenerate or revalidate the setup.
[[nodiscard]] CompiledCalculixResult compile_calculix_result(
    const CompiledStructuralSetup &setup,
    const CalculixRunEvidence &evidence);

} // namespace prometheus::structural
