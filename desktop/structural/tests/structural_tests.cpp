#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/prepared_mesh.hpp"
#include "prometheus/structural/structural_archive.hpp"
#include "prometheus/structural/structural_request.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/structural/structural_setup.hpp"
#include "prometheus/structural/surface_groups.hpp"
#include "prometheus/structural/surface_selection.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ps = prometheus::structural;
namespace fs = std::filesystem;

[[noreturn]] void fail(const char *message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition) fail(message);
}

std::string fixtureBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "solver evidence fixture opens");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void writeFixtureBytes(const fs::path &path, const std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(static_cast<bool>(output), "test fixture bytes write exactly");
}

std::string jsonNumber(const double value) {
  char buffer[128]{};
  const auto [end, error] = std::to_chars(
      std::begin(buffer), std::end(buffer), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  require(error == std::errc{}, "test JSON number formats exactly");
  return {buffer, end};
}

fs::path createLegacyV1Archive(
    const fs::path &root, const ps::StructuralRequest &request,
    const ps::CalculixRunEvidence &evidence,
    const ps::CalculixMetrics &metrics) {
  fs::create_directories(root);
  const std::string setupName = "reviewed-structural-setup.json";
  const std::string deckName = "legacy.inp";
  const std::string datName = "legacy.dat";
  const std::string frdName = "legacy.frd";
  const std::string staName = "legacy.sta";
  const std::string stdoutName = "solver.stdout.txt";
  const std::string stderrName = "solver.stderr.txt";
  std::string nodeIds;
  for (const auto &node : request.nodes) {
    if (!nodeIds.empty())
      nodeIds += ',';
    nodeIds += std::to_string(node.id);
  }
  std::string elementIds;
  for (const auto &element : request.elements) {
    if (!elementIds.empty())
      elementIds += ',';
    elementIds += std::to_string(element.id);
  }
  const auto setup = prometheus::integrity::canonicalize_json_bytes(
      std::string{"{\"$schema\":\"urn:prometheus:schema:reviewed-structural-setup:1.0.0\","}
      + "\"analysis_id\":\"" + request.analysis_id + "\"," +
      "\"component_name\":\"" + request.component_name + "\"," +
      "\"geometry_sha256\":\"" + request.geometry_sha256 + "\"," +
      "\"mesh\":{\"element_ids\":[" + elementIds +
      "],\"node_ids\":[" + nodeIds + "]}}" );
  writeFixtureBytes(root / setupName, setup);
  writeFixtureBytes(root / deckName, evidence.deck_bytes);
  writeFixtureBytes(root / datName, evidence.data_bytes);
  writeFixtureBytes(root / frdName, "legacy frd\n");
  writeFixtureBytes(root / staName, evidence.status_bytes);
  writeFixtureBytes(root / stdoutName, evidence.standard_output);
  writeFixtureBytes(root / stderrName, evidence.standard_error);
  const auto artifact = [&](const std::string &name) {
    const auto bytes = fixtureBytes(root / name);
    return std::string{"{\"byte_length\":"} +
           std::to_string(bytes.size()) + ",\"file\":\"" + name +
           "\",\"sha256\":\"" +
           prometheus::integrity::sha256_bytes(bytes) + "\"}";
  };
  const auto manifest = prometheus::integrity::canonicalize_json_bytes(
      std::string{"{\"$schema\":\"urn:prometheus:schema:structural-run-archive:1.0.0\","}
      + "\"analysis_id\":\"" + request.analysis_id + "\"," +
      "\"archive_kind\":\"completed_linear_static_run\"," +
      "\"artifacts\":{\"dat\":" + artifact(datName) +
      ",\"deck\":" + artifact(deckName) +
      ",\"frd\":" + artifact(frdName) +
      ",\"setup\":" + artifact(setupName) +
      ",\"sta\":" + artifact(staName) +
      ",\"stderr\":" + artifact(stderrName) +
      ",\"stdout\":" + artifact(stdoutName) + "}," +
      "\"component_name\":\"" + request.component_name + "\"," +
      "\"coverage\":{\"declared_obligations\":0,\"evaluated_obligations\":0}," +
      "\"execution\":{\"elapsed_ms\":1,\"exit_code\":0,\"status\":\"completed\"}," +
      "\"findings\":[],\"geometry_sha256\":\"" +
      request.geometry_sha256 + "\",\"job_name\":\"legacy\"," +
      "\"limitation\":\"legacy bounded claim\",\"metrics\":{" +
      "\"displacement_rows\":" +
      std::to_string(metrics.displacement_rows) +
      ",\"maximum_displacement_m\":" +
      jsonNumber(metrics.maximum_displacement_m) +
      ",\"maximum_von_mises_pa\":" +
      jsonNumber(metrics.maximum_von_mises_pa) +
      ",\"stress_rows\":" + std::to_string(metrics.stress_rows) + "}," +
      "\"requirements\":{\"displacement_limit_m\":null,\"von_mises_limit_pa\":null}," +
      "\"schema_version\":\"1.0.0\",\"solver_identity\":\"legacy fixture\"}" );
  const auto path = root / "prometheus-structural-run.json";
  writeFixtureBytes(path, manifest);
  return path;
}

std::string replaceOnce(std::string source, const std::string_view from,
                        const std::string_view to) {
  const auto position = source.find(from);
  require(position != std::string::npos, "test replacement source exists");
  source.replace(position, from.size(), to);
  return source;
}

template <typename Function>
void requireThrows(Function &&function, const std::string_view expected,
                   const char *message) {
  try {
    std::forward<Function>(function)();
  } catch (const std::exception &error) {
    require(std::string_view(error.what()).find(expected) !=
                std::string_view::npos,
            message);
    return;
  }
  fail(message);
}

ps::StructuralRequest validRequest() {
  return {
      .analysis_id = "yubi-bracket-linear-static-1",
      .component_name = "YUBI BRACKET_GRIPPER",
      .geometry_sha256 = "sha256:4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a",
      .nodes = {{4, {0.0, 0.0, 1.0}}, {2, {1.0, 0.0, 0.0}},
                {1, {0.0, 0.0, 0.0}}, {3, {0.0, 1.0, 0.0}}},
      .elements = {{1, {1, 2, 3, 4}}},
      .youngs_modulus_pa = 7.0e10,
      .poisson_ratio = 0.33,
      .fully_fixed_node_ids = {3, 1, 2},
      .nodal_forces = {{4, {0.0, 0.0, -100.0}}},
      .displacement_limit_m = 0.001,
      .von_mises_limit_pa = 1.0e8,
      .material_reviewed = true,
      .loads_reviewed = true,
      .restraints_reviewed = true,
      .requirements_reviewed = true,
      .scenario_confirmed = true,
      .material_designation = "benchmark isotropic material",
      .material_temper = "not_applicable",
      .material_product_form = "synthetic benchmark",
      .material_applicability = "known",
      .material_evidence_sha256 =
          "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
      .mesh_sha256 =
          "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
      .mesh_coordinate_scale_to_m = 1.0,
      .reviewed_force_magnitude_n = 100.0,
      .reviewed_force_direction = {0.0, 0.0, -1.0},
      .selected_load_area_m2 = 0.5,
      .mesh_target_size_m = 0.1,
      .minimum_mean_ratio_threshold = 0.05,
      .observed_minimum_mean_ratio = 0.75,
      .displacement_limit_basis = "reviewed benchmark displacement limit",
      .von_mises_limit_basis = "reviewed benchmark stress limit",
      .mesh_reviewed = true,
  };
}

bool hasIssue(const std::vector<ps::ValidationIssue> &issues,
              const std::string &code) {
  return std::ranges::any_of(issues, [&](const auto &value) {
    return value.code == code;
  });
}

bool hasResultIssue(const ps::CompiledCalculixResult &result,
                    const std::string_view code) {
  return std::ranges::any_of(result.issues, [&](const auto &value) {
    return value.code == code;
  });
}

ps::CalculixRunEvidence completeCalculixEvidence(
    const ps::StructuralRequest &request, std::string statusBytes,
    std::string dataBytes, std::string standardOutput,
    std::string standardError) {
  return {
      .process_exit_code = 0,
      .solver_executable_sha256 =
          "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
      .solver_version = "CalculiX Version 2.23",
      .deck_bytes = ps::generate_calculix_deck(request),
      .standard_output = std::move(standardOutput),
      .standard_error = std::move(standardError),
      .status_bytes = std::move(statusBytes),
      .data_bytes = std::move(dataBytes),
      .frd_sha256 =
          "sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
      .frd_byte_length = 12U,
  };
}

int main() {
  const auto axialBenchmark = ps::axial_tension_bar_benchmark();
  require(ps::validate_request(axialBenchmark.setup.request).empty() &&
              std::abs(axialBenchmark.expected_maximum_displacement_m - 5.0e-7) < 1e-18 &&
              std::abs(axialBenchmark.expected_maximum_von_mises_pa - 1.0e5) < 1e-9,
          "axial benchmark derives closed-form displacement and stress references");
  const auto exactComparison = ps::compare_benchmark(
      axialBenchmark, {5.0e-7, 1.0e5, 8, 6});
  require(exactComparison.passed(), "exact analytic benchmark metrics pass tolerance");
  const auto badComparison = ps::compare_benchmark(
      axialBenchmark, {6.0e-7, 1.2e5, 8, 6});
  require(!badComparison.passed(), "benchmark comparison rejects material error");
  const auto cantilever = ps::cantilever_benchmark(20, 3, 3);
  require(ps::validate_request(cantilever.setup.request).empty() &&
              cantilever.setup.request.nodes.size() == 336 &&
              cantilever.setup.request.elements.size() == 1080 &&
              std::abs(cantilever.expected_maximum_displacement_m - 0.0002) < 1e-15 &&
              std::abs(cantilever.expected_maximum_von_mises_pa - 6.0e6) < 1e-8,
          "cantilever benchmark derives beam references and bounded solid mesh");
  const auto request = validRequest();
  require(ps::validate_request(request).empty(), "reviewed tetra request validates");
  const auto deck = ps::generate_calculix_deck(request);
  require(deck == ps::generate_calculix_deck(request),
          "CalculiX deck bytes are deterministic");
  require(deck.find("1, 0.0000000000e+00, 0.0000000000e+00, 0.0000000000e+00") != std::string::npos,
          "nodes are ordered deterministically in SI units");
  require(deck.find("*ELEMENT, TYPE=C3D4") != std::string::npos &&
              deck.find("*STATIC, SOLVER=SPOOLES") != std::string::npos &&
              deck.find("4, 3, -1.0000000000e+02") != std::string::npos,
          "deck pins the solver and contains bounded tetra and reviewed force");
  const auto restoredDeckMesh = ps::parse_gmsh_abaqus_mesh(deck, 1.0);
  require(restoredDeckMesh.nodes.size() == request.nodes.size() &&
              restoredDeckMesh.elements.size() == request.elements.size(),
          "generated CalculiX deck restores its exact submitted volume mesh");

  auto blocked = request;
  blocked.material_reviewed = false;
  blocked.scenario_confirmed = false;
  blocked.fully_fixed_node_ids = {1, 2};
  blocked.displacement_limit_m.reset();
  blocked.von_mises_limit_pa.reset();
  const auto issues = ps::validate_request(blocked);
  require(hasIssue(issues, "material_unreviewed") &&
              hasIssue(issues, "scenario_unconfirmed") &&
              hasIssue(issues, "inadequate_restraints") &&
              hasIssue(issues, "missing_requirement"),
          "missing review, restraints, and requirements stay blocked");
  try {
    (void)ps::generate_calculix_deck(blocked);
    fail("invalid request generated a solver deck");
  } catch (const std::invalid_argument &) {
  }

  auto missingNode = request;
  missingNode.nodal_forces.front().node_id = 99;
  require(hasIssue(ps::validate_request(missingNode), "load_node_missing"),
          "load on missing mesh node stays blocked");

  auto zeroVolume = request;
  zeroVolume.nodes.front().position_m = {1.0, 1.0, 0.0};
  require(hasIssue(ps::validate_request(zeroVolume), "zero_volume_element"),
          "zero-volume tetrahedron stays blocked");

  auto collinear = request;
  collinear.nodes[3].position_m = {2.0, 0.0, 0.0};
  require(hasIssue(ps::validate_request(collinear), "inadequate_restraints"),
          "collinear fixed nodes stay blocked");

  constexpr auto rawDat = R"(
 displacements (vx,vy,vz) for set NALL and time  0.1000000E+01

         1  0.000000E+00  0.000000E+00  0.000000E+00
         4  0.000000E+00  0.000000E+00 -2.228571E-08

 stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set COMPONENT and time  0.1000000E+01

         1   1 -2.571429E+03 -2.571429E+03 -6.000000E+03  0.000000E+00  0.000000E+00  0.000000E+00
)";
  const auto parsedRows = ps::parse_calculix_dat(rawDat);
  require(parsedRows.displacements.size() == 2 &&
              parsedRows.displacements.back().node_id == 4 &&
              parsedRows.stresses.size() == 1 &&
              parsedRows.stresses.front().element_id == 1,
          "raw CalculiX parser retains typed row identities without judging coverage");
  try {
    (void)ps::parse_calculix_dat("solver stopped before result output\n");
    fail("missing solver output became typed rows");
  } catch (const std::runtime_error &) {
  }

  const auto solverFixtureRoot =
      fs::path(PROMETHEUS_REPOSITORY_ROOT) /
      "fixtures/structural/calculix-smoke/complete";
  const auto completeEvidence = completeCalculixEvidence(
      request,
      fixtureBytes(solverFixtureRoot / "prometheus_tetra_smoke.sta"),
      fixtureBytes(solverFixtureRoot / "prometheus_tetra_smoke.dat"),
      fixtureBytes(solverFixtureRoot / "prometheus_tetra_smoke.stdout.txt"),
      fixtureBytes(solverFixtureRoot / "prometheus_tetra_smoke.stderr.txt"));
  const auto compiledResult =
      ps::compile_calculix_result(request, completeEvidence);
  require(compiledResult.complete() && compiledResult.metrics.has_value(),
          "complete converged evidence produces metrics");
  require(compiledResult.backend.executable_sha256 ==
              "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
          "result binds the exact solver executable identity");
  require(compiledResult.convergence.has_value() &&
              compiledResult.convergence->total_time == 1.0,
          "result binds the completed final step");
  require(compiledResult.normalized.displacements.size() ==
                  request.nodes.size() &&
              compiledResult.normalized.stresses.size() ==
                  request.elements.size(),
          "result covers every submitted node and element identity exactly");
  require(compiledResult.artifacts.deck.sha256.starts_with("sha256:") &&
              compiledResult.artifacts.deck.byte_length ==
                  completeEvidence.deck_bytes.size() &&
              compiledResult.artifacts.frd.sha256 ==
                  completeEvidence.frd_sha256 &&
              compiledResult.identity.starts_with("sha256:"),
          "compiled result records exact artifact lengths, hashes, and identity");
  constexpr std::string_view coarseResultHash =
      "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
  constexpr std::string_view fineResultHash =
      "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
  auto coarseBenchmarkResult = compiledResult;
  coarseBenchmarkResult.identity = coarseResultHash;
  coarseBenchmarkResult.metrics->maximum_displacement_m = 2.1e-5;
  coarseBenchmarkResult.metrics->maximum_von_mises_pa = 1.04e6;
  auto fineBenchmarkResult = compiledResult;
  fineBenchmarkResult.identity = fineResultHash;
  fineBenchmarkResult.metrics->maximum_displacement_m = 2.0e-5;
  fineBenchmarkResult.metrics->maximum_von_mises_pa = 1.0e6;
  const auto benchmarkRefinement =
      ps::compile_structural_refinement_evidence(
          coarseBenchmarkResult, fineBenchmarkResult, 0.10);
  require(benchmarkRefinement.complete &&
              benchmarkRefinement.criteria_satisfied &&
              benchmarkRefinement.result_sha256 ==
                  std::vector<std::string>({std::string(coarseResultHash),
                                            std::string(fineResultHash)}),
          "benchmark refinement uses exact result identities and one criterion");
  coarseBenchmarkResult.metrics->maximum_displacement_m = 4.0e-5;
  require(!ps::compile_structural_refinement_evidence(
               coarseBenchmarkResult, fineBenchmarkResult, 0.10)
               .criteria_satisfied,
          "benchmark refinement rejects excessive coarse-to-fine change");

  auto failedExit = completeEvidence;
  failedExit.process_exit_code = 9;
  require(hasResultIssue(ps::compile_calculix_result(request, failedExit),
                         "solver_process_failed"),
          "nonzero solver status remains indeterminate");
  auto missingSolverIdentity = completeEvidence;
  missingSolverIdentity.solver_executable_sha256.clear();
  require(hasResultIssue(
              ps::compile_calculix_result(request, missingSolverIdentity),
              "invalid_solver_identity"),
          "missing solver executable identity remains indeterminate");
  auto unsafeSolverVersion = completeEvidence;
  unsafeSolverVersion.solver_version = "CalculiX 2.23\nforged";
  require(hasResultIssue(
              ps::compile_calculix_result(request, unsafeSolverVersion),
              "invalid_solver_version"),
          "unsafe solver version evidence remains indeterminate");
  auto missingCompletion = completeEvidence;
  missingCompletion.standard_output = "CalculiX Version 2.23\n";
  require(hasResultIssue(
              ps::compile_calculix_result(request, missingCompletion),
              "solver_completion_marker_missing"),
          "missing completion marker remains indeterminate");
  auto deceptiveCompletion = completeEvidence;
  deceptiveCompletion.standard_output =
      "No Job finished marker was emitted by the solver\n";
  require(hasResultIssue(
              ps::compile_calculix_result(request, deceptiveCompletion),
              "solver_completion_marker_missing"),
          "completion words embedded in another line do not count");
  auto solverError = completeEvidence;
  solverError.standard_error = "*ERROR in e_c3d: nonpositive jacobian\n";
  require(hasResultIssue(ps::compile_calculix_result(request, solverError),
                         "solver_reported_error"),
          "solver error output cannot be hidden by a completion marker");
  auto staleStep = completeEvidence;
  staleStep.status_bytes = "1 1 1 1 0.5 0.5 0.5\n";
  require(hasResultIssue(ps::compile_calculix_result(request, staleStep),
                         "solver_step_incomplete"),
          "incomplete final step remains indeterminate");
  auto malformedStatus = completeEvidence;
  malformedStatus.status_bytes = "1 1 1\n";
  require(hasResultIssue(
              ps::compile_calculix_result(request, malformedStatus),
              "invalid_solver_status"),
          "malformed convergence evidence remains indeterminate");
  auto wrongDeck = completeEvidence;
  wrongDeck.deck_bytes += "** changed after execution\n";
  require(hasResultIssue(ps::compile_calculix_result(request, wrongDeck),
                         "deck_request_mismatch"),
          "raw output cannot detach from the reviewed deck");
  auto invalidFrd = completeEvidence;
  invalidFrd.frd_sha256 = "sha256:INVALID";
  require(hasResultIssue(ps::compile_calculix_result(request, invalidFrd),
                         "invalid_frd_identity"),
          "FRD metadata requires an exact content identity");

  auto missingNodeResult = completeEvidence;
  missingNodeResult.data_bytes = replaceOnce(
      missingNodeResult.data_bytes,
      "         2  0.100000E-08  0.000000E+00  0.000000E+00\n", "");
  require(hasResultIssue(
              ps::compile_calculix_result(request, missingNodeResult),
              "missing_displacement_row"),
          "missing displacement identities remain indeterminate");
  auto duplicateNodeResult = completeEvidence;
  duplicateNodeResult.data_bytes = replaceOnce(
      duplicateNodeResult.data_bytes,
      " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz)",
      "         2  0.100000E-08  0.000000E+00  0.000000E+00\n\n"
      " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz)");
  require(hasResultIssue(
              ps::compile_calculix_result(request, duplicateNodeResult),
              "duplicate_displacement_row"),
          "duplicate displacement identities remain indeterminate");
  auto unexpectedNodeResult = completeEvidence;
  unexpectedNodeResult.data_bytes = replaceOnce(
      unexpectedNodeResult.data_bytes,
      " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz)",
      "        99  0.000000E+00  0.000000E+00  0.000000E+00\n\n"
      " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz)");
  require(hasResultIssue(
              ps::compile_calculix_result(request, unexpectedNodeResult),
              "unexpected_displacement_row"),
          "foreign displacement identities remain indeterminate");
  auto missingStressResult = completeEvidence;
  missingStressResult.data_bytes = replaceOnce(
      missingStressResult.data_bytes,
      "         1   1 -2.571429E+03 -2.571429E+03 -6.000000E+03  0.000000E+00  0.000000E+00  0.000000E+00\n",
      "");
  require(hasResultIssue(
              ps::compile_calculix_result(request, missingStressResult),
              "missing_stress_row"),
          "missing stress identities remain indeterminate");
  auto duplicateStressResult = completeEvidence;
  duplicateStressResult.data_bytes +=
      "         1   1 -2.571429E+03 -2.571429E+03 -6.000000E+03  0.000000E+00  0.000000E+00  0.000000E+00\n";
  require(hasResultIssue(
              ps::compile_calculix_result(request, duplicateStressResult),
              "duplicate_stress_row"),
          "duplicate stress identities remain indeterminate");
  auto unexpectedStressResult = completeEvidence;
  unexpectedStressResult.data_bytes +=
      "        99   1 -2.571429E+03 -2.571429E+03 -6.000000E+03  0.000000E+00  0.000000E+00  0.000000E+00\n";
  require(hasResultIssue(
              ps::compile_calculix_result(request, unexpectedStressResult),
              "unexpected_stress_row"),
          "foreign stress identities remain indeterminate");
  auto nonfiniteResult = completeEvidence;
  nonfiniteResult.data_bytes = replaceOnce(
      nonfiniteResult.data_bytes, "-2.228571E-08", "nan");
  require(hasResultIssue(
              ps::compile_calculix_result(request, nonfiniteResult),
              "invalid_result_data"),
          "non-finite result rows remain indeterminate");

  constexpr auto rawMesh = R"(*Heading
*NODE
4, 0, 0, 10
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
*ELEMENT, type=CPS3, ELSET=Surface1
8, 1, 2, 3
*ELEMENT, type=C3D4, ELSET=Volume1
9, 1, 2, 3, 4
)";
  constexpr std::string_view twoGroupTetra = R"(*HEADING
