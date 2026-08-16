#include "prometheus/structural/structural_finding.hpp"

#include "prometheus/structural/calculix_deck.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace prometheus::structural {
namespace {

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool valid_positive(const std::optional<double> value) {
  return value.has_value() && std::isfinite(*value) && *value > 0.0;
}

std::vector<std::string>
result_evidence(const CompiledCalculixResult &result,
                const StructuralRefinementEvidence &refinement) {
  std::vector<std::string> hashes{
      result.artifacts.deck_sha256,
      result.artifacts.status_sha256,
      result.artifacts.data_sha256,
      result.artifacts.standard_output_sha256,
      result.artifacts.standard_error_sha256,
      result.backend.executable_sha256,
  };
  hashes.insert(hashes.end(), refinement.evidence_sha256.begin(),
                refinement.evidence_sha256.end());
  return hashes;
}

bool complete_refinement(const StructuralRefinementEvidence &refinement) {
  return refinement.complete && refinement.criteria_satisfied &&
         std::isfinite(
             refinement.medium_to_fine_displacement_change_fraction) &&
         refinement.medium_to_fine_displacement_change_fraction >= 0.0 &&
         std::isfinite(refinement.maximum_allowed_change_fraction) &&
         refinement.maximum_allowed_change_fraction >= 0.0 &&
         refinement.medium_to_fine_displacement_change_fraction <=
             refinement.maximum_allowed_change_fraction &&
         refinement.evidence_sha256.size() >= 2U &&
         std::set<std::string>(refinement.evidence_sha256.begin(),
                               refinement.evidence_sha256.end())
                 .size() == refinement.evidence_sha256.size() &&
         std::ranges::all_of(refinement.evidence_sha256, strict_sha256);
}

std::vector<std::string> assumptions(const StructuralRequest &request) {
  std::vector<std::string> values{
      "Linear-static, small-deformation, isotropic material model."};
  if (request.material_applicability == "assumed")
    values.push_back("Material applicability is explicitly assumed: " +
                     request.material_designation + " " +
                     request.material_temper + " " +
                     request.material_product_form + ".");
  return values;
}

StructuralFinding make_finding(const StructuralRequest &request,
                               const CompiledCalculixResult &result,
                               const StructuralRefinementEvidence &refinement,
                               const StructuralMetric metric,
                               const std::optional<double> observed,
                               const std::optional<double> limit,
                               const std::string &basis,
                               const std::string &unit) {
  StructuralFinding finding{
      .metric = metric,
      .requirement_id = request.analysis_id + ":" +
                        std::string(structural_metric_name(metric)),
      .scope = request.component_name + " / " + request.analysis_id,
      .observed_value = observed,
      .limit_value = limit,
      .unit = unit,
      .basis = basis,
      .evidence_sha256 = result_evidence(result, refinement),
      .assumptions = assumptions(request),
  };

  if (!result.complete()) {
    finding.explanation =
        "Solver execution, convergence, or result coverage is incomplete.";
    return finding;
  }

  bool deckMatches = false;
  try {
    deckMatches =
        prometheus::integrity::sha256_bytes(generate_calculix_deck(request)) ==
        result.artifacts.deck_sha256;
  } catch (const std::exception &) {
    deckMatches = false;
  }
  if (!deckMatches) {
    finding.explanation =
        "Result evidence does not match the current structural inputs.";
    return finding;
  }

  if (!complete_refinement(refinement)) {
    finding.explanation =
        "Predeclared mesh-refinement evidence is incomplete or outside its "
        "acceptance criterion.";
    return finding;
  }
  if (!valid_positive(limit) || basis.empty()) {
    finding.explanation = "No reviewed positive limit and basis is available.";
    return finding;
  }
  if (!observed.has_value() || !std::isfinite(*observed) || *observed < 0.0) {
    finding.explanation = "The requested normalized metric is unavailable.";
    return finding;
  }

  finding.status =
      *observed >= *limit ? FindingStatus::fail : FindingStatus::pass;
  finding.explanation =
      finding.status == FindingStatus::pass
          ? "Observed value is below the reviewed limit."
          : "Observed value equals or exceeds the reviewed limit.";
  return finding;
}

} // namespace

std::string_view finding_status_name(const FindingStatus status) {
  switch (status) {
  case FindingStatus::pass:
    return "pass";
  case FindingStatus::fail:
    return "fail";
  case FindingStatus::indeterminate:
    return "indeterminate";
  }
  throw std::invalid_argument("unknown structural finding status");
}

std::string_view structural_metric_name(const StructuralMetric metric) {
  switch (metric) {
  case StructuralMetric::maximum_displacement:
    return "maximum_displacement";
  case StructuralMetric::maximum_von_mises:
    return "maximum_von_mises";
  }
  throw std::invalid_argument("unknown structural metric");
}

std::vector<StructuralFinding>
compile_structural_findings(const StructuralRequest &request,
                            const CompiledCalculixResult &result,
                            const StructuralRefinementEvidence &refinement) {
  std::optional<double> displacement;
  std::optional<double> stress;
  if (result.metrics.has_value()) {
    displacement = result.metrics->maximum_displacement_m;
    stress = result.metrics->maximum_von_mises_pa;
  }
  return {
      make_finding(request, result, refinement,
                   StructuralMetric::maximum_displacement, displacement,
                   request.displacement_limit_m,
                   request.displacement_limit_basis, "m"),
      make_finding(request, result, refinement,
                   StructuralMetric::maximum_von_mises, stress,
                   request.von_mises_limit_pa, request.von_mises_limit_basis,
                   "Pa"),
  };
}

} // namespace prometheus::structural
