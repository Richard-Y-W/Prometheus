#include "prometheus/structural/structural_findings.hpp"

#include <stdexcept>

namespace prometheus::structural {
namespace {

StructuralFinding finding(std::string obligation, const double measured,
                          const double limit, std::string unit) {
  return {std::move(obligation),
          measured <= limit
              ? StructuralFindingDisposition::no_violation_detected_within_scope
              : StructuralFindingDisposition::violated,
          measured,
          limit,
          limit - measured,
          std::move(unit),
          "isotropic linear-elastic C3D4 model under the confirmed scenario"};
}

} // namespace

StructuralEvaluation compile_structural_findings(const StructuralRequest &request,
                                                 const SolverRunResult &run) {
  StructuralEvaluation result;
  result.execution_status = run.status;
  result.declared_obligations =
      static_cast<int>(request.displacement_limit_m.has_value()) +
      static_cast<int>(request.von_mises_limit_pa.has_value());
  result.limitation =
      "These findings do not establish safety, fatigue life, buckling, contact, "
      "fastener adequacy, nonlinear behavior, or project-wide correctness.";
  if (run.status != SolverRunStatus::completed) return result;
  if (!run.metrics)
    throw std::invalid_argument("Completed structural run is missing metrics");
  if (request.displacement_limit_m)
    result.findings.push_back(finding("maximum_displacement",
        run.metrics->maximum_displacement_m, *request.displacement_limit_m, "m"));
  if (request.von_mises_limit_pa)
    result.findings.push_back(finding("maximum_von_mises_stress",
        run.metrics->maximum_von_mises_pa, *request.von_mises_limit_pa, "Pa"));
  result.evaluated_obligations = static_cast<int>(result.findings.size());
  return result;
}

} // namespace prometheus::structural
