#pragma once

#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/structural_setup.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace prometheus::structural {

enum class StructuralObservableQuantity {
  displacement_magnitude_m,
  von_mises_stress_pa
};

enum class StructuralObservableReduction { maximum };

enum class StructuralObservableRegionKind {
  all_nodes,
  all_elements,
  element_centroid_box_m
};

struct StructuralElementCentroidBox final {
  std::array<double, 3> minimum_m{};
  std::array<double, 3> maximum_m{};
};

struct StructuralObservableRegion final {
  StructuralObservableRegionKind kind{};
  StructuralElementCentroidBox element_centroid_box_m{};
};

struct StructuralObservableSpec final {
  std::string id;
  StructuralObservableQuantity quantity{};
  StructuralObservableReduction reduction{};
  StructuralObservableRegion region;
  double maximum_change_fraction{};
};

struct StructuralObservableDefinition final {
  StructuralObservableSpec spec;
  std::string identity;
};

struct StructuralObservableValue final {
  StructuralObservableDefinition definition;
  double value{};
  std::size_t selected_rows{};
};

struct StructuralObservableIssue final {
  std::string code;
  std::string observable_id;
  std::string message;
};

struct StructuralObservableCompilation final {
  std::vector<StructuralObservableValue> values;
  std::vector<StructuralObservableIssue> issues;

  [[nodiscard]] bool complete() const noexcept {
    return !values.empty() && issues.empty();
  }
};

[[nodiscard]] std::vector<StructuralObservableDefinition>
compile_structural_observable_definitions(
    std::vector<StructuralObservableSpec> specs);

[[nodiscard]] std::vector<StructuralObservableSpec>
global_structural_observable_specs(double maximum_change_fraction);

[[nodiscard]] StructuralObservableCompilation
evaluate_structural_observables(
    const std::vector<StructuralObservableDefinition> &definitions,
    const CompiledStructuralSetup &setup,
    const CompiledCalculixResult &result);

} // namespace prometheus::structural
