#pragma once

#include <cstdint>
#include <string>

namespace prometheus::decision {

enum class Verdict {
  satisfied_within_scope,
  requirements_violated,
  indeterminate
};

enum class Coverage { sufficient, insufficient, not_assessed };

enum class ExecutionState {
  not_started,
  ready,
  running,
  blocked,
  completed,
  completed_with_blocked_work,
  failed,
  cancelled
};

struct Counts final {
  std::uint64_t satisfied_within_scope{};
  std::uint64_t violated{};
  std::uint64_t indeterminate{};
  std::uint64_t not_applicable{};
  std::uint64_t not_evaluated{};
};

struct ProjectSummary final {
  Verdict verdict;
  Coverage coverage;
  ExecutionState execution_state;
  Counts counts;
  std::uint64_t obligation_total;
  std::string assessment_scope_id;
  std::string decision_core_name{"prometheus_cpp"};
  std::string decision_core_version{"1.0.0"};
};

[[nodiscard]] ProjectSummary summarize(Counts counts,
                                       ExecutionState execution_state,
                                       std::uint64_t obligation_total,
                                       std::string assessment_scope_id);

}  // namespace prometheus::decision
