#include "prometheus/structural/structural_findings.hpp"

#include <algorithm>
#include <cmath>

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

StructuralFinding finding(std::string obligation, const double measured,
                          const double limit, std::string unit,
                          std::vector<std::string> evidence,
                          std::string scope) {
  const auto margin = limit - measured;
  return {std::move(obligation),
          margin > 0.0
              ? StructuralFindingDisposition::no_violation_detected_within_scope
              : StructuralFindingDisposition::violated,
          measured,
          limit,
          margin,
          std::move(unit),
          std::move(scope),
          std::move(evidence),
          {"small-deformation linear static response",
           "isotropic linear-elastic material behavior",
           "reviewed loads and fully fixed restraints represent the scenario",
           "reported extrema are bounded by the submitted mesh and solver output"}};
}

bool has_accepted_global_observable(
    const VerifiedStructuralRefinement &refinement,
    const StructuralObservableQuantity quantity,
    const StructuralObservableRegionKind region) {
  if (refinement.coarse().criterion().legacy_global_extrema_only())
    return refinement.status() == StructuralRefinementStatus::accepted;
  return std::ranges::any_of(
      refinement.observable_comparisons(), [&](const auto &comparison) {
        return comparison.definition.spec.quantity == quantity &&
               comparison.definition.spec.reduction ==
                   StructuralObservableReduction::maximum &&
               comparison.definition.spec.region.kind == region &&
               comparison.status ==
                   StructuralObservableConvergenceStatus::accepted;
      });
}

void add_unknown(StructuralEvaluation &evaluation, std::string obligation,
                 std::string code, std::string detail) {
  evaluation.unknowns.push_back(
      {std::move(obligation), std::move(code), std::move(detail)});
}

} // namespace

StructuralEvaluation compile_structural_findings(
    const VerifiedStructuralRefinement &refinement) {
  StructuralEvaluation result;
  const auto &fineSample = refinement.fine();
  const auto &request = fineSample.setup().request;
  const auto &validatedResult = fineSample.run().validated_result;
  result.declared_obligations =
      static_cast<int>(request.displacement_limit_m.has_value()) +
      static_cast<int>(request.von_mises_limit_pa.has_value());
  result.limitation =
      "These findings do not establish safety, fatigue life, buckling, contact, "
      "fastener adequacy, nonlinear behavior, or project-wide correctness.";
  result.comparison = StructuralRefinementSummary{
      .status = refinement.status(),
      .displacement_change_fraction =
          refinement.displacement_change_fraction(),
      .stress_change_fraction = refinement.stress_change_fraction(),
      .maximum_change_fraction = refinement.maximum_change_fraction(),
      .maximum_allowed_change_fraction =
          fineSample.criterion().maximum_change_fraction(),
      .setup_sha256 = {refinement.coarse().setup().identity,
                       fineSample.setup().identity},
      .result_sha256 = {
          refinement.coarse().run().validated_result->identity,
          validatedResult->identity},
      .observables = refinement.observable_comparisons(),
      .global_extrema = refinement.global_extremum_diagnostics()};

  const bool resultValid = validatedResult && valid_result(*validatedResult);
  if (resultValid)
    result.execution_status = SolverRunStatus::completed;
  else if (validatedResult)
    result.execution_status = SolverRunStatus::result_invalid;
  const auto addDeclaredUnknowns = [&](const std::string &code,
                                       const std::string &detail) {
    if (request.displacement_limit_m)
      add_unknown(result, "maximum_displacement", code, detail);
    if (request.von_mises_limit_pa)
      add_unknown(result, "maximum_von_mises_stress", code, detail);
  };
  if (refinement.status() == StructuralRefinementStatus::indeterminate) {
    addDeclaredUnknowns(
        "refinement_observable_not_converged",
        "At least one required refinement observable exceeded its "
        "predeclared change threshold.");
    return result;
  }
  if (!resultValid) {
    addDeclaredUnknowns(
        "validated_result_invalid",
        "The fine solver result does not contain complete validated evidence.");
    return result;
  }

  const auto validLimit = [](const std::optional<double> value,
                             const std::string &basis) {
    return !value || (std::isfinite(*value) && *value > 0.0 &&
                      !basis.empty());
  };
  if (!request.requirements_reviewed || !request.scenario_confirmed ||
      !validLimit(request.displacement_limit_m,
                  request.displacement_limit_basis) ||
      !validLimit(request.von_mises_limit_pa,
                  request.von_mises_limit_basis)) {
    addDeclaredUnknowns(
        "structural_review_incomplete",
        "Reviewed requirements and a confirmed scenario are required.");
    return result;
  }

  std::vector<std::string> evidence{
      refinement.coarse().setup().identity,
      fineSample.setup().identity,
      refinement.coarse().run().validated_result->identity,
      validatedResult->identity};
  std::ranges::sort(evidence);
  evidence.erase(std::unique(evidence.begin(), evidence.end()),
                 evidence.end());
  if (evidence.size() != 4U) {
    addDeclaredUnknowns(
        "structural_evidence_incomplete",
        "Both setup and both result identities are required.");
    return result;
  }
  const auto legacyScope =
      "isotropic linear-elastic C3D4 model under the confirmed scenario "
      "with accepted mesh-refinement evidence";
  if (request.displacement_limit_m) {
    if (has_accepted_global_observable(
            refinement,
            StructuralObservableQuantity::displacement_magnitude_m,
            StructuralObservableRegionKind::all_nodes))
      result.findings.push_back(finding(
          "maximum_displacement",
          validatedResult->metrics->maximum_displacement_m,
          *request.displacement_limit_m, "m", evidence,
          refinement.coarse().criterion().legacy_global_extrema_only()
              ? legacyScope
              : "maximum displacement over all reviewed mesh nodes in the "
                "confirmed isotropic linear-elastic C3D4 scenario"));
    else
      add_unknown(
          result, "maximum_displacement",
          "matching_converged_scope_missing",
          "The global displacement obligation has no accepted all-nodes "
          "displacement observable.");
  }
  if (request.von_mises_limit_pa) {
    if (has_accepted_global_observable(
            refinement,
            StructuralObservableQuantity::von_mises_stress_pa,
            StructuralObservableRegionKind::all_elements))
      result.findings.push_back(finding(
          "maximum_von_mises_stress",
          validatedResult->metrics->maximum_von_mises_pa,
          *request.von_mises_limit_pa, "Pa", std::move(evidence),
          refinement.coarse().criterion().legacy_global_extrema_only()
              ? legacyScope
              : "maximum von Mises stress over all reviewed C3D4 elements "
                "in the confirmed isotropic linear-elastic scenario"));
    else
      add_unknown(
          result, "maximum_von_mises_stress",
          "matching_converged_scope_missing",
          "The global stress obligation has no accepted all-elements stress "
          "observable.");
  }
  result.evaluated_obligations = static_cast<int>(result.findings.size());
  if (result.declared_obligations !=
      result.evaluated_obligations +
          static_cast<int>(result.unknowns.size())) {
    result.findings.clear();
    result.unknowns.clear();
    result.evaluated_obligations = 0;
    addDeclaredUnknowns(
        "structural_coverage_inconsistent",
        "Declared obligations do not have exactly one finding or unknown.");
  }
  return result;
}

} // namespace prometheus::structural
