#include <prometheus/run_store/run_store.hpp>
#include <prometheus/run_store/structural_archive_store.hpp>
#include <prometheus/run_store/project_evidence_archive.hpp>

#include "platform_io.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace prometheus::run_store {
namespace detail {
namespace {

std::string bounded(std::string value, const std::size_t maximum_bytes) {
  if (value.size() <= maximum_bytes) {
    return value;
  }
  auto boundary = maximum_bytes;
  while (boundary > 0U && boundary < value.size() &&
         (static_cast<unsigned char>(value[boundary]) & 0xC0U) == 0x80U) {
    --boundary;
  }
  value.resize(boundary);
  return value;
}

} // namespace

Diagnostic store_diagnostic(std::string code, std::string message,
                            std::optional<std::string> field,
                            std::optional<std::filesystem::path> path) {
  if (field.has_value()) {
    *field = bounded(std::move(*field), 512U);
  }
  std::optional<std::string> path_text;
  if (path.has_value()) {
    path_text = bounded(path->generic_string(), 4096U);
  }
  return Diagnostic{"store", bounded(std::move(code), 128U),
                    bounded(std::move(message), 4096U), std::move(field),
                    std::move(path_text)};
}

std::optional<Diagnostic>
check_boundary(const TransactionOptions &options,
               const TransactionBoundary boundary) noexcept {
  if (!options.boundary_hook) {
    return std::nullopt;
  }
  try {
    if (options.boundary_hook(boundary)) {
      return store_diagnostic(
          "injected_failure",
          "transaction stopped at an injected durable boundary");
    }
    return std::nullopt;
  } catch (const std::exception &failure) {
    return store_diagnostic("boundary_hook_failed", failure.what());
  } catch (...) {
    return store_diagnostic("boundary_hook_failed",
                            "transaction boundary hook threw an unknown error");
  }
}

std::optional<Diagnostic>
check_cancelled(const TransactionOptions &options) noexcept {
  if (options.stop_token.stop_requested()) {
    return store_diagnostic(
        "operation_cancelled",
        "transaction was cancelled before project replacement");
  }
  return std::nullopt;
}

} // namespace detail
namespace {

using Json = nlohmann::json;
constexpr std::uint64_t maximum_safe_integer = 9007199254740991ULL;

Result<detail::Unit> append_event(ProjectV2 &project, std::string event_kind,
                                  std::string status,
                                  const std::string &related_hash);

template <typename T> Result<T> failure_from(const Diagnostic &diagnostic) {
  return Result<T>::failure(diagnostic);
}

Diagnostic normalized(Diagnostic diagnostic,
                      const std::filesystem::path &path) {
  return detail::store_diagnostic(
      std::move(diagnostic.code), std::move(diagnostic.message),
      std::move(diagnostic.field),
      diagnostic.path.has_value()
          ? std::optional<std::filesystem::path>(*diagnostic.path)
          : std::optional<std::filesystem::path>(path));
}

Result<ProjectV2> parse_stored_project(const std::string_view bytes,
                                       const std::filesystem::path &path) {
  auto parsed = parse_project_v2(bytes);
  if (!parsed.has_value()) {
    return Result<ProjectV2>::failure(normalized(parsed.diagnostic(), path));
  }
  return parsed;
}

Result<ProjectV2> read_locked_project(const std::filesystem::path &path) {
  const auto bytes = detail::read_project_index_file(path);
  if (!bytes.has_value()) {
    return failure_from<ProjectV2>(bytes.diagnostic());
  }
  return parse_stored_project(bytes.value(), path);
}

Result<ProjectV2> persist_project(const std::filesystem::path &path,
                                  const ProjectV2 &project,
                                  const bool replace_existing,
                                  const TransactionOptions &options) {
  const auto serialized = serialize_project_v2(project);
  if (!serialized.has_value()) {
    return Result<ProjectV2>::failure(
        normalized(serialized.diagnostic(), path));
  }
  const auto written = detail::replace_project_index_file(
      path, serialized.value(), replace_existing, options);
  if (!written.has_value()) {
    return failure_from<ProjectV2>(written.diagnostic());
  }
  return parse_stored_project(serialized.value(), path);
}

bool exact_keys(const Json &value,
                const std::initializer_list<std::string_view> members) {
  if (!value.is_object() || value.size() != members.size()) {
    return false;
  }
  return std::all_of(members.begin(), members.end(), [&](const auto member) {
    return value.contains(std::string(member));
  });
}

std::optional<StoredObjectReference> reference_from_json(const Json &value) {
  if (!exact_keys(value, {"object_hash", "byte_length", "media_type",
                          "schema_id", "schema_version"}) ||
      !value.at("object_hash").is_string() ||
      (!value.at("byte_length").is_number_integer() &&
       !value.at("byte_length").is_number_unsigned()) ||
      !value.at("media_type").is_string() ||
      !value.at("schema_id").is_string() ||
      !value.at("schema_version").is_string()) {
    return std::nullopt;
  }
  if (value.at("byte_length").is_number_integer() &&
      value.at("byte_length").get<std::int64_t>() < 0) {
    return std::nullopt;
  }
  return StoredObjectReference{value.at("object_hash").get<std::string>(),
                               value.at("byte_length").get<std::uint64_t>(),
                               value.at("media_type").get<std::string>(),
                               value.at("schema_id").get<std::string>(),
                               value.at("schema_version").get<std::string>()};
}

bool string_equals(const Json &object, const std::string_view member,
                   const std::string_view expected) {
  return object.contains(std::string(member)) &&
         object.at(std::string(member)).is_string() &&
         object.at(std::string(member)).get_ref<const std::string &>() ==
             expected;
}

std::optional<std::string> string_member(const Json &object,
                                         const std::string_view member) {
  if (!object.contains(std::string(member)) ||
      !object.at(std::string(member)).is_string()) {
    return std::nullopt;
  }
  return object.at(std::string(member)).get<std::string>();
}

Result<detail::Unit>
validate_publication_graph(const ProjectV2 &project,
                           const CompletedRunObjects &objects,
                           const bool require_current_bindings) {
  for (const auto *object :
       {&objects.package, &objects.scenario, &objects.request, &objects.result,
        &objects.manifest}) {
    const auto valid =
        detail::verify_stored_object(object->reference, object->bytes);
    if (!valid.has_value()) {
      return valid;
    }
  }
  try {
    const auto manifest = Json::parse(objects.manifest.bytes);
    if (!exact_keys(manifest,
                    {"$schema", "schema_version", "manifest_kind", "package",
                     "scenario", "request", "result", "assembly_artifact_hash",
                     "backend_id", "backend_contract_version",
                     "package_consumer_contract_hash", "numeric_profile"}) ||
        !string_equals(manifest, "$schema",
                       "urn:prometheus:schema:run-manifest:1.0.0") ||
        !string_equals(manifest, "schema_version", "1.0.0") ||
        !string_equals(manifest, "manifest_kind", "completed_analysis_run")) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "manifest_contract_invalid",
          "manifest is not the closed completed-run contract"));
    }
    const auto package_reference = reference_from_json(manifest.at("package"));
    const auto scenario_reference =
        reference_from_json(manifest.at("scenario"));
    const auto request_reference = reference_from_json(manifest.at("request"));
    const auto result_reference = reference_from_json(manifest.at("result"));
    if (!package_reference.has_value() || !scenario_reference.has_value() ||
        !request_reference.has_value() || !result_reference.has_value() ||
        *package_reference != objects.package.reference ||
        *scenario_reference != objects.scenario.reference ||
        *request_reference != objects.request.reference ||
        *result_reference != objects.result.reference) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "manifest_reference_mismatch",
          "manifest references do not match the supplied immutable objects"));
    }
    const auto manifest_assembly =
        string_member(manifest, "assembly_artifact_hash");
    if (!manifest_assembly.has_value()) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "assembly_artifact_mismatch", "manifest assembly hash is invalid"));
    }
    if (require_current_bindings &&
        *manifest_assembly != project.assembly_artifact_hash) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "assembly_artifact_mismatch",
          "manifest assembly hash does not match the project snapshot"));
    }

    const auto request = Json::parse(objects.request.bytes);
    if (!exact_keys(request,
                    {"$schema", "schema_version", "request_kind",
                     "package_hash", "scenario_hash", "assembly_artifact_hash",
                     "bound_cad_entity_id", "backend_id",
                     "backend_contract_version",
                     "package_consumer_contract_hash", "obligation_ids"}) ||
        !string_equals(request, "$schema",
                       "urn:prometheus:schema:analysis-request:1.0.0") ||
        !string_equals(request, "schema_version", "1.0.0") ||
        !string_equals(request, "request_kind", "motor_arm_analysis") ||
        !string_equals(request, "package_hash",
                       objects.package.reference.object_hash) ||
        !string_equals(request, "scenario_hash",
                       objects.scenario.reference.object_hash) ||
        !string_equals(request, "assembly_artifact_hash", *manifest_assembly)) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "request_binding_mismatch",
          "request hashes do not match the publication snapshot"));
    }
    const auto entity = string_member(request, "bound_cad_entity_id");
    if (!entity.has_value()) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "request_binding_mismatch", "request CAD entity is invalid"));
    }
    for (const auto member : {"backend_id", "backend_contract_version",
                              "package_consumer_contract_hash"}) {
      const auto request_value = string_member(request, member);
      const auto manifest_value = string_member(manifest, member);
      if (!request_value.has_value() || !manifest_value.has_value() ||
          request_value != manifest_value) {
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "manifest_request_identity_mismatch",
            "manifest and request execution identities disagree"));
      }
    }

    if (require_current_bindings) {
      std::unordered_set<std::uint64_t> superseded;
      for (const auto &binding : project.execution.package_bindings) {
        if (binding.supersedes_binding_revision.has_value()) {
          superseded.insert(*binding.supersedes_binding_revision);
        }
      }
      const auto active = std::find_if(
          project.execution.package_bindings.begin(),
          project.execution.package_bindings.end(), [&](const auto &binding) {
            return binding.cad_entity_id == *entity &&
                   !superseded.contains(binding.binding_revision);
          });
      if (active == project.execution.package_bindings.end() ||
          active->package != objects.package.reference) {
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "active_package_binding_mismatch",
            "request package is not the active package for its CAD entity"));
      }
      if (!project.execution.current_scenario.has_value() ||
          *project.execution.current_scenario != objects.scenario.reference) {
        return Result<detail::Unit>::failure(
            detail::store_diagnostic("current_scenario_mismatch",
                                     "request scenario is not the project's "
                                     "confirmed current scenario"));
      }
    }

    const auto result = Json::parse(objects.result.bytes);
    if (!exact_keys(result,
                    {"$schema", "schema_version", "execution_disposition",
                     "request_hash", "package_hash", "backend", "calculations",
                     "consumed_inputs", "sensitivities", "obligation_outcomes",
                     "missing_information", "assumptions", "limitations",
                     "applicability", "coverage"}) ||
        !string_equals(result, "$schema",
                       "urn:prometheus:schema:analysis-result:1.0.0") ||
        !string_equals(result, "schema_version", "1.0.0") ||
        !string_equals(result, "execution_disposition", "completed") ||
        !string_equals(result, "request_hash",
                       objects.request.reference.object_hash) ||
        !string_equals(result, "package_hash",
                       objects.package.reference.object_hash)) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "result_binding_mismatch",
          "result does not bind the supplied request and package"));
    }
    const auto &result_backend = result.at("backend");
    if (!exact_keys(result_backend,
                    {"backend_id", "contract_version", "numeric_profile"}) ||
        !string_equals(result_backend, "backend_id",
                       *string_member(manifest, "backend_id")) ||
        !string_equals(result_backend, "contract_version",
                       *string_member(manifest, "backend_contract_version")) ||
        result_backend.at("numeric_profile") !=
            manifest.at("numeric_profile")) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "manifest_result_identity_mismatch",
          "manifest and result backend identities disagree"));
    }
    return Result<detail::Unit>::success(detail::Unit{});
  } catch (const std::exception &failure) {
    return Result<detail::Unit>::failure(
        detail::store_diagnostic("publication_graph_invalid", failure.what()));
  } catch (...) {
    return Result<detail::Unit>::failure(detail::store_diagnostic(
        "publication_graph_invalid",
        "unknown publication-graph verification failure"));
  }
}

