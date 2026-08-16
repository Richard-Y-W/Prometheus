#include "prometheus/structural/structural_archive.hpp"

#include "prometheus/structural/gmsh_mesh.hpp"

#include "prometheus/integrity/canonical_json.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace prometheus::structural {
namespace {

using Json = nlohmann::json;
constexpr auto archiveName = "prometheus-structural-run.json";
constexpr auto archiveSchemaV1 =
    "urn:prometheus:schema:structural-run-archive:1.0.0";
constexpr auto archiveSchemaV2 =
    "urn:prometheus:schema:structural-run-archive:2.0.0";
constexpr auto setupSchemaV1 =
    "urn:prometheus:schema:reviewed-structural-setup:1.0.0";
constexpr auto setupSchemaV2 =
    "urn:prometheus:schema:reviewed-structural-setup:2.0.0";

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("archive artifact is missing");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path &path, const std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) throw std::runtime_error("archive artifact could not be written");
}

bool exact_keys(const Json &value,
                const std::initializer_list<std::string_view> keys) {
  if (!value.is_object() || value.size() != keys.size()) return false;
  return std::ranges::all_of(keys, [&](const auto key) {
    return value.contains(std::string(key));
  });
}

bool safe_file(const std::string &name) {
  return !name.empty() && name.size() <= 128 &&
         name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos && name != "." && name != "..";
}

StructuralArchiveVerification failure(std::string code, std::string detail) {
  return {false, std::move(code), std::move(detail), std::nullopt, 0, 0,
          {}, {}};
}

class ArchiveVerificationError final : public std::runtime_error {
public:
  ArchiveVerificationError(std::string code, std::string detail)
      : std::runtime_error(std::move(detail)), code_(std::move(code)) {}

  [[nodiscard]] const std::string &code() const noexcept { return code_; }

private:
  std::string code_;
};

[[noreturn]] void reject(std::string code, std::string detail) {
  throw ArchiveVerificationError(std::move(code), std::move(detail));
}

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool safe_job_name(const std::string_view value) {
  return !value.empty() && value.size() <= 128U &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return (character >= 'a' && character <= 'z') ||
                  (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') ||
                  character == '_' || character == '-';
         });
}

std::string bounded_read(const std::filesystem::path &path,
                         const std::uintmax_t maximumBytes) {
  std::error_code error;
  const auto length = std::filesystem::file_size(path, error);
  if (error || !std::filesystem::is_regular_file(path))
    reject("archive_artifact_missing", "archive artifact is missing");
  if (length > maximumBytes)
    reject("archive_artifact_too_large", "archive artifact exceeds its bound");
  return read(path);
}

std::string json_string(const Json &object, const std::string_view key) {
  const auto name = std::string(key);
  if (!object.contains(name) || !object.at(name).is_string())
    reject("archive_contract_invalid", name + " must be a string");
  return object.at(name).get<std::string>();
}

bool json_bool(const Json &object, const std::string_view key) {
  const auto name = std::string(key);
  if (!object.contains(name) || !object.at(name).is_boolean())
    reject("archive_contract_invalid", name + " must be a Boolean");
  return object.at(name).get<bool>();
}

double json_number(const Json &object, const std::string_view key) {
  const auto name = std::string(key);
  if (!object.contains(name) || !object.at(name).is_number())
    reject("archive_contract_invalid", name + " must be a number");
  const auto value = object.at(name).get<double>();
  if (!std::isfinite(value))
    reject("archive_contract_invalid", name + " must be finite");
  return value;
}

std::optional<double> json_optional_number(const Json &object,
                                           const std::string_view key) {
  const auto name = std::string(key);
  if (!object.contains(name))
    reject("archive_contract_invalid", name + " is missing");
  if (object.at(name).is_null()) return std::nullopt;
  return json_number(object, key);
}

Json artifact_json(const std::string &name,
                   const CalculixArtifactIdentity &identity) {
  return {{"file", name},
          {"byte_length", identity.byte_length},
          {"sha256", identity.sha256}};
}

Json metrics_json(const CalculixMetrics &metrics) {
  return {{"maximum_displacement_m", metrics.maximum_displacement_m},
          {"maximum_von_mises_pa", metrics.maximum_von_mises_pa},
          {"displacement_rows", metrics.displacement_rows},
          {"stress_rows", metrics.stress_rows}};
}

Json convergence_json(const CalculixConvergenceEvidence &convergence) {
  return {{"step", convergence.step},
          {"increment", convergence.increment},
          {"attempt", convergence.attempt},
          {"iterations", convergence.iterations},
          {"total_time", convergence.total_time},
          {"step_time", convergence.step_time},
          {"increment_time", convergence.increment_time}};
}

Json requirements_json(const StructuralRequest &request) {
  return {{"displacement_limit_m", request.displacement_limit_m},
          {"von_mises_limit_pa", request.von_mises_limit_pa},
          {"displacement_limit_basis", request.displacement_limit_basis},
          {"von_mises_limit_basis", request.von_mises_limit_basis}};
}

