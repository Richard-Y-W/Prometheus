#include "prometheus/decision/project_summary.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using prometheus::decision::Counts;
using prometheus::decision::Coverage;
using prometheus::decision::ExecutionState;
using prometheus::decision::Verdict;
using prometheus::decision::summarize;

constexpr auto kScopeId =
    "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Exception, typename Callable>
void expect_throws(Callable&& callable, const std::string& message) {
  try {
    callable();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error(message);
}

void test_truth_table() {
  struct Case {
    Counts counts;
    ExecutionState execution;
    Verdict verdict;
    Coverage coverage;
  };

  const std::vector<Case> cases{
      {{8, 2, 3, 1, 5}, ExecutionState::completed_with_blocked_work,
       Verdict::requirements_violated, Coverage::insufficient},
      {{3, 0, 0, 0, 0}, ExecutionState::completed,
       Verdict::satisfied_within_scope, Coverage::sufficient},
      {{2, 0, 1, 0, 0}, ExecutionState::completed, Verdict::indeterminate,
       Coverage::insufficient},
      {{0, 0, 0, 4, 0}, ExecutionState::not_started, Verdict::indeterminate,
       Coverage::not_assessed},
      {{0, 0, 0, 0, 2}, ExecutionState::failed, Verdict::indeterminate,
       Coverage::insufficient},
  };

  for (const auto& test_case : cases) {
    const auto total = test_case.counts.satisfied_within_scope +
                       test_case.counts.violated +
                       test_case.counts.indeterminate +
                       test_case.counts.not_applicable +
                       test_case.counts.not_evaluated;
    const auto summary =
        summarize(test_case.counts, test_case.execution, total, kScopeId);
    expect(summary.verdict == test_case.verdict, "unexpected verdict");
    expect(summary.coverage == test_case.coverage, "unexpected coverage");
    expect(summary.execution_state == test_case.execution,
           "execution state was changed");
    expect(summary.obligation_total == total, "obligation total was changed");
    expect(summary.assessment_scope_id == kScopeId, "scope ID was changed");
    expect(summary.decision_core_name == "prometheus_cpp",
           "decision core name is not authoritative");
    expect(summary.decision_core_version == "1.0.0",
           "decision core version is not explicit");
  }
}

void test_failure_and_satisfaction_boundaries() {
  constexpr std::array noncompleted_states{
      ExecutionState::blocked,
      ExecutionState::failed,
      ExecutionState::cancelled,
      ExecutionState::completed_with_blocked_work,
  };
  for (const auto state : noncompleted_states) {
    const auto summary = summarize({3, 0, 0, 0, 0}, state, 3, kScopeId);
    expect(summary.verdict == Verdict::indeterminate,
           "noncompleted work became satisfaction");
    expect(summary.coverage == Coverage::sufficient,
           "workflow state incorrectly changed coverage");
  }

  const auto violation =
      summarize({0, 1, 0, 0, 0}, ExecutionState::failed, 1, kScopeId);
  expect(violation.verdict == Verdict::requirements_violated,
         "confirmed violation did not dominate workflow failure");
}

void test_invalid_inputs() {
  expect_throws<std::overflow_error>(
      [] {
        (void)summarize(
            {std::numeric_limits<std::uint64_t>::max(), 1, 0, 0, 0},
            ExecutionState::completed,
            std::numeric_limits<std::uint64_t>::max(), kScopeId);
      },
      "count overflow was accepted");

  expect_throws<std::invalid_argument>(
      [] {
        (void)summarize({1, 0, 0, 0, 0}, ExecutionState::completed, 2,
                        kScopeId);
      },
      "count sum mismatch was accepted");

  const std::array invalid_scope_ids{
      "sha256:0123",
      "SHA256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "sha256:0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef",
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  };
  for (const auto* scope_id : invalid_scope_ids) {
    expect_throws<std::invalid_argument>(
        [scope_id] {
          (void)summarize({1, 0, 0, 0, 0}, ExecutionState::completed, 1,
                          scope_id);
        },
        "invalid scope hash spelling was accepted");
  }
}

}  // namespace

int main() {
  try {
    test_truth_table();
    test_failure_and_satisfaction_boundaries();
    test_invalid_inputs();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
