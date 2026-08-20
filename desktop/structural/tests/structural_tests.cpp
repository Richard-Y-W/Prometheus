#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/prepared_mesh.hpp"
#include "prometheus/structural/structural_archive.hpp"
#include "prometheus/structural/structural_request.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/structural/structural_refinement.hpp"
#include "prometheus/structural/structural_setup.hpp"
#include "prometheus/structural/surface_groups.hpp"
#include "prometheus/structural/surface_selection.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

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
#include <vector>

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

void requireRetiredStructuralApisAbsent() {
  const auto repository = fs::path(PROMETHEUS_REPOSITORY_ROOT);
  std::string activeSource;
  for (const auto &root : {
           repository / "desktop/structural/include",
           repository / "desktop/structural/src",
           repository / "desktop/structural/tools"}) {
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
      if (entry.is_regular_file() &&
          (entry.path().extension() == ".hpp" ||
           entry.path().extension() == ".cpp"))
        activeSource += fixtureBytes(entry.path());
    }
  }
  for (const auto &path : {
           repository / "desktop/app/structural_backend.hpp",
           repository / "desktop/app/structural_backend.cpp"})
    activeSource += fixtureBytes(path);

  bool clean = true;
  for (const auto token : {
           "struct StructuralRefinementEvidence",
           "compile_structural_refinement_evidence(",
           "compile_structural_findings(request,",
           "write_structural_archive(",
           "DesktopStructuralRun execute("}) {
    if (activeSource.find(token) != std::string::npos) {
      std::cerr << "retired structural API remains: " << token << '\n';
      clean = false;
    }
  }
  require(clean, "retired structural production APIs are absent");
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

std::string jsonString(const std::string_view value) {
  constexpr char hexadecimal[] = "0123456789abcdef";
  std::string result{"\""};
  for (const unsigned char character : value) {
    switch (character) {
    case '\"': result += "\\\""; break;
    case '\\': result += "\\\\"; break;
    case '\b': result += "\\b"; break;
    case '\f': result += "\\f"; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (character < 0x20U) {
        result += "\\u00";
        result += hexadecimal[character >> 4U];
        result += hexadecimal[character & 0x0fU];
      } else {
        result += static_cast<char>(character);
      }
    }
  }
  result += '\"';
  return result;
}

std::string optionalJsonNumber(const std::optional<double> &value) {
  return value ? jsonNumber(*value) : "null";
}

std::string evidenceRootV3Identity(
    const ps::CompiledCalculixResult &result,
    const std::string_view geometryIdentity) {
  const nlohmann::json document{
      {"$schema",
       "urn:prometheus:schema:compiled-calculix-result:3.0.0"},
      {"schema_version", "3.0.0"},
      {"compiler_version", "calculix-evidence-compiler-v3"},
      {"compiled_setup_identity", result.compiled_setup_identity},
      {"request_geometry_sha256", geometryIdentity},
      {"backend",
       {{"executable_sha256", result.backend.executable_sha256},
        {"version", result.backend.version}}},
      {"artifacts",
       {{"deck", result.artifacts.deck.sha256},
        {"sta", result.artifacts.sta.sha256},
        {"dat", result.artifacts.dat.sha256},
        {"frd", result.artifacts.frd.sha256},
        {"stdout", result.artifacts.standard_output.sha256},
        {"stderr", result.artifacts.standard_error.sha256}}}};
  return prometheus::integrity::sha256_bytes(
      prometheus::integrity::canonicalize_json_bytes(document.dump()));
}

std::string legacyV2ResultIdentity(
    const std::string &setupIdentity,
    const std::string &geometryIdentity,
    const nlohmann::json &backend,
    const nlohmann::json &artifacts,
    const nlohmann::json &convergence,
    const nlohmann::json &metrics) {
  const nlohmann::json document{
      {"$schema",
       "urn:prometheus:schema:compiled-calculix-result:2.0.0"},
      {"schema_version", "2.0.0"},
      {"compiler_version", "calculix-evidence-compiler-v2"},
      {"compiled_setup_identity", setupIdentity},
      {"request_geometry_sha256", geometryIdentity},
      {"backend", backend},
      {"artifacts",
       {{"deck", artifacts.at("deck").at("sha256")},
        {"sta", artifacts.at("sta").at("sha256")},
        {"dat", artifacts.at("dat").at("sha256")},
        {"frd", artifacts.at("frd").at("sha256")},
        {"stdout", artifacts.at("stdout").at("sha256")},
        {"stderr", artifacts.at("stderr").at("sha256")}}},
      {"convergence", convergence},
      {"metrics", metrics}};
  return prometheus::integrity::sha256_bytes(
      prometheus::integrity::canonicalize_json_bytes(document.dump()));
}

