#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_observables.hpp"
#include "prometheus/structural/structural_refinement.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps = prometheus::structural;

namespace {

[[noreturn]] void fail(const std::string_view message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const std::string_view message) {
  if (!condition) fail(message);
}

template <typename Function>
void requireThrowsCode(Function &&function, const std::string_view code,
                       const std::string_view message) {
  try {
    std::forward<Function>(function)();
  } catch (const std::invalid_argument &error) {
    require(std::string_view(error.what()).starts_with(code), message);
    return;
  }
  fail(message);
}

bool hasIssue(const ps::StructuralObservableCompilation &compilation,
              const std::string_view code) {
  for (const auto &issue : compilation.issues)
    if (issue.code == code) return true;
  return false;
}

ps::CompiledCalculixResult completeResult(
    const ps::CompiledStructuralSetup &setup) {
  ps::CompiledCalculixResult result;
  for (const auto &node : setup.request.nodes) {
    const double magnitude = static_cast<double>(node.id) * 1.0e-6;
    result.normalized.displacements.push_back(
        {.node_id = node.id,
         .x_m = magnitude,
         .y_m = 0.0,
         .z_m = 0.0,
         .magnitude_m = magnitude,
         .time = 1.0});
  }
  for (const auto &element : setup.request.elements) {
    const double stress = static_cast<double>(element.id) * 1.0e5;
    result.normalized.stresses.push_back(
        {.element_id = element.id,
         .integration_point = 1,
         .xx_pa = stress,
         .yy_pa = 0.0,
         .zz_pa = 0.0,
         .xy_pa = 0.0,
         .xz_pa = 0.0,
         .yz_pa = 0.0,
         .von_mises_pa = stress,
         .time = 1.0});
  }
  result.metrics = ps::summarize_calculix_dat(result.normalized);
  result.convergence = ps::CalculixConvergenceEvidence{
      .step = 1,
      .increment = 1,
      .attempt = 1,
      .iterations = 1,
      .total_time = 1.0,
      .step_time = 1.0,
      .increment_time = 1.0};
  result.compiled_setup_identity = setup.identity;
  result.identity =
      "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  result.backend =
      {.executable_sha256 =
           "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
       .version = "CalculiX Version 2.23"};
  const ps::CalculixArtifactIdentity artifact{
      .sha256 =
          "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
      .byte_length = 1U};
  result.artifacts = {.deck = artifact,
                      .sta = artifact,
                      .dat = artifact,
                      .frd = artifact,
                      .standard_output = artifact,
                      .standard_error = artifact};
  return result;
}

std::vector<ps::StructuralObservableSpec> validSpecs() {
  return {
      {.id = "test.maximum_displacement",
       .quantity =
           ps::StructuralObservableQuantity::displacement_magnitude_m,
       .reduction = ps::StructuralObservableReduction::maximum,
       .region = {.kind = ps::StructuralObservableRegionKind::all_nodes},
       .maximum_change_fraction = 0.10},
      {.id = "test.section_stress",
       .quantity = ps::StructuralObservableQuantity::von_mises_stress_pa,
       .reduction = ps::StructuralObservableReduction::maximum,
       .region =
           {.kind =
                ps::StructuralObservableRegionKind::element_centroid_box_m,
            .element_centroid_box_m =
                {.minimum_m = {0.25, 0.0, -0.05},
                 .maximum_m = {0.50, 0.1, 0.05}}},
       .maximum_change_fraction = 0.10}};
}

std::array<double, 3> elementCentroid(
    const ps::CompiledStructuralSetup &setup,
    const ps::Tetrahedron &element) {
  std::array<double, 3> centroid{};
  for (const int nodeId : element.node_ids) {
    const auto node = std::ranges::find_if(
        setup.request.nodes,
        [&](const auto &candidate) { return candidate.id == nodeId; });
    require(node != setup.request.nodes.end(),
            "synthetic result element nodes exist");
    for (std::size_t axis = 0; axis < 3U; ++axis)
      centroid[axis] += node->position_m[axis] / 4.0;
  }
  return centroid;
}

ps::CompiledCalculixResult studyResult(
    const ps::CompiledStructuralSetup &setup, const double displacement,
    const double regionalStress, const double globalPeak,
    const char identityCharacter) {
  auto result = completeResult(setup);
  for (auto &row : result.normalized.displacements) {
    row.x_m = displacement;
    row.y_m = 0.0;
    row.z_m = 0.0;
    row.magnitude_m = displacement;
  }
  for (auto &row : result.normalized.stresses) {
    const auto element = std::ranges::find_if(
        setup.request.elements,
        [&](const auto &candidate) { return candidate.id == row.element_id; });
    require(element != setup.request.elements.end(),
            "synthetic stress element exists");
    const auto centroid = elementCentroid(setup, *element);
    const bool inRegion = centroid[0] >= 0.25 && centroid[0] <= 0.50 &&
                          centroid[1] >= 0.0 && centroid[1] <= 0.1 &&
                          centroid[2] >= -0.05 && centroid[2] <= 0.05;
    row.xx_pa = inRegion ? regionalStress : 1.0e5;
    row.von_mises_pa = row.xx_pa;
  }
  result.normalized.stresses.front().xx_pa = globalPeak;
  result.normalized.stresses.front().von_mises_pa = globalPeak;
  result.metrics = ps::summarize_calculix_dat(result.normalized);
  result.identity = "sha256:" + std::string(64U, identityCharacter);
  return result;
}

ps::SolverRunResult completedRun(ps::CompiledCalculixResult result) {
  return {.status = ps::SolverRunStatus::completed,
          .exit_code = 0,
          .elapsed = std::chrono::milliseconds(1),
          .standard_output = {},
          .standard_error = {},
          .detail = "synthetic completed run",
          .validated_result = std::move(result)};
}

} // namespace