Result<ObjectToStore> build_execution_project_snapshot(
    const ProjectV2 &project, const std::string_view executionKind,
    const std::string_view pendingManifestHash) {
  const auto projectBytes = serialize_project_v2(project);
  if (!projectBytes.has_value())
    return failure_from<ObjectToStore>(projectBytes.diagnostic());
  const Json snapshot{
      {"$schema", execution_project_snapshot_schema_id},
      {"schema_version", "1.0.0"},
      {"snapshot_kind", "pre_execution_project"},
      {"execution_kind", executionKind},
      {"pending_manifest_hash", pendingManifestHash},
      {"project_index_sha256", integrity::sha256_bytes(projectBytes.value())},
      {"project_index", projectBytes.value()}};
  auto bytes = integrity::canonicalize_json_bytes(snapshot.dump());
  StoredObjectReference reference{
      integrity::sha256_bytes(bytes), bytes.size(),
      std::string(execution_project_snapshot_media_type),
      std::string(execution_project_snapshot_schema_id), "1.0.0"};
  return Result<ObjectToStore>::success({std::move(reference), std::move(bytes)});
}

Result<detail::Unit> install_execution_project_snapshot(
    const std::filesystem::path &projectPath, ProjectV2 &project,
    const std::string_view executionKind,
    const std::string_view pendingManifestHash,
    const TransactionOptions &options) {
  if (project.execution.committed_runs.size() + 2U > maximum_committed_runs)
    return Result<detail::Unit>::failure(detail::store_diagnostic(
        "committed_run_limit_exceeded",
        "project history has no room for a run and its execution snapshot"));
  const auto snapshot = build_execution_project_snapshot(
      project, executionKind, pendingManifestHash);
  if (!snapshot.has_value())
    return failure_from<detail::Unit>(snapshot.diagnostic());
  const auto installed = detail::install_object_file(
      projectPath, snapshot.value().reference, snapshot.value().bytes, options);
  if (!installed.has_value())
    return failure_from<detail::Unit>(installed.diagnostic());
  project.execution.committed_runs.push_back(snapshot.value().reference);
  const auto event = append_event(project, "execution_project_snapshotted",
                                  "completed",
                                  snapshot.value().reference.object_hash);
  if (!event.has_value()) return event;
  return Result<detail::Unit>::success({});
}

