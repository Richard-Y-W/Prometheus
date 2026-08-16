#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/structural_case.hpp"
#include "prometheus/structural/structural_finding.hpp"
#include "prometheus/structural/surface_setup.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace integrity = prometheus::integrity;
namespace ps = prometheus::structural;
using Json = nlohmann::json;

namespace {

constexpr std::uintmax_t maximumEvidenceBytes = 512U * 1024U * 1024U;
constexpr std::string_view executionSchema =
    "urn:prometheus:structural-execution-package:0.1.0";
constexpr std::string_view refinementSchema =
    "urn:prometheus:structural-refinement-evidence:0.1.0";
constexpr std::string_view resultSchema =
    "urn:prometheus:structural-result:0.1.0";
constexpr std::string_view manifestName =
    "prometheus-structural-execution.json";
constexpr std::string_view caseName = "reviewed-structural-case.json";
constexpr std::string_view meshName = "source-mesh.inp";
constexpr std::string_view jobName = "prometheus_structural_case";
constexpr std::string_view genericResultProfile = "structural_findings_v1";
constexpr std::string_view tensionBarResultProfile =
    "analytic_tension_bar_v1";

struct BenchmarkMetrics final {
  double average_loaded_face_axial_displacement_m{};
  double volume_weighted_central_axial_stress_pa{};
  std::size_t loaded_face_node_count{};
  std::size_t central_band_element_count{};
};

std::string read_file(const fs::path &path) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > maximumEvidenceBytes)
    throw std::runtime_error("cannot read bounded evidence file: " +
                             path.string());
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot open evidence file: " + path.string());
  std::string bytes(static_cast<std::size_t>(size), '\0');
  if (size != 0U)
    stream.read(bytes.data(), static_cast<std::streamsize>(size));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(size))
    throw std::runtime_error("evidence changed or could not be read exactly: " +
                             path.string());
  return bytes;
}

void write_file(const fs::path &path, const std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream)
    throw std::runtime_error("cannot open result manifest: " + path.string());
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream)
    throw std::runtime_error("cannot write result manifest: " + path.string());
}

Json parse_json(const std::string &bytes, const bool requireCanonical) {
  const auto canonical = integrity::canonicalize_json_bytes(bytes);
  if (requireCanonical && canonical != bytes)
    throw std::runtime_error("stored JSON manifest is not canonical");
  return Json::parse(canonical);
}

void require_keys(const Json &value,
                  const std::initializer_list<std::string_view> expected,
                  const std::string_view field) {
  if (!value.is_object())
    throw std::runtime_error(std::string(field) + " must be an object");
  std::set<std::string> expectedKeys;
  for (const auto key : expected)
    expectedKeys.emplace(key);
  std::set<std::string> actualKeys;
  for (const auto &[key, unused] : value.items()) {
    (void)unused;
    actualKeys.insert(key);
  }
  if (actualKeys != expectedKeys)
    throw std::runtime_error(std::string(field) +
                             " has missing or unknown members");
}

const Json &required_object(const Json &value, const std::string &key,
                            const std::string_view field) {
  const auto found = value.find(key);
  if (found == value.end() || !found->is_object())
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be an object");
  return *found;
}

std::string required_string(const Json &value, const std::string &key,
                            const std::string_view field) {
  const auto found = value.find(key);
  if (found == value.end() || !found->is_string())
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be a string");
  const auto text = found->get<std::string>();
  if (text.empty() || text.size() > 4096U)
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be bounded nonempty text");
  return text;
}

double required_number(const Json &value, const std::string &key,
                       const std::string_view field) {
  const auto found = value.find(key);
  if (found == value.end() || !found->is_number())
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be a number");
  const auto number = found->get<double>();
  if (!std::isfinite(number))
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be finite");
  return number;
}

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool near(const double first, const double second) {
  return std::abs(first - second) <=
         std::max({1.0, std::abs(first), std::abs(second)}) * 1.0e-12;
}