std::string legacyV2ResultIdentity(
    const std::string &setupIdentity,
    const std::string &geometryIdentity,
    const ps::CompiledCalculixResult &result) {
  require(result.metrics && result.convergence,
          "legacy identity requires a complete result");
  const nlohmann::json backend{
      {"executable_sha256", result.backend.executable_sha256},
      {"version", result.backend.version}};
  const nlohmann::json artifacts{
      {"deck", {{"sha256", result.artifacts.deck.sha256}}},
      {"sta", {{"sha256", result.artifacts.sta.sha256}}},
      {"dat", {{"sha256", result.artifacts.dat.sha256}}},
      {"frd", {{"sha256", result.artifacts.frd.sha256}}},
      {"stdout", {{"sha256", result.artifacts.standard_output.sha256}}},
      {"stderr", {{"sha256", result.artifacts.standard_error.sha256}}}};
  const auto &storedConvergence = *result.convergence;
  const nlohmann::json convergence{
      {"step", storedConvergence.step},
      {"increment", storedConvergence.increment},
      {"attempt", storedConvergence.attempt},
      {"iterations", storedConvergence.iterations},
      {"total_time", storedConvergence.total_time},
      {"step_time", storedConvergence.step_time},
      {"increment_time", storedConvergence.increment_time}};
  const auto &storedMetrics = *result.metrics;
  const nlohmann::json metrics{
      {"maximum_displacement_m", storedMetrics.maximum_displacement_m},
      {"maximum_von_mises_pa", storedMetrics.maximum_von_mises_pa},
      {"displacement_rows", storedMetrics.displacement_rows},
      {"stress_rows", storedMetrics.stress_rows}};
  return legacyV2ResultIdentity(setupIdentity, geometryIdentity, backend,
                                artifacts, convergence, metrics);
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

fs::path createLegacyV2Archive(
    const fs::path &root, const fs::path &sourceDirectory,
    const std::string_view sourceJob,
    const ps::CompiledStructuralSetup &setup,
    const ps::SolverRunResult &run,
    const std::string_view secondResultIdentity) {
  require(run.status == ps::SolverRunStatus::completed &&
              run.validated_result && run.validated_result->complete() &&
              run.validated_result->metrics &&
              run.validated_result->convergence,
          "legacy v2 fixture starts from a completed result");
  fs::create_directories(root);
  const std::string jobName = "legacy_v2";
  const std::string setupName = "reviewed-structural-setup.json";
  const std::string deckName = jobName + ".inp";
  const std::string datName = jobName + ".dat";
  const std::string frdName = jobName + ".frd";
  const std::string staName = jobName + ".sta";
  const std::string stdoutName = "solver.stdout.txt";
  const std::string stderrName = "solver.stderr.txt";
  writeFixtureBytes(root / setupName, setup.canonical_setup_evidence);
  for (const auto extension : {".inp", ".dat", ".frd", ".sta"})
    writeFixtureBytes(root / (jobName + extension),
                      fixtureBytes(sourceDirectory /
                                   (std::string(sourceJob) + extension)));
  writeFixtureBytes(root / stdoutName, run.standard_output);
  writeFixtureBytes(root / stderrName, run.standard_error);

  const auto artifact = [&](const std::string &name) {
    const auto bytes = fixtureBytes(root / name);
    return std::string{"{\"byte_length\":"} +
           std::to_string(bytes.size()) + ",\"file\":" +
           jsonString(name) + ",\"sha256\":" +
           jsonString(prometheus::integrity::sha256_bytes(bytes)) + "}";
  };
  const auto &validated = *run.validated_result;
  const auto &request = setup.request;
  const auto &metrics = *validated.metrics;
  const auto &convergence = *validated.convergence;
  const auto legacyIdentity = legacyV2ResultIdentity(
      setup.identity, request.geometry_sha256, validated);
  std::vector<std::string> findingEvidence{
      std::string(secondResultIdentity), legacyIdentity};
  std::ranges::sort(findingEvidence);
  findingEvidence.erase(
      std::unique(findingEvidence.begin(), findingEvidence.end()),
      findingEvidence.end());
  const auto evidenceJson = [&] {
    std::string result{"["};
    for (const auto &identity : findingEvidence) {
      if (result.size() > 1U)
        result += ',';
      result += jsonString(identity);
    }
    return result + ']';
  }();
  const std::string assumptions =
      "[\"small-deformation linear static response\","
      "\"isotropic linear-elastic material behavior\","
      "\"reviewed loads and fully fixed restraints represent the scenario\","
      "\"reported extrema are bounded by the submitted mesh and solver output\"]";
  const auto finding = [&](const std::string_view obligation,
                           const double measured, const double limit,
                           const std::string_view unit) {
    const auto margin = limit - measured;
    return std::string{"{\"assumptions\":"} + assumptions +
           ",\"disposition\":" +
           jsonString(margin > 0.0
                          ? "no_violation_detected_within_scope"
                          : "violated") +
           ",\"evidence_sha256\":" + evidenceJson +
           ",\"limit\":" + jsonNumber(limit) +
           ",\"margin\":" + jsonNumber(margin) +
           ",\"measured\":" + jsonNumber(measured) +
           ",\"obligation\":" + jsonString(obligation) +
           ",\"scope\":" +
           jsonString(
               "isotropic linear-elastic C3D4 model under the confirmed "
               "scenario with accepted mesh-refinement evidence") +
           ",\"unit\":" + jsonString(unit) + "}";
  };
  std::string findings{"["};
  if (request.displacement_limit_m)
    findings += finding("maximum_displacement",
                        metrics.maximum_displacement_m,
                        *request.displacement_limit_m, "m");
  if (request.von_mises_limit_pa) {
    if (findings.size() > 1U)
      findings += ',';
    findings += finding("maximum_von_mises_stress",
                        metrics.maximum_von_mises_pa,
                        *request.von_mises_limit_pa, "Pa");
  }
  findings += ']';
  const int obligations =
      static_cast<int>(request.displacement_limit_m.has_value()) +
      static_cast<int>(request.von_mises_limit_pa.has_value());
  const std::string limitation =
      "These findings do not establish safety, fatigue life, buckling, contact, "
      "fastener adequacy, nonlinear behavior, or project-wide correctness.";
  const auto manifest = prometheus::integrity::canonicalize_json_bytes(
      std::string{"{\"$schema\":\"urn:prometheus:schema:structural-run-archive:2.0.0\","} +
      "\"analysis_id\":" + jsonString(request.analysis_id) +
      ",\"archive_kind\":\"completed_linear_static_run\"," +
      "\"artifacts\":{\"dat\":" + artifact(datName) +
      ",\"deck\":" + artifact(deckName) +
      ",\"frd\":" + artifact(frdName) +
      ",\"setup\":" + artifact(setupName) +
      ",\"sta\":" + artifact(staName) +
      ",\"stderr\":" + artifact(stderrName) +
      ",\"stdout\":" + artifact(stdoutName) + "}," +
      "\"backend\":{\"executable_sha256\":" +
      jsonString(validated.backend.executable_sha256) +
      ",\"version\":" + jsonString(validated.backend.version) + "}," +
      "\"compiled_setup_identity\":" + jsonString(setup.identity) +
      ",\"component_name\":" + jsonString(request.component_name) +
      ",\"convergence\":{\"attempt\":" +
      std::to_string(convergence.attempt) + ",\"increment\":" +
      std::to_string(convergence.increment) +
      ",\"increment_time\":" + jsonNumber(convergence.increment_time) +
      ",\"iterations\":" + std::to_string(convergence.iterations) +
      ",\"step\":" + std::to_string(convergence.step) +
      ",\"step_time\":" + jsonNumber(convergence.step_time) +
      ",\"total_time\":" + jsonNumber(convergence.total_time) + "}," +
      "\"coverage\":{\"declared_obligations\":" +
      std::to_string(obligations) + ",\"evaluated_obligations\":" +
      std::to_string(obligations) + "},\"execution\":{\"elapsed_ms\":" +
      std::to_string(run.elapsed.count()) +
      ",\"exit_code\":0,\"status\":\"completed\"}," +
      "\"findings\":" + findings + ",\"geometry_sha256\":" +
      jsonString(request.geometry_sha256) + ",\"job_name\":" +
      jsonString(jobName) + ",\"limitation\":" + jsonString(limitation) +
      ",\"metrics\":{\"displacement_rows\":" +
      std::to_string(metrics.displacement_rows) +
      ",\"maximum_displacement_m\":" +
      jsonNumber(metrics.maximum_displacement_m) +
      ",\"maximum_von_mises_pa\":" +
      jsonNumber(metrics.maximum_von_mises_pa) + ",\"stress_rows\":" +
      std::to_string(metrics.stress_rows) + "}," +
      "\"refinement\":{\"coarse_to_fine_change_fraction\":0.04,"
      "\"complete\":true,\"criteria_satisfied\":true,"
      "\"maximum_allowed_change_fraction\":0.1,\"result_sha256\":[" +
      jsonString(secondResultIdentity) + ',' + jsonString(legacyIdentity) +
      "]},\"requirements\":{\"displacement_limit_basis\":" +
      jsonString(request.displacement_limit_basis) +
      ",\"displacement_limit_m\":" +
      optionalJsonNumber(request.displacement_limit_m) +
      ",\"von_mises_limit_basis\":" +
      jsonString(request.von_mises_limit_basis) +
      ",\"von_mises_limit_pa\":" +
      optionalJsonNumber(request.von_mises_limit_pa) +
      "},\"schema_version\":\"2.0.0\",\"validated_result_identity\":" +
      jsonString(legacyIdentity) + "}" );
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

std::string replaceJsonMemberAfter(
    std::string source, const std::string_view marker,
    const std::string_view key, const std::string_view encodedValue) {
  const auto markerPosition = source.find(marker);
  require(markerPosition != std::string::npos,
          "scoped test replacement marker exists");
  const auto member = "\"" + std::string(key) + "\":";
  const auto memberPosition =
      source.find(member, markerPosition + marker.size());
  require(memberPosition != std::string::npos,
          "scoped JSON member exists");
  const auto valuePosition = memberPosition + member.size();
  const auto valueEnd = source.find_first_of(",}", valuePosition);
  require(valueEnd != std::string::npos,
          "scoped JSON member has a bounded value");
  source.replace(valuePosition, valueEnd - valuePosition, encodedValue);
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

bool hasRefinementIssue(
    const ps::StructuralRefinementCompilation &compiled,
    const std::string_view code) {
  return std::ranges::any_of(compiled.issues(), [&](const auto &issue) {
    return issue.code == code;
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
  requireRetiredStructuralApisAbsent();
  const auto axialBenchmark = ps::axial_tension_bar_benchmark();
  const auto refinedAxialBenchmark =
      ps::axial_tension_bar_benchmark(4, 2, 2);
  require(ps::validate_request(axialBenchmark.setup.request).empty() &&
              axialBenchmark.setup.request.analysis_id ==
                  "analytic-axial-tension-bar-refinement-v1" &&
              refinedAxialBenchmark.setup.request.analysis_id ==
                  axialBenchmark.setup.request.analysis_id &&
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
  const auto validationMeshes = ps::cantilever_validation_mesh_pair();
  require(validationMeshes.coarse.length_divisions == 80 &&
              validationMeshes.coarse.width_divisions == 12 &&
              validationMeshes.coarse.height_divisions == 12 &&
              validationMeshes.fine.length_divisions == 120 &&
              validationMeshes.fine.width_divisions == 18 &&
              validationMeshes.fine.height_divisions == 18,
          "cantilever validation pair pins the approved denser meshes");
  const auto validationFineElements =
      6ULL * static_cast<unsigned long long>(
                 validationMeshes.fine.length_divisions) *
      static_cast<unsigned long long>(
          validationMeshes.fine.width_divisions) *
      static_cast<unsigned long long>(
          validationMeshes.fine.height_divisions);
  require(validationFineElements == 233280ULL &&
              validationFineElements <= 480000ULL,
          "approved fine cantilever remains inside the mesher element bound");
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
  const auto expectedEvidenceIdentity =
      evidenceRootV3Identity(compiledResult, request.geometry_sha256);
  require(compiledResult.identity == expectedEvidenceIdentity,
          "compiled result identity is rooted in exact solver evidence");
  auto changedDerivedMetrics = compiledResult;
  changedDerivedMetrics.metrics->maximum_displacement_m = std::nextafter(
      changedDerivedMetrics.metrics->maximum_displacement_m,
      std::numeric_limits<double>::infinity());
  require(evidenceRootV3Identity(changedDerivedMetrics,
                                 request.geometry_sha256) ==
              expectedEvidenceIdentity,
          "derived metric rounding does not change evidence identity");
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
      ("prometheus-structural-process-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
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

  auto coarseReviewed =
      ps::cantilever_benchmark(8, 2, 2).setup.reviewed_setup;
  auto fineReviewed =
      ps::cantilever_benchmark(12, 3, 3).setup.reviewed_setup;
  require(coarseReviewed.analysis_id ==
                  "analytic-cantilever-refinement-v1" &&
              fineReviewed.analysis_id == coarseReviewed.analysis_id,
          "benchmark mesh resolution does not change analysis lineage");
  const auto coarseSetup = ps::compile_structural_setup(coarseReviewed);
  const auto fineSetup = ps::compile_structural_setup(fineReviewed);
  const auto criterion =
      ps::compile_structural_refinement_criterion(0.10);
  const auto refinementRoot = processRoot / "typed-refinement-study";
  fs::create_directory(refinementRoot);
  const ps::SolverRunOptions coarseOptions{
      fixture, refinementRoot, "typed_cantilever_coarse",
      std::chrono::seconds(5)};
  const ps::SolverRunOptions fineOptions{
      fixture, refinementRoot, "typed_cantilever_fine",
      std::chrono::seconds(5)};
  const auto coarseRun = ps::run_calculix(coarseOptions, coarseSetup);
  const auto fineRun = ps::run_calculix(fineOptions, fineSetup);
  const auto coarseSample = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::coarse, criterion, coarseOptions,
      coarseSetup, coarseRun);
  const auto fineSample = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, criterion, fineOptions,
      fineSetup, fineRun);
  const auto correspondence =
      ps::review_structural_boundary_correspondence(
          coarseSetup, fineSetup, true, true);
  const auto verified = ps::compile_structural_refinement(
      coarseSample, fineSample, correspondence);
  require(verified.complete() &&
              verified.value()->status() ==
                  ps::StructuralRefinementStatus::accepted &&
              verified.value()->coarse().run().validated_result->identity !=
                  verified.value()->fine().run().validated_result->identity,
          "two completed ordered samples produce one accepted typed comparison");

  const auto acceptedEvaluation =
      ps::compile_structural_findings(*verified.value());
  require(acceptedEvaluation.declared_obligations == 2 &&
              acceptedEvaluation.evaluated_obligations == 2 &&
              acceptedEvaluation.comparison.has_value() &&
              acceptedEvaluation.comparison->status ==
                  ps::StructuralRefinementStatus::accepted,
          "an accepted verified pair evaluates the fine obligations");
  require(std::ranges::all_of(
              acceptedEvaluation.findings, [](const auto &finding) {
                return finding.evidence_sha256.size() == 4U;
              }),
          "each finding binds both setup and both result identities");

  const auto strictCriterion =
      ps::compile_structural_refinement_criterion(0.01);
  const auto strictCoarse = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::coarse, strictCriterion, coarseOptions,
      coarseSetup, coarseRun);
  const auto strictFine = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, strictCriterion, fineOptions,
      fineSetup, fineRun);
  const auto unresolvedPair = ps::compile_structural_refinement(
      strictCoarse, strictFine, correspondence);
  const auto unresolvedEvaluation =
      ps::compile_structural_findings(*unresolvedPair.value());
  require(unresolvedPair.complete() &&
              unresolvedPair.value()->status() ==
                  ps::StructuralRefinementStatus::indeterminate &&
              unresolvedEvaluation.declared_obligations == 2 &&
              unresolvedEvaluation.evaluated_obligations == 0 &&
              unresolvedEvaluation.findings.empty(),
          "a valid pair above its criterion remains honestly indeterminate");

  auto equalityCoarseRun = coarseRun;
  auto equalityFineRun = fineRun;
  const double equalityLimit =
      *fineSetup.request.displacement_limit_m;
  const auto setMaximumDisplacement = [](ps::SolverRunResult &run,
                                         const double magnitude) {
    for (auto &row : run.validated_result->normalized.displacements) {
      row.x_m = 0.0;
      row.y_m = 0.0;
      row.z_m = 0.0;
      row.magnitude_m = 0.0;
    }
    auto &maximum = run.validated_result->normalized.displacements.front();
    maximum.z_m = -magnitude;
    maximum.magnitude_m = magnitude;
    run.validated_result->metrics = ps::summarize_calculix_dat(
        run.validated_result->normalized);
  };
  setMaximumDisplacement(equalityCoarseRun, equalityLimit * 0.99);
  setMaximumDisplacement(equalityFineRun, equalityLimit);
  equalityCoarseRun.validated_result->identity =
      "sha256:1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
  equalityFineRun.validated_result->identity =
      "sha256:abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
  const auto equalityCoarse = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::coarse, criterion, coarseOptions,
      coarseSetup, equalityCoarseRun);
  const auto equalityFine = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, criterion, fineOptions,
      fineSetup, equalityFineRun);
  const auto equalityPair = ps::compile_structural_refinement(
      equalityCoarse, equalityFine, correspondence);
  const auto equalityEvaluation =
      ps::compile_structural_findings(*equalityPair.value());
  require(equalityPair.complete() &&
              !equalityEvaluation.findings.empty() &&
              equalityEvaluation.findings.front().disposition ==
                  ps::StructuralFindingDisposition::violated &&
              equalityEvaluation.findings.front().margin_to_limit == 0.0,
          "a fine result equal to its reviewed limit is a violation");

  for (const double invalidCriterion :
       {0.0, -0.1, 1.01, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    requireThrows(
        [&] {
          (void)ps::compile_structural_refinement_criterion(
              invalidCriterion);
        },
        "refinement_criterion_invalid",
        "invalid refinement criteria fail before baseline execution");
  }

  const auto reusedFineSample = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, criterion, coarseOptions,
      coarseSetup, coarseRun);
  const auto reusedMesh = ps::compile_structural_refinement(
      coarseSample, reusedFineSample,
      ps::review_structural_boundary_correspondence(
          coarseSetup, coarseSetup, true, true));
  require(hasRefinementIssue(reusedMesh,
                             "refinement_mesh_identity_reused"),
          "one mesh cannot occupy both refinement roles");

  const auto reversedCoarse = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::coarse, criterion, fineOptions,
      fineSetup, fineRun);
  const auto reversedFine = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, criterion, coarseOptions,
      coarseSetup, coarseRun);
  const auto reversed = ps::compile_structural_refinement(
      reversedCoarse, reversedFine,
      ps::review_structural_boundary_correspondence(
          fineSetup, coarseSetup, true, true));
  require(hasRefinementIssue(reversed, "refinement_mesh_not_finer"),
          "the fine role requires more elements and a smaller target size");

  const auto requireLineageMismatch =
      [&](const ps::StructuralSetup &changedFine, const char *message) {
        const auto changedSetup =
            ps::compile_structural_setup(changedFine);
        const auto changedSample =
            ps::compile_completed_structural_sample(
                ps::StructuralSampleRole::fine, criterion, fineOptions,
                changedSetup, fineRun);
        const auto changedPair = ps::compile_structural_refinement(
            coarseSample, changedSample,
            ps::review_structural_boundary_correspondence(
                coarseSetup, changedSetup, true, true));
        require(hasRefinementIssue(changedPair,
                                   "refinement_lineage_mismatch"),
                message);
      };
  auto changedMaterial = fineReviewed;
  changedMaterial.material.youngs_modulus_pa *= 0.99;
  requireLineageMismatch(changedMaterial,
                         "changed material breaks refinement lineage");
  auto changedForce = fineReviewed;
  changedForce.load.total_force_n[2] -= 1.0;
  requireLineageMismatch(changedForce,
                         "changed force breaks refinement lineage");
  auto changedRequirement = fineReviewed;
  *changedRequirement.requirement.displacement_limit_m *= 0.99;
  requireLineageMismatch(changedRequirement,
                         "changed requirements break refinement lineage");
  auto changedScenario = fineReviewed;
  changedScenario.scenario_description += " changed";
  requireLineageMismatch(changedScenario,
                         "changed scenario breaks refinement lineage");

  auto changedBackendRun = fineRun;
  changedBackendRun.validated_result->backend.version =
      "CalculiX Version 9.99";
  const auto changedBackendSample =
      ps::compile_completed_structural_sample(
          ps::StructuralSampleRole::fine, criterion, fineOptions,
          fineSetup, changedBackendRun);
  const auto changedBackend = ps::compile_structural_refinement(
      coarseSample, changedBackendSample, correspondence);
  require(hasRefinementIssue(changedBackend,
                             "refinement_backend_mismatch"),
          "both samples must use one authoritative backend identity");

  const auto unreviewedBoundary = ps::compile_structural_refinement(
      coarseSample, fineSample,
      ps::review_structural_boundary_correspondence(
          coarseSetup, fineSetup, false, true));
  require(hasRefinementIssue(unreviewedBoundary,
                             "refinement_boundary_review_required"),
          "arbitrary mesh boundaries require explicit correspondence review");

  auto incompleteRun = fineRun;
  incompleteRun.status = ps::SolverRunStatus::result_invalid;
  incompleteRun.validated_result->issues.push_back(
      {"test_incomplete", "synthetic incomplete refinement result"});
  const auto incompleteSample =
      ps::compile_completed_structural_sample(
          ps::StructuralSampleRole::fine, criterion, fineOptions,
          fineSetup, incompleteRun);
  const auto incompletePair = ps::compile_structural_refinement(
      coarseSample, incompleteSample, correspondence);
  require(hasRefinementIssue(incompletePair,
                             "refinement_result_incomplete"),
          "an incomplete sample cannot become a verified comparison");

  const auto acceptedArchive = ps::write_structural_refinement_archive(
      *verified.value(), acceptedEvaluation);
  require(acceptedArchive.schema_version == "3.0.0" &&
              acceptedArchive.coarse_result_identity ==
                  coarseRun.validated_result->identity &&
              acceptedArchive.validated_result_identity ==
                  fineRun.validated_result->identity,
          "new writes bind both active validated results under v3");
  const auto replay =
      ps::verify_structural_archive(acceptedArchive.manifest_path);
  require(replay.valid && replay.schema_version == "3.0.0" &&
              replay.refinement &&
              replay.refinement->status() ==
                  ps::StructuralRefinementStatus::accepted &&
              replay.evaluation &&
              replay.evaluation->evaluated_obligations == 2,
          "v3 replay reconstructs both results and findings");

  const auto v4Root = processRoot / "typed-v4-refinement";
  fs::create_directory(v4Root);
  for (const auto *job : {"typed_cantilever_coarse",
                          "typed_cantilever_fine"})
    for (const auto *extension : {".inp", ".dat", ".frd", ".sta"})
      fs::copy_file(refinementRoot / (std::string(job) + extension),
                    v4Root / (std::string(job) + extension));
  auto v4CoarseOptions = coarseOptions;
  v4CoarseOptions.working_directory = v4Root;
  auto v4FineOptions = fineOptions;
  v4FineOptions.working_directory = v4Root;
  const auto typedGlobalCriterion =
      ps::compile_structural_refinement_criterion(
          ps::global_structural_observable_specs(0.10));
  const auto v4CoarseSample = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::coarse, typedGlobalCriterion,
      v4CoarseOptions, coarseSetup, coarseRun);
  const auto v4FineSample = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, typedGlobalCriterion,
      v4FineOptions, fineSetup, fineRun);
  const auto v4Pair = ps::compile_structural_refinement(
      v4CoarseSample, v4FineSample, correspondence);
  const auto v4Evaluation =
      ps::compile_structural_findings(*v4Pair.value());
  const auto v4Archive = ps::write_structural_refinement_archive(
      *v4Pair.value(), v4Evaluation);
  require(v4Archive.schema_version == "4.0.0",
          "typed refinement writes archive v4");
  const auto v4Replay =
      ps::verify_structural_archive(v4Archive.manifest_path);
  require(v4Replay.valid && v4Replay.schema_version == "4.0.0" &&
              v4Replay.refinement && v4Replay.evaluation &&
              v4Replay.evaluation->unknowns.empty() &&
              v4Replay.refinement->observable_comparisons().size() == 2U,
          "archive v4 replays scoped comparison, coverage, and findings");

  const auto regionalRoot = processRoot / "regional-v4-refinement";
  fs::create_directory(regionalRoot);
  for (const auto *job : {"typed_cantilever_coarse",
                          "typed_cantilever_fine"})
    for (const auto *extension : {".inp", ".dat", ".frd", ".sta"})
      fs::copy_file(refinementRoot / (std::string(job) + extension),
                    regionalRoot / (std::string(job) + extension));
  auto regionalCoarseOptions = coarseOptions;
  regionalCoarseOptions.working_directory = regionalRoot;
  auto regionalFineOptions = fineOptions;
  regionalFineOptions.working_directory = regionalRoot;
  auto regionalSpecs = ps::global_structural_observable_specs(0.10);
  regionalSpecs[1].id = "benchmark.maximum_von_mises_stress_window";
  regionalSpecs[1].region = {
      .kind = ps::StructuralObservableRegionKind::element_centroid_box_m,
      .element_centroid_box_m = {
          .minimum_m = {0.0, -1.0, -1.0},
          .maximum_m = {1.0, 1.0, 1.0}}};
  const auto regionalCriterion =
      ps::compile_structural_refinement_criterion(regionalSpecs);
  const auto regionalCoarse = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::coarse, regionalCriterion,
      regionalCoarseOptions, coarseSetup, coarseRun);
  const auto regionalFine = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, regionalCriterion,
      regionalFineOptions, fineSetup, fineRun);
  const auto regionalPair = ps::compile_structural_refinement(
      regionalCoarse, regionalFine, correspondence);
  require(regionalPair.complete() &&
              regionalPair.value()->status() ==
                  ps::StructuralRefinementStatus::accepted,
          "the bounded regional observable pair is accepted");
  const auto regionalEvaluation =
      ps::compile_structural_findings(*regionalPair.value());
  require(regionalEvaluation.findings.size() == 1U &&
              regionalEvaluation.unknowns.size() == 1U &&
              regionalEvaluation.unknowns.front().code ==
                  "matching_converged_scope_missing",
          "regional convergence leaves the unmatched global stress obligation unknown");
  const auto regionalArchive = ps::write_structural_refinement_archive(
      *regionalPair.value(), regionalEvaluation);
  const auto regionalReplay =
      ps::verify_structural_archive(regionalArchive.manifest_path);
  require(regionalReplay.valid && regionalReplay.evaluation &&
              regionalReplay.evaluation->findings.size() == 1U &&
              regionalReplay.evaluation->unknowns.size() == 1U,
          "archive v4 replays a partial finding with an explicit unknown");

  const auto indeterminateV4Root =
      processRoot / "indeterminate-v4-refinement";
  fs::create_directory(indeterminateV4Root);
  for (const auto *job : {"typed_cantilever_coarse",
                          "typed_cantilever_fine"})
    for (const auto *extension : {".inp", ".dat", ".frd", ".sta"})
      fs::copy_file(refinementRoot / (std::string(job) + extension),
                    indeterminateV4Root /
                        (std::string(job) + extension));
  auto indeterminateCoarseOptions = coarseOptions;
  indeterminateCoarseOptions.working_directory = indeterminateV4Root;
  auto indeterminateFineOptions = fineOptions;
  indeterminateFineOptions.working_directory = indeterminateV4Root;
  const auto indeterminateCriterion =
      ps::compile_structural_refinement_criterion(
          ps::global_structural_observable_specs(0.01));
  const auto indeterminateCoarse =
      ps::compile_completed_structural_sample(
          ps::StructuralSampleRole::coarse, indeterminateCriterion,
          indeterminateCoarseOptions, coarseSetup, coarseRun);
  const auto indeterminateFine = ps::compile_completed_structural_sample(
      ps::StructuralSampleRole::fine, indeterminateCriterion,
      indeterminateFineOptions, fineSetup, fineRun);
  const auto indeterminatePair = ps::compile_structural_refinement(
      indeterminateCoarse, indeterminateFine, correspondence);
  require(indeterminatePair.complete(),
          "typed strict refinement remains a valid compiled pair");
  const auto indeterminateEvaluation =
      ps::compile_structural_findings(*indeterminatePair.value());
  require(indeterminateEvaluation.findings.empty() &&
              indeterminateEvaluation.unknowns.size() == 2U,
          "typed indeterminate refinement retains one unknown per obligation");
  const auto indeterminateArchive =
      ps::write_structural_refinement_archive(
          *indeterminatePair.value(), indeterminateEvaluation);
  const auto indeterminateReplay =
      ps::verify_structural_archive(indeterminateArchive.manifest_path);
  require(indeterminateReplay.valid && indeterminateReplay.evaluation &&
              indeterminateReplay.schema_version == "4.0.0" &&
              indeterminateReplay.evaluation->findings.empty() &&
              indeterminateReplay.evaluation->unknowns.size() == 2U,
          "archive v4 replays indeterminate coverage without findings");

  const auto tamperV4 = [&](const ps::StructuralArchive &source,
                            const std::string &name, auto mutate) {
    const auto copied = ps::export_structural_archive(
        source.manifest_path, processRoot / name);
    auto document = nlohmann::json::parse(
        fixtureBytes(copied.manifest_path));
    mutate(document);
    const auto canonical =
        prometheus::integrity::canonicalize_json_bytes(document.dump());
    writeFixtureBytes(copied.manifest_path, canonical);
    return ps::verify_structural_archive(copied.manifest_path);
  };
  require(!tamperV4(
               regionalArchive, "tampered-v4-quantity", [](auto &document) {
                 document["criterion"]["observables"][1]["quantity"] =
                     "displacement_magnitude_m";
               })
               .valid,
          "a changed v4 observable quantity cannot replay");
  require(!tamperV4(
               regionalArchive, "tampered-v4-box-bound", [](auto &document) {
                 document["criterion"]["observables"][1]["region"]
                         ["maximum_m"][0] = 0.5;
               })
               .valid,
          "a changed v4 regional bound cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-threshold", [](auto &document) {
                 document["criterion"]["observables"][0]
                         ["maximum_change_fraction"] = 0.11;
               })
               .valid,
          "a changed v4 observable threshold cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-row-count", [](auto &document) {
                 auto &count = document["comparison"]["observables"][0]
                                       ["coarse_selected_rows"];
                 count = count.template get<std::size_t>() + 1U;
               })
               .valid,
          "a changed v4 selected-row count cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-coarse-value", [](auto &document) {
                 auto &value = document["comparison"]["observables"][0]
                                       ["coarse_value"];
                 value = value.template get<double>() * 1.01;
               })
               .valid,
          "a changed v4 coarse observable value cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-fine-value", [](auto &document) {
                 auto &value = document["comparison"]["observables"][0]
                                       ["fine_value"];
                 value = value.template get<double>() * 1.01;
               })
               .valid,
          "a changed v4 fine observable value cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-change", [](auto &document) {
                 auto &change = document["comparison"]["observables"][0]
                                        ["change_fraction"];
                 change = change.template get<double>() + 0.01;
               })
               .valid,
          "a changed v4 observable change cannot replay");
  require(!tamperV4(
               regionalArchive, "tampered-v4-participation",
               [](auto &document) {
                 document["comparison"]["global_extrema"][1]
                         ["participated_in_acceptance"] = true;
               })
               .valid,
          "a changed v4 global-peak participation flag cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-global-entity", [](auto &document) {
                 auto &entity = document["comparison"]["global_extrema"][1]
                                        ["fine_entity_id"];
                 entity = entity.template get<int>() + 1;
               })
               .valid,
          "a changed v4 global-peak entity cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-global-location", [](auto &document) {
                 auto &coordinate =
                     document["comparison"]["global_extrema"][1]
                             ["fine_position_m"][0];
                 coordinate = coordinate.template get<double>() + 0.001;
               })
               .valid,
          "a changed v4 global-peak location cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-global-value", [](auto &document) {
                 auto &value = document["comparison"]["global_extrema"][1]
                                       ["fine_value"];
                 value = value.template get<double>() + 1.0;
               })
               .valid,
          "a changed v4 global-peak value cannot replay");
  require(!tamperV4(
               regionalArchive, "tampered-v4-unknown-code",
               [](auto &document) {
                 document["unknowns"][0]["code"] = "forged_unknown";
               })
               .valid,
          "a changed v4 unknown reason cannot replay");
  require(!tamperV4(
               v4Archive, "tampered-v4-status", [](auto &document) {
                 document["comparison"]["status"] = "indeterminate";
               })
               .valid,
          "a changed v4 refinement status cannot replay");

  const auto relocated = ps::export_structural_archive(
      acceptedArchive.manifest_path, processRoot / "relocated-v3");
  require(ps::verify_structural_archive(relocated.manifest_path).valid &&
              relocated.manifest_sha256 ==
                  acceptedArchive.manifest_sha256,
          "v3 export copies all fourteen declared artifacts and replays exactly");

  const auto unresolvedRoot = processRoot / "unresolved-refinement-study";
  fs::create_directory(unresolvedRoot);
  for (const auto *job : {"typed_cantilever_coarse",
                          "typed_cantilever_fine"})
    for (const auto *extension : {".inp", ".dat", ".frd", ".sta"})
      fs::copy_file(refinementRoot / (std::string(job) + extension),
                    unresolvedRoot / (std::string(job) + extension));
  auto unresolvedCoarseOptions = coarseOptions;
  unresolvedCoarseOptions.working_directory = unresolvedRoot;
  auto unresolvedFineOptions = fineOptions;
  unresolvedFineOptions.working_directory = unresolvedRoot;
  const auto archivedStrictCoarse =
      ps::compile_completed_structural_sample(
          ps::StructuralSampleRole::coarse, strictCriterion,
          unresolvedCoarseOptions, coarseSetup, coarseRun);
  const auto archivedStrictFine =
      ps::compile_completed_structural_sample(
          ps::StructuralSampleRole::fine, strictCriterion,
          unresolvedFineOptions, fineSetup, fineRun);
  const auto archivedUnresolvedPair = ps::compile_structural_refinement(
      archivedStrictCoarse, archivedStrictFine, correspondence);
  const auto archivedUnresolvedEvaluation =
      ps::compile_structural_findings(*archivedUnresolvedPair.value());
  const auto unresolvedArchive = ps::write_structural_refinement_archive(
      *archivedUnresolvedPair.value(), archivedUnresolvedEvaluation);
  const auto unresolvedReplay =
      ps::verify_structural_archive(unresolvedArchive.manifest_path);
  require(unresolvedReplay.valid && unresolvedReplay.evaluation &&
              unresolvedReplay.evaluation->evaluated_obligations == 0 &&
              unresolvedReplay.evaluation->findings.empty(),
          "an above-threshold study remains replayable and indeterminate");

  const auto tamperManifest =
      [&](const std::string &name, const std::string_view marker,
          const std::string_view key,
          const std::string_view encodedValue) {
        const auto copied = ps::export_structural_archive(
            acceptedArchive.manifest_path, processRoot / name);
        auto bytes = fixtureBytes(copied.manifest_path);
        bytes = replaceJsonMemberAfter(
            std::move(bytes), marker, key, encodedValue);
        writeFixtureBytes(copied.manifest_path, bytes);
        return ps::verify_structural_archive(copied.manifest_path);
      };
  const auto forgedMaximum = jsonNumber(
      verified.value()->maximum_change_fraction() + 0.01);
  require(!tamperManifest(
               "tampered-v3-maximum", "\"comparison\":",
               "maximum_change_fraction", forgedMaximum)
               .valid,
          "a caller-authored maximum refinement change cannot replay");
  require(!tamperManifest(
               "tampered-v3-status", "\"comparison\":",
               "status", "\"indeterminate\"")
               .valid,
          "a caller-authored refinement status cannot replay");
  require(!tamperManifest(
               "tampered-v3-coarse-result", "\"samples\":{\"coarse\":",
               "validated_result_identity",
               "\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"")
               .valid,
          "a detached coarse result identity cannot replay");
  require(!tamperManifest(
               "tampered-v3-fine-setup", "\"fine\":",
               "compiled_setup_identity",
               "\"sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"")
               .valid,
          "a detached fine setup identity cannot replay");

  const auto tamperedDat = ps::export_structural_archive(
      acceptedArchive.manifest_path, processRoot / "tampered-v3-coarse-dat");
  const auto coarseDatPath = tamperedDat.manifest_path.parent_path() /
      "typed_cantilever_coarse.dat";
  writeFixtureBytes(coarseDatPath, fixtureBytes(coarseDatPath) + "tampered\n");
  require(!ps::verify_structural_archive(tamperedDat.manifest_path).valid,
          "changed coarse DAT bytes cannot replay");
  const auto tamperedSta = ps::export_structural_archive(
      acceptedArchive.manifest_path, processRoot / "tampered-v3-fine-sta");
  const auto fineStaPath = tamperedSta.manifest_path.parent_path() /
      "typed_cantilever_fine.sta";
  writeFixtureBytes(fineStaPath, fixtureBytes(fineStaPath) + "tampered\n");
  require(!ps::verify_structural_archive(tamperedSta.manifest_path).valid,
          "changed fine STA bytes cannot replay");

  const auto staleOutputs = runFixture("success", std::chrono::seconds(5));
  require(staleOutputs.status == ps::SolverRunStatus::output_conflict &&
              !staleOutputs.validated_result,
          "pre-existing raw solver outputs cannot be reused as a new run");
  const auto legacyManifest = createLegacyV1Archive(
      processRoot / "legacy-v1", request, completeEvidence,
      *compiledResult.metrics);
  const auto legacyVerification =
      ps::verify_structural_archive(legacyManifest);
  require(legacyVerification.valid &&
              legacyVerification.schema_version == "1.0.0",
          "legacy v1 archive remains readable under its original claim");

  constexpr std::string_view legacySecondResultIdentity =
      "sha256:1111111111111111111111111111111111111111111111111111111111111111";
  const auto legacyV2Manifest = createLegacyV2Archive(
      processRoot / "legacy-v2", processRoot, "success", compiled,
      completed, legacySecondResultIdentity);
  const auto storedLegacyV2Identity =
      nlohmann::json::parse(fixtureBytes(legacyV2Manifest))
          .at("validated_result_identity")
          .get<std::string>();
  require(storedLegacyV2Identity != completed.validated_result->identity,
          "legacy v2 fixture retains the historical metric-bearing identity");
  const auto archiveVerification =
      ps::verify_structural_archive(legacyV2Manifest);
  require(archiveVerification.valid &&
              archiveVerification.schema_version == "2.0.0" &&
              archiveVerification.validated_result_identity ==
                  storedLegacyV2Identity &&
              archiveVerification.normalized.has_value() &&
              archiveVerification.reviewed_setup.has_value() &&
              archiveVerification.compiled_setup.has_value() &&
              archiveVerification.evaluation.has_value() &&
              archiveVerification.evaluation->evaluated_obligations == 2 &&
              !archiveVerification.refinement,
          "generated canonical v2 fixture remains readable but cannot become a typed pair");
  const auto detachedLegacyV2Directory = processRoot / "legacy-v2-detached";
  fs::copy(legacyV2Manifest.parent_path(), detachedLegacyV2Directory,
           fs::copy_options::recursive);
  const auto detachedLegacyV2Manifest =
      detachedLegacyV2Directory / legacyV2Manifest.filename();
  auto detachedLegacyV2Bytes = fixtureBytes(detachedLegacyV2Manifest);
  detachedLegacyV2Bytes = replaceOnce(
      std::move(detachedLegacyV2Bytes),
      "\"result_sha256\":[" + jsonString(legacySecondResultIdentity) + ',' +
          jsonString(storedLegacyV2Identity) + ']',
      "\"result_sha256\":[" + jsonString(legacySecondResultIdentity) + ',' +
          jsonString(
              "sha256:2222222222222222222222222222222222222222222222222222222222222222") +
          ']');
  writeFixtureBytes(detachedLegacyV2Manifest, detachedLegacyV2Bytes);
  require(!ps::verify_structural_archive(detachedLegacyV2Manifest).valid,
          "legacy v2 compatibility rejects refinement detached from its active result");
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
  fs::remove_all(processRoot);
  return 0;
}