std::string decode_base64(const std::string_view text) {
  if (text.size() % 4U != 0U) throw std::runtime_error("invalid base64 length");
  const auto value = [](const char character) -> int {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
  };
  std::string result;
  result.reserve(text.size() / 4U * 3U);
  for (std::size_t offset = 0; offset < text.size(); offset += 4U) {
    const auto a = value(text[offset]);
    const auto b = value(text[offset + 1U]);
    const auto c = text[offset + 2U] == '=' ? 0 : value(text[offset + 2U]);
    const auto d = text[offset + 3U] == '=' ? 0 : value(text[offset + 3U]);
    if (a < 0 || b < 0 || c < 0 || d < 0 ||
        (text[offset + 2U] == '=' && text[offset + 3U] != '=') ||
        (offset + 4U != text.size() &&
         (text[offset + 2U] == '=' || text[offset + 3U] == '=')))
      throw std::runtime_error("invalid base64 data");
    const auto packed = (static_cast<std::uint32_t>(a) << 18U) |
                        (static_cast<std::uint32_t>(b) << 12U) |
                        (static_cast<std::uint32_t>(c) << 6U) |
                        static_cast<std::uint32_t>(d);
    result.push_back(static_cast<char>((packed >> 16U) & 255U));
    if (text[offset + 2U] != '=')
      result.push_back(static_cast<char>((packed >> 8U) & 255U));
    if (text[offset + 3U] != '=')
      result.push_back(static_cast<char>(packed & 255U));
  }
  return result;
}

Result<detail::Unit>
validate_structural_manifest(const ObjectToStore &manifest) {
  const auto stored =
      detail::verify_stored_object(manifest.reference, manifest.bytes);
  if (!stored.has_value()) return stored;
  const bool referenceV1 =
      manifest.reference.schema_id == structural_manifest_schema_id_v1 &&
      manifest.reference.schema_version == "1.0.0";
  const bool referenceV2 =
      manifest.reference.schema_id == structural_manifest_schema_id_v2 &&
      manifest.reference.schema_version == "2.0.0";
  if (manifest.reference.media_type != structural_manifest_media_type ||
      (!referenceV1 && !referenceV2)) {
    return Result<detail::Unit>::failure(detail::store_diagnostic(
        "structural_manifest_reference_invalid",
        "structural manifest reference has the wrong registered contract"));
  }
  try {
    const auto root = Json::parse(manifest.bytes);
    const bool documentV1 =
        referenceV1 &&
        exact_keys(root, {"$schema", "schema_version", "archive_kind",
                          "analysis_id", "component_name", "geometry_sha256",
                          "solver_identity", "job_name", "execution",
                          "artifacts", "metrics", "requirements", "coverage",
                          "findings", "limitation"}) &&
        string_equals(root, "$schema", structural_manifest_schema_id_v1) &&
        string_equals(root, "schema_version", "1.0.0");
    const bool documentV2 =
        referenceV2 &&
        exact_keys(root, {"$schema", "schema_version", "archive_kind",
                          "analysis_id", "component_name", "geometry_sha256",
                          "job_name", "compiled_setup_identity",
                          "validated_result_identity", "execution", "backend",
                          "convergence", "artifacts", "metrics", "requirements",
                          "refinement", "coverage", "findings", "limitation"}) &&
        string_equals(root, "$schema", structural_manifest_schema_id_v2) &&
        string_equals(root, "schema_version", "2.0.0");
    if ((!documentV1 && !documentV2) ||
        !string_equals(root, "archive_kind", "completed_linear_static_run")) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "structural_manifest_contract_invalid",
          "structural manifest is not the closed completed-run archive contract"));
    }
    const auto &artifacts = root.at("artifacts");
    if (!exact_keys(artifacts,
                    {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"})) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "structural_manifest_contract_invalid",
          "structural manifest artifact set is incomplete"));
    }
    std::unordered_set<std::string> filenames;
    for (const auto key : {"setup", "deck", "dat", "frd", "sta", "stdout", "stderr"}) {
      const auto &artifact = artifacts.at(key);
      if (!exact_keys(artifact, {"file", "byte_length", "sha256"}) ||
          !artifact.at("file").is_string() ||
          !artifact.at("byte_length").is_number_unsigned() ||
          !artifact.at("sha256").is_string()) {
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "structural_manifest_contract_invalid",
            "structural artifact reference is invalid"));
      }
      const auto filename = artifact.at("file").get<std::string>();
      const auto hash = artifact.at("sha256").get<std::string>();
      if (filename.empty() || filename.size() > 128U ||
          filename.find('/') != std::string::npos ||
          filename.find('\\') != std::string::npos ||
          !filenames.insert(filename).second || !is_valid_object_hash(hash)) {
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "structural_manifest_contract_invalid",
            "structural artifact identity is unsafe or duplicated"));
      }
    }
    return Result<detail::Unit>::success(detail::Unit{});
  } catch (const std::exception &failure) {
    return Result<detail::Unit>::failure(detail::store_diagnostic(
        "structural_manifest_contract_invalid", failure.what()));
  }
}

