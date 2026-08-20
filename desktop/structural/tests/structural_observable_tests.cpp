#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/structural/structural_observables.hpp"

#include <cmath>
#include <cstdlib>
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
  return 0;
}
