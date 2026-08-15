#include "prometheus/structural/structural_benchmarks.hpp"

#include <cmath>
#include <stdexcept>
#include <map>

namespace prometheus::structural {

BenchmarkReference axial_tension_bar_benchmark() {
  constexpr double forceN = 1000.0;
  constexpr double lengthM = 1.0;
  constexpr double areaM2 = 0.01;
  constexpr double youngsModulusPa = 2.0e11;
  StructuralRequest request{
      .analysis_id = "analytic-axial-tension-bar-v1",
      .component_name = "1m x 0.1m x 0.1m analytic tension bar",
      .geometry_sha256 =
          "sha256:1111111111111111111111111111111111111111111111111111111111111111",
      .nodes = {{1, {0.0, 0.0, 0.0}}, {2, {1.0, 0.0, 0.0}},
                {3, {1.0, 0.1, 0.0}}, {4, {0.0, 0.1, 0.0}},
                {5, {0.0, 0.0, 0.1}}, {6, {1.0, 0.0, 0.1}},
                {7, {1.0, 0.1, 0.1}}, {8, {0.0, 0.1, 0.1}}},
      .elements = {{1, {1, 2, 3, 7}}, {2, {1, 3, 4, 7}},
                   {3, {1, 4, 8, 7}}, {4, {1, 8, 5, 7}},
                   {5, {1, 5, 6, 7}}, {6, {1, 6, 2, 7}}},
      .youngs_modulus_pa = youngsModulusPa,
      .poisson_ratio = 0.0,
      .fully_fixed_node_ids = {1, 4, 5, 8},
      .nodal_forces = {{2, {forceN / 3.0, 0.0, 0.0}},
                       {3, {forceN / 6.0, 0.0, 0.0}},
                       {6, {forceN / 6.0, 0.0, 0.0}},
                       {7, {forceN / 3.0, 0.0, 0.0}}},
      .displacement_limit_m = 1.0e-6,
      .von_mises_limit_pa = 2.0e5,
      .material_reviewed = true,
      .loads_reviewed = true,
      .restraints_reviewed = true,
      .requirements_reviewed = true,
      .scenario_confirmed = true};
  return {std::move(request), forceN * lengthM / (areaM2 * youngsModulusPa),
          forceN / areaM2, 1.0e-5, 1.0e-5};
}

BenchmarkReference cantilever_benchmark(const int nx, const int ny,
                                        const int nz) {
  if (nx < 1 || ny < 1 || nz < 1 || nx > 200 || ny > 20 || nz > 20)
    throw std::invalid_argument("Cantilever mesh divisions are outside bounded limits");
  constexpr double length = 1.0;
  constexpr double width = 0.1;
  constexpr double height = 0.1;
  constexpr double force = 1000.0;
  constexpr double youngsModulus = 2.0e11;
  constexpr double poissonRatio = 0.3;
  const auto nodeId = [=](const int i, const int j, const int k) {
    return i * (ny + 1) * (nz + 1) + j * (nz + 1) + k + 1;
  };
  StructuralRequest request{
      .analysis_id = "analytic-cantilever-v1",
      .component_name = "1m x 0.1m x 0.1m analytic cantilever",
      .geometry_sha256 =
          "sha256:2222222222222222222222222222222222222222222222222222222222222222",
      .youngs_modulus_pa = youngsModulus,
      .poisson_ratio = poissonRatio,
      .displacement_limit_m = 0.003,
      .von_mises_limit_pa = 1.0e7,
      .material_reviewed = true,
      .loads_reviewed = true,
      .restraints_reviewed = true,
      .requirements_reviewed = true,
      .scenario_confirmed = true};
  for (int i = 0; i <= nx; ++i)
    for (int j = 0; j <= ny; ++j)
      for (int k = 0; k <= nz; ++k) {
        request.nodes.push_back({nodeId(i, j, k),
            {length * i / nx, width * j / ny,
             -height / 2.0 + height * k / nz}});
        if (i == 0) request.fully_fixed_node_ids.push_back(nodeId(i, j, k));
      }
  int elementId = 1;
  for (int i = 0; i < nx; ++i)
    for (int j = 0; j < ny; ++j)
      for (int k = 0; k < nz; ++k) {
        const int n1 = nodeId(i, j, k);
        const int n2 = nodeId(i + 1, j, k);
        const int n3 = nodeId(i + 1, j + 1, k);
        const int n4 = nodeId(i, j + 1, k);
        const int n5 = nodeId(i, j, k + 1);
        const int n6 = nodeId(i + 1, j, k + 1);
        const int n7 = nodeId(i + 1, j + 1, k + 1);
        const int n8 = nodeId(i, j + 1, k + 1);
        for (const auto nodes : {std::array<int, 4>{n1, n2, n3, n7},
                                 std::array<int, 4>{n1, n3, n4, n7},
                                 std::array<int, 4>{n1, n4, n8, n7},
                                 std::array<int, 4>{n1, n8, n5, n7},
                                 std::array<int, 4>{n1, n5, n6, n7},
                                 std::array<int, 4>{n1, n6, n2, n7}})
          request.elements.push_back({elementId++, nodes});
      }
  std::map<int, double> loadByNode;
  const double triangleFraction = 1.0 / (2.0 * ny * nz);
  for (int j = 0; j < ny; ++j)
    for (int k = 0; k < nz; ++k) {
      const int a = nodeId(nx, j, k);
      const int b = nodeId(nx, j + 1, k);
      const int c = nodeId(nx, j + 1, k + 1);
      const int d = nodeId(nx, j, k + 1);
      for (const int id : {a, b, c}) loadByNode[id] -= force * triangleFraction / 3.0;
      for (const int id : {a, c, d}) loadByNode[id] -= force * triangleFraction / 3.0;
    }
  for (const auto &[id, value] : loadByNode)
    request.nodal_forces.push_back({id, {0.0, 0.0, value}});
  constexpr double inertia = width * height * height * height / 12.0;
  const double expectedDisplacement = force * length * length * length /
                                      (3.0 * youngsModulus * inertia);
  const double expectedStress = 6.0 * force * length / (width * height * height);
  return {std::move(request), expectedDisplacement, expectedStress, 0.15, 0.25};
}

BenchmarkComparison compare_benchmark(const BenchmarkReference &reference,
                                      const CalculixMetrics &actual) {
  if (!std::isfinite(reference.expected_maximum_displacement_m) ||
      reference.expected_maximum_displacement_m <= 0.0 ||
      !std::isfinite(reference.expected_maximum_von_mises_pa) ||
      reference.expected_maximum_von_mises_pa <= 0.0 ||
      !std::isfinite(reference.displacement_relative_tolerance) ||
      reference.displacement_relative_tolerance < 0.0 ||
      !std::isfinite(reference.stress_relative_tolerance) ||
      reference.stress_relative_tolerance < 0.0)
    throw std::invalid_argument("Benchmark reference and tolerances must be finite and positive");
  const double displacementError = std::abs(
      actual.maximum_displacement_m - reference.expected_maximum_displacement_m) /
      reference.expected_maximum_displacement_m;
  const double stressError = std::abs(
      actual.maximum_von_mises_pa - reference.expected_maximum_von_mises_pa) /
      reference.expected_maximum_von_mises_pa;
  return {displacementError, stressError,
          displacementError <= reference.displacement_relative_tolerance,
          stressError <= reference.stress_relative_tolerance};
}

} // namespace prometheus::structural
