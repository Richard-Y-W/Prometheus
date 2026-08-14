#include "prometheus/decision/project_summary.hpp"

#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace prometheus::decision {
namespace {

void add_checked(std::uint64_t& total, std::uint64_t value) {
  if (value > std::numeric_limits<std::uint64_t>::max() - total) {
    throw std::overflow_error("obligation count sum overflows uint64");
  }
  total += value;
}

bool is_lower_hex(char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

bool is_sha256_id(std::string_view value) {
  constexpr std::string_view prefix{"sha256:"};
  if (value.size() != prefix.size() + 64 || !value.starts_with(prefix)) {
    return false;
  }
  for (const char character : value.substr(prefix.size())) {
    if (!is_lower_hex(character)) {
      return false;
    }
  }
  return true;
}

}  // namespace

ProjectSummary summarize(Counts counts, ExecutionState execution_state,
                         std::uint64_t obligation_total,
                         std::string assessment_scope_id) {
  std::uint64_t count_sum = 0;
  add_checked(count_sum, counts.satisfied_within_scope);
  add_checked(count_sum, counts.violated);
  add_checked(count_sum, counts.indeterminate);
  add_checked(count_sum, counts.not_applicable);
  add_checked(count_sum, counts.not_evaluated);
  if (count_sum != obligation_total) {
    throw std::invalid_argument(
        "obligation counts must sum to obligation_total");
  }
  if (!is_sha256_id(assessment_scope_id)) {
    throw std::invalid_argument(
        "assessment_scope_id must be lowercase sha256 identity");
  }

  const auto applicable = count_sum - counts.not_applicable;
  const auto unresolved = counts.indeterminate + counts.not_evaluated;

  Coverage coverage = Coverage::sufficient;
  if (applicable == 0) {
    coverage = Coverage::not_assessed;
  } else if (unresolved != 0) {
    coverage = Coverage::insufficient;
  }

  Verdict verdict = Verdict::indeterminate;
  if (counts.violated != 0) {
    verdict = Verdict::requirements_violated;
  } else if (unresolved == 0 && applicable != 0 &&
             execution_state == ExecutionState::completed) {
    verdict = Verdict::satisfied_within_scope;
  }

  return {
      .verdict = verdict,
      .coverage = coverage,
      .execution_state = execution_state,
      .counts = counts,
      .obligation_total = obligation_total,
      .assessment_scope_id = std::move(assessment_scope_id),
  };
}

}  // namespace prometheus::decision