Synthetic two-group tetrahedron; coordinates are millimetres
*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
*ELEMENT, TYPE=CPS3, ELSET=FixedFaces
1, 1, 3, 2
*ELEMENT, TYPE=CPS3, ELSET=LoadedFaces
2, 1, 2, 4
3, 1, 4, 3
4, 2, 3, 4
*ELEMENT, TYPE=C3D4, ELSET=Volume
5, 1, 2, 3, 4
)";
  const auto prepared = ps::prepare_gmsh_abaqus_mesh(twoGroupTetra, 0.001);
  require(prepared.mesh.nodes.size() == 4,
          "prepared mesh retains nodes");
  require(prepared.mesh.elements.size() == 1,
          "prepared mesh retains C3D4 elements");
  require(prepared.boundary_faces.size() == 4,
          "prepared mesh derives the complete exterior once");
  require(prepared.source_surface_groups.size() == 2 &&
              prepared.source_surface_groups.front().name == "FixedFaces" &&
              prepared.source_surface_groups.back().name == "LoadedFaces",
          "prepared mesh retains source labels as non-authoritative hints");
  require(prepared.diagnostics.connected_components == 1,
          "prepared mesh records face connectivity");
  require(prepared.diagnostics.minimum_mean_ratio > 0.0 &&
              prepared.diagnostics.maximum_mean_ratio <= 1.0,
          "prepared mesh records bounded tetra quality");
  require(prepared.identity.source_sha256.starts_with("sha256:") &&
              prepared.identity.coordinate_scale_to_m == 0.001,
          "prepared mesh binds exact source bytes and coordinate scale");

  constexpr std::string_view invertedTetra = R"(*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