Result<detail::Unit> validate_embedded_structural_graph(
    const ProjectV2 &project, const StructuralArchiveObjects &objects,
    const bool requireCurrentAssembly) {
  const auto archiveValid = validate_structural_manifest(objects.archive_manifest);
  if (!archiveValid.has_value()) return archiveValid;
  const auto projectValid = detail::verify_stored_object(
      objects.project_manifest.reference, objects.project_manifest.bytes);
  if (!projectValid.has_value()) return projectValid;
  if (objects.project_manifest.reference.media_type !=
          structural_project_run_media_type ||
      objects.project_manifest.reference.schema_id !=
          structural_project_run_schema_id)
    return Result<detail::Unit>::failure(detail::store_diagnostic(
        "structural_project_manifest_reference_invalid",
        "embedded structural manifest reference has the wrong contract"));
  try {
    const auto projectManifest = Json::parse(objects.project_manifest.bytes);
    if (!exact_keys(projectManifest,
                    {"$schema", "schema_version", "manifest_kind",
                     "assembly_artifact_hash", "archive_manifest", "artifacts"}) ||
        !string_equals(projectManifest, "$schema",
                       structural_project_run_schema_id) ||
        !string_equals(projectManifest, "schema_version", "1.0.0") ||
        !string_equals(projectManifest, "manifest_kind",
                       "embedded_structural_run") ||
        !projectManifest.at("artifacts").is_array() ||
        projectManifest.at("artifacts").size() != 7U)
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "structural_project_manifest_invalid",
          "embedded structural project manifest is invalid"));
    if (!projectManifest.at("assembly_artifact_hash").is_string() ||
        !is_valid_object_hash(
            projectManifest.at("assembly_artifact_hash").get<std::string>()) ||
        (requireCurrentAssembly &&
         projectManifest.at("assembly_artifact_hash").get<std::string>() !=
             project.assembly_artifact_hash))
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "assembly_artifact_mismatch",
          "structural run assembly identity does not match the project snapshot"));
    const auto archiveReference =
        reference_from_json(projectManifest.at("archive_manifest"));
    if (!archiveReference || *archiveReference != objects.archive_manifest.reference)
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "structural_archive_reference_mismatch",
          "project manifest does not bind the supplied archive manifest"));
    const auto archive = Json::parse(objects.archive_manifest.bytes);
    if (!archive.contains("geometry_sha256") ||
        !archive.at("geometry_sha256").is_string() ||
        archive.at("geometry_sha256") !=
            projectManifest.at("assembly_artifact_hash"))
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "structural_geometry_binding_mismatch",
          "structural archive geometry does not match its project assembly"));
    std::unordered_map<std::string, const ObjectToStore *> suppliedChunks;
    for (const auto &chunk : objects.chunks) {
      const auto verified = detail::verify_stored_object(chunk.reference, chunk.bytes);
      if (!verified.has_value()) return verified;
      if (chunk.reference.media_type != structural_artifact_chunk_media_type ||
          chunk.reference.schema_id != structural_artifact_chunk_schema_id ||
          !suppliedChunks.emplace(chunk.reference.object_hash, &chunk).second)
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "structural_chunk_set_invalid",
            "structural chunk contract or identity is invalid"));
    }
    std::unordered_set<std::string> referencedChunks;
    std::unordered_set<std::string> roles;
    for (const auto &artifact : projectManifest.at("artifacts")) {
      if (!exact_keys(artifact,
                      {"role", "file", "byte_length", "sha256", "chunks"}) ||
          !artifact.at("role").is_string() || !artifact.at("file").is_string() ||
          !artifact.at("byte_length").is_number_unsigned() ||
          !artifact.at("sha256").is_string() ||
          !artifact.at("chunks").is_array())
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "structural_project_manifest_invalid",
            "embedded structural artifact entry is invalid"));
      const auto role = artifact.at("role").get<std::string>();
      if (!roles.insert(role).second || !archive.at("artifacts").contains(role))
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "structural_project_manifest_invalid",
            "embedded structural artifact role is invalid"));
      const auto &declared = archive.at("artifacts").at(role);
      if (artifact.at("file") != declared.at("file") ||
          artifact.at("byte_length") != declared.at("byte_length") ||
          artifact.at("sha256") != declared.at("sha256"))
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "structural_artifact_binding_mismatch",
            "embedded artifact identity differs from archive manifest"));
      std::size_t expectedIndex = 0U;
      std::uint64_t decodedBytes = 0U;
      std::string decodedArtifact;
      decodedArtifact.reserve(
          artifact.at("byte_length").get<std::size_t>());
      for (const auto &referenceJson : artifact.at("chunks")) {
        const auto reference = reference_from_json(referenceJson);
        if (!reference || !referencedChunks.insert(reference->object_hash).second ||
            !suppliedChunks.contains(reference->object_hash) ||
            suppliedChunks.at(reference->object_hash)->reference != *reference)
          return Result<detail::Unit>::failure(detail::store_diagnostic(
              "structural_chunk_reference_mismatch",
              "structural chunk reference is missing, duplicated, or changed"));
        const auto chunk =
            Json::parse(suppliedChunks.at(reference->object_hash)->bytes);
        if (!exact_keys(chunk,
                        {"$schema", "schema_version", "encoding",
                         "artifact_sha256", "chunk_index", "chunk_count",
                         "byte_offset", "decoded_length", "data"}) ||
            !string_equals(chunk, "$schema",
                           structural_artifact_chunk_schema_id) ||
            !string_equals(chunk, "schema_version", "1.0.0") ||
            !string_equals(chunk, "encoding", "base64") ||
            chunk.at("artifact_sha256") != artifact.at("sha256") ||
            chunk.at("chunk_index").get<std::size_t>() != expectedIndex ||
            chunk.at("chunk_count").get<std::size_t>() !=
                artifact.at("chunks").size() ||
            chunk.at("byte_offset").get<std::uint64_t>() != decodedBytes)
          return Result<detail::Unit>::failure(detail::store_diagnostic(
              "structural_chunk_binding_mismatch",
              "structural chunk metadata does not form the declared artifact"));
        const auto decoded =
            decode_base64(chunk.at("data").get<std::string>());
        if (decoded.size() !=
            chunk.at("decoded_length").get<std::size_t>())
          return Result<detail::Unit>::failure(detail::store_diagnostic(
              "structural_chunk_decoding_mismatch",
              "decoded structural chunk length differs from its metadata"));
        decodedBytes += decoded.size();
        decodedArtifact += decoded;
        ++expectedIndex;
      }
      if (decodedBytes != artifact.at("byte_length").get<std::uint64_t>() ||
          prometheus::integrity::sha256_bytes(decodedArtifact) !=
              artifact.at("sha256").get<std::string>())
        return Result<detail::Unit>::failure(detail::store_diagnostic(
            "structural_chunk_identity_mismatch",
            "structural chunks do not reconstruct the declared artifact"));
    }
    if (referencedChunks.size() != suppliedChunks.size())
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "structural_chunk_set_invalid",
          "unreferenced structural chunks were supplied"));
    return Result<detail::Unit>::success(detail::Unit{});
  } catch (const std::exception &failure) {
    return Result<detail::Unit>::failure(detail::store_diagnostic(
        "structural_project_graph_invalid", failure.what()));
  }
}