void verify_case_mesh(const ps::StructuralRequest &request,
                      const ps::VolumeMesh &mesh) {
  auto requestNodes = request.nodes;
  auto meshNodes = mesh.nodes;
  std::ranges::sort(requestNodes, {}, &ps::Node::id);
  std::ranges::sort(meshNodes, {}, &ps::Node::id);
  if (requestNodes.size() != meshNodes.size())
    throw std::runtime_error("case node coverage differs from its mesh");
  for (std::size_t index = 0; index < requestNodes.size(); ++index)
    if (requestNodes[index].id != meshNodes[index].id ||
        requestNodes[index].position_m != meshNodes[index].position_m)
      throw std::runtime_error("case nodes differ from their mesh");

  auto requestElements = request.elements;
  auto meshElements = mesh.elements;
  std::ranges::sort(requestElements, {}, &ps::Tetrahedron::id);
  std::ranges::sort(meshElements, {}, &ps::Tetrahedron::id);
  if (requestElements.size() != meshElements.size())
    throw std::runtime_error("case element coverage differs from its mesh");
  for (std::size_t index = 0; index < requestElements.size(); ++index)
    if (requestElements[index].id != meshElements[index].id ||
        requestElements[index].node_ids != meshElements[index].node_ids)
      throw std::runtime_error("case elements differ from their mesh");

  const auto setup = ps::compile_surface_setup(
      mesh, request.restraint_surface_groups, request.load_surface_groups,
      request.reviewed_force_magnitude_n, request.reviewed_force_direction);
  auto requestFixed = request.fully_fixed_node_ids;
  auto compiledFixed = setup.fully_fixed_node_ids;
  std::ranges::sort(requestFixed);
  std::ranges::sort(compiledFixed);
  auto requestForces = request.nodal_forces;
  auto compiledForces = setup.nodal_forces;
  std::ranges::sort(requestForces, {}, &ps::NodalForce::node_id);
  std::ranges::sort(compiledForces, {}, &ps::NodalForce::node_id);
  if (requestFixed != compiledFixed ||
      requestForces.size() != compiledForces.size() ||
      !near(request.selected_load_area_m2, setup.selected_load_area_m2) ||
      !near(request.observed_minimum_mean_ratio,
            mesh.diagnostics.minimum_mean_ratio))
    throw std::runtime_error(
        "case selections do not reproduce from the exact mesh");
  for (std::size_t index = 0; index < requestForces.size(); ++index) {
    if (requestForces[index].node_id != compiledForces[index].node_id)
      throw std::runtime_error("case force identities differ from the mesh");
    for (std::size_t axis = 0; axis < 3U; ++axis)
      if (!near(requestForces[index].force_n[axis],
                compiledForces[index].force_n[axis]))
        throw std::runtime_error("case forces differ from the mesh");
  }
}

std::size_t required_size(const Json &value, const std::string &key,
                          const std::string_view field) {
  const auto found = value.find(key);
  if (found == value.end() || !found->is_number_unsigned())
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be an unsigned integer");
  return found->get<std::size_t>();
}

int parse_integer(const std::string_view text) {
  int value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::runtime_error("process exit code is not a base-10 integer");
  return value;
}

double parse_nonnegative(const std::string_view text) {
  double value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value,
                      std::chars_format::general);
  if (error != std::errc{} || end != text.data() + text.size() ||
      !std::isfinite(value) || value < 0.0)
    throw std::runtime_error("elapsed milliseconds must be nonnegative");
  return value;
}