Json refinement_json(const StructuralRefinementEvidence &refinement) {
  return {{"complete", refinement.complete},
          {"criteria_satisfied", refinement.criteria_satisfied},
          {"coarse_to_fine_change_fraction",
           refinement.coarse_to_fine_change_fraction},
          {"maximum_allowed_change_fraction",
           refinement.maximum_allowed_change_fraction},
          {"result_sha256", refinement.result_sha256}};
}

Json findings_json(const StructuralEvaluation &evaluation) {
  Json findings = Json::array();
  for (const auto &finding : evaluation.findings)
    findings.push_back(
        {{"obligation", finding.obligation},
         {"disposition",
          finding.disposition == StructuralFindingDisposition::violated
              ? "violated"
              : "no_violation_detected_within_scope"},
         {"measured", finding.measured_value},
         {"limit", finding.limit_value},
         {"margin", finding.margin_to_limit},
         {"unit", finding.unit},
         {"scope", finding.scope},
         {"evidence_sha256", finding.evidence_sha256},
         {"assumptions", finding.assumptions}});
  return findings;
}

BoundarySelection selection_from_json(const Json &value) {
  if (!exact_keys(value, {"label", "face_node_ids", "node_ids", "area_m2"}) ||
      !value.at("face_node_ids").is_array() ||
      !value.at("node_ids").is_array())
    reject("setup_contract_invalid", "reviewed boundary selection is invalid");
  BoundarySelection selection;
  selection.label = json_string(value, "label");
  selection.area_m2 = json_number(value, "area_m2");
  for (const auto &face : value.at("face_node_ids")) {
    if (!face.is_array() || face.size() != 3U ||
        !std::ranges::all_of(face, [](const auto &id) {
          return id.is_number_integer();
        }))
      reject("setup_contract_invalid", "reviewed face identity is invalid");
    selection.face_node_ids.push_back(
        {face[0].get<int>(), face[1].get<int>(), face[2].get<int>()});
  }
  for (const auto &node : value.at("node_ids")) {
    if (!node.is_number_integer())
      reject("setup_contract_invalid", "reviewed node identity is invalid");
    selection.node_ids.push_back(node.get<int>());
  }
  return selection;
}

StructuralSetup deserialize_setup(const std::string &setupBytes,
                                  const std::string &deckBytes) {
  const auto canonical = integrity::verify_canonical_bytes(
      setupBytes,
      integrity::Limits{8U * 1024U * 1024U, 64U, 500000U, 10000U,
                        100000U, 4U * 1024U * 1024U});
  const auto root = Json::parse(canonical);
  if (!exact_keys(root, {"$schema", "schema_version", "analysis_id",
                         "component_name", "geometry_sha256", "mesh",
                         "material", "load", "restraint", "requirement",
                         "scenario", "selection_patch_angle_degrees"}) ||
      root.at("$schema") != setupSchemaV2 ||
      root.at("schema_version") != "2.0.0")
    reject("setup_contract_invalid", "reviewed setup v2 root is invalid");

  const auto &mesh = root.at("mesh");
  if (!exact_keys(mesh, {"source_sha256", "coordinate_scale_to_m",
                         "node_count", "element_count", "boundary_face_count",
                         "node_ids", "element_ids", "minimum_size_m",
                         "maximum_size_m", "target_size_m",
                         "minimum_mean_ratio_threshold",
                         "observed_minimum_mean_ratio", "mesher_identity",
                         "reviewed"}))
    reject("setup_contract_invalid", "reviewed mesh evidence is invalid");
  const auto &material = root.at("material");
  if (!exact_keys(material, {"designation", "temper", "product_form",
                             "source_sha256", "applicability",
                             "youngs_modulus_pa", "poisson_ratio",
                             "reviewed"}))
    reject("setup_contract_invalid", "reviewed material evidence is invalid");
  const auto &load = root.at("load");
  if (!exact_keys(load, {"selection", "total_force_n", "reviewed"}) ||
      !load.at("total_force_n").is_array() ||
      load.at("total_force_n").size() != 3U)
    reject("setup_contract_invalid", "reviewed load evidence is invalid");
  const auto &restraint = root.at("restraint");
  if (!exact_keys(restraint, {"selection", "reviewed"}))
    reject("setup_contract_invalid", "reviewed restraint evidence is invalid");
  const auto &requirement = root.at("requirement");
  if (!exact_keys(requirement,
                  {"displacement_limit_m", "von_mises_limit_pa",
                   "source_or_exploratory_rationale", "displacement_limit_basis",
                   "von_mises_limit_basis", "reviewed"}))
    reject("setup_contract_invalid", "reviewed requirement evidence is invalid");
  const auto &scenario = root.at("scenario");
  if (!exact_keys(scenario, {"description", "confirmed"}))
    reject("setup_contract_invalid", "reviewed scenario evidence is invalid");

  StructuralSetup setup;
  setup.analysis_id = json_string(root, "analysis_id");
  setup.component_name = json_string(root, "component_name");
  setup.geometry_sha256 = json_string(root, "geometry_sha256");
  setup.mesh = parse_gmsh_abaqus_mesh(deckBytes, 1.0);
  setup.boundary_faces = extract_boundary_faces(setup.mesh);
  setup.material = {
      .designation = json_string(material, "designation"),
      .source_sha256 = json_string(material, "source_sha256"),
      .applicability = json_string(material, "applicability"),
      .youngs_modulus_pa = json_number(material, "youngs_modulus_pa"),
      .poisson_ratio = json_number(material, "poisson_ratio"),
      .reviewed = json_bool(material, "reviewed"),
      .temper = json_string(material, "temper"),
      .product_form = json_string(material, "product_form")};
  std::array<double, 3> force{};
  for (std::size_t index = 0; index < force.size(); ++index) {
    if (!load.at("total_force_n")[index].is_number())
      reject("setup_contract_invalid", "reviewed force vector is invalid");
    force[index] = load.at("total_force_n")[index].get<double>();
    if (!std::isfinite(force[index]))
      reject("setup_contract_invalid", "reviewed force vector is non-finite");
  }
  setup.load = {.selection = selection_from_json(load.at("selection")),
                .total_force_n = force,
                .reviewed = json_bool(load, "reviewed")};
  setup.restraint = {
      .selection = selection_from_json(restraint.at("selection")),
      .reviewed = json_bool(restraint, "reviewed")};
  setup.requirement = {
      .displacement_limit_m =
          json_optional_number(requirement, "displacement_limit_m"),
      .von_mises_limit_pa =
          json_optional_number(requirement, "von_mises_limit_pa"),
      .source_or_exploratory_rationale =
          json_string(requirement, "source_or_exploratory_rationale"),
      .reviewed = json_bool(requirement, "reviewed"),
      .displacement_limit_basis =
          json_string(requirement, "displacement_limit_basis"),
      .von_mises_limit_basis =
          json_string(requirement, "von_mises_limit_basis")};
  setup.mesh_controls = {
      .minimum_size_m = json_number(mesh, "minimum_size_m"),
      .maximum_size_m = json_number(mesh, "maximum_size_m"),
      .mesher_identity = json_string(mesh, "mesher_identity"),
      .reviewed = json_bool(mesh, "reviewed"),
      .mesh_sha256 = json_string(mesh, "source_sha256"),
      .coordinate_scale_to_m = json_number(mesh, "coordinate_scale_to_m"),
      .target_size_m = json_number(mesh, "target_size_m"),
      .minimum_mean_ratio_threshold =
          json_number(mesh, "minimum_mean_ratio_threshold"),
      .observed_minimum_mean_ratio =
          json_number(mesh, "observed_minimum_mean_ratio")};
  setup.scenario_description = json_string(scenario, "description");
  setup.scenario_confirmed = json_bool(scenario, "confirmed");
  setup.selection_patch_angle_degrees =
      json_number(root, "selection_patch_angle_degrees");
  return setup;
}