std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm broken_down{};
#ifdef _WIN32
  static_cast<void>(::gmtime_s(&broken_down, &time));
#else
  static_cast<void>(::gmtime_r(&time, &broken_down));
#endif
  std::ostringstream output;
  output << std::put_time(&broken_down, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

Result<detail::Unit> append_event(ProjectV2 &project, std::string event_kind,
                                  std::string status,
                                  const std::string &related_hash) {
  std::uint64_t sequence = 1U;
  if (!project.execution.events.empty()) {
    if (project.execution.events.back().sequence >= maximum_safe_integer) {
      return Result<detail::Unit>::failure(detail::store_diagnostic(
          "event_sequence_exhausted",
          "event sequence reached the interoperable integer limit"));
    }
    sequence = project.execution.events.back().sequence + 1U;
  }
  if (project.execution.events.size() == maximum_events) {
    project.execution.events.erase(project.execution.events.begin());
  }
  project.execution.events.push_back(Event{sequence, std::move(event_kind),
                                           std::move(status), related_hash,
                                           utc_now(), "none"});
  return Result<detail::Unit>::success(detail::Unit{});
}

} // namespace

Result<ProjectV2> create_project_v2(const std::filesystem::path &project_path,
                                    const ProjectV2 &initial_project,
                                    TransactionOptions options) noexcept {
  try {
    if (const auto cancelled = detail::check_cancelled(options);
        cancelled.has_value()) {
      return Result<ProjectV2>::failure(*cancelled);
    }
    if (!initial_project.execution.package_bindings.empty() ||
        initial_project.execution.current_scenario.has_value() ||
        !initial_project.execution.committed_runs.empty()) {
      return Result<ProjectV2>::failure(detail::store_diagnostic(
          "initial_execution_references_not_empty",
          "a new project cannot cite execution objects before installation"));
    }
    const auto serialized = serialize_project_v2(initial_project);
    if (!serialized.has_value()) {
      return Result<ProjectV2>::failure(
          normalized(serialized.diagnostic(), project_path));
    }
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, true, options.lock_timeout);
    if (!lock.has_value()) {
      return failure_from<ProjectV2>(lock.diagnostic());
    }
    return persist_project(project_path, initial_project, false, options);
  } catch (const std::exception &failure) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_create_failed", failure.what(), std::nullopt, project_path));
  } catch (...) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_create_failed", "unknown project creation failure",
        std::nullopt, project_path));
  }
}

Result<ProjectV2> open_project_index_read_only(
    const std::filesystem::path &project_path) noexcept {
  try {
    const auto bytes = detail::read_project_index_file(project_path);
    if (!bytes.has_value()) {
      return failure_from<ProjectV2>(bytes.diagnostic());
    }
    return parse_stored_project(bytes.value(), project_path);
  } catch (const std::exception &failure) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_open_failed", failure.what(), std::nullopt, project_path));
  } catch (...) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_open_failed", "unknown project-index open failure",
        std::nullopt, project_path));
  }
}

Result<ProjectV2>
save_project_snapshot(const std::filesystem::path &project_path,
                      const ProjectV2 &snapshot,
                      TransactionOptions options) noexcept {
  try {
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) {
      return failure_from<ProjectV2>(lock.diagnostic());
    }
    if (const auto cancelled = detail::check_cancelled(options);
        cancelled.has_value()) {
      return Result<ProjectV2>::failure(*cancelled);
    }
    auto stored = read_locked_project(project_path);
    if (!stored.has_value()) {
      return stored;
    }
    auto project = std::move(stored.value());
    project.name = snapshot.name;
    project.cad_source = snapshot.cad_source;
    project.assembly_artifact_hash = snapshot.assembly_artifact_hash;
    project.coordinate_system = snapshot.coordinate_system;
    project.length_unit = snapshot.length_unit;
    project.component_bindings = snapshot.component_bindings;
    project.placement_overrides = snapshot.placement_overrides;
    project.connections = snapshot.connections;
    project.interference_classifications =
        snapshot.interference_classifications;
    project.engineering = snapshot.engineering;
    return persist_project(project_path, project, true, options);
  } catch (const std::exception &failure) {
    return Result<ProjectV2>::failure(
        detail::store_diagnostic("project_snapshot_save_failed", failure.what(),
                                 std::nullopt, project_path));
  } catch (...) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_snapshot_save_failed", "unknown project snapshot save failure",
        std::nullopt, project_path));
  }
}

Result<ProjectV2> install_package_binding(
    const std::filesystem::path &project_path, std::string cad_entity_id,
    const StoredObjectReference &package_reference,
    const std::string_view package_bytes, TransactionOptions options) noexcept {
  try {
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) {
      return failure_from<ProjectV2>(lock.diagnostic());
    }
    auto project_result = read_locked_project(project_path);
    if (!project_result.has_value()) {
      return project_result;
    }
    auto project = std::move(project_result.value());
    if (project.execution.package_bindings.size() >= maximum_package_bindings) {
      return Result<ProjectV2>::failure(detail::store_diagnostic(
          "package_binding_limit_exceeded",
          "project already has the maximum package-binding revisions"));
    }
    std::optional<std::uint64_t> supersedes;
    std::unordered_set<std::uint64_t> superseded;
    for (const auto &binding : project.execution.package_bindings) {
      if (binding.supersedes_binding_revision.has_value()) {
        superseded.insert(*binding.supersedes_binding_revision);
      }
    }
    for (auto iterator = project.execution.package_bindings.rbegin();
         iterator != project.execution.package_bindings.rend(); ++iterator) {
      if (iterator->cad_entity_id == cad_entity_id &&
          !superseded.contains(iterator->binding_revision)) {
        supersedes = iterator->binding_revision;
        break;
      }
    }
    std::uint64_t revision = 1U;
    if (!project.execution.package_bindings.empty()) {
      const auto previous =
          project.execution.package_bindings.back().binding_revision;
      if (previous >= maximum_safe_integer) {
        return Result<ProjectV2>::failure(detail::store_diagnostic(
            "binding_revision_exhausted", "package-binding revision reached "
                                          "the interoperable integer limit"));
      }
      revision = previous + 1U;
    }
    project.execution.package_bindings.push_back(PackageBinding{
        revision, supersedes, std::move(cad_entity_id), package_reference});
    const auto candidate = serialize_project_v2(project);
    if (!candidate.has_value()) {
      return Result<ProjectV2>::failure(
          normalized(candidate.diagnostic(), project_path));
    }
    const auto installed = detail::install_object_file(
        project_path, package_reference, package_bytes, options);
    if (!installed.has_value()) {
      return failure_from<ProjectV2>(installed.diagnostic());
    }
    return persist_project(project_path, project, true, options);
  } catch (const std::exception &failure) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "package_binding_failed", failure.what(), std::nullopt, project_path));
  } catch (...) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "package_binding_failed", "unknown package-binding failure",
        std::nullopt, project_path));
  }
}

