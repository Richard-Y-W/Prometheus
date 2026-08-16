#include "prometheus/structural/structural_findings.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace prometheus::structural {
namespace {

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool valid_result(const CompiledCalculixResult &result) {
  if (!result.complete() || !strict_sha256(result.identity) ||
      !result.metrics ||
      !std::isfinite(result.metrics->maximum_displacement_m) ||
      result.metrics->maximum_displacement_m < 0.0 ||
      !std::isfinite(result.metrics->maximum_von_mises_pa) ||
      result.metrics->maximum_von_mises_pa < 0.0 ||
      result.metrics->displacement_rows == 0U ||
      result.metrics->stress_rows == 0U ||
      result.metrics->displacement_rows !=
          result.normalized.displacements.size() ||
      result.metrics->stress_rows != result.normalized.stresses.size())
    return false;
  return strict_sha256(result.backend.executable_sha256) &&
         !result.backend.version.empty() &&
         strict_sha256(result.artifacts.deck.sha256) &&
         result.artifacts.deck.byte_length > 0U &&
         strict_sha256(result.artifacts.sta.sha256) &&
         result.artifacts.sta.byte_length > 0U &&
         strict_sha256(result.artifacts.dat.sha256) &&
         result.artifacts.dat.byte_length > 0U &&
         strict_sha256(result.artifacts.frd.sha256) &&
         result.artifacts.frd.byte_length > 0U &&
         strict_sha256(result.artifacts.standard_output.sha256) &&
         strict_sha256(result.artifacts.standard_error.sha256);
}

bool valid_refinement(const StructuralRefinementEvidence &refinement) {
  if (!refinement.complete || !refinement.criteria_satisfied ||
      !std::isfinite(refinement.coarse_to_fine_change_fraction) ||
      refinement.coarse_to_fine_change_fraction < 0.0 ||
      !std::isfinite(refinement.maximum_allowed_change_fraction) ||
      refinement.maximum_allowed_change_fraction <= 0.0 ||
      refinement.maximum_allowed_change_fraction > 1.0 ||
      refinement.coarse_to_fine_change_fraction >
          refinement.maximum_allowed_change_fraction ||
      refinement.result_sha256.size() < 2U ||
      refinement.result_sha256.size() > 16U)
    return false;
  std::set<std::string> identities;
  for (const auto &identity : refinement.result_sha256)
    if (!strict_sha256(identity) || !identities.insert(identity).second)
      return false;
  return true;
}

StructuralFinding finding(std::string obligation, const double measured,
                          const double limit, std::string unit,
                          std::vector<std::string> evidence) {
  const auto margin = limit - measured;
  return {std::move(obligation),
          margin > 0.0
              ? StructuralFindingDisposition::no_violation_detected_within_scope
              : StructuralFindingDisposition::violated,
          measured,
          limit,
          margin,
          std::move(unit),
          "isotropic linear-elastic C3D4 model under the confirmed scenario "
          "with accepted mesh-refinement evidence",
          std::move(evidence),
          {"small-deformation linear static response",
           "isotropic linear-elastic material behavior",
           "reviewed loads and fully fixed restraints represent the scenario",
           "reported extrema are bounded by the submitted mesh and solver output"}};
}

} // namespace

StructuralEvaluation compile_structural_findings(
    const StructuralRequest &request,
    const std::optional<CompiledCalculixResult> &validatedResult,
    const std::optional<StructuralRefinementEvidence> &refinement) {
  StructuralEvaluation result;
  result.declared_obligations =
      static_cast<int>(request.displacement_limit_m.has_value()) +
      static_cast<int>(request.von_mises_limit_pa.has_value());
  result.limitation =
      "These findings do not establish safety, fatigue life, buckling, contact, "
      "fastener adequacy, nonlinear behavior, or project-wide correctness.";
  const bool resultValid =
      validatedResult && valid_result(*validatedResult);
  if (resultValid)
    result.execution_status = SolverRunStatus::completed;
  else if (validatedResult)
    result.execution_status = SolverRunStatus::result_invalid;
  if (!resultValid || !refinement ||
      !valid_refinement(*refinement))
    return result;
  result.refinement = *refinement;

  const auto validLimit = [](const std::optional<double> value,
                             const std::string &basis) {
    return !value || (std::isfinite(*value) && *value > 0.0 &&
                      !basis.empty());
  };
  if (!request.requirements_reviewed || !request.scenario_confirmed ||
      !validLimit(request.displacement_limit_m,
                  request.displacement_limit_basis) ||
      !validLimit(request.von_mises_limit_pa,
                  request.von_mises_limit_basis))
    return result;

  auto evidence = refinement->result_sha256;
  evidence.push_back(validatedResult->identity);
  std::ranges::sort(evidence);
  evidence.erase(std::unique(evidence.begin(), evidence.end()), evidence.end());
  if (request.displacement_limit_m)
    result.findings.push_back(finding("maximum_displacement",
        validatedResult->metrics->maximum_displacement_m,
        *request.displacement_limit_m, "m", evidence));
  if (request.von_mises_limit_pa)
    result.findings.push_back(finding("maximum_von_mises_stress",
        validatedResult->metrics->maximum_von_mises_pa,
        *request.von_mises_limit_pa, "Pa", std::move(evidence)));
  result.evaluated_obligations = static_cast<int>(result.findings.size());
  return result;
}

} // namespace prometheus::structural
