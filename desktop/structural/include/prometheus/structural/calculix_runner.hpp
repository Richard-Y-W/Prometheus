#pragma once

#include "prometheus/structural/calculix_result.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace prometheus::structural {

enum class SolverRunStatus {
  completed,
  launch_failed,
  timed_out,
  nonzero_exit,
  output_conflict,
  output_missing,
  result_invalid
};

struct SolverRunResult final {
  SolverRunStatus status{SolverRunStatus::launch_failed};
  int exit_code{-1};
  std::chrono::milliseconds elapsed{};
  std::string standard_output;
  std::string standard_error;
  std::string detail;
  std::optional<CalculixMetrics> metrics;
};

struct SolverRunOptions final {
  std::filesystem::path executable;
  std::filesystem::path working_directory;
  std::string job_name;
  std::chrono::milliseconds timeout{std::chrono::minutes(5)};
};

[[nodiscard]] SolverRunResult run_calculix(const SolverRunOptions &options);

} // namespace prometheus::structural