Result<ProjectV2>
set_current_scenario(const std::filesystem::path &project_path,
                     const StoredObjectReference &scenario_reference,
                     const std::string_view scenario_bytes,
                     TransactionOptions options) noexcept {
  try {
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) {
      return failure_from<ProjectV2>(lock.diagnostic());
    }
    auto project_result = read_locked_project(project_path);
    if (!project_result.has_value()) {
      return project_result;
    }
    auto project = std::move(project_result.value());
    project.execution.current_scenario = scenario_reference;
    const auto candidate = serialize_project_v2(project);
    if (!candidate.has_value()) {
      return Result<ProjectV2>::failure(
          normalized(candidate.diagnostic(), project_path));
    }
    const auto installed = detail::install_object_file(
        project_path, scenario_reference, scenario_bytes, options);
    if (!installed.has_value()) {
      return failure_from<ProjectV2>(installed.diagnostic());
    }
    return persist_project(project_path, project, true, options);
  } catch (const std::exception &failure) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "scenario_update_failed", failure.what(), std::nullopt, project_path));
  } catch (...) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "scenario_update_failed", "unknown scenario update failure",
        std::nullopt, project_path));
  }
}

Result<Publication>
publish_completed_run(const std::filesystem::path &project_path,
                      const CompletedRunObjects &objects,
                      TransactionOptions options) noexcept {
  try {
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) {
      return failure_from<Publication>(lock.diagnostic());
    }
    auto project_result = read_locked_project(project_path);
    if (!project_result.has_value()) {
      return failure_from<Publication>(project_result.diagnostic());
    }
    auto project = std::move(project_result.value());
    if (const auto cancelled = detail::check_cancelled(options);
        cancelled.has_value()) {
      return Result<Publication>::failure(*cancelled);
    }
    bool already_committed = false;
    for (const auto &reference : project.execution.committed_runs) {
      if (reference.object_hash == objects.manifest.reference.object_hash) {
        if (reference != objects.manifest.reference) {
          return Result<Publication>::failure(detail::store_diagnostic(
              "committed_run_reference_conflict",
              "manifest hash is already committed with different metadata"));
        }
        already_committed = true;
      }
    }
    const auto graph =
        validate_publication_graph(project, objects, !already_committed);
    if (!graph.has_value()) {
      return failure_from<Publication>(graph.diagnostic());
    }
    for (const auto *object :
         {&objects.package, &objects.scenario, &objects.request,
          &objects.result, &objects.manifest}) {
      const auto installed = detail::install_object_file(
          project_path, object->reference, object->bytes, options);
      if (!installed.has_value()) {
        return failure_from<Publication>(installed.diagnostic());
      }
    }

    if (!already_committed) {
      const auto snapshotted = install_execution_project_snapshot(
          project_path, project, "motor_analysis",
          objects.manifest.reference.object_hash, options);
      if (!snapshotted.has_value())
        return failure_from<Publication>(snapshotted.diagnostic());
      auto snapshotReference = std::move(project.execution.committed_runs.back());
      project.execution.committed_runs.pop_back();
      project.execution.committed_runs.push_back(objects.manifest.reference);
      project.execution.committed_runs.push_back(std::move(snapshotReference));
    }
    const auto event = append_event(
        project, already_committed ? "run_invoked" : "run_published",
        already_committed ? "already_committed" : "completed",
        objects.manifest.reference.object_hash);
    if (!event.has_value()) {
      return failure_from<Publication>(event.diagnostic());
    }
    const auto persisted =
        persist_project(project_path, project, true, options);
    if (!persisted.has_value()) {
      return failure_from<Publication>(persisted.diagnostic());
    }
    return Result<Publication>::success(
        Publication{persisted.value(), already_committed});
  } catch (const std::exception &failure) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "run_publication_failed", failure.what(), std::nullopt, project_path));
  } catch (...) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "run_publication_failed", "unknown run publication failure",
        std::nullopt, project_path));
  }
}

Result<ProjectV2> recover_previous_project_index(
    const std::filesystem::path &project_path,
    TransactionOptions options) noexcept {
  try {
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) return failure_from<ProjectV2>(lock.diagnostic());
    if (const auto cancelled = detail::check_cancelled(options);
        cancelled.has_value())
      return Result<ProjectV2>::failure(*cancelled);
    const auto current = detail::read_project_index_file(project_path);
    if (current.has_value() &&
        parse_stored_project(current.value(), project_path).has_value())
      return Result<ProjectV2>::failure(detail::store_diagnostic(
          "project_recovery_not_required",
          "current project index is valid and cannot be rolled back",
          std::nullopt, project_path));
    const auto previous =
        detail::read_previous_project_index_file(project_path);
    if (!previous.has_value())
      return failure_from<ProjectV2>(previous.diagnostic());
    auto recovered = parse_stored_project(previous.value(), project_path);
    if (!recovered.has_value())
      return Result<ProjectV2>::failure(detail::store_diagnostic(
          "previous_project_index_invalid",
          "retained previous project index is invalid", std::nullopt,
          project_path));
    const auto written = detail::replace_project_index_file(
        project_path, previous.value(), true, options, false);
    if (!written.has_value())
      return failure_from<ProjectV2>(written.diagnostic());
    return recovered;
  } catch (const std::exception &failure) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_recovery_failed", failure.what(), std::nullopt,
        project_path));
  } catch (...) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_recovery_failed", "unknown project recovery failure",
        std::nullopt, project_path));
  }
}

