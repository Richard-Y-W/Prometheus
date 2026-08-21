#include "prometheus/structural/structural_benchmarks.hpp"

#include "prometheus/structural/mesh_validation.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace prometheus::structural {
namespace {

struct Block final {
  VolumeMesh mesh;
  double cell_minimum_m{};
  double cell_diagonal_m{};
};

void bounded_divisions(const int nx, const int ny, const int nz) {
  if (nx < 1 || ny < 1 || nz < 1 || nx > 200 || ny > 20 || nz > 20)
    throw std::invalid_argument(
        "Benchmark mesh divisions are outside bounded limits");
}

Block block_mesh(const int nx, const int ny, const int nz,
                 const double length, const double width,
                 const double zMinimum, const double zMaximum) {
  bounded_divisions(nx, ny, nz);
  const auto nodeId = [=](const int i, const int j, const int k) {
    return i * (ny + 1) * (nz + 1) + j * (nz + 1) + k + 1;
  };
  Block result;
  for (int i = 0; i <= nx; ++i)
    for (int j = 0; j <= ny; ++j)
      for (int k = 0; k <= nz; ++k)
        result.mesh.nodes.push_back(
            {nodeId(i, j, k),
             {length * i / nx, width * j / ny,
              zMinimum + (zMaximum - zMinimum) * k / nz}});

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
        for (const auto nodes : {
                 std::array<int, 4>{n1, n2, n3, n7},
                 std::array<int, 4>{n1, n3, n4, n7},
                 std::array<int, 4>{n1, n4, n8, n7},
                 std::array<int, 4>{n1, n8, n5, n7},
                 std::array<int, 4>{n1, n5, n6, n7},
                 std::array<int, 4>{n1, n6, n2, n7}})
          result.mesh.elements.push_back({elementId++, nodes});
      }
  const double dx = length / nx;
  const double dy = width / ny;
  const double dz = (zMaximum - zMinimum) / nz;
  result.cell_minimum_m = std::min({dx, dy, dz});
  result.cell_diagonal_m = std::hypot(dx, dy, dz);
  return result;
}

std::string mesh_identity(const VolumeMesh &mesh) {
  std::ostringstream bytes;
  bytes.imbue(std::locale::classic());
  bytes << "prometheus-structured-block-mesh-v1\n" << std::scientific
        << std::setprecision(17);
  for (const auto &node : mesh.nodes)
    bytes << "n " << node.id << ' ' << node.position_m[0] << ' '
          << node.position_m[1] << ' ' << node.position_m[2] << '\n';
  for (const auto &element : mesh.elements)
    bytes << "e " << element.id << ' ' << element.node_ids[0] << ' '
          << element.node_ids[1] << ' ' << element.node_ids[2] << ' '
          << element.node_ids[3] << '\n';
  return integrity::sha256_bytes(bytes.str());
}

BoundarySelection plane_selection(const VolumeMesh &mesh,
                                  const std::vector<BoundaryFace> &boundary,
                                  const double x, std::string label) {
  std::map<int, const Node *> nodes;
  for (const auto &node : mesh.nodes)
    nodes.emplace(node.id, &node);
  const double tolerance = std::max(1.0, std::abs(x)) * 1.0e-12;
  std::set<std::array<int, 3>> selectedFaces;
  std::set<int> selectedNodes;
  double area = 0.0;
  for (const auto &face : boundary) {
    const bool onPlane = std::ranges::all_of(face.node_ids, [&](const int id) {
      return std::abs(nodes.at(id)->position_m[0] - x) <= tolerance;
    });
    if (!onPlane)
      continue;
    auto identity = face.node_ids;
    std::ranges::sort(identity);
    if (!selectedFaces.insert(identity).second)
      throw std::runtime_error("Benchmark boundary contains a duplicate face");
    selectedNodes.insert(identity.begin(), identity.end());
    area += face.area_m2;
  }
  if (selectedFaces.empty() || !std::isfinite(area) || area <= 0.0)
    throw std::runtime_error("Benchmark boundary plane is missing");
  return {.label = std::move(label),
          .face_node_ids = {selectedFaces.begin(), selectedFaces.end()},
          .node_ids = {selectedNodes.begin(), selectedNodes.end()},
          .area_m2 = area};
}