ps::StructuralRefinementEvidence parse_refinement(const std::string &bytes) {
  const auto root = parse_json(bytes, false);
  require_keys(root,
               {"$schema", "complete", "criteria_satisfied", "evidence_sha256",
                "maximum_allowed_change_fraction",
                "medium_to_fine_displacement_change_fraction"},
               "refinement evidence");
  if (required_string(root, "$schema", "refinement evidence") !=
      refinementSchema)
    throw std::runtime_error("unsupported refinement evidence schema");
  if (!root.at("complete").is_boolean() ||
      !root.at("criteria_satisfied").is_boolean() ||
      !root.at("evidence_sha256").is_array() ||
      root.at("evidence_sha256").size() > 64U)
    throw std::runtime_error("refinement evidence types are invalid");
  const auto complete = root.at("complete").get<bool>();
  const auto criteriaSatisfied = root.at("criteria_satisfied").get<bool>();
  if ((!complete && criteriaSatisfied) ||
      (complete && root.at("evidence_sha256").size() < 2U))
    throw std::runtime_error(
        "refinement completion is inconsistent with its evidence");
  ps::StructuralRefinementEvidence result{
      .complete = complete,
      .criteria_satisfied = criteriaSatisfied,
      .medium_to_fine_displacement_change_fraction =
          required_number(root, "medium_to_fine_displacement_change_fraction",
                          "refinement evidence"),
      .maximum_allowed_change_fraction = required_number(
          root, "maximum_allowed_change_fraction", "refinement evidence"),
  };
  std::set<std::string> evidenceIdentities;
  for (const auto &value : root.at("evidence_sha256")) {
    if (!value.is_string())
      throw std::runtime_error("refinement evidence hashes must be strings");
    const auto hash = value.get<std::string>();
    if (!strict_sha256(hash))
      throw std::runtime_error("refinement evidence hash is malformed");
    if (!evidenceIdentities.insert(hash).second)
      throw std::runtime_error(
          "refinement evidence identities must be distinct");
    result.evidence_sha256.push_back(hash);
  }
  result.evidence_sha256.push_back(integrity::sha256_bytes(bytes));
  return result;
}

double determinant(const std::array<double, 3> &a,
                   const std::array<double, 3> &b,
                   const std::array<double, 3> &c) {
  return a[0] * (b[1] * c[2] - b[2] * c[1]) -
         a[1] * (b[0] * c[2] - b[2] * c[0]) +
         a[2] * (b[0] * c[1] - b[1] * c[0]);
}

BenchmarkMetrics benchmark_metrics(const ps::StructuralRequest &request,
                                   const ps::VolumeMesh &mesh,
                                   const ps::CalculixDat &normalized) {
  std::set<int> loadedNodes;
  for (const auto &name : request.load_surface_groups) {
    const auto group =
        std::ranges::find(mesh.surface_groups, name, &ps::SurfaceGroup::name);
    if (group == mesh.surface_groups.end())
      throw std::runtime_error("result scope references a missing load group");
    loadedNodes.insert(group->node_ids.begin(), group->node_ids.end());
  }
  if (loadedNodes.empty())
    throw std::runtime_error("result scope has no loaded-face nodes");

  std::map<int, ps::DisplacementRow> displacementByNode;
  for (const auto &row : normalized.displacements)
    displacementByNode.emplace(row.node_id, row);
  const auto &axis = request.reviewed_force_direction;
  double displacementSum = 0.0;
  for (const int nodeId : loadedNodes) {
    const auto row = displacementByNode.find(nodeId);
    if (row == displacementByNode.end())
      throw std::runtime_error("loaded-face displacement row is missing");
    for (std::size_t component = 0; component < 3U; ++component)
      displacementSum +=
          axis[component] * row->second.displacement_m[component];
  }

  std::map<int, ps::Node> nodeById;
  double minimumProjection = std::numeric_limits<double>::max();
  double maximumProjection = std::numeric_limits<double>::lowest();
  for (const auto &node : request.nodes) {
    nodeById.emplace(node.id, node);
    const auto projection = axis[0] * node.position_m[0] +
                            axis[1] * node.position_m[1] +
                            axis[2] * node.position_m[2];
    minimumProjection = std::min(minimumProjection, projection);
    maximumProjection = std::max(maximumProjection, projection);
  }
  const auto projectedLength = maximumProjection - minimumProjection;
  if (!std::isfinite(projectedLength) || projectedLength <= 0.0)
    throw std::runtime_error("result scope has no positive axial length");

  std::map<int, ps::StressRow> stressByElement;
  for (const auto &row : normalized.stresses)
    stressByElement.emplace(row.element_id, row);
  double weightedStress = 0.0;
  double centralVolume = 0.0;
  std::size_t centralCount = 0U;
  for (const auto &element : request.elements) {
    std::array<std::array<double, 3>, 4> positions{};
    std::array<double, 3> centroid{};
    for (std::size_t index = 0; index < 4U; ++index) {
      const auto node = nodeById.find(element.node_ids[index]);
      if (node == nodeById.end())
        throw std::runtime_error("result element references a missing node");
      positions[index] = node->second.position_m;
      for (std::size_t component = 0; component < 3U; ++component)
        centroid[component] += positions[index][component] / 4.0;
    }
    const auto centroidProjection =
        axis[0] * centroid[0] + axis[1] * centroid[1] + axis[2] * centroid[2];
    const auto fraction =
        (centroidProjection - minimumProjection) / projectedLength;
    if (fraction < 0.4 || fraction > 0.6)
      continue;
    const auto stress = stressByElement.find(element.id);
    if (stress == stressByElement.end())
      throw std::runtime_error("central-band stress row is missing");
    std::array<std::array<double, 3>, 3> differences{};
    for (std::size_t row = 0; row < 3U; ++row)
      for (std::size_t component = 0; component < 3U; ++component)
        differences[row][component] =
            positions[row + 1U][component] - positions[0][component];
    const auto volume =
        std::abs(determinant(differences[0], differences[1], differences[2])) /
        6.0;
    const auto &s = stress->second.stress_pa;
    const auto axialStress =
        axis[0] * axis[0] * s[0] + axis[1] * axis[1] * s[1] +
        axis[2] * axis[2] * s[2] + 2.0 * axis[0] * axis[1] * s[3] +
        2.0 * axis[0] * axis[2] * s[4] + 2.0 * axis[1] * axis[2] * s[5];
    weightedStress += volume * axialStress;
    centralVolume += volume;
    ++centralCount;
  }
  if (!std::isfinite(centralVolume) || centralVolume <= 0.0)
    throw std::runtime_error(
        "no positive-volume element lies in the central band");

  return {
      .average_loaded_face_axial_displacement_m =
          displacementSum / static_cast<double>(loadedNodes.size()),
      .volume_weighted_central_axial_stress_pa = weightedStress / centralVolume,
      .loaded_face_node_count = loadedNodes.size(),
      .central_band_element_count = centralCount,
  };
}