Result<Publication> commit_structural_archive_manifest(
    const std::filesystem::path &project_path, const ObjectToStore &manifest,
    TransactionOptions options) noexcept {
  try {
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) return failure_from<Publication>(lock.diagnostic());
    auto projectResult = read_locked_project(project_path);
    if (!projectResult.has_value())
      return failure_from<Publication>(projectResult.diagnostic());
    if (const auto cancelled = detail::check_cancelled(options); cancelled)
      return Result<Publication>::failure(*cancelled);
    const auto valid = validate_structural_manifest(manifest);
    if (!valid.has_value()) return failure_from<Publication>(valid.diagnostic());

    auto project = std::move(projectResult.value());
    bool alreadyCommitted = false;
    for (const auto &reference : project.execution.committed_runs) {
      if (reference.object_hash != manifest.reference.object_hash) continue;
      if (reference != manifest.reference)
        return Result<Publication>::failure(detail::store_diagnostic(
            "committed_run_reference_conflict",
            "manifest hash is already committed with different metadata"));
      alreadyCommitted = true;
    }
    const auto installed = detail::install_object_file(
        project_path, manifest.reference, manifest.bytes, options);
    if (!installed.has_value())
      return failure_from<Publication>(installed.diagnostic());
    if (!alreadyCommitted) {
      if (project.execution.committed_runs.size() >= maximum_committed_runs)
        return Result<Publication>::failure(detail::store_diagnostic(
            "committed_run_limit_exceeded",
            "project already has the maximum committed runs"));
      project.execution.committed_runs.push_back(manifest.reference);
    }
    const auto event = append_event(
        project,
        alreadyCommitted ? "structural_run_invoked" : "structural_run_anchored",
        alreadyCommitted ? "already_committed" : "completed",
        manifest.reference.object_hash);
    if (!event.has_value()) return failure_from<Publication>(event.diagnostic());
    const auto persisted = persist_project(project_path, project, true, options);
    if (!persisted.has_value())
      return failure_from<Publication>(persisted.diagnostic());
    return Result<Publication>::success(
        Publication{persisted.value(), alreadyCommitted});
  } catch (const std::exception &failure) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "structural_manifest_commit_failed", failure.what(), std::nullopt,
        project_path));
  } catch (...) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "structural_manifest_commit_failed", "unknown structural commit failure",
        std::nullopt, project_path));
  }
}

Result<Publication> commit_project_inventory_snapshot(
    const std::filesystem::path &project_path, const ObjectToStore &snapshot,
    TransactionOptions options) noexcept {
  try {
    if (snapshot.reference.media_type != project_inventory_media_type ||
        snapshot.reference.schema_id != project_inventory_schema_id ||
        snapshot.reference.schema_version != "1.0.0")
      return Result<Publication>::failure(detail::store_diagnostic(
          "inventory_reference_invalid",
          "inventory snapshot has unsupported registered metadata"));
    const auto canonical = integrity::verify_canonical_bytes(snapshot.bytes);
    const auto document = Json::parse(canonical);
    if (!exact_keys(document, {"$schema", "schema_version", "snapshot_kind",
                               "root_label", "artifacts"}) ||
        !string_equals(document, "$schema", project_inventory_schema_id) ||
        !string_equals(document, "schema_version", "1.0.0") ||
        !string_equals(document, "snapshot_kind", "accounted_project_folder") ||
        !document.at("root_label").is_string() ||
        document.at("root_label").get_ref<const std::string &>().size() > 512U ||
        !document.at("artifacts").is_array() ||
        document.at("artifacts").size() > 100000U)
      return Result<Publication>::failure(detail::store_diagnostic(
          "inventory_snapshot_invalid", "inventory snapshot contract is invalid"));
    std::string previousPath;
    for (const auto &artifact : document.at("artifacts")) {
      if (!exact_keys(artifact, {"relative_path", "byte_length", "sha256",
                                 "category", "analysis_state", "detail"}) ||
          !artifact.at("relative_path").is_string() ||
          !artifact.at("byte_length").is_number_unsigned() ||
          !(artifact.at("sha256").is_null() || artifact.at("sha256").is_string()) ||
          !artifact.at("category").is_string() ||
          !artifact.at("analysis_state").is_string() ||
          !artifact.at("detail").is_string())
        return Result<Publication>::failure(detail::store_diagnostic(
            "inventory_artifact_invalid", "inventory artifact record is invalid"));
      const auto path = artifact.at("relative_path").get<std::string>();
      if (path.size() > 4096U ||
          artifact.at("category").get_ref<const std::string &>().size() > 128U ||
          artifact.at("analysis_state").get_ref<const std::string &>().size() >
              128U ||
          artifact.at("detail").get_ref<const std::string &>().size() > 4096U)
        return Result<Publication>::failure(detail::store_diagnostic(
            "inventory_artifact_invalid", "inventory artifact text is too large"));
      const auto relative = std::filesystem::path(path);
      if (path.empty() || relative.is_absolute() || relative.has_root_path() ||
          relative != relative.lexically_normal() ||
          (!previousPath.empty() && path <= previousPath))
        return Result<Publication>::failure(detail::store_diagnostic(
            "inventory_path_invalid",
            "inventory paths must be safe, unique, and strictly sorted"));
      for (const auto &component : relative)
        if (component == "." || component == "..")
          return Result<Publication>::failure(detail::store_diagnostic(
              "inventory_path_invalid", "inventory path traversal is forbidden"));
      if (!artifact.at("sha256").is_null() &&
          !is_valid_object_hash(artifact.at("sha256").get<std::string>()))
        return Result<Publication>::failure(detail::store_diagnostic(
            "inventory_hash_invalid", "inventory file identity is invalid"));
      previousPath = path;
    }

    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) return failure_from<Publication>(lock.diagnostic());
    auto loaded = read_locked_project(project_path);
    if (!loaded.has_value()) return failure_from<Publication>(loaded.diagnostic());
    auto project = std::move(loaded.value());
    const auto found = std::find(project.execution.committed_runs.begin(),
                                 project.execution.committed_runs.end(),
                                 snapshot.reference);
    const bool alreadyCommitted = found != project.execution.committed_runs.end();
    const auto installed = detail::install_object_file(
        project_path, snapshot.reference, snapshot.bytes, options);
    if (!installed.has_value()) return failure_from<Publication>(installed.diagnostic());
    if (!alreadyCommitted) {
      if (project.execution.committed_runs.size() >= maximum_committed_runs)
        return Result<Publication>::failure(detail::store_diagnostic(
            "committed_run_limit_exceeded", "project history is full"));
      project.execution.committed_runs.push_back(snapshot.reference);
    }
    const auto event = append_event(
        project, alreadyCommitted ? "inventory_snapshot_reused" :
                                    "inventory_snapshot_anchored",
        alreadyCommitted ? "already_committed" : "completed",
        snapshot.reference.object_hash);
    if (!event.has_value()) return failure_from<Publication>(event.diagnostic());
    const auto persisted = persist_project(project_path, project, true, options);
    if (!persisted.has_value()) return failure_from<Publication>(persisted.diagnostic());
    return Result<Publication>::success({persisted.value(), alreadyCommitted});
  } catch (const integrity::CanonicalJsonError &failure) {
    return Result<Publication>::failure(detail::store_diagnostic(
        failure.code(), failure.what(), std::nullopt, project_path));
  } catch (const std::exception &failure) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "inventory_snapshot_commit_failed", failure.what(), std::nullopt,
        project_path));
  } catch (...) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "inventory_snapshot_commit_failed", "unknown inventory commit failure",
        std::nullopt, project_path));
  }
}