*ELEMENT, TYPE=C3D4
1, 1, 3, 2, 4
)";
  requireThrows(
      [&] { (void)ps::prepare_gmsh_abaqus_mesh(invertedTetra, 0.001); },
      "inverted tetrahedron", "inverted tetrahedra are rejected during preparation");

  constexpr std::string_view disconnectedTetrahedra = R"(*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
5, 20, 0, 0
6, 30, 0, 0
7, 20, 10, 0
8, 20, 0, 10
*ELEMENT, TYPE=C3D4
1, 1, 2, 3, 4
2, 5, 6, 7, 8
)";
  requireThrows(
      [&] {
        (void)ps::prepare_gmsh_abaqus_mesh(disconnectedTetrahedra, 0.001);
      },
      "face-connected volume component",
      "face-disconnected tetrahedral regions are rejected");

  constexpr std::string_view nonManifoldTetrahedra = R"(*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
5, 0, 0, -10
6, 0, 0, 20
*ELEMENT, TYPE=C3D4
1, 1, 2, 3, 4
2, 1, 3, 2, 5
3, 1, 2, 3, 6
)";
  requireThrows(
      [&] {
        (void)ps::prepare_gmsh_abaqus_mesh(nonManifoldTetrahedra, 0.001);
      },
      "non-manifold tetrahedral face",
      "non-manifold tetrahedral faces are rejected");

  constexpr std::string_view duplicateSourceTriangle = R"(*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