CompiledStructuralSetup compile_benchmark_setup(
    std::string analysisId, std::string componentName,
    std::string geometrySha256, Block block, const double poissonRatio,
    const std::array<double, 3> &totalForceN,
    const double displacementLimitM, const double vonMisesLimitPa,
    const double minimumMeanRatioThreshold) {
  const auto measured = validate_and_measure_mesh(block.mesh, {});
  const auto load = plane_selection(block.mesh, measured.boundary_faces, 1.0,
                                    "reviewed loaded end face");
  const auto fixed = plane_selection(block.mesh, measured.boundary_faces, 0.0,
                                     "reviewed fixed end face");
  const auto meshSha256 = mesh_identity(block.mesh);
  StructuralSetup setup{
      .analysis_id = std::move(analysisId),
      .component_name = std::move(componentName),
      .geometry_sha256 = std::move(geometrySha256),
      .mesh = std::move(block.mesh),
      .boundary_faces = measured.boundary_faces,
      .material =
          {.designation = "analytic isotropic benchmark material",
           .source_sha256 =
               "sha256:3111111111111111111111111111111111111111111111111111111111111111",
           .applicability = "known",
           .youngs_modulus_pa = 2.0e11,
           .poisson_ratio = poissonRatio,
           .reviewed = true,
           .temper = "not_applicable",
           .product_form = "synthetic benchmark"},
      .load = {.selection = load,
               .total_force_n = totalForceN,
               .reviewed = true},
      .restraint = {.selection = fixed, .reviewed = true},
      .requirements =
          {{.quantity = RequirementQuantity::displacement,
            .comparator = RequirementComparator::less_or_equal,
            .limit_value = displacementLimitM,
            .unit = "m",
            .applicability = "bounded analytic benchmark",
            .criticality = RequirementCriticality::advisory,
            .source_or_exploratory_rationale =
                "predeclared analytic benchmark acceptance envelope",
            .reviewed = true,
            .limit_basis =
                "closed-form benchmark displacement envelope"},
           {.quantity = RequirementQuantity::von_mises_stress,
            .comparator = RequirementComparator::less_or_equal,
            .limit_value = vonMisesLimitPa,
            .unit = "Pa",
            .applicability = "bounded analytic benchmark",
            .criticality = RequirementCriticality::advisory,
            .source_or_exploratory_rationale =
                "predeclared analytic benchmark acceptance envelope",
            .reviewed = true,
            .limit_basis =
                "closed-form benchmark stress envelope"}},
      .mesh_controls =
          {.minimum_size_m = block.cell_minimum_m,
           .maximum_size_m = block.cell_diagonal_m,
           .mesher_identity =
               "Prometheus deterministic structured-block benchmark mesher v1",
           .reviewed = true,
           .mesh_sha256 = meshSha256,
           .coordinate_scale_to_m = 1.0,
           .target_size_m = block.cell_minimum_m,
           .minimum_mean_ratio_threshold = minimumMeanRatioThreshold,
           .observed_minimum_mean_ratio =
               measured.diagnostics.minimum_mean_ratio},
      .scenario_description =
          "bounded synthetic linear-static solver validation scenario",
      .scenario_confirmed = true};
  return compile_structural_setup(setup);
}

CompiledStructuralSetup compile_modal_benchmark_setup(
    std::string analysisId, std::string componentName,
    std::string geometrySha256, Block block, const double poissonRatio,
    const double densityKgM3, const double minimumFrequencyHz,
    const double minimumMeanRatioThreshold) {
  const auto measured = validate_and_measure_mesh(block.mesh, {});
  const auto fixed = plane_selection(block.mesh, measured.boundary_faces, 0.0,
                                     "reviewed fixed end face");
  const auto meshSha256 = mesh_identity(block.mesh);
  StructuralSetup setup{
      .analysis_id = std::move(analysisId),
      .component_name = std::move(componentName),
      .geometry_sha256 = std::move(geometrySha256),
      .mesh = std::move(block.mesh),
      .boundary_faces = measured.boundary_faces,
      .material =
          {.designation = "analytic isotropic benchmark material",
           .source_sha256 =
               "sha256:3111111111111111111111111111111111111111111111111111111111111111",
           .applicability = "known",
           .youngs_modulus_pa = 2.0e11,
           .poisson_ratio = poissonRatio,
           .reviewed = true,
           .temper = "not_applicable",
           .product_form = "synthetic benchmark",
           .density_kg_m3 = densityKgM3},
      .restraint = {.selection = fixed, .reviewed = true},
      .requirements =
          {{.quantity = RequirementQuantity::natural_frequency,
            .comparator = RequirementComparator::greater_or_equal,
            .limit_value = minimumFrequencyHz,
            .unit = "Hz",
            .applicability = "bounded analytic benchmark",
            .criticality = RequirementCriticality::advisory,
            .source_or_exploratory_rationale =
                "predeclared analytic benchmark acceptance envelope",
            .reviewed = true,
            .limit_basis =
                "closed-form benchmark first natural frequency envelope"}},
      .mesh_controls =
          {.minimum_size_m = block.cell_minimum_m,
           .maximum_size_m = block.cell_diagonal_m,
           .mesher_identity =
               "Prometheus deterministic structured-block benchmark mesher v1",
           .reviewed = true,
           .mesh_sha256 = meshSha256,
           .coordinate_scale_to_m = 1.0,
           .target_size_m = block.cell_minimum_m,
           .minimum_mean_ratio_threshold = minimumMeanRatioThreshold,
           .observed_minimum_mean_ratio =
               measured.diagnostics.minimum_mean_ratio},
      .scenario_description =
          "bounded synthetic modal_frequency solver validation scenario",
      .scenario_confirmed = true};
  return compile_structural_setup(setup);
}

} // namespace

