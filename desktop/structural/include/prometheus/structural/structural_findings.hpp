#pragma once

#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace prometheus::structural {

enum class StructuralFindingDisposition {
  no_violation_detected_within_scope,
  violated
};

struct StructuralRefinementEvidence final {
  bool complete{};
  bool criteria_satisfied{};
  double coarse_to_fine_change_fraction{};
  double maximum_allowed_change_fraction{};
  std::vector<std::string> result_sha256;
};

struct StructuralFinding final {
  std::string obligation;
  StructuralFindingDisposition disposition{};
  double measured_value{};
  double limit_value{};
  double margin_to_limit{};
  std::string unit;
  std::string scope;
  std::vector<std::string> evidence_sha256;
  std::vector<std::string> assumptions;
};

struct StructuralEvaluation final {
  SolverRunStatus execution_status{SolverRunStatus::launch_failed};
  std::vector<StructuralFinding> findings;
  int declared_obligations{};
  int evaluated_obligations{};
  std::string limitation;
};

[[nodiscard]] StructuralEvaluation compile_structural_findings(
    const StructuralRequest &request,
    const std::optional<CompiledCalculixResult> &validated_result,
    const std::optional<StructuralRefinementEvidence> &refinement);

} // namespace prometheus::structural