*ELEMENT, TYPE=CPS3, ELSET=FirstLabel
1, 1, 2, 3
*ELEMENT, TYPE=CPS3, ELSET=SecondLabel
2, 1, 3, 2
*ELEMENT, TYPE=C3D4
3, 1, 2, 3, 4
)";
  requireThrows(
      [&] {
        (void)ps::prepare_gmsh_abaqus_mesh(duplicateSourceTriangle, 0.001);
      },
      "more than once",
      "one boundary triangle cannot appear under multiple source labels");

  constexpr std::string_view interiorSourceTriangle = R"(*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
5, 0, 0, -10
*ELEMENT, TYPE=CPS3, ELSET=InteriorLabel
1, 1, 2, 3
*ELEMENT, TYPE=C3D4
2, 1, 2, 3, 4
3, 1, 3, 2, 5
)";
  requireThrows(
      [&] {
        (void)ps::prepare_gmsh_abaqus_mesh(interiorSourceTriangle, 0.001);
      },
      "not a volume boundary face",
      "source labels cannot turn an interior face into a selectable boundary");

  const auto mesh = ps::parse_gmsh_abaqus_mesh(rawMesh, 0.001);
  require(mesh.nodes.size() == 4 && mesh.elements.size() == 1 &&
              mesh.nodes.front().position_m[2] == 0.01,
          "Gmsh mesh parser retains only C3D4 and converts mm to m explicitly");
  const auto boundary = ps::extract_boundary_faces(mesh);
  require(boundary.size() == 4,
          "one tetrahedron exposes four deterministic boundary faces");
  double boundaryArea = 0.0;
  for (const auto &face : boundary) {
    boundaryArea += face.area_m2;
    const auto oppositeId = *std::ranges::find_if(
        mesh.elements.front().node_ids, [&](const int nodeId) {
          return std::ranges::find(face.node_ids, nodeId) == face.node_ids.end();
        });
    const auto oppositeNode = std::ranges::find_if(
        mesh.nodes, [&](const auto &node) { return node.id == oppositeId; });
    require(oppositeNode != mesh.nodes.end(), "opposite boundary node exists");
    const auto &opposite = oppositeNode->position_m;
    const auto towardInterior = std::array<double, 3>{
        opposite[0] - face.centroid_m[0], opposite[1] - face.centroid_m[1],
        opposite[2] - face.centroid_m[2]};
    require(face.outward_unit_normal[0] * towardInterior[0] +
                    face.outward_unit_normal[1] * towardInterior[1] +
                    face.outward_unit_normal[2] * towardInterior[2] <
                0.0,
            "boundary normals point away from the tetrahedron interior");
  }
  require(std::abs(boundaryArea - (0.00015 + std::sqrt(3.0) * 0.00005)) < 1e-12,
          "boundary face areas are reported in square metres");
  require(ps::group_boundary_faces(boundary, 1.0).size() == 4,
          "sharp tetrahedron faces remain separate selectable patches");

  const std::vector<ps::BoundaryFace> flatFaces{
      {1, {1, 2, 3}, {2.0 / 3.0, 1.0 / 3.0, 0.0}, {0.0, 0.0, 1.0}, 0.5},
      {2, {2, 4, 3}, {1.0 / 3.0, 2.0 / 3.0, 0.0}, {0.0, 0.0, 1.0}, 0.5}};
  const auto flatPatches = ps::group_boundary_faces(flatFaces, 0.0);
  require(flatPatches.size() == 1 && flatPatches.front().id == 1 &&
              flatPatches.front().face_node_ids.size() == 2 &&
              flatPatches.front().node_ids == std::vector<int>({1, 2, 3, 4}) &&
              std::abs(flatPatches.front().area_m2 - 1.0) < 1e-15 &&
              std::abs(flatPatches.front().area_weighted_centroid_m[0] - 0.5) < 1e-15,
          "adjacent coplanar triangles form one deterministic SI surface patch");
  const auto selected = ps::resolve_boundary_selection(
      "reviewed load face", flatPatches, {flatPatches.front().id});
  require(selected.face_node_ids.size() == 2 && selected.node_ids.size() == 4 &&
              std::abs(selected.area_m2 - 1.0) < 1e-15,
          "visual patches resolve into durable exact boundary topology");
  const auto distributed = ps::distribute_surface_total_force(
      selected, {0.0, 0.0, -120.0}, flatFaces);
  std::array<double, 3> distributedSum{};
  for (const auto &nodal : distributed)
    for (int axis = 0; axis < 3; ++axis)
      distributedSum[axis] += nodal.force_n[axis];
  require(distributed.size() == 4 && std::abs(distributedSum[2] + 120.0) < 1e-12 &&
              std::abs(distributed[0].force_n[2] + 20.0) < 1e-12 &&
              std::abs(distributed[1].force_n[2] + 40.0) < 1e-12,
          "surface force distribution preserves the reviewed total vector");
  try {
    (void)ps::resolve_boundary_selection("reviewed load face", flatPatches,
                                         {1, 1});
    fail("duplicate visual patch selection was accepted");
  } catch (const std::invalid_argument &) {
  }
  try {
    (void)ps::group_boundary_faces(flatFaces, -1.0);
    fail("invalid surface grouping angle was accepted");
  } catch (const std::invalid_argument &) {
  }

  const auto tetraPatches =
      ps::group_boundary_faces(prepared.boundary_faces, 1.0);
  ps::StructuralSetup setup{
      .analysis_id = "reviewed-tetra-setup",
      .component_name = "benchmark tetrahedron",
      .geometry_sha256 = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      .mesh = prepared.mesh,
      .boundary_faces = prepared.boundary_faces,
      .material =
          {.designation = "benchmark isotropic material",
           .source_sha256 =
               "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
           .applicability = "known",
           .youngs_modulus_pa = 7.0e10,
           .poisson_ratio = 0.33,
           .reviewed = true,
           .temper = "not_applicable",
           .product_form = "synthetic benchmark"},
      .load = {ps::resolve_boundary_selection("load", tetraPatches, {1}),
               {0.0, 0.0, -100.0}, true},
      .restraint = {ps::resolve_boundary_selection("fixed", tetraPatches, {2}), true},
      .requirement =
          {.displacement_limit_m = 0.001,
           .von_mises_limit_pa = 1.0e8,
           .source_or_exploratory_rationale =
               "explicit exploratory benchmark limits",
           .reviewed = true,
           .displacement_limit_basis =
               "explicit exploratory displacement limit",
           .von_mises_limit_basis =
               "explicit exploratory stress limit"},
      .mesh_controls =
          {.minimum_size_m = 0.001,
           .maximum_size_m = 0.003,
           .mesher_identity = "test mesher 1.0",
           .reviewed = true,
           .mesh_sha256 = prepared.identity.source_sha256,
           .coordinate_scale_to_m = prepared.identity.coordinate_scale_to_m,
           .target_size_m = 0.002,
           .minimum_mean_ratio_threshold = 0.05,
           .observed_minimum_mean_ratio =
               prepared.diagnostics.minimum_mean_ratio},
      .scenario_description = "one bounded linear-static benchmark scenario",
      .scenario_confirmed = true};
  require(ps::validate_setup(setup).empty(),
          "reviewed setup with provenance and exact topology validates");
  const auto reviewedRequest = ps::compile_structural_request(setup);
  require(reviewedRequest.fully_fixed_node_ids.size() == 3 &&
              reviewedRequest.nodal_forces.size() == 3,
          "reviewed surface setup compiles into the narrow solver request");
  require(reviewedRequest.material_temper == "not_applicable" &&
              reviewedRequest.material_product_form == "synthetic benchmark" &&
              reviewedRequest.mesh_sha256 == prepared.identity.source_sha256 &&
              reviewedRequest.mesh_reviewed,
          "compiled request retains reviewed material and mesh provenance");

  auto zeroForce = reviewedRequest;
  zeroForce.nodal_forces = {{1, {0.0, 0.0, 0.0}}};
  require(hasIssue(ps::validate_request(zeroForce), "zero_resultant_load"),
          "all-zero force cannot enter a deck");
  auto duplicateForce = reviewedRequest;
  duplicateForce.nodal_forces.push_back(duplicateForce.nodal_forces.front());
  require(hasIssue(ps::validate_request(duplicateForce), "duplicate_load_node"),
          "duplicate nodal loads require deterministic aggregation");
  auto uppercaseHash = reviewedRequest;
  uppercaseHash.geometry_sha256 =
      "sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  require(hasIssue(ps::validate_request(uppercaseHash),
                   "invalid_geometry_identity"),
          "SHA-256 identity is strict lowercase");
  auto injectedHeading = reviewedRequest;
  injectedHeading.component_name = "bracket\n*INCLUDE, INPUT=other.inp";
  require(hasIssue(ps::validate_request(injectedHeading), "unsafe_heading_text"),
          "heading text cannot inject CalculiX keywords");
  auto unresolvedMaterial = reviewedRequest;
  unresolvedMaterial.material_applicability = "unresolved";
  require(hasIssue(ps::validate_request(unresolvedMaterial),
                   "material_applicability_unresolved"),
          "unresolved material applicability cannot become reviewed");
  auto weakMesh = reviewedRequest;
  weakMesh.observed_minimum_mean_ratio = 0.04;
  weakMesh.minimum_mean_ratio_threshold = 0.05;
  require(hasIssue(ps::validate_request(weakMesh),
                   "mesh_quality_below_limit"),
          "mesh quality below the reviewed floor remains blocked");
  auto invalidMeshScale = reviewedRequest;
  invalidMeshScale.mesh_coordinate_scale_to_m = 0.0;
  require(hasIssue(ps::validate_request(invalidMeshScale),
                   "invalid_mesh_coordinate_scale"),
          "source mesh unit scale cannot disappear during compilation");
  auto inverted = reviewedRequest;
  std::swap(inverted.elements.front().node_ids[0],
            inverted.elements.front().node_ids[1]);
  require(hasIssue(ps::validate_request(inverted), "inverted_element"),
          "inverted tetrahedra cannot enter a deck");
  auto loadMismatch = reviewedRequest;
  loadMismatch.nodal_forces.front().force_n[2] += 1.0;
  require(hasIssue(ps::validate_request(loadMismatch),
                   "compiled_load_mismatch"),
          "compiled nodal force reproduces the reviewed resultant");
  auto nonunitDirection = reviewedRequest;
  nonunitDirection.reviewed_force_direction = {0.0, 0.0, -2.0};
  require(hasIssue(ps::validate_request(nonunitDirection),
                   "invalid_reviewed_force_direction"),
          "reviewed force direction must be normalized");
  auto missingBasis = reviewedRequest;
  missingBasis.displacement_limit_basis.clear();
  require(hasIssue(ps::validate_request(missingBasis),
                   "missing_displacement_limit_basis"),
          "a displacement limit requires a reviewed basis");

  const auto compiled = ps::compile_structural_setup(setup);
  require(compiled.calculix_deck ==
              ps::generate_calculix_deck(compiled.request),
          "compiled setup retains its exact deterministic deck");
  require(compiled.identity.starts_with("sha256:") &&
              !compiled.canonical_setup_evidence.empty(),
          "compiled setup has canonical evidence and a content identity");
  const auto compiledAgain = ps::compile_structural_setup(setup);
  require(compiledAgain.identity == compiled.identity &&
              compiledAgain.canonical_setup_evidence ==
                  compiled.canonical_setup_evidence &&
              compiledAgain.calculix_deck == compiled.calculix_deck,
          "identical reviewed setup compiles to identical immutable bytes");
  auto unreviewedSetup = setup;
  unreviewedSetup.material.reviewed = false;
  unreviewedSetup.requirement.source_or_exploratory_rationale.clear();
  unreviewedSetup.scenario_confirmed = false;
  const auto setupIssues = ps::validate_setup(unreviewedSetup);
  require(hasIssue(setupIssues, "material_unreviewed") &&
              hasIssue(setupIssues, "requirement_provenance_missing") &&
              hasIssue(setupIssues, "scenario_unconfirmed"),
          "unreviewed setup and missing rationale remain explicit blockers");
  auto staleSelection = setup;
  staleSelection.load.selection.area_m2 *= 2.0;
  require(hasIssue(ps::validate_setup(staleSelection), "load_selection_invalid"),
          "stale exact boundary selection is rejected before solver compilation");
  auto invalidPatchAngle = setup;
  invalidPatchAngle.selection_patch_angle_degrees = 0.0;
  require(hasIssue(ps::validate_setup(invalidPatchAngle),
                   "selection_patch_angle_invalid"),
          "invalid reviewed surface grouping angle is rejected");
  try {
    (void)ps::compile_structural_request(unreviewedSetup);
    fail("unreviewed structural setup compiled into a solver request");
  } catch (const std::invalid_argument &) {
  }

  auto paired = mesh;
  paired.nodes.push_back({5, {0.0, 0.0, -0.01}});
  paired.elements.push_back({10, {1, 3, 2, 5}});
  require(ps::extract_boundary_faces(paired).size() == 6,
          "a shared tetrahedron face is excluded from the exterior boundary");
  auto nonManifold = paired;
  nonManifold.nodes.push_back({6, {0.0, 0.0, 0.02}});
  nonManifold.elements.push_back({11, {1, 2, 3, 6}});
  try {
    (void)ps::extract_boundary_faces(nonManifold);
    fail("non-manifold volume face was accepted");
  } catch (const std::invalid_argument &) {
  }
  try {
    (void)ps::parse_gmsh_abaqus_mesh(rawMesh, 0.0);
    fail("invalid mesh scale was accepted");
  } catch (const std::invalid_argument &) {
  }

  const auto processRoot = fs::temp_directory_path() /
      ("prometheus-structural-process-" + std::to_string(std::rand()));
  fs::create_directories(processRoot);
  const auto fixture = fs::path(PROMETHEUS_SOLVER_FIXTURE_PATH);
  const auto runFixture = [&](const std::string &job,
                              const std::chrono::milliseconds timeout) {
    return ps::run_calculix({fixture, processRoot, job, timeout}, compiled);
  };
  const auto completed = runFixture("success", std::chrono::seconds(5));
  require(completed.status == ps::SolverRunStatus::completed &&
              completed.exit_code == 0 && completed.validated_result &&
              completed.validated_result->complete() &&
              completed.validated_result->compiled_setup_identity ==
                  compiled.identity &&
              std::abs(completed.validated_result->metrics
                           ->maximum_displacement_m -
                       2.0e-5) < 1e-15 &&
              completed.standard_output.find("CalculiX Version 2.23") !=
                  std::string::npos &&
              completed.standard_error.find("fixture stderr") != std::string::npos,
          "isolated solver captures and compiles complete evidence exactly once");
  const auto staleOutputs = runFixture("success", std::chrono::seconds(5));
  require(staleOutputs.status == ps::SolverRunStatus::output_conflict &&
              !staleOutputs.validated_result,
          "pre-existing raw solver outputs cannot be reused as a new run");
  const ps::StructuralRefinementEvidence acceptedRefinement{
      .complete = true,
      .criteria_satisfied = true,
      .coarse_to_fine_change_fraction = 0.04,
      .maximum_allowed_change_fraction = 0.10,
      .result_sha256 = {std::string(coarseResultHash),
                        std::string(fineResultHash)}};
  const auto &findingRequest = compiled.request;
  const auto withinLimits = ps::compile_structural_findings(
      findingRequest, completed.validated_result, acceptedRefinement);
  require(withinLimits.declared_obligations == 2 &&
              withinLimits.evaluated_obligations == 2 &&
              withinLimits.refinement.has_value() &&
              withinLimits.refinement->result_sha256 ==
                  acceptedRefinement.result_sha256 &&
              std::ranges::all_of(withinLimits.findings, [](const auto &finding) {
                return finding.disposition ==
                    ps::StructuralFindingDisposition::no_violation_detected_within_scope;
              }) &&
              std::ranges::all_of(withinLimits.findings, [&](const auto &finding) {
                return std::ranges::find(
                           finding.evidence_sha256,
                           completed.validated_result->identity) !=
                           finding.evidence_sha256.end() &&
                       !finding.assumptions.empty();
              }) &&
              withinLimits.limitation.find("project-wide") != std::string::npos,
          "validated and refined evidence compiles into scoped findings");
  auto strictRequest = findingRequest;
  strictRequest.displacement_limit_m = 1.0e-6;
  strictRequest.von_mises_limit_pa = 1.0e5;
  const auto violated = ps::compile_structural_findings(
      strictRequest, completed.validated_result, acceptedRefinement);
  require(std::ranges::all_of(violated.findings, [](const auto &finding) {
            return finding.disposition == ps::StructuralFindingDisposition::violated &&
                   finding.margin_to_limit < 0.0;
          }),
          "the same completed metrics produce known-fail violations at tighter limits");
  auto equalityResult = *completed.validated_result;
  equalityResult.metrics->maximum_displacement_m =
      *findingRequest.displacement_limit_m;
  const auto equality = ps::compile_structural_findings(
      findingRequest, equalityResult, acceptedRefinement);
  require(!equality.findings.empty() &&
              equality.findings.front().disposition ==
                  ps::StructuralFindingDisposition::violated &&
              equality.findings.front().margin_to_limit == 0.0,
          "a zero margin is reported as a violation");
  const auto unrefined = ps::compile_structural_findings(
      findingRequest, completed.validated_result, std::nullopt);
  require(unrefined.evaluated_obligations == 0 && unrefined.findings.empty(),
          "missing refinement evidence cannot generate findings");
  auto inconsistentRefinement = acceptedRefinement;
  inconsistentRefinement.coarse_to_fine_change_fraction = 0.20;
  const auto inconsistent = ps::compile_structural_findings(
      findingRequest, completed.validated_result, inconsistentRefinement);
  require(inconsistent.evaluated_obligations == 0 &&
              inconsistent.findings.empty(),
          "inconsistent refinement evidence cannot generate findings");
  auto invalidCompiledResult = *completed.validated_result;
  invalidCompiledResult.issues.push_back(
      {"test_invalid", "synthetic invalid result"});
  const auto invalidEvidence = ps::compile_structural_findings(
      findingRequest, invalidCompiledResult, acceptedRefinement);
  require(invalidEvidence.evaluated_obligations == 0 &&
              invalidEvidence.findings.empty(),
          "invalid solver evidence cannot generate findings");
  const auto legacyManifest = createLegacyV1Archive(
      processRoot / "legacy-v1", request, completeEvidence,
      *compiledResult.metrics);
  const auto legacyVerification =
      ps::verify_structural_archive(legacyManifest);
  require(legacyVerification.valid &&
              legacyVerification.schema_version == "1.0.0",
          "legacy v1 archive remains readable under its original claim");

  const auto archive = ps::write_structural_archive(
      processRoot, "success", compiled, completed, withinLimits);
  require(archive.schema_version == "2.0.0" &&
              archive.validated_result_identity ==
                  completed.validated_result->identity,
          "new archive reuses the active validated result under v2");
  const auto archiveVerification =
      ps::verify_structural_archive(archive.manifest_path);
  require(archiveVerification.valid &&
              archiveVerification.schema_version == "2.0.0" &&
              archiveVerification.validated_result_identity ==
                  completed.validated_result->identity,
          "persisted v2 archive replays after crossing the trust boundary");
  const auto nonzero = runFixture("nonzero", std::chrono::seconds(5));
  require(nonzero.status == ps::SolverRunStatus::nonzero_exit &&
              nonzero.exit_code == 7 && !nonzero.validated_result,
          "nonzero solver exit cannot become completed metrics");
  const auto missing = runFixture("missing", std::chrono::seconds(5));
  require(missing.status == ps::SolverRunStatus::output_missing &&
              !missing.validated_result,
          "successful process without required raw files fails closed");
  const auto invalid = runFixture("invalid", std::chrono::seconds(5));
  require(invalid.status == ps::SolverRunStatus::result_invalid &&
              invalid.validated_result &&
              !invalid.validated_result->complete(),
          "malformed solver result evidence remains inspectable and indeterminate");
  const auto timedOut = runFixture("timeout", std::chrono::milliseconds(30));
  require(timedOut.status == ps::SolverRunStatus::timed_out &&
              !timedOut.validated_result,
          "solver timeout is terminated and classified without metrics");
  const auto indeterminate = ps::compile_structural_findings(
      findingRequest, timedOut.validated_result, acceptedRefinement);
  require(indeterminate.declared_obligations == 2 &&
              indeterminate.evaluated_obligations == 0 &&
              indeterminate.findings.empty(),
          "failed execution cannot satisfy or violate engineering obligations");
  fs::remove_all(processRoot);
  return 0;
}
