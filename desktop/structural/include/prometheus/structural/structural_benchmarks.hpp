#pragma once

#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_setup.hpp"

#include <vector>

namespace prometheus::structural {

struct BenchmarkReference final {
  CompiledStructuralSetup setup;
  double expected_maximum_displacement_m{};
  double expected_maximum_von_mises_pa{};
  double displacement_relative_tolerance{};
  double stress_relative_tolerance{};
};

struct BenchmarkComparison final {
  double displacement_relative_error{};
  double stress_relative_error{};
  bool displacement_within_tolerance{};
  bool stress_within_tolerance{};

  [[nodiscard]] bool passed() const noexcept {
    return displacement_within_tolerance && stress_within_tolerance;
  }
};

struct BenchmarkMeshDivisions final {
  int length_divisions{};
  int width_divisions{};
  int height_divisions{};
};

struct CantileverValidationMeshPair final {
  BenchmarkMeshDivisions coarse;
  BenchmarkMeshDivisions fine;
};

struct ModalBenchmarkReference final {
  CompiledStructuralSetup setup;
  double expected_first_natural_frequency_hz{};
  double frequency_relative_tolerance{};
};

struct ModalBenchmarkComparison final {
  double frequency_relative_error{};
  bool within_tolerance{};

  [[nodiscard]] bool passed() const noexcept { return within_tolerance; }
};

// A 1 m x 0.1 m x 0.1 m, nu=0 bar under 1000 N uniform axial
// traction. Closed-form references are u=F L/(A E) and sigma=F/A.
[[nodiscard]] BenchmarkReference axial_tension_bar_benchmark(
    int length_divisions = 1, int width_divisions = 1,
    int height_divisions = 1);

// A 1 m x 0.1 m x 0.1 m cantilever under a 1000 N end load. References use
// Euler-Bernoulli tip displacement F L^3/(3 E I) and root stress 6 F L/(b h^2).
[[nodiscard]] BenchmarkReference cantilever_benchmark(
    int length_divisions, int width_divisions, int height_divisions);

// Approved validation pair for the real CalculiX cantilever refinement gate.
[[nodiscard]] CantileverValidationMeshPair
cantilever_validation_mesh_pair() noexcept;

[[nodiscard]] std::vector<StructuralObservableSpec>
cantilever_validation_observable_specs();

[[nodiscard]] BenchmarkComparison compare_cantilever_validation(
    const VerifiedStructuralRefinement &refinement);

[[nodiscard]] BenchmarkComparison compare_benchmark(
    const BenchmarkReference &reference, const CalculixMetrics &actual);

// A 1 m x 0.1 m x 0.1 m cantilever, no applied load, reviewed for its
// modal_frequency capability instead. Closed-form reference is the first
// Euler-Bernoulli cantilever bending mode,
// f1 = (beta1 L)^2 / (2 pi L^2) * sqrt(E I / (rho A)), beta1 L = 1.875104.
[[nodiscard]] ModalBenchmarkReference cantilever_modal_benchmark(
    int length_divisions, int width_divisions, int height_divisions);

[[nodiscard]] ModalBenchmarkComparison compare_modal_benchmark(
    const ModalBenchmarkReference &reference, const CalculixMetrics &actual);

} // namespace prometheus::structural