Json optional_number(const std::optional<double> value) {
  return value.has_value() ? Json(*value) : Json(nullptr);
}

Json finding_json(const ps::StructuralFinding &finding) {
  return {
      {"assumptions", finding.assumptions},
      {"basis", finding.basis},
      {"evidence_sha256", finding.evidence_sha256},
      {"explanation", finding.explanation},
      {"limit_value", optional_number(finding.limit_value)},
      {"metric", ps::structural_metric_name(finding.metric)},
      {"observed_value", optional_number(finding.observed_value)},
      {"requirement_id", finding.requirement_id},
      {"scope", finding.scope},
      {"status", ps::finding_status_name(finding.status)},
      {"unit", finding.unit},
  };
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 10) {
    std::cerr << "usage: prometheus_verify_structural_case EXECUTION_DIRECTORY "
                 "PROCESS_EXIT_CODE SOLVER_EXECUTABLE_SHA256 SOLVER_VERSION "
                 "STDOUT_FILE STDERR_FILE REFINEMENT_JSON ELAPSED_MILLISECONDS "
                 "RESULT_JSON\n";
    return 2;
  }

  try {
    const fs::path executionDirectory(argv[1]);
    const auto processExitCode = parse_integer(argv[2]);
    const auto elapsedMilliseconds = parse_nonnegative(argv[8]);
    const fs::path resultPath(argv[9]);
    const auto manifestBytes = read_file(executionDirectory / manifestName);
    const auto manifest = parse_json(manifestBytes, true);
    require_keys(manifest,
                 {"$schema", "analysis_id", "case", "component_name", "deck",
                  "expected_result_coverage", "mesh", "result_profile"},
                 "execution manifest");
    if (required_string(manifest, "$schema", "execution manifest") !=
        executionSchema)
      throw std::runtime_error("unsupported execution manifest schema");
    const auto resultProfile =
        required_string(manifest, "result_profile", "execution manifest");
    if (resultProfile != genericResultProfile &&
        resultProfile != tensionBarResultProfile)
      throw std::runtime_error("unsupported structural result profile");

    const auto &caseEntry =
        required_object(manifest, "case", "execution manifest");
    const auto &deckEntry =
        required_object(manifest, "deck", "execution manifest");
    const auto &meshEntry =
        required_object(manifest, "mesh", "execution manifest");
    const auto &coverage = required_object(manifest, "expected_result_coverage",
                                           "execution manifest");
    require_keys(caseEntry, {"path", "sha256"}, "execution manifest.case");
    require_keys(deckEntry, {"job_name", "path", "sha256"},
                 "execution manifest.deck");
    require_keys(meshEntry, {"coordinate_scale_to_m", "path", "sha256"},
                 "execution manifest.mesh");
    require_keys(coverage, {"displacement_rows", "stress_rows"},
                 "execution manifest.expected_result_coverage");
    if (required_string(caseEntry, "path", "execution manifest.case") !=
            caseName ||
        required_string(deckEntry, "job_name", "execution manifest.deck") !=
            jobName ||
        required_string(deckEntry, "path", "execution manifest.deck") !=
            std::string(jobName) + ".inp" ||
        required_string(meshEntry, "path", "execution manifest.mesh") !=
            meshName)
      throw std::runtime_error("execution package uses an unexpected path");

    const auto caseBytes = read_file(executionDirectory / caseName);
    const auto structuralCase = ps::parse_structural_case(caseBytes);
    if (structuralCase.object_hash !=
            required_string(caseEntry, "sha256", "execution manifest.case") ||
        structuralCase.request.analysis_id !=
            required_string(manifest, "analysis_id", "execution manifest") ||
        structuralCase.request.component_name !=
            required_string(manifest, "component_name", "execution manifest"))
      throw std::runtime_error("execution manifest case identity differs");

    const auto meshBytes = read_file(executionDirectory / meshName);
    if (integrity::sha256_bytes(meshBytes) !=
            required_string(meshEntry, "sha256", "execution manifest.mesh") ||
        structuralCase.request.mesh_sha256 !=
            integrity::sha256_bytes(meshBytes) ||
        structuralCase.request.mesh_coordinate_scale_to_m !=
            required_number(meshEntry, "coordinate_scale_to_m",
                            "execution manifest.mesh"))
      throw std::runtime_error("execution manifest mesh identity differs");
    const auto mesh = ps::parse_gmsh_abaqus_mesh(
        meshBytes, structuralCase.request.mesh_coordinate_scale_to_m);
    verify_case_mesh(structuralCase.request, mesh);

    const auto deckBytes =
        read_file(executionDirectory / (std::string(jobName) + ".inp"));
    if (integrity::sha256_bytes(deckBytes) !=
            required_string(deckEntry, "sha256", "execution manifest.deck") ||
        deckBytes != ps::generate_calculix_deck(structuralCase.request))
      throw std::runtime_error("execution deck differs from the reviewed case");
    if (required_size(coverage, "displacement_rows",
                      "execution manifest.expected_result_coverage") !=
            structuralCase.request.nodes.size() ||
        required_size(coverage, "stress_rows",
                      "execution manifest.expected_result_coverage") !=
            structuralCase.request.elements.size())
      throw std::runtime_error(
          "declared result coverage differs from the case");

    const auto standardOutput = read_file(argv[5]);
    const auto standardError = read_file(argv[6]);
    const auto statusBytes =
        read_file(executionDirectory / (std::string(jobName) + ".sta"));
    const auto dataBytes =
        read_file(executionDirectory / (std::string(jobName) + ".dat"));
    const auto frdBytes =
        read_file(executionDirectory / (std::string(jobName) + ".frd"));
    const ps::CalculixRunEvidence runEvidence{
        .process_exit_code = processExitCode,
        .solver_executable_sha256 = argv[3],
        .solver_version = argv[4],
        .deck_bytes = deckBytes,
        .standard_output = standardOutput,
        .standard_error = standardError,
        .status_bytes = statusBytes,
        .data_bytes = dataBytes,
    };
    const auto compiled =
        ps::compile_calculix_result(structuralCase.request, runEvidence);
    const auto refinementBytes = read_file(argv[7]);
    const auto refinement = parse_refinement(refinementBytes);
    const auto findings = ps::compile_structural_findings(
        structuralCase.request, compiled, refinement);

    std::vector<std::string> verificationIssues;
    std::optional<BenchmarkMetrics> benchmark;
    if (compiled.complete() && resultProfile == tensionBarResultProfile) {
      try {
        benchmark = benchmark_metrics(structuralCase.request, mesh,
                                      compiled.normalized);
      } catch (const std::exception &error) {
        verificationIssues.push_back(error.what());
      }
    }
    for (const auto &finding : findings)
      if (finding.limit_value.has_value() &&
          finding.status == ps::FindingStatus::indeterminate)
        verificationIssues.push_back(
            std::string(ps::structural_metric_name(finding.metric)) +
            " finding is indeterminate");

    Json issues = Json::array();
    for (const auto &issue : compiled.issues)
      issues.push_back({{"code", issue.code}, {"message", issue.message}});
    for (const auto &message : verificationIssues)
      issues.push_back(
          {{"code", "verification_blocked"}, {"message", message}});
    Json findingValues = Json::array();
    for (const auto &finding : findings)
      findingValues.push_back(finding_json(finding));

    Json metrics = nullptr;
    if (compiled.metrics.has_value())
      metrics = {
          {"displacement_rows", compiled.metrics->displacement_rows},
          {"maximum_displacement_m", compiled.metrics->maximum_displacement_m},
          {"maximum_von_mises_pa", compiled.metrics->maximum_von_mises_pa},
          {"stress_rows", compiled.metrics->stress_rows}};
    Json benchmarkValues = nullptr;
    if (benchmark.has_value())
      benchmarkValues = {
          {"average_loaded_face_axial_displacement_m",
           benchmark->average_loaded_face_axial_displacement_m},
          {"central_band_element_count", benchmark->central_band_element_count},
          {"central_band_x_over_length", {0.4, 0.6}},
          {"loaded_face_node_count", benchmark->loaded_face_node_count},
          {"volume_weighted_central_axial_stress_pa",
           benchmark->volume_weighted_central_axial_stress_pa},
      };
    Json convergence = nullptr;
    if (compiled.convergence.has_value())
      convergence = {
          {"attempt", compiled.convergence->attempt},
          {"increment", compiled.convergence->increment},
          {"increment_time", compiled.convergence->increment_time},
          {"iterations", compiled.convergence->iterations},
          {"step", compiled.convergence->step},
          {"step_time", compiled.convergence->step_time},
          {"total_time", compiled.convergence->total_time},
      };
    const bool complete = compiled.complete() && verificationIssues.empty();
    const Json result{
        {"$schema", resultSchema},
        {"backend",
         {{"executable_sha256", compiled.backend.executable_sha256},
          {"version", compiled.backend.version}}},
        {"benchmark_metrics", benchmarkValues},
        {"case_sha256", structuralCase.object_hash},
        {"complete", complete},
        {"convergence", convergence},
        {"elapsed_milliseconds", elapsedMilliseconds},
        {"execution_manifest_sha256", integrity::sha256_bytes(manifestBytes)},
        {"findings", findingValues},
        {"issues", issues},
        {"metrics", metrics},
        {"raw_artifacts",
         {{"data_sha256", compiled.artifacts.data_sha256},
          {"deck_sha256", compiled.artifacts.deck_sha256},
          {"frd_sha256", integrity::sha256_bytes(frdBytes)},
          {"refinement_sha256", integrity::sha256_bytes(refinementBytes)},
          {"standard_error_sha256", compiled.artifacts.standard_error_sha256},
          {"standard_output_sha256", compiled.artifacts.standard_output_sha256},
          {"status_sha256", compiled.artifacts.status_sha256}}},
        {"refinement",
         {{"complete", refinement.complete},
          {"criteria_satisfied", refinement.criteria_satisfied},
          {"evidence_sha256", refinement.evidence_sha256},
          {"maximum_allowed_change_fraction",
           refinement.maximum_allowed_change_fraction},
          {"medium_to_fine_displacement_change_fraction",
           refinement.medium_to_fine_displacement_change_fraction}}},
        {"status", complete ? "complete" : "indeterminate"},
    };
    const auto resultBytes = integrity::canonicalize_json_bytes(result.dump());
    write_file(resultPath, resultBytes);
    std::cout << resultPath.string() << '\n';
    return complete ? 0 : 9;
  } catch (const std::exception &error) {
    std::cerr << "structural_case_verification_blocked: " << error.what()
              << '\n';
    return 9;
  }
}