int main() {
  const auto setup = ps::cantilever_benchmark(4, 2, 2).setup;
  const auto result = completeResult(setup);
  const auto definitions =
      ps::compile_structural_observable_definitions(validSpecs());
  const auto evaluated =
      ps::evaluate_structural_observables(definitions, setup, result);
  require(evaluated.complete() && evaluated.values.size() == 2U,
          "typed observables evaluate from one normalized result");
  require(evaluated.values[0].selected_rows == setup.request.nodes.size(),
          "all-node displacement requires complete node coverage");
  require(evaluated.values[1].selected_rows > 0U &&
              evaluated.values[1].selected_rows <
                  setup.request.elements.size(),
          "centroid box selects a strict physical element subset");

  auto changedId = validSpecs();
  changedId[0].id = "test.changed_displacement";
  auto changedBox = validSpecs();
  changedBox[1].region.element_centroid_box_m.maximum_m[0] = 0.75;
  auto changedThreshold = validSpecs();
  changedThreshold[0].maximum_change_fraction = 0.05;
  auto changedQuantityAndRegion = validSpecs();
  changedQuantityAndRegion[0].quantity =
      ps::StructuralObservableQuantity::von_mises_stress_pa;
  changedQuantityAndRegion[0].region.kind =
      ps::StructuralObservableRegionKind::all_elements;
  for (const auto &changed : {changedId, changedBox, changedThreshold,
                              changedQuantityAndRegion}) {
    const auto compiled =
        ps::compile_structural_observable_definitions(changed);
    require(compiled.front().identity != definitions.front().identity ||
                compiled.back().identity != definitions.back().identity,
            "every valid definition change alters canonical identity");
  }

  requireThrowsCode(
      [] { (void)ps::compile_structural_observable_definitions({}); },
      "refinement_observable_invalid", "empty observable list is rejected");
  auto duplicate = validSpecs();
  duplicate[1].id = duplicate[0].id;
  requireThrowsCode(
      [&] {
        (void)ps::compile_structural_observable_definitions(duplicate);
      },
      "refinement_observable_duplicate", "duplicate IDs are rejected");
  auto unsafe = validSpecs();
  unsafe[0].id = "Unsafe ID";
  requireThrowsCode(
      [&] { (void)ps::compile_structural_observable_definitions(unsafe); },
      "refinement_observable_invalid", "unsafe IDs are rejected");
  auto wrongPair = validSpecs();
  wrongPair[0].region.kind =
      ps::StructuralObservableRegionKind::all_elements;
  requireThrowsCode(
      [&] {
        (void)ps::compile_structural_observable_definitions(wrongPair);
      },
      "refinement_observable_invalid",
      "unsupported quantity and region pairs are rejected");
  auto reversed = validSpecs();
  reversed[1].region.element_centroid_box_m.minimum_m[0] = 0.75;
  requireThrowsCode(
      [&] { (void)ps::compile_structural_observable_definitions(reversed); },
      "refinement_region_invalid", "reversed boxes are rejected");
  auto nonfiniteBox = validSpecs();
  nonfiniteBox[1].region.element_centroid_box_m.maximum_m[1] =
      std::numeric_limits<double>::infinity();
  requireThrowsCode(
      [&] {
        (void)ps::compile_structural_observable_definitions(nonfiniteBox);
      },
      "refinement_region_invalid", "non-finite boxes are rejected");
  auto badThreshold = validSpecs();
  badThreshold[0].maximum_change_fraction = 0.0;
  requireThrowsCode(
      [&] {
        (void)ps::compile_structural_observable_definitions(badThreshold);
      },
      "refinement_observable_invalid", "invalid thresholds are rejected");
  auto badReduction = validSpecs();
  badReduction[0].reduction =
      static_cast<ps::StructuralObservableReduction>(99);
  requireThrowsCode(
      [&] {
        (void)ps::compile_structural_observable_definitions(badReduction);
      },
      "refinement_observable_invalid", "unknown reductions are rejected");

  auto missing = result;
  missing.normalized.displacements.pop_back();
  require(hasIssue(ps::evaluate_structural_observables(
                       definitions, setup, missing),
                   "refinement_observable_row_missing"),
          "missing selected rows fail closed");
  auto duplicateRow = result;
  duplicateRow.normalized.displacements.push_back(
      duplicateRow.normalized.displacements.front());
  require(hasIssue(ps::evaluate_structural_observables(
                       definitions, setup, duplicateRow),
                   "refinement_observable_row_duplicate"),
          "duplicate selected rows fail closed");
  auto unknown = result;
  unknown.normalized.stresses.front().element_id = 999999;
  require(hasIssue(ps::evaluate_structural_observables(
                       definitions, setup, unknown),
                   "refinement_observable_entity_unknown"),
          "unknown result entities fail closed");
  auto nonfinite = result;
  nonfinite.normalized.displacements.front().magnitude_m =
      std::numeric_limits<double>::quiet_NaN();
  require(hasIssue(ps::evaluate_structural_observables(
                       definitions, setup, nonfinite),
                   "refinement_observable_nonfinite"),
          "non-finite values fail closed");
  auto negative = result;
  negative.normalized.stresses.front().von_mises_pa = -1.0;
  require(hasIssue(ps::evaluate_structural_observables(
                       definitions, setup, negative),
                   "refinement_observable_nonfinite"),
          "negative values fail closed");
  auto wrongIntegrationPoint = result;
  wrongIntegrationPoint.normalized.stresses.front().integration_point = 2;
  const auto allElements = ps::compile_structural_observable_definitions(
      ps::global_structural_observable_specs(0.10));
  require(hasIssue(ps::evaluate_structural_observables(
                       allElements, setup, wrongIntegrationPoint),
                   "refinement_observable_row_missing"),
          "wrong integration points fail closed");
  auto emptyRegionSpecs = validSpecs();
  emptyRegionSpecs[1].region.element_centroid_box_m =
      {.minimum_m = {2.0, 2.0, 2.0},
       .maximum_m = {3.0, 3.0, 3.0}};
  const auto emptyRegionDefinitions =
      ps::compile_structural_observable_definitions(emptyRegionSpecs);
  require(hasIssue(ps::evaluate_structural_observables(
                       emptyRegionDefinitions, setup, result),
                   "refinement_region_empty"),
          "empty physical regions fail closed");

  const auto coarseSetup = ps::cantilever_benchmark(4, 2, 2).setup;
  const auto fineSetup = ps::cantilever_benchmark(8, 4, 4).setup;
  auto pairSpecs = validSpecs();
  pairSpecs[0].id = "study.maximum_displacement";
  pairSpecs[1].id = "study.section_stress";
  const auto criterion =
      ps::compile_structural_refinement_criterion(pairSpecs);
  const ps::SolverRunOptions coarseOptions{
      .executable = "synthetic-ccx",
      .working_directory = std::filesystem::temp_directory_path(),
      .job_name = "observable_coarse",
      .timeout = std::chrono::seconds(1)};
  const ps::SolverRunOptions fineOptions{
      .executable = "synthetic-ccx",
      .working_directory = std::filesystem::temp_directory_path(),
      .job_name = "observable_fine",
      .timeout = std::chrono::seconds(1)};
  const auto coarseSample = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::coarse, criterion, coarseOptions,
      coarseSetup,
      completedRun(studyResult(coarseSetup, 1.0e-3, 5.0e6, 10.0e6,
                               'b')));
  const auto fineSample = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, criterion, fineOptions, fineSetup,
      completedRun(studyResult(fineSetup, 1.02e-3, 5.1e6, 11.2e6,
                               'c')));
  const auto correspondence =
      ps::review_structural_boundary_correspondence(
          coarseSetup, fineSetup, true, true);
  const auto scoped = ps::compile_structural_refinement(
      coarseSample, fineSample, correspondence);
  require(scoped.complete() &&
              scoped.value()->status() ==
                  ps::StructuralRefinementStatus::accepted &&
              scoped.value()->observable_comparisons().size() == 2U,
          "all declared scoped observables control refinement acceptance");
  const auto stressComparison = std::ranges::find_if(
      scoped.value()->observable_comparisons(), [](const auto &comparison) {
        return comparison.definition.spec.quantity ==
            ps::StructuralObservableQuantity::von_mises_stress_pa;
      });
  require(stressComparison !=
              scoped.value()->observable_comparisons().end() &&
              stressComparison->change_fraction < 0.02 &&
              stressComparison->status ==
                  ps::StructuralObservableConvergenceStatus::accepted,
          "regional stress comparison accepts its stable physical window");
  const auto stressDiagnostic = std::ranges::find_if(
      scoped.value()->global_extremum_diagnostics(), [](const auto &diagnostic) {
        return diagnostic.quantity ==
            ps::StructuralObservableQuantity::von_mises_stress_pa;
      });
  require(stressDiagnostic !=
              scoped.value()->global_extremum_diagnostics().end() &&
              stressDiagnostic->change_fraction > 0.10 &&
              !stressDiagnostic->participated_in_acceptance &&
              !stressDiagnostic->within_threshold &&
              stressDiagnostic->coarse_entity_id == 1 &&
              stressDiagnostic->fine_entity_id == 1,
          "unstable global peak remains located and diagnostic-only");

  auto unstableFineResult =
      studyResult(fineSetup, 1.2e-3, 5.1e6, 11.2e6, 'd');
  const auto unstableFineSample = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, criterion, fineOptions, fineSetup,
      completedRun(std::move(unstableFineResult)));
  const auto unstable = ps::compile_structural_refinement(
      coarseSample, unstableFineSample, correspondence);
  require(unstable.complete() &&
              unstable.value()->status() ==
                  ps::StructuralRefinementStatus::indeterminate,
          "one above-threshold required observable makes the pair indeterminate");

  const auto scopedEvaluation =
      ps::compile_structural_findings(*scoped.value());
  require(scopedEvaluation.declared_obligations == 2 &&
              scopedEvaluation.evaluated_obligations == 1 &&
              scopedEvaluation.findings.size() == 1U &&
              scopedEvaluation.findings.front().obligation ==
                  "maximum_displacement" &&
              scopedEvaluation.unknowns.size() == 1U &&
              scopedEvaluation.unknowns.front().obligation ==
                  "maximum_von_mises_stress" &&
              scopedEvaluation.unknowns.front().code ==
                  "matching_converged_scope_missing" &&
              scopedEvaluation.unknowns.front().detail ==
                  "The global stress obligation has no accepted all-elements "
                  "stress observable.",
          "regional convergence cannot authorize a global stress finding");

  const auto globalCriterion =
      ps::compile_structural_refinement_criterion(
          ps::global_structural_observable_specs(0.10));
  const auto globalCoarse = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::coarse, globalCriterion, coarseOptions,
      coarseSetup,
      completedRun(studyResult(coarseSetup, 1.0e-3, 5.0e6, 10.0e6,
                               'e')));
  const auto globalFine = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, globalCriterion, fineOptions,
      fineSetup,
      completedRun(studyResult(fineSetup, 1.02e-3, 5.1e6, 10.5e6,
                               'f')));
  const auto globalPair = ps::compile_structural_refinement(
      globalCoarse, globalFine, correspondence);
  const auto globalEvaluation =
      ps::compile_structural_findings(*globalPair.value());
  require(globalPair.complete() &&
              globalPair.value()->status() ==
                  ps::StructuralRefinementStatus::accepted &&
              globalEvaluation.evaluated_obligations == 2 &&
              globalEvaluation.findings.size() == 2U &&
              globalEvaluation.unknowns.empty(),
          "accepted global observables support both global findings");

  const auto unstableEvaluation =
      ps::compile_structural_findings(*unstable.value());
  require(unstableEvaluation.evaluated_obligations == 0 &&
              unstableEvaluation.findings.empty() &&
              unstableEvaluation.unknowns.size() == 2U &&
              std::ranges::all_of(
                  unstableEvaluation.unknowns, [](const auto &unknown) {
                    return unknown.code ==
                        "refinement_observable_not_converged";
                  }),
          "an indeterminate pair retains both obligations as unknown");
  return 0;
}