Result<Publication> publish_project_inventory_archive(
    const std::filesystem::path &projectPath,
    const ObjectToStore &inventorySnapshot,
    const ProjectEvidenceArchiveObjects &archive,
    TransactionOptions options) noexcept {
  try {
    const auto valid = validate_project_evidence_archive(inventorySnapshot, archive);
    if (!valid.has_value()) return failure_from<Publication>(valid.diagnostic());
    auto lock = detail::acquire_project_lock(
        projectPath, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) return failure_from<Publication>(lock.diagnostic());
    auto loaded = read_locked_project(projectPath);
    if (!loaded.has_value()) return failure_from<Publication>(loaded.diagnostic());
    auto project = std::move(loaded.value());
    const auto contains = [&](const StoredObjectReference &reference) {
      return std::ranges::find(project.execution.committed_runs, reference) !=
             project.execution.committed_runs.end();
    };
    const bool snapshotPresent = contains(inventorySnapshot.reference);
    const bool archivePresent = contains(archive.manifest.reference);
    if (project.execution.committed_runs.size() +
            (snapshotPresent ? 0U : 1U) + (archivePresent ? 0U : 1U) >
        maximum_committed_runs)
      return Result<Publication>::failure(detail::store_diagnostic(
          "committed_run_limit_exceeded", "project evidence history is full"));
    for (const auto &chunk : archive.chunks) {
      const auto installed = detail::install_object_file(
          projectPath, chunk.reference, chunk.bytes, options);
      if (!installed.has_value())
        return failure_from<Publication>(installed.diagnostic());
    }
    for (const auto *object : {&inventorySnapshot, &archive.manifest}) {
      const auto installed = detail::install_object_file(
          projectPath, object->reference, object->bytes, options);
      if (!installed.has_value())
        return failure_from<Publication>(installed.diagnostic());
    }
    if (!snapshotPresent)
      project.execution.committed_runs.push_back(inventorySnapshot.reference);
    if (!archivePresent)
      project.execution.committed_runs.push_back(archive.manifest.reference);
    const auto event = append_event(
        project, archivePresent ? "project_evidence_archive_reused" :
                                  "project_evidence_archive_published",
        archivePresent ? "already_committed" : "completed",
        archive.manifest.reference.object_hash);
    if (!event.has_value()) return failure_from<Publication>(event.diagnostic());
    const auto persisted = persist_project(projectPath, project, true, options);
    if (!persisted.has_value()) return failure_from<Publication>(persisted.diagnostic());
    return Result<Publication>::success(
        {persisted.value(), snapshotPresent && archivePresent});
  } catch (const std::exception &error) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "project_evidence_publication_failed", error.what(), std::nullopt,
        projectPath));
  }
}

Result<Publication> publish_structural_archive(
    const std::filesystem::path &project_path,
    const StructuralArchiveObjects &objects,
    TransactionOptions options) noexcept {
  try {
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::exclusive, false, options.lock_timeout);
    if (!lock.has_value()) return failure_from<Publication>(lock.diagnostic());
    auto projectResult = read_locked_project(project_path);
    if (!projectResult.has_value())
      return failure_from<Publication>(projectResult.diagnostic());
    if (const auto cancelled = detail::check_cancelled(options); cancelled)
      return Result<Publication>::failure(*cancelled);
    auto project = std::move(projectResult.value());
    bool alreadyCommitted = false;
    for (const auto &reference : project.execution.committed_runs) {
      if (reference.object_hash != objects.project_manifest.reference.object_hash)
        continue;
      if (reference != objects.project_manifest.reference)
        return Result<Publication>::failure(detail::store_diagnostic(
            "committed_run_reference_conflict",
            "structural project manifest hash has conflicting metadata"));
      alreadyCommitted = true;
    }
    const auto graph = validate_embedded_structural_graph(
        project, objects, !alreadyCommitted);
    if (!graph.has_value()) return failure_from<Publication>(graph.diagnostic());
    const auto archiveInstalled = detail::install_object_file(
        project_path, objects.archive_manifest.reference,
        objects.archive_manifest.bytes, options);
    if (!archiveInstalled.has_value())
      return failure_from<Publication>(archiveInstalled.diagnostic());
    for (const auto &chunk : objects.chunks) {
      const auto installed = detail::install_object_file(
          project_path, chunk.reference, chunk.bytes, options);
      if (!installed.has_value())
        return failure_from<Publication>(installed.diagnostic());
    }
    const auto manifestInstalled = detail::install_object_file(
        project_path, objects.project_manifest.reference,
        objects.project_manifest.bytes, options);
    if (!manifestInstalled.has_value())
      return failure_from<Publication>(manifestInstalled.diagnostic());
    if (!alreadyCommitted) {
      const auto snapshotted = install_execution_project_snapshot(
          project_path, project, "structural_linear_static",
          objects.project_manifest.reference.object_hash, options);
      if (!snapshotted.has_value())
        return failure_from<Publication>(snapshotted.diagnostic());
      auto snapshotReference = std::move(project.execution.committed_runs.back());
      project.execution.committed_runs.pop_back();
      project.execution.committed_runs.push_back(objects.project_manifest.reference);
      project.execution.committed_runs.push_back(std::move(snapshotReference));
    }
    const auto event = append_event(
        project,
        alreadyCommitted ? "structural_run_invoked" : "structural_run_published",
        alreadyCommitted ? "already_committed" : "completed",
        objects.project_manifest.reference.object_hash);
    if (!event.has_value()) return failure_from<Publication>(event.diagnostic());
    const auto persisted = persist_project(project_path, project, true, options);
    if (!persisted.has_value())
      return failure_from<Publication>(persisted.diagnostic());
    return Result<Publication>::success(
        Publication{persisted.value(), alreadyCommitted});
  } catch (const std::exception &failure) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "structural_archive_publication_failed", failure.what(), std::nullopt,
        project_path));
  } catch (...) {
    return Result<Publication>::failure(detail::store_diagnostic(
        "structural_archive_publication_failed",
        "unknown structural archive publication failure", std::nullopt,
        project_path));
  }
}

Result<ProjectV2> open_read_only(const std::filesystem::path &project_path,
                                 TransactionOptions options) noexcept {
  try {
    auto lock = detail::acquire_project_lock(
        project_path, detail::LockMode::shared, false, options.lock_timeout);
    if (!lock.has_value()) {
      return failure_from<ProjectV2>(lock.diagnostic());
    }
    if (const auto cancelled = detail::check_cancelled(options);
        cancelled.has_value()) {
      return Result<ProjectV2>::failure(*cancelled);
    }
    return read_locked_project(project_path);
  } catch (const std::exception &failure) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_open_failed", failure.what(), std::nullopt, project_path));
  } catch (...) {
    return Result<ProjectV2>::failure(detail::store_diagnostic(
        "project_open_failed", "unknown project open failure", std::nullopt,
        project_path));
  }
}

} // namespace prometheus::run_store