BenchmarkReference axial_tension_bar_benchmark(
    const int nx, const int ny, const int nz) {
  constexpr double forceN = 1000.0;
  constexpr double lengthM = 1.0;
  constexpr double widthM = 0.1;
  constexpr double heightM = 0.1;
  constexpr double areaM2 = widthM * heightM;
  constexpr double youngsModulusPa = 2.0e11;
  auto setup = compile_benchmark_setup(
      "analytic-axial-tension-bar-refinement-v1",
      "1m x 0.1m x 0.1m analytic tension bar",
      "sha256:1111111111111111111111111111111111111111111111111111111111111111",
      block_mesh(nx, ny, nz, lengthM, widthM, 0.0, heightM), 0.0,
      {forceN, 0.0, 0.0}, 1.0e-6, 2.0e5, 0.001);
  return {std::move(setup), forceN * lengthM / (areaM2 * youngsModulusPa),
          forceN / areaM2, 1.0e-5, 1.0e-5};
}

BenchmarkReference cantilever_benchmark(const int nx, const int ny,
                                        const int nz) {
  constexpr double length = 1.0;
  constexpr double width = 0.1;
  constexpr double height = 0.1;
  constexpr double force = 1000.0;
  constexpr double youngsModulus = 2.0e11;
  auto setup = compile_benchmark_setup(
      "analytic-cantilever-refinement-v1",
      "1m x 0.1m x 0.1m analytic cantilever",
      "sha256:2222222222222222222222222222222222222222222222222222222222222222",
      block_mesh(nx, ny, nz, length, width, -height / 2.0, height / 2.0),
      0.3, {0.0, 0.0, -force}, 0.003, 1.0e7, 0.001);
  constexpr double inertia = width * height * height * height / 12.0;
  const double expectedDisplacement =
      force * length * length * length / (3.0 * youngsModulus * inertia);
  const double expectedStress =
      6.0 * force * length / (width * height * height);
  return {std::move(setup), expectedDisplacement, expectedStress, 0.15, 0.25};
}

CantileverValidationMeshPair cantilever_validation_mesh_pair() noexcept {
  return {
      .coarse = {80, 12, 12},
      .fine = {120, 18, 18},
  };
}

std::vector<StructuralObservableSpec>
cantilever_validation_observable_specs() {
  return {
      {.id = "cantilever.maximum_displacement",
       .quantity = StructuralObservableQuantity::displacement_magnitude_m,
       .reduction = StructuralObservableReduction::maximum,
       .region = {.kind = StructuralObservableRegionKind::all_nodes},
       .maximum_change_fraction = 0.10},
      {.id = "cantilever.section_von_mises",
       .quantity = StructuralObservableQuantity::von_mises_stress_pa,
       .reduction = StructuralObservableReduction::maximum,
       .region =
           {.kind =
                StructuralObservableRegionKind::element_centroid_box_m,
            .element_centroid_box_m =
                {.minimum_m = {0.100, 0.000, -0.050},
                 .maximum_m = {0.125, 0.100, 0.050}}},
       .maximum_change_fraction = 0.10}};
}

