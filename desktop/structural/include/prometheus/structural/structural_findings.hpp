#pragma once

#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/types.hpp"

#include <string>
#include <vector>

namespace prometheus::structural {

enum class StructuralFindingDisposition {
  no_violation_detected_within_scope,
  violated
};

struct StructuralFinding final {
  std::string obligation;
  StructuralFindingDisposition disposition{};
  double measured_value{};
  double limit_value{};
  double margin_to_limit{};
  std::string unit;
  std::string scope;
};

struct StructuralEvaluation final {
  SolverRunStatus execution_status{SolverRunStatus::launch_failed};
  std::vector<StructuralFinding> findings;
  int declared_obligations{};
  int evaluated_obligations{};
  std::string limitation;
};

[[nodiscard]] StructuralEvaluation compile_structural_findings(
    const StructuralRequest &request, const SolverRunResult &run);

} // namespace prometheus::structural