StructuralRefinementEvidence refinement_from_json(const Json &value) {
  if (!exact_keys(value, {"complete", "criteria_satisfied",
                          "coarse_to_fine_change_fraction",
                          "maximum_allowed_change_fraction",
                          "result_sha256"}) ||
      !value.at("result_sha256").is_array())
    reject("archive_contract_invalid", "refinement evidence is invalid");
  StructuralRefinementEvidence result{
      .complete = json_bool(value, "complete"),
      .criteria_satisfied = json_bool(value, "criteria_satisfied"),
      .coarse_to_fine_change_fraction =
          json_number(value, "coarse_to_fine_change_fraction"),
      .maximum_allowed_change_fraction =
          json_number(value, "maximum_allowed_change_fraction")};
  for (const auto &identity : value.at("result_sha256")) {
    if (!identity.is_string() ||
        !strict_sha256(identity.get_ref<const std::string &>()))
      reject("archive_contract_invalid", "refinement result identity is invalid");
    result.result_sha256.push_back(identity.get<std::string>());
  }
  return result;
}

struct PersistedArtifacts final {
  std::string setup;
  std::string deck;
  std::string dat;
  std::string sta;
  std::string standard_output;
  std::string standard_error;
  CalculixArtifactIdentity frd;
};

PersistedArtifacts verify_and_load_artifacts(
    const std::filesystem::path &directory, const Json &artifacts) {
  if (!exact_keys(artifacts,
                  {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"}))
    reject("archive_contract_invalid", "archive artifact set is invalid");
  std::set<std::string> names;
  std::map<std::string, std::string> loaded;
  CalculixArtifactIdentity frd;
  const std::map<std::string, std::uintmax_t> bounds{
      {"setup", 8U * 1024U * 1024U},
      {"deck", 512U * 1024U * 1024U},
      {"dat", 512U * 1024U * 1024U},
      {"sta", 64U * 1024U * 1024U},
      {"stdout", 16U * 1024U * 1024U},
      {"stderr", 16U * 1024U * 1024U}};
  for (const auto key : {"setup", "deck", "dat", "frd", "sta", "stdout",
                         "stderr"}) {
    const auto &reference = artifacts.at(key);
    if (!exact_keys(reference, {"file", "byte_length", "sha256"}) ||
        !reference.at("byte_length").is_number_unsigned())
      reject("archive_contract_invalid", "artifact reference is invalid");
    const auto name = json_string(reference, "file");
    const auto hash = json_string(reference, "sha256");
    if (!safe_file(name) || !names.insert(name).second || !strict_sha256(hash))
      reject("archive_contract_invalid", "artifact identity is unsafe");
    const auto length = reference.at("byte_length").get<std::uintmax_t>();
    const auto path = directory / name;
    std::error_code error;
    const auto actualLength = std::filesystem::file_size(path, error);
    if (error || !std::filesystem::is_regular_file(path) ||
        actualLength != length)
      reject("artifact_identity_mismatch", name + " bytes changed");
    if (std::string_view(key) == "frd") {
      if (length == 0U)
        reject("archive_contract_invalid", "FRD evidence must be nonempty");
      if (integrity::sha256_file(path) != hash)
        reject("artifact_identity_mismatch", name + " bytes changed");
      frd = {.sha256 = hash, .byte_length = length};
    } else {
      auto bytes = bounded_read(path, bounds.at(key));
      if (integrity::sha256_bytes(bytes) != hash)
        reject("artifact_identity_mismatch", name + " bytes changed");
      loaded.emplace(key, std::move(bytes));
    }
  }
  return {.setup = std::move(loaded.at("setup")),
          .deck = std::move(loaded.at("deck")),
          .dat = std::move(loaded.at("dat")),
          .sta = std::move(loaded.at("sta")),
          .standard_output = std::move(loaded.at("stdout")),
          .standard_error = std::move(loaded.at("stderr")),
          .frd = std::move(frd)};
}

std::optional<std::string> legacy_v1_binding_issue(
    const StructuralRequest &request, const CalculixDat &result) {
  std::set<int> expectedNodes;
  for (const auto &node : request.nodes)
    expectedNodes.insert(node.id);
  std::set<int> actualNodes;
  for (const auto &row : result.displacements)
    if (!actualNodes.insert(row.node_id).second)
      return "duplicate_displacement_node";
  if (actualNodes != expectedNodes)
    return "displacement_mesh_mismatch";

  std::set<int> expectedElements;
  for (const auto &element : request.elements)
    expectedElements.insert(element.id);
  std::set<int> actualElements;
  std::set<std::pair<int, int>> integrationPoints;
  for (const auto &row : result.stresses) {
    actualElements.insert(row.element_id);
    if (!integrationPoints.emplace(row.element_id, row.integration_point).second)
      return "duplicate_stress_integration_point";
  }
  if (actualElements != expectedElements)
    return "stress_mesh_mismatch";
  return std::nullopt;
}

StructuralEvaluation replay_legacy_v1_findings(
    const StructuralRequest &request, const CalculixMetrics &metrics) {
  StructuralEvaluation result;
  result.execution_status = SolverRunStatus::completed;
  result.declared_obligations =
      static_cast<int>(request.displacement_limit_m.has_value()) +
      static_cast<int>(request.von_mises_limit_pa.has_value());
  result.limitation =
      "These findings do not establish safety, fatigue life, buckling, contact, "
      "fastener adequacy, nonlinear behavior, or project-wide correctness.";
  const auto append = [&](std::string obligation, const double measured,
                          const double limit, std::string unit) {
    result.findings.push_back(
        {.obligation = std::move(obligation),
         .disposition =
             measured <= limit
                 ? StructuralFindingDisposition::no_violation_detected_within_scope
                 : StructuralFindingDisposition::violated,
         .measured_value = measured,
         .limit_value = limit,
         .margin_to_limit = limit - measured,
         .unit = std::move(unit),
         .scope =
             "isotropic linear-elastic C3D4 model under the confirmed scenario"});
  };
  if (request.displacement_limit_m)
    append("maximum_displacement", metrics.maximum_displacement_m,
           *request.displacement_limit_m, "m");
  if (request.von_mises_limit_pa)
    append("maximum_von_mises_stress", metrics.maximum_von_mises_pa,
           *request.von_mises_limit_pa, "Pa");
  result.evaluated_obligations = static_cast<int>(result.findings.size());
  return result;
}

} // namespace

StructuralArchive write_structural_archive(
    const std::filesystem::path &workingDirectory, std::string jobName,
    const CompiledStructuralSetup &setup,
    const SolverRunResult &run, const StructuralEvaluation &evaluation) {
  if (run.status != SolverRunStatus::completed || !run.validated_result ||
      !run.validated_result->complete() || !run.validated_result->metrics ||
      !run.validated_result->convergence)
    throw std::invalid_argument("only a completed structural run can be archived");
  if (!std::filesystem::is_directory(workingDirectory) ||
      !safe_job_name(jobName))
    throw std::invalid_argument("archive directory and safe job name are required");
  const auto &validated = *run.validated_result;
  const auto &request = setup.request;
  if (!strict_sha256(setup.identity) ||
      validated.compiled_setup_identity != setup.identity ||
      validated.artifacts.deck.sha256 !=
          integrity::sha256_bytes(setup.calculix_deck) ||
      validated.artifacts.deck.byte_length != setup.calculix_deck.size())
    throw std::invalid_argument("validated result is not bound to this compiled setup");
  const auto verifiedSetup = integrity::verify_canonical_bytes(
      setup.canonical_setup_evidence,
      integrity::Limits{8U * 1024U * 1024U, 64U, 500000U, 10000U, 100000U,
                        4U * 1024U * 1024U});
  const auto setupJson = Json::parse(verifiedSetup);
  if (!setupJson.is_object() ||
      setupJson.value("$schema", "") != setupSchemaV2 ||
      setupJson.value("schema_version", "") != "2.0.0" ||
      setupJson.value("analysis_id", "") != request.analysis_id ||
      setupJson.value("component_name", "") != request.component_name ||
      setupJson.value("geometry_sha256", "") != request.geometry_sha256)
    throw std::invalid_argument("reviewed setup evidence binding is invalid");
  const auto declaredObligations =
      static_cast<int>(request.displacement_limit_m.has_value()) +
      static_cast<int>(request.von_mises_limit_pa.has_value());
  if (evaluation.execution_status != SolverRunStatus::completed ||
      !evaluation.refinement ||
      evaluation.declared_obligations != declaredObligations ||
      evaluation.evaluated_obligations != declaredObligations ||
      evaluation.findings.size() !=
          static_cast<std::size_t>(declaredObligations) ||
      evaluation.limitation.empty())
    throw std::invalid_argument(
        "completed archive requires accepted refinement and complete findings");
  const auto deckName = jobName + ".inp";
  const auto datName = jobName + ".dat";
  const auto frdName = jobName + ".frd";
  const auto staName = jobName + ".sta";
  const std::string stdoutName = "solver.stdout.txt";
  const std::string stderrName = "solver.stderr.txt";
  const std::string setupName = "reviewed-structural-setup.json";
  const auto manifestPath = workingDirectory / archiveName;
  if (std::filesystem::exists(manifestPath) ||
      std::filesystem::exists(workingDirectory / setupName) ||
      std::filesystem::exists(workingDirectory / stdoutName) ||
      std::filesystem::exists(workingDirectory / stderrName))
    throw std::runtime_error("structural archive output already exists");
  const auto matchesFile = [&](const std::string &name,
                               const CalculixArtifactIdentity &identity) {
    std::error_code error;
    const auto length = std::filesystem::file_size(workingDirectory / name,
                                                   error);
    return !error &&
           std::filesystem::is_regular_file(workingDirectory / name) &&
           length == identity.byte_length && strict_sha256(identity.sha256) &&
           integrity::sha256_file(workingDirectory / name) == identity.sha256;
  };
  if (!matchesFile(deckName, validated.artifacts.deck) ||
      !matchesFile(datName, validated.artifacts.dat) ||
      !matchesFile(frdName, validated.artifacts.frd) ||
      !matchesFile(staName, validated.artifacts.sta) ||
      validated.artifacts.standard_output !=
          CalculixArtifactIdentity{integrity::sha256_bytes(run.standard_output),
                                   run.standard_output.size()} ||
      validated.artifacts.standard_error !=
          CalculixArtifactIdentity{integrity::sha256_bytes(run.standard_error),
                                   run.standard_error.size()})
    throw std::runtime_error("active solver artifacts changed before archiving");
  write(workingDirectory / setupName, verifiedSetup);
  write(workingDirectory / stdoutName, run.standard_output);
  write(workingDirectory / stderrName, run.standard_error);

  const CalculixArtifactIdentity setupIdentity{
      integrity::sha256_bytes(verifiedSetup), verifiedSetup.size()};
  const Json document{
      {"$schema", archiveSchemaV2}, {"schema_version", "2.0.0"},
      {"archive_kind", "completed_linear_static_run"},
      {"analysis_id", request.analysis_id},
      {"component_name", request.component_name},
      {"geometry_sha256", request.geometry_sha256},
      {"compiled_setup_identity", setup.identity},
      {"validated_result_identity", validated.identity},
      {"job_name", std::move(jobName)},
      {"execution", {{"exit_code", run.exit_code},
                       {"elapsed_ms", run.elapsed.count()},
                       {"status", "completed"}}},
      {"backend", {{"executable_sha256", validated.backend.executable_sha256},
                    {"version", validated.backend.version}}},
      {"convergence", convergence_json(*validated.convergence)},
      {"artifacts", {{"setup", artifact_json(setupName, setupIdentity)},
                      {"deck", artifact_json(deckName, validated.artifacts.deck)},
                      {"dat", artifact_json(datName, validated.artifacts.dat)},
                      {"frd", artifact_json(frdName, validated.artifacts.frd)},
                      {"sta", artifact_json(staName, validated.artifacts.sta)},
                      {"stdout", artifact_json(stdoutName,
                                               validated.artifacts.standard_output)},
                      {"stderr", artifact_json(stderrName,
                                               validated.artifacts.standard_error)}}},
      {"metrics", metrics_json(*validated.metrics)},
      {"requirements", requirements_json(request)},
      {"refinement", refinement_json(*evaluation.refinement)},
      {"coverage", {{"declared_obligations", evaluation.declared_obligations},
                     {"evaluated_obligations", evaluation.evaluated_obligations}}},
      {"findings", findings_json(evaluation)},
      {"limitation", evaluation.limitation}};
  const auto canonical = integrity::canonicalize_json_bytes(document.dump());
  write(manifestPath, canonical);
  return {manifestPath, integrity::sha256_bytes(canonical), "2.0.0",
          validated.identity};
}

namespace {

StructuralArchiveVerification verify_v2_archive(
    const std::filesystem::path &manifestPath, const Json &root) {
  if (!exact_keys(root, {"$schema", "schema_version", "archive_kind",
                         "analysis_id", "component_name", "geometry_sha256",
                         "job_name", "compiled_setup_identity",
                         "validated_result_identity", "execution", "backend",
                         "convergence", "artifacts", "metrics", "requirements",
                         "refinement", "coverage", "findings", "limitation"}) ||
      root.at("$schema") != archiveSchemaV2 ||
      root.at("schema_version") != "2.0.0" ||
      root.at("archive_kind") != "completed_linear_static_run")
    return failure("archive_contract_invalid", "archive v2 root contract is invalid");

  const auto analysisId = json_string(root, "analysis_id");
  const auto componentName = json_string(root, "component_name");
  const auto geometryIdentity = json_string(root, "geometry_sha256");
  const auto jobName = json_string(root, "job_name");
  const auto setupIdentity = json_string(root, "compiled_setup_identity");
  const auto resultIdentity = json_string(root, "validated_result_identity");
  if (analysisId.empty() || componentName.empty() ||
      !strict_sha256(geometryIdentity) || !safe_job_name(jobName) ||
      !strict_sha256(setupIdentity) || !strict_sha256(resultIdentity))
    return failure("archive_contract_invalid", "archive v2 identities are invalid");

  const auto &execution = root.at("execution");
  if (!exact_keys(execution, {"exit_code", "elapsed_ms", "status"}) ||
      !execution.at("exit_code").is_number_integer() ||
      !execution.at("elapsed_ms").is_number_integer() ||
      execution.at("exit_code").get<int>() != 0 ||
      execution.at("elapsed_ms").get<std::int64_t>() < 0 ||
      execution.at("status") != "completed")
    return failure("archive_contract_invalid", "archive execution evidence is invalid");
  const auto &backend = root.at("backend");
  if (!exact_keys(backend, {"executable_sha256", "version"}))
    return failure("archive_contract_invalid", "archive backend identity is invalid");
  const auto backendHash = json_string(backend, "executable_sha256");
  const auto backendVersion = json_string(backend, "version");
  if (!strict_sha256(backendHash) || backendVersion.empty())
    return failure("archive_contract_invalid", "archive backend identity is invalid");

  const auto artifacts = verify_and_load_artifacts(
      manifestPath.parent_path(), root.at("artifacts"));
  const auto deserializedSetup =
      deserialize_setup(artifacts.setup, artifacts.deck);
  const auto compiledSetup = compile_structural_setup(deserializedSetup);
  if (compiledSetup.canonical_setup_evidence != artifacts.setup ||
      compiledSetup.calculix_deck != artifacts.deck ||
      compiledSetup.identity != setupIdentity ||
      compiledSetup.request.analysis_id != analysisId ||
      compiledSetup.request.component_name != componentName ||
      compiledSetup.request.geometry_sha256 != geometryIdentity)
    return failure("setup_binding_mismatch",
                   "persisted setup does not reproduce the archived setup identity");
  if (root.at("requirements") != requirements_json(compiledSetup.request))
    return failure("setup_binding_mismatch",
                   "archive requirements differ from the reviewed setup");

  const CalculixRunEvidence evidence{
      .process_exit_code = execution.at("exit_code").get<int>(),
      .solver_executable_sha256 = backendHash,
      .solver_version = backendVersion,
      .deck_bytes = artifacts.deck,
      .standard_output = artifacts.standard_output,
      .standard_error = artifacts.standard_error,
      .status_bytes = artifacts.sta,
      .data_bytes = artifacts.dat,
      .frd_sha256 = artifacts.frd.sha256,
      .frd_byte_length = artifacts.frd.byte_length};
  auto replayedResult = compile_calculix_result(compiledSetup, evidence);
  if (!replayedResult.complete()) {
    const auto detail = replayedResult.issues.empty()
                            ? "persisted solver evidence is incomplete"
                            : replayedResult.issues.front().code + ": " +
                                  replayedResult.issues.front().message;
    return failure("replay_result_invalid", detail);
  }
  if (replayedResult.compiled_setup_identity != setupIdentity ||
      replayedResult.identity != resultIdentity)
    return failure("replay_result_identity_mismatch",
                   "persisted solver evidence produces a different result identity");
  if (root.at("backend") !=
          Json{{"executable_sha256", replayedResult.backend.executable_sha256},
               {"version", replayedResult.backend.version}} ||
      root.at("convergence") != convergence_json(*replayedResult.convergence) ||
      root.at("metrics") != metrics_json(*replayedResult.metrics))
    return failure("replay_result_mismatch",
                   "persisted solver evidence differs from archived result fields");

  const auto refinement = refinement_from_json(root.at("refinement"));
  const std::optional<CompiledCalculixResult> validatedResult{
      std::move(replayedResult)};
  const auto evaluation = compile_structural_findings(
      compiledSetup.request, validatedResult, refinement);
  const Json expectedCoverage{
      {"declared_obligations", evaluation.declared_obligations},
      {"evaluated_obligations", evaluation.evaluated_obligations}};
  if (evaluation.execution_status != SolverRunStatus::completed ||
      !evaluation.refinement || root.at("refinement") !=
                                  refinement_json(*evaluation.refinement) ||
      root.at("coverage") != expectedCoverage ||
      root.at("findings") != findings_json(evaluation) ||
      json_string(root, "limitation") != evaluation.limitation)
    return failure("replay_finding_mismatch",
                   "persisted evidence produces different scoped findings");
  return {true,
          "verified",
          "v2 artifact identities, setup, solver evidence, and findings replay verified",
          validatedResult->metrics,
          evaluation.declared_obligations,
          evaluation.evaluated_obligations,
          "2.0.0",
          validatedResult->identity};
}

} // namespace

StructuralArchiveVerification verify_structural_archive(
    const std::filesystem::path &manifestPath) noexcept {
  try {
    const auto bytes = bounded_read(manifestPath, 8U * 1024U * 1024U);
    const auto canonical = integrity::verify_canonical_bytes(bytes);
    const auto root = Json::parse(canonical);
    if (root.is_object() && root.value("$schema", "") == archiveSchemaV2 &&
        root.value("schema_version", "") == "2.0.0")
      return verify_v2_archive(manifestPath, root);
    if (!exact_keys(root, {"$schema", "schema_version", "archive_kind",
                           "analysis_id", "component_name", "geometry_sha256",
                           "solver_identity", "job_name", "execution",
                           "artifacts", "metrics", "requirements", "coverage", "findings",
                           "limitation"}) ||
        root.at("$schema") != archiveSchemaV1 ||
        root.at("schema_version") != "1.0.0" ||
        root.at("archive_kind") != "completed_linear_static_run")
      return failure("archive_contract_invalid", "archive root contract is invalid");
    const auto &artifacts = root.at("artifacts");
    if (!exact_keys(artifacts, {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"}))
      return failure("archive_contract_invalid", "archive artifact set is invalid");
    const auto directory = manifestPath.parent_path();
    for (const auto key : {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"}) {
      const auto &reference = artifacts.at(key);
      if (!exact_keys(reference, {"file", "byte_length", "sha256"}) ||
          !reference.at("file").is_string() ||
          !reference.at("byte_length").is_number_unsigned() ||
          !reference.at("sha256").is_string())
        return failure("archive_contract_invalid", "artifact reference is invalid");
      const auto name = reference.at("file").get<std::string>();
      if (!safe_file(name))
        return failure("unsafe_artifact_path", "artifact path is not a safe filename");
      const auto artifactBytes = read(directory / name);
      if (artifactBytes.size() != reference.at("byte_length").get<std::size_t>() ||
          integrity::sha256_bytes(artifactBytes) != reference.at("sha256").get<std::string>())
        return failure("artifact_identity_mismatch", name + " bytes changed");
    }
    const auto parsed = parse_calculix_dat(read(directory /
        artifacts.at("dat").at("file").get<std::string>()));
    const auto parsedMetrics = summarize_calculix_dat(parsed);
    const auto &metrics = root.at("metrics");
    if (!exact_keys(metrics, {"maximum_displacement_m", "maximum_von_mises_pa",
                              "displacement_rows", "stress_rows"}) ||
        parsedMetrics.maximum_displacement_m !=
            metrics.at("maximum_displacement_m").get<double>() ||
        parsedMetrics.maximum_von_mises_pa !=
            metrics.at("maximum_von_mises_pa").get<double>() ||
        parsedMetrics.displacement_rows !=
            metrics.at("displacement_rows").get<std::size_t>() ||
        parsedMetrics.stress_rows !=
            metrics.at("stress_rows").get<std::size_t>())
      return failure("replay_metric_mismatch", "raw DAT replay differs from archived metrics");
    const auto setupBytes = read(directory /
        artifacts.at("setup").at("file").get<std::string>());
    const auto setup = Json::parse(integrity::verify_canonical_bytes(setupBytes));
    if (!setup.is_object() || setup.value("$schema", "") != setupSchemaV1 ||
        setup.value("analysis_id", "") != root.value("analysis_id", "") ||
        setup.value("component_name", "") != root.value("component_name", "") ||
        setup.value("geometry_sha256", "") != root.value("geometry_sha256", ""))
      return failure("setup_binding_mismatch", "reviewed setup identity differs from archive");
    const auto &coverage = root.at("coverage");
    if (!exact_keys(coverage, {"declared_obligations", "evaluated_obligations"}) ||
        !coverage.at("declared_obligations").is_number_integer() ||
        !coverage.at("evaluated_obligations").is_number_integer())
      return failure("archive_contract_invalid", "archive coverage is invalid");
    const auto &requirements = root.at("requirements");
    if (!exact_keys(requirements, {"displacement_limit_m", "von_mises_limit_pa"}))
      return failure("archive_contract_invalid", "archive requirements are invalid");
    StructuralRequest replayRequest;
    if (setup.contains("mesh") && setup.at("mesh").is_object() &&
        setup.at("mesh").contains("node_ids") &&
        setup.at("mesh").contains("element_ids")) {
      if (!setup.at("mesh").at("node_ids").is_array() ||
          !setup.at("mesh").at("element_ids").is_array())
        return failure("setup_contract_invalid", "reviewed setup mesh identities are invalid");
      for (const auto &id : setup.at("mesh").at("node_ids")) {
        if (!id.is_number_integer())
          return failure("setup_contract_invalid", "reviewed setup node identity is invalid");
        replayRequest.nodes.push_back({id.get<int>(), {}});
      }
      for (const auto &id : setup.at("mesh").at("element_ids")) {
        if (!id.is_number_integer())
          return failure("setup_contract_invalid", "reviewed setup element identity is invalid");
        replayRequest.elements.push_back({id.get<int>(), {}});
      }
      if (const auto binding = legacy_v1_binding_issue(replayRequest, parsed);
          binding.has_value())
        return failure("replay_mesh_binding_mismatch",
                       *binding);
    }
    if (!requirements.at("displacement_limit_m").is_null())
      replayRequest.displacement_limit_m = requirements.at("displacement_limit_m").get<double>();
    if (!requirements.at("von_mises_limit_pa").is_null())
      replayRequest.von_mises_limit_pa = requirements.at("von_mises_limit_pa").get<double>();
    const auto replayEvaluation =
        replay_legacy_v1_findings(replayRequest, parsedMetrics);
    if (replayEvaluation.declared_obligations !=
            root.at("coverage").at("declared_obligations").get<int>() ||
        replayEvaluation.evaluated_obligations !=
            root.at("coverage").at("evaluated_obligations").get<int>() ||
        replayEvaluation.findings.size() != root.at("findings").size())
      return failure("replay_finding_mismatch", "replayed findings differ from archive");
    for (std::size_t index = 0; index < replayEvaluation.findings.size(); ++index) {
      const auto &actual = replayEvaluation.findings[index];
      const auto &stored = root.at("findings")[index];
      const auto disposition = actual.disposition == StructuralFindingDisposition::violated
          ? "violated" : "no_violation_detected_within_scope";
      if (!exact_keys(stored, {"obligation", "disposition", "measured", "limit",
                               "margin", "unit", "scope"}) ||
          stored.at("obligation") != actual.obligation ||
          stored.at("disposition") != disposition ||
          stored.at("measured") != actual.measured_value ||
          stored.at("limit") != actual.limit_value ||
          stored.at("margin") != actual.margin_to_limit ||
          stored.at("unit") != actual.unit || stored.at("scope") != actual.scope)
        return failure("replay_finding_mismatch", "replayed finding bytes differ from archive");
    }
    return {true, "verified", "exact artifact identities and DAT replay verified",
            parsedMetrics, coverage.at("declared_obligations").get<int>(),
            coverage.at("evaluated_obligations").get<int>(), "1.0.0", {}};
  } catch (const ArchiveVerificationError &error) {
    return failure(error.code(), error.what());
  } catch (const integrity::CanonicalJsonError &error) {
    return failure(error.code(), error.what());
  } catch (const std::exception &error) {
    return failure("archive_verification_failed", error.what());
  } catch (...) {
    return failure("archive_verification_failed", "unknown archive verification failure");
  }
}

StructuralArchive export_structural_archive(
    const std::filesystem::path &manifestPath,
    const std::filesystem::path &destinationDirectory) {
  const auto sourceVerification = verify_structural_archive(manifestPath);
  if (!sourceVerification.valid)
    throw std::runtime_error(sourceVerification.code + ": " +
                             sourceVerification.detail);
  if (destinationDirectory.empty() ||
      std::filesystem::exists(destinationDirectory))
    throw std::invalid_argument("archive export destination must not exist");
  const auto parent = destinationDirectory.parent_path();
  if (parent.empty() || !std::filesystem::is_directory(parent))
    throw std::invalid_argument("archive export parent must exist");

  std::mt19937_64 random{std::random_device{}()};
  auto temporary = destinationDirectory;
  temporary += ".partial-" + std::to_string(random());
  if (std::filesystem::exists(temporary))
    throw std::runtime_error("archive export temporary path already exists");
  try {
    std::filesystem::create_directory(temporary);
    const auto manifestBytes = read(manifestPath);
    const auto manifest = Json::parse(manifestBytes);
    for (const auto key : {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"}) {
      const auto name = manifest.at("artifacts").at(key).at("file").get<std::string>();
      if (!safe_file(name))
        throw std::runtime_error("archive contains an unsafe artifact filename");
      const auto bytes = read(manifestPath.parent_path() / name);
      write(temporary / name, bytes);
    }
    write(temporary / archiveName, manifestBytes);
    const auto copiedManifest = temporary / archiveName;
    const auto copyVerification = verify_structural_archive(copiedManifest);
    if (!copyVerification.valid)
      throw std::runtime_error(copyVerification.code + ": " +
                               copyVerification.detail);
    std::filesystem::rename(temporary, destinationDirectory);
    return {destinationDirectory / archiveName,
            integrity::sha256_bytes(manifestBytes),
            copyVerification.schema_version,
            copyVerification.validated_result_identity};
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
    throw;
  }
}

} // namespace prometheus::structural