BenchmarkComparison compare_cantilever_validation(
    const VerifiedStructuralRefinement &refinement) {
  constexpr double expectedDisplacementM = 0.0002;
  constexpr double expectedSectionStressPa = 5.4e6;
  constexpr double displacementTolerance = 0.15;
  constexpr double stressTolerance = 0.25;
  const auto expectedCriterion = compile_structural_refinement_criterion(
      cantilever_validation_observable_specs());
  const auto &expectedDefinitions = expectedCriterion.observables();
  if (refinement.coarse().criterion().legacy_global_extrema_only() ||
      refinement.coarse().criterion().identity() !=
          expectedCriterion.identity() ||
      refinement.observable_comparisons().size() !=
          expectedDefinitions.size())
    throw std::invalid_argument(
        "Cantilever validation requires its exact typed observable profile");

  const auto valueFor = [&](const StructuralObservableDefinition &expected) {
    const auto comparison = std::ranges::find_if(
        refinement.observable_comparisons(), [&](const auto &candidate) {
          return candidate.definition.spec.id == expected.spec.id;
        });
    if (comparison == refinement.observable_comparisons().end() ||
        comparison->definition.identity != expected.identity ||
        !std::isfinite(comparison->fine_value) ||
        comparison->fine_value < 0.0)
      throw std::invalid_argument(
          "Cantilever validation observable is missing or does not match "
          "its locked definition");
    return comparison->fine_value;
  };
  const double displacement = valueFor(expectedDefinitions[0]);
  const double sectionStress = valueFor(expectedDefinitions[1]);
  const double displacementError =
      std::abs(displacement - expectedDisplacementM) /
      expectedDisplacementM;
  const double stressError =
      std::abs(sectionStress - expectedSectionStressPa) /
      expectedSectionStressPa;
  return {displacementError, stressError,
          displacementError <= displacementTolerance,
          stressError <= stressTolerance};
}

ModalBenchmarkReference cantilever_modal_benchmark(const int nx, const int ny,
                                                    const int nz) {
  constexpr double length = 1.0;
  constexpr double width = 0.1;
  constexpr double height = 0.1;
  constexpr double youngsModulus = 2.0e11;
  constexpr double densityKgM3 = 7850.0;
  constexpr double betaOneLength = 1.875104;
  constexpr double inertia = width * height * height * height / 12.0;
  constexpr double area = width * height;
  const double expectedFrequencyHz =
      (betaOneLength * betaOneLength) /
      (2.0 * std::numbers::pi * length * length) *
      std::sqrt(youngsModulus * inertia / (densityKgM3 * area));
  auto setup = compile_modal_benchmark_setup(
      "analytic-cantilever-modal-v1",
      "1m x 0.1m x 0.1m analytic modal cantilever",
      "sha256:4444444444444444444444444444444444444444444444444444444444444444",
      block_mesh(nx, ny, nz, length, width, -height / 2.0, height / 2.0),
      0.3, densityKgM3, 0.5 * expectedFrequencyHz, 0.001);
  return {std::move(setup), expectedFrequencyHz, 0.15};
}

ModalBenchmarkComparison compare_modal_benchmark(
    const ModalBenchmarkReference &reference, const CalculixMetrics &actual) {
  if (!std::isfinite(reference.expected_first_natural_frequency_hz) ||
      reference.expected_first_natural_frequency_hz <= 0.0 ||
      !std::isfinite(reference.frequency_relative_tolerance) ||
      reference.frequency_relative_tolerance < 0.0 ||
      !actual.first_natural_frequency_hz.has_value() ||
      !std::isfinite(*actual.first_natural_frequency_hz))
    throw std::invalid_argument(
        "Modal benchmark reference, metrics, and tolerance must be finite and positive");
  const double frequencyError =
      std::abs(*actual.first_natural_frequency_hz -
               reference.expected_first_natural_frequency_hz) /
      reference.expected_first_natural_frequency_hz;
  return {frequencyError,
          frequencyError <= reference.frequency_relative_tolerance};
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
      reference.stress_relative_tolerance < 0.0 ||
      !std::isfinite(actual.maximum_displacement_m) ||
      !std::isfinite(actual.maximum_von_mises_pa))
    throw std::invalid_argument(
        "Benchmark reference, metrics, and tolerances must be finite and positive");
  const double displacementError =
      std::abs(actual.maximum_displacement_m -
               reference.expected_maximum_displacement_m) /
      reference.expected_maximum_displacement_m;
  const double stressError =
      std::abs(actual.maximum_von_mises_pa -
               reference.expected_maximum_von_mises_pa) /
      reference.expected_maximum_von_mises_pa;
  return {displacementError, stressError,
          displacementError <= reference.displacement_relative_tolerance,
          stressError <= reference.stress_relative_tolerance};
}

} // namespace prometheus::structural
