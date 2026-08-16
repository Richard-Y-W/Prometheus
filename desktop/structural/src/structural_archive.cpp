#include "prometheus/structural/structural_archive.hpp"

#include "prometheus/integrity/canonical_json.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <stdexcept>

namespace prometheus::structural {
namespace {

using Json = nlohmann::json;
constexpr auto archiveName = "prometheus-structural-run.json";
constexpr auto schema = "urn:prometheus:schema:structural-run-archive:1.0.0";
constexpr auto setupSchema = "urn:prometheus:schema:reviewed-structural-setup:1.0.0";

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

Json artifact(const std::filesystem::path &root, const std::string &name) {
  const auto bytes = read(root / name);
  return {{"file", name},
          {"byte_length", bytes.size()},
          {"sha256", integrity::sha256_bytes(bytes)}};
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
  return {false, std::move(code), std::move(detail), std::nullopt, 0, 0};
}

} // namespace

std::string serialize_structural_setup_evidence(const StructuralSetup &setup) {
  const auto issues = validate_setup(setup);
  if (!issues.empty())
    throw std::invalid_argument(issues.front().code + ": " + issues.front().message);
  const auto selection = [](const BoundarySelection &value) {
    Json faces = Json::array();
    for (const auto &face : value.face_node_ids)
      faces.push_back({face[0], face[1], face[2]});
    return Json{{"label", value.label}, {"face_node_ids", std::move(faces)},
                {"node_ids", value.node_ids}, {"area_m2", value.area_m2}};
  };
  const auto optionalNumber = [](const std::optional<double> value) {
    return value ? Json(*value) : Json(nullptr);
  };
  Json nodeIds = Json::array();
  for (const auto &node : setup.mesh.nodes) nodeIds.push_back(node.id);
  Json elementIds = Json::array();
  for (const auto &element : setup.mesh.elements) elementIds.push_back(element.id);
  const Json document{
      {"$schema", setupSchema}, {"schema_version", "1.0.0"},
      {"analysis_id", setup.analysis_id}, {"component_name", setup.component_name},
      {"geometry_sha256", setup.geometry_sha256},
      {"mesh", {{"node_count", setup.mesh.nodes.size()},
                 {"element_count", setup.mesh.elements.size()},
                 {"boundary_face_count", setup.boundary_faces.size()},
                 {"node_ids", std::move(nodeIds)},
                 {"element_ids", std::move(elementIds)}}},
      {"material", {{"designation", setup.material.designation},
                     {"source_sha256", setup.material.source_sha256},
                     {"applicability", setup.material.applicability},
                     {"youngs_modulus_pa", setup.material.youngs_modulus_pa},
                     {"poisson_ratio", setup.material.poisson_ratio},
                     {"reviewed", setup.material.reviewed}}},
      {"load", {{"selection", selection(setup.load.selection)},
                 {"total_force_n", setup.load.total_force_n},
                 {"reviewed", setup.load.reviewed}}},
      {"restraint", {{"selection", selection(setup.restraint.selection)},
                      {"reviewed", setup.restraint.reviewed}}},
      {"requirement", {{"displacement_limit_m", optionalNumber(setup.requirement.displacement_limit_m)},
                        {"von_mises_limit_pa", optionalNumber(setup.requirement.von_mises_limit_pa)},
                        {"source_or_exploratory_rationale",
                         setup.requirement.source_or_exploratory_rationale},
                        {"reviewed", setup.requirement.reviewed}}},
      {"mesh_controls", {{"minimum_size_m", setup.mesh_controls.minimum_size_m},
                          {"maximum_size_m", setup.mesh_controls.maximum_size_m},
                          {"mesher_identity", setup.mesh_controls.mesher_identity},
                          {"reviewed", setup.mesh_controls.reviewed}}},
      {"scenario", {{"description", setup.scenario_description},
                     {"confirmed", setup.scenario_confirmed}}},
      {"selection_patch_angle_degrees", setup.selection_patch_angle_degrees}};
  return integrity::canonicalize_json_bytes(document.dump(),
      integrity::Limits{8U * 1024U * 1024U, 64U, 500000U, 10000U, 100000U,
                        4U * 1024U * 1024U});
}

StructuralArchive write_structural_archive(
    const std::filesystem::path &workingDirectory, std::string jobName,
    std::string solverIdentity, std::string reviewedSetupBytes,
    const StructuralRequest &request,
    const SolverRunResult &run, const StructuralEvaluation &evaluation) {
  if (run.status != SolverRunStatus::completed || !run.metrics)
    throw std::invalid_argument("only a completed structural run can be archived");
  if (jobName.empty() || solverIdentity.empty())
    throw std::invalid_argument("archive job and solver identities are required");
  const auto verifiedSetup = integrity::verify_canonical_bytes(
      reviewedSetupBytes,
      integrity::Limits{8U * 1024U * 1024U, 64U, 500000U, 10000U, 100000U,
                        4U * 1024U * 1024U});
  const auto setupJson = Json::parse(verifiedSetup);
  if (!setupJson.is_object() || setupJson.value("$schema", "") != setupSchema ||
      setupJson.value("analysis_id", "") != request.analysis_id ||
      setupJson.value("component_name", "") != request.component_name ||
      setupJson.value("geometry_sha256", "") != request.geometry_sha256)
    throw std::invalid_argument("reviewed setup evidence binding is invalid");
  const auto deckName = jobName + ".inp";
  const auto datName = jobName + ".dat";
  const auto frdName = jobName + ".frd";
  const auto staName = jobName + ".sta";
  const std::string stdoutName = "solver.stdout.txt";
  const std::string stderrName = "solver.stderr.txt";
  const std::string setupName = "reviewed-structural-setup.json";
  write(workingDirectory / setupName, verifiedSetup);
  write(workingDirectory / stdoutName, run.standard_output);
  write(workingDirectory / stderrName, run.standard_error);

  Json findings = Json::array();
  for (const auto &finding : evaluation.findings)
    findings.push_back({
        {"obligation", finding.obligation},
        {"disposition", finding.disposition == StructuralFindingDisposition::violated
             ? "violated" : "no_violation_detected_within_scope"},
        {"measured", finding.measured_value}, {"limit", finding.limit_value},
        {"margin", finding.margin_to_limit}, {"unit", finding.unit},
        {"scope", finding.scope}});
  const Json document{
      {"$schema", schema}, {"schema_version", "1.0.0"},
      {"archive_kind", "completed_linear_static_run"},
      {"analysis_id", request.analysis_id},
      {"component_name", request.component_name},
      {"geometry_sha256", request.geometry_sha256},
      {"solver_identity", std::move(solverIdentity)},
      {"job_name", std::move(jobName)},
      {"execution", {{"exit_code", run.exit_code},
                       {"elapsed_ms", run.elapsed.count()},
                       {"status", "completed"}}},
      {"artifacts", {{"setup", artifact(workingDirectory, setupName)},
                      {"deck", artifact(workingDirectory, deckName)},
                      {"dat", artifact(workingDirectory, datName)},
                      {"frd", artifact(workingDirectory, frdName)},
                      {"sta", artifact(workingDirectory, staName)},
                      {"stdout", artifact(workingDirectory, stdoutName)},
                      {"stderr", artifact(workingDirectory, stderrName)}}},
      {"metrics", {{"maximum_displacement_m", run.metrics->maximum_displacement_m},
                    {"maximum_von_mises_pa", run.metrics->maximum_von_mises_pa},
                    {"displacement_rows", run.metrics->displacement_rows},
                    {"stress_rows", run.metrics->stress_rows}}},
      {"requirements", {{"displacement_limit_m", request.displacement_limit_m},
                         {"von_mises_limit_pa", request.von_mises_limit_pa}}},
      {"coverage", {{"declared_obligations", evaluation.declared_obligations},
                     {"evaluated_obligations", evaluation.evaluated_obligations}}},
      {"findings", std::move(findings)},
      {"limitation", evaluation.limitation}};
  const auto canonical = integrity::canonicalize_json_bytes(document.dump());
  const auto manifestPath = workingDirectory / archiveName;
  if (std::filesystem::exists(manifestPath))
    throw std::runtime_error("structural archive manifest already exists");
  write(manifestPath, canonical);
  return {manifestPath, integrity::sha256_bytes(canonical)};
}

StructuralArchiveVerification verify_structural_archive(
    const std::filesystem::path &manifestPath) noexcept {
  try {
    const auto bytes = read(manifestPath);
    const auto canonical = integrity::verify_canonical_bytes(bytes);
    const auto root = Json::parse(canonical);
    if (!exact_keys(root, {"$schema", "schema_version", "archive_kind",
                           "analysis_id", "component_name", "geometry_sha256",
                           "solver_identity", "job_name", "execution",
                           "artifacts", "metrics", "requirements", "coverage", "findings",
                           "limitation"}) ||
        root.at("$schema") != schema || root.at("schema_version") != "1.0.0" ||
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
    const auto &metrics = root.at("metrics");
    if (!exact_keys(metrics, {"maximum_displacement_m", "maximum_von_mises_pa",
                              "displacement_rows", "stress_rows"}) ||
        parsed.maximum_displacement_m != metrics.at("maximum_displacement_m").get<double>() ||
        parsed.maximum_von_mises_pa != metrics.at("maximum_von_mises_pa").get<double>() ||
        parsed.displacement_rows != metrics.at("displacement_rows").get<std::size_t>() ||
        parsed.stress_rows != metrics.at("stress_rows").get<std::size_t>())
      return failure("replay_metric_mismatch", "raw DAT replay differs from archived metrics");
    const auto setupBytes = read(directory /
        artifacts.at("setup").at("file").get<std::string>());
    const auto setup = Json::parse(integrity::verify_canonical_bytes(setupBytes));
    if (!setup.is_object() || setup.value("$schema", "") != setupSchema ||
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
      if (const auto binding = validate_calculix_result_binding(replayRequest, parsed);
          !binding.empty())
        return failure("replay_mesh_binding_mismatch",
                       binding.front().code + ": " + binding.front().message);
    }
    if (!requirements.at("displacement_limit_m").is_null())
      replayRequest.displacement_limit_m = requirements.at("displacement_limit_m").get<double>();
    if (!requirements.at("von_mises_limit_pa").is_null())
      replayRequest.von_mises_limit_pa = requirements.at("von_mises_limit_pa").get<double>();
    SolverRunResult replayRun;
    replayRun.status = SolverRunStatus::completed;
    replayRun.metrics = parsed;
    const auto replayEvaluation = compile_structural_findings(replayRequest, replayRun);
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
            parsed, coverage.at("declared_obligations").get<int>(),
            coverage.at("evaluated_obligations").get<int>()};
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
            integrity::sha256_bytes(manifestBytes)};
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
    throw;
  }
}

} // namespace prometheus::structural
