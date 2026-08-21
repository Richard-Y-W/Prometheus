#pragma once

#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_refinement.hpp"
#include "prometheus/structural/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace prometheus::structural {

enum class StructuralFindingDisposition {
  no_violation_detected_within_scope,
  violated,
  cannot_answer
};

struct StructuralRefinementSummary final {
  StructuralRefinementStatus status{
      StructuralRefinementStatus::indeterminate};
  double displacement_change_fraction{};
  double stress_change_fraction{};
  double maximum_change_fraction{};
  double maximum_allowed_change_fraction{};
  std::vector<std::string> setup_sha256;
  std::vector<std::string> result_sha256;
  std::vector<StructuralObservableComparison> observables;
  std::vector<StructuralGlobalExtremumDiagnostic> global_extrema;
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

struct StructuralUnevaluatedObligation final {
  std::string obligation;
  std::string code;
  std::string detail;
};

struct StructuralEvaluation final {
  SolverRunStatus execution_status{SolverRunStatus::launch_failed};
  std::vector<StructuralFinding> findings;
  std::optional<StructuralRefinementSummary> comparison;
  int declared_obligations{};
  int evaluated_obligations{};
  std::string limitation;
  std::vector<StructuralUnevaluatedObligation> unknowns;
};

[[nodiscard]] StructuralEvaluation compile_structural_findings(
    const VerifiedStructuralRefinement &refinement);

// The modal_frequency analogue of compile_structural_findings. A modal run
// has no coarse/fine mesh-refinement pairing (see StructuralCapability's
// doc comment in types.hpp) -- this compiles a single completed run's
// eigenvalue result directly against the reviewed minimum-frequency
// obligation, using the same StructuralEvaluation/StructuralFinding shapes.
[[nodiscard]] StructuralEvaluation compile_modal_structural_findings(
    const CompiledStructuralSetup &setup, const SolverRunResult &run);

} // namespace prometheus::structural
