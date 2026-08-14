#include <prometheus/replay/replay.hpp>

#include <prometheus/execution/contracts.hpp>
#include <prometheus/execution/execute.hpp>
#include <prometheus/execution/numeric_profile.hpp>
#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/object_store.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace prometheus::replay {
namespace {

using Json = nlohmann::json;

constexpr std::string_view package_media_type =
    "application/vnd.prometheus.execution-component+json;version=2.0.0";
constexpr std::string_view package_schema_id =
    "urn:prometheus:schema:execution-component:2.0.0";
constexpr std::string_view schema_version = "1.0.0";

enum class ReferenceKind { package, scenario, request, result };

class ReplayError final : public std::runtime_error {
public:
  ReplayError(std::string stage, std::string code, std::string message)
      : std::runtime_error(std::move(message)), stage_(std::move(stage)),
        code_(std::move(code)) {}

  [[nodiscard]] const std::string &stage() const noexcept { return stage_; }
  [[nodiscard]] const std::string &code() const noexcept { return code_; }

private:
  std::string stage_;
  std::string code_;
};

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

[[noreturn]] void reject(std::string stage, std::string code,
                         std::string message) {
  throw ReplayError(bounded(std::move(stage), 128U),
                    bounded(std::move(code), 128U),
                    bounded(std::move(message), 4096U));
}

ReplayReport failed(const ReplayStatus status, const std::string_view hash,
                    std::string stage, std::string code,
                    std::string message,
                    std::optional<std::string> recorded = std::nullopt,
                    std::optional<std::string> replayed = std::nullopt) {
  return ReplayReport{
      status,
      bounded(std::string(hash), 128U),
      std::move(recorded),
      std::move(replayed),
      ReplayDiagnostic{bounded(std::move(stage), 128U),
                       bounded(std::move(code), 128U),
                       bounded(std::move(message), 4096U)},
  };
}

bool contains(const std::initializer_list<std::string_view> allowed,
              const std::string_view candidate) {
  return std::find(allowed.begin(), allowed.end(), candidate) != allowed.end();
}

std::filesystem::path native_path_from_utf8(const std::string_view value) {
  std::u8string encoded;
  encoded.reserve(value.size());
  for (const auto byte : value) {
    encoded.push_back(static_cast<char8_t>(
        static_cast<unsigned char>(byte)));
  }
  return std::filesystem::path(encoded);
}

void require_exact_keys(
    const Json &value, const std::initializer_list<std::string_view> keys,
    const std::string_view field) {
  if (!value.is_object()) {
    reject("manifest_verification", "invalid_type",
           std::string(field) + " must be an object");
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!contains(keys, iterator.key())) {
      reject("manifest_verification", "unknown_field",
             std::string(field) + " contains unknown field " +
                 iterator.key());
    }
  }
  for (const auto key : keys) {
    if (!value.contains(std::string(key))) {
      reject("manifest_verification", "missing_field",
             std::string(field) + " is missing " + std::string(key));
    }
  }
}

std::string require_string(const Json &value, const std::string_view key,
                           const std::string_view field) {
  if (!value.contains(std::string(key)) ||
      !value.at(std::string(key)).is_string()) {
    reject("manifest_verification", "invalid_type",
           std::string(field) + " must be a string");
  }
  auto result = value.at(std::string(key)).get<std::string>();
  if (result.empty() || result.size() > 4096U) {
    reject("manifest_verification", "invalid_value",
           std::string(field) + " is empty or over limit");
  }
  return result;
}

void require_contract(const run_store::StoredObjectReference &reference,
                      const ReferenceKind kind,
                      const std::string_view field) {
  std::string_view media_type;
  std::string_view schema_id;
  std::string_view version;
  switch (kind) {
  case ReferenceKind::package:
    media_type = package_media_type;
    schema_id = package_schema_id;
    version = "2.0.0";
    break;
  case ReferenceKind::scenario:
    media_type = execution::motor_arm_scenario_media_type;
    schema_id = execution::motor_arm_scenario_schema_id;
    version = schema_version;
    break;
  case ReferenceKind::request:
    media_type = execution::analysis_request_media_type;
    schema_id = execution::analysis_request_schema_id;
    version = schema_version;
    break;
  case ReferenceKind::result:
    media_type = execution::analysis_result_media_type;
    schema_id = execution::analysis_result_schema_id;
    version = schema_version;
    break;
  }
  if (!run_store::is_valid_object_hash(reference.object_hash)) {
    reject("manifest_verification", "invalid_hash",
           std::string(field) + " has an invalid object hash");
  }
  if (reference.byte_length > run_store::maximum_object_bytes) {
    reject("manifest_verification", "object_too_large",
           std::string(field) + " exceeds the object limit");
  }
  if (reference.media_type != media_type || reference.schema_id != schema_id ||
      reference.schema_version != version) {
    reject("manifest_verification", "unsupported_object_contract",
           std::string(field) + " has unsupported object metadata");
  }
}

run_store::StoredObjectReference
parse_reference(const Json &value, const ReferenceKind kind,
                const std::string_view field) {
  require_exact_keys(value,
                     {"object_hash", "byte_length", "media_type",
                      "schema_id", "schema_version"},
                     field);
  if (!value.at("byte_length").is_number_unsigned()) {
    reject("manifest_verification", "invalid_type",
           std::string(field) + ".byte_length must be unsigned");
  }
  run_store::StoredObjectReference reference{
      require_string(value, "object_hash",
                     std::string(field) + ".object_hash"),
      value.at("byte_length").get<std::uint64_t>(),
      require_string(value, "media_type",
                     std::string(field) + ".media_type"),
      require_string(value, "schema_id", std::string(field) + ".schema_id"),
      require_string(value, "schema_version",
                     std::string(field) + ".schema_version"),
  };
  require_contract(reference, kind, field);
  return reference;
}

execution::ToolIdentity parse_tool(const Json &value,
                                   const std::string_view field) {
  require_exact_keys(value, {"id", "version"}, field);
  return execution::ToolIdentity{
      require_string(value, "id", std::string(field) + ".id"),
      require_string(value, "version", std::string(field) + ".version")};
}

execution::NumericProfile parse_numeric_profile(const Json &value) {
  require_exact_keys(value,
                     {"operating_system", "compiler", "standard_library",
                      "math_runtime", "backend_build_fingerprint",
                      "floating_point", "numeric_serialization_version"},
                     "numeric_profile");
  const auto &system = value.at("operating_system");
  require_exact_keys(system, {"name", "release", "architecture"},
                     "numeric_profile.operating_system");
  const auto &floating = value.at("floating_point");
  require_exact_keys(floating, {"contraction", "fast_math", "rounding_mode"},
                     "numeric_profile.floating_point");
  if (!floating.at("fast_math").is_boolean()) {
    reject("manifest_verification", "invalid_type",
           "numeric_profile.floating_point.fast_math must be boolean");
  }
  auto fingerprint = require_string(value, "backend_build_fingerprint",
                                    "numeric_profile.backend_build_fingerprint");
  if (!run_store::is_valid_object_hash(fingerprint)) {
    reject("manifest_verification", "invalid_hash",
           "numeric profile backend fingerprint is invalid");
  }
  return execution::NumericProfile{
      execution::PlatformIdentity{
          require_string(system, "name", "numeric_profile.operating_system.name"),
          require_string(system, "release",
                         "numeric_profile.operating_system.release"),
          require_string(system, "architecture",
                         "numeric_profile.operating_system.architecture")},
      parse_tool(value.at("compiler"), "numeric_profile.compiler"),
      parse_tool(value.at("standard_library"),
                 "numeric_profile.standard_library"),
      parse_tool(value.at("math_runtime"), "numeric_profile.math_runtime"),
      std::move(fingerprint),
      execution::FloatingPointPolicy{
          require_string(floating, "contraction",
                         "numeric_profile.floating_point.contraction"),
          floating.at("fast_math").get<bool>(),
          require_string(floating, "rounding_mode",
                         "numeric_profile.floating_point.rounding_mode")},
      require_string(value, "numeric_serialization_version",
                     "numeric_profile.numeric_serialization_version"),
  };
}

struct Manifest final {
  run_store::StoredObjectReference package;
  run_store::StoredObjectReference scenario;
  run_store::StoredObjectReference request;
  run_store::StoredObjectReference result;
  std::string assembly_artifact_hash;
  std::string backend_id;
  std::string backend_contract_version;
  std::string package_consumer_contract_hash;
  execution::NumericProfile numeric_profile;
};

Manifest parse_manifest(const std::string_view bytes) {
  const auto root = Json::parse(bytes);
  require_exact_keys(
      root,
      {"$schema", "schema_version", "manifest_kind", "package", "scenario",
       "request", "result", "assembly_artifact_hash", "backend_id",
       "backend_contract_version", "package_consumer_contract_hash",
       "numeric_profile"},
      "manifest");
  if (require_string(root, "$schema", "manifest.$schema") !=
          execution::run_manifest_schema_id ||
      require_string(root, "schema_version", "manifest.schema_version") !=
          schema_version ||
      require_string(root, "manifest_kind", "manifest.manifest_kind") !=
          "completed_analysis_run") {
    reject("manifest_verification", "unsupported_manifest_contract",
           "manifest identity or kind is unsupported");
  }
  auto assembly = require_string(root, "assembly_artifact_hash",
                                 "manifest.assembly_artifact_hash");
  auto consumer = require_string(root, "package_consumer_contract_hash",
                                 "manifest.package_consumer_contract_hash");
  if (!run_store::is_valid_object_hash(assembly) ||
      !run_store::is_valid_object_hash(consumer)) {
    reject("manifest_verification", "invalid_hash",
           "manifest assembly or consumer hash is invalid");
  }
  return Manifest{
      parse_reference(root.at("package"), ReferenceKind::package,
                      "manifest.package"),
      parse_reference(root.at("scenario"), ReferenceKind::scenario,
                      "manifest.scenario"),
      parse_reference(root.at("request"), ReferenceKind::request,
                      "manifest.request"),
      parse_reference(root.at("result"), ReferenceKind::result,
                      "manifest.result"),
      std::move(assembly),
      require_string(root, "backend_id", "manifest.backend_id"),
      require_string(root, "backend_contract_version",
                     "manifest.backend_contract_version"),
      std::move(consumer),
      parse_numeric_profile(root.at("numeric_profile")),
  };
}

std::string
read_verified_object(const std::filesystem::path &project_path,
                     const run_store::StoredObjectReference &reference) {
  auto result = run_store::read_object(project_path, reference);
  if (!result.has_value()) {
    reject(result.diagnostic().stage, result.diagnostic().code,
           result.diagnostic().message);
  }
  return std::move(result.value());
}

void verify_external_assembly(const std::filesystem::path &project_path,
                              const run_store::ProjectV2 &project,
                              const std::string_view recorded_hash) {
  if (project.assembly_artifact_hash != recorded_hash) {
    reject("assembly_verification", "assembly_reference_mismatch",
           "project and run manifest cite different assembly snapshots");
  }
  const auto stored = native_path_from_utf8(project.cad_source);
  if (stored.empty() || stored != stored.lexically_normal()) {
    reject("assembly_verification", "unsafe_cad_source",
           "CAD source must be a normalized path");
  }
  for (const auto &component : stored.relative_path()) {
    if (component.empty() || component == "." || component == "..") {
      reject("assembly_verification", "unsafe_cad_source",
             "CAD source contains an unsafe path component");
    }
  }
  auto parent = project_path.parent_path();
  if (parent.empty()) {
    parent = ".";
  }
  const auto anchored = stored.is_absolute() ? stored : parent / stored;
  const auto components =
      stored.is_absolute() ? std::filesystem::path(stored.filename()) : stored;
  auto current = stored.is_absolute() ? stored.parent_path() : parent;
  for (auto iterator = components.begin(); iterator != components.end();
       ++iterator) {
    current /= *iterator;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(current, error);
    if (error || status.type() == std::filesystem::file_type::not_found) {
      reject("assembly_verification", "assembly_missing",
             "recorded external CAD artifact is missing");
    }
    if (std::filesystem::is_symlink(status)) {
      reject("assembly_verification", "unsafe_assembly_path",
             "external CAD path contains a symbolic link");
    }
    const auto final = std::next(iterator) == components.end();
    if ((!final && !std::filesystem::is_directory(status)) ||
        (final && !std::filesystem::is_regular_file(status))) {
      reject("assembly_verification", "unsafe_assembly_path",
             "external CAD path is not a regular anchored file");
    }
  }
  std::string actual_hash;
  try {
    actual_hash = integrity::sha256_file(anchored);
  } catch (const integrity::CanonicalJsonError &failure) {
    reject("assembly_verification", failure.code(), failure.what());
  }
  if (actual_hash != recorded_hash) {
    reject("assembly_verification", "assembly_hash_mismatch",
           "external CAD bytes differ from the recorded assembly snapshot");
  }
}

void verify_request_graph(const std::string_view bytes,
                          const Manifest &manifest) {
  const auto request = Json::parse(bytes);
  require_exact_keys(
      request,
      {"$schema", "schema_version", "request_kind", "package_hash",
       "scenario_hash", "assembly_artifact_hash", "bound_cad_entity_id",
       "backend_id", "backend_contract_version",
       "package_consumer_contract_hash", "obligation_ids"},
      "request");
  const auto package_hash =
      require_string(request, "package_hash", "request.package_hash");
  const auto scenario_hash =
      require_string(request, "scenario_hash", "request.scenario_hash");
  const auto assembly_hash = require_string(
      request, "assembly_artifact_hash", "request.assembly_artifact_hash");
  const auto backend_id =
      require_string(request, "backend_id", "request.backend_id");
  const auto backend_version = require_string(
      request, "backend_contract_version", "request.backend_contract_version");
  const auto consumer_hash = require_string(
      request, "package_consumer_contract_hash",
      "request.package_consumer_contract_hash");
  if (require_string(request, "$schema", "request.$schema") !=
          execution::analysis_request_schema_id ||
      require_string(request, "schema_version", "request.schema_version") !=
          schema_version ||
      require_string(request, "request_kind", "request.request_kind") !=
          "motor_arm_analysis" ||
      !run_store::is_valid_object_hash(package_hash) ||
      !run_store::is_valid_object_hash(scenario_hash) ||
      !run_store::is_valid_object_hash(assembly_hash) ||
      !run_store::is_valid_object_hash(consumer_hash) ||
      package_hash != manifest.package.object_hash ||
      scenario_hash != manifest.scenario.object_hash ||
      assembly_hash != manifest.assembly_artifact_hash ||
      backend_id != manifest.backend_id ||
      backend_version != manifest.backend_contract_version ||
      consumer_hash != manifest.package_consumer_contract_hash) {
    reject("reference_verification", "manifest_request_mismatch",
           "manifest and request bindings disagree");
  }
  const auto entity = require_string(request, "bound_cad_entity_id",
                                     "request.bound_cad_entity_id");
  if (entity.find_first_not_of(" \t\r\n") == std::string::npos) {
    reject("reference_verification", "invalid_bound_entity",
           "request bound CAD entity is empty");
  }
  const auto &obligations = request.at("obligation_ids");
  if (!obligations.is_array() ||
      obligations.size() != execution::motor_arm_obligation_ids.size()) {
    reject("reference_verification", "unsupported_obligations",
           "request obligation set is unsupported");
  }
  for (std::size_t index = 0U; index < obligations.size(); ++index) {
    if (!obligations[index].is_string() ||
        obligations[index].get<std::string_view>() !=
            execution::motor_arm_obligation_ids[index]) {
      reject("reference_verification", "unsupported_obligations",
             "request obligations are not in the authoritative order");
    }
  }
}

void verify_recorded_result(const std::string_view bytes,
                            const Manifest &manifest) {
  const auto root = Json::parse(bytes);
  require_exact_keys(
      root,
      {"$schema", "schema_version", "execution_disposition", "request_hash",
       "package_hash", "backend", "calculations", "consumed_inputs",
       "sensitivities", "obligation_outcomes", "missing_information",
       "assumptions", "limitations", "applicability", "coverage"},
      "result");
  if (require_string(root, "$schema", "result.$schema") !=
          execution::analysis_result_schema_id ||
      require_string(root, "schema_version", "result.schema_version") !=
          schema_version ||
      require_string(root, "execution_disposition",
                     "result.execution_disposition") != "completed" ||
      require_string(root, "request_hash", "result.request_hash") !=
          manifest.request.object_hash ||
      require_string(root, "package_hash", "result.package_hash") !=
          manifest.package.object_hash) {
    reject("result_verification", "result_binding_mismatch",
           "recorded result identity or input bindings disagree");
  }
  const auto &backend = root.at("backend");
  require_exact_keys(backend,
                     {"backend_id", "contract_version", "numeric_profile"},
                     "result.backend");
  if (require_string(backend, "backend_id", "result.backend.backend_id") !=
          manifest.backend_id ||
      require_string(backend, "contract_version",
                     "result.backend.contract_version") !=
          manifest.backend_contract_version ||
      parse_numeric_profile(backend.at("numeric_profile")) !=
          manifest.numeric_profile) {
    reject("result_verification", "result_backend_mismatch",
           "recorded result and manifest execution identities disagree");
  }
}

struct VerifiedRecordedRun final {
  Manifest manifest;
  std::string manifest_bytes;
  std::string package_bytes;
  std::string scenario_bytes;
  std::string request_bytes;
  std::string result_bytes;
};

VerifiedRecordedRun verify_recorded_run(
    const std::filesystem::path &project_path,
    const std::string_view manifest_hash,
    const run_store::TransactionOptions &options,
    const bool require_external_assembly) {
  if (!run_store::is_valid_object_hash(manifest_hash)) {
    reject("arguments", "invalid_manifest_hash",
           "manifest hash must use strict lowercase SHA-256 spelling");
  }
  const auto opened = run_store::open_read_only(project_path, options);
  if (!opened.has_value()) {
    reject(opened.diagnostic().stage, opened.diagnostic().code,
           opened.diagnostic().message);
  }
  const auto &project = opened.value();
  const auto committed = std::find_if(
      project.execution.committed_runs.begin(),
      project.execution.committed_runs.end(), [&](const auto &reference) {
        return reference.object_hash == manifest_hash;
      });
  if (committed == project.execution.committed_runs.end()) {
    reject("reference_verification", "manifest_not_committed",
           "requested manifest is not in project run history");
  }

  auto manifest_bytes = read_verified_object(project_path, *committed);
  auto manifest = parse_manifest(manifest_bytes);
  auto package_bytes = read_verified_object(project_path, manifest.package);
  auto scenario_bytes = read_verified_object(project_path, manifest.scenario);
  auto request_bytes = read_verified_object(project_path, manifest.request);
  auto result_bytes = read_verified_object(project_path, manifest.result);
  if (project.assembly_artifact_hash != manifest.assembly_artifact_hash) {
    reject("assembly_verification", "assembly_reference_mismatch",
           "project and run manifest cite different assembly snapshots");
  }
  if (require_external_assembly) {
    verify_external_assembly(project_path, project,
                             manifest.assembly_artifact_hash);
  }
  verify_request_graph(request_bytes, manifest);
  verify_recorded_result(result_bytes, manifest);
  return {std::move(manifest), std::move(manifest_bytes),
          std::move(package_bytes), std::move(scenario_bytes),
          std::move(request_bytes), std::move(result_bytes)};
}

RecordedRunReport inspection_failed(const std::string_view hash,
                                    std::string stage, std::string code,
                                    std::string message) {
  return {RecordedRunStatus::verification_failed,
          bounded(std::string(hash), 128U),
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          ReplayDiagnostic{bounded(std::move(stage), 128U),
                           bounded(std::move(code), 128U),
                           bounded(std::move(message), 4096U)}};
}

} // namespace

std::string_view status_name(const ReplayStatus status) noexcept {
  switch (status) {
  case ReplayStatus::exact_match:
    return "exact_match";
  case ReplayStatus::verification_failed:
    return "verification_failed";
  case ReplayStatus::execution_unavailable:
    return "execution_unavailable";
  case ReplayStatus::execution_failed:
    return "execution_failed";
  case ReplayStatus::mismatch:
    return "mismatch";
  }
  return "execution_failed";
}

int recommended_exit_code(const ReplayStatus status) noexcept {
  switch (status) {
  case ReplayStatus::exact_match:
    return 0;
  case ReplayStatus::verification_failed:
    return 3;
  case ReplayStatus::execution_unavailable:
  case ReplayStatus::execution_failed:
    return 4;
  case ReplayStatus::mismatch:
    return 5;
  }
  return 4;
}

RecordedRunReport inspect_recorded(
    const std::filesystem::path &project_path,
    const std::string_view manifest_hash,
    run_store::TransactionOptions options) noexcept {
  try {
    auto verified =
        verify_recorded_run(project_path, manifest_hash, options, false);
    return {RecordedRunStatus::recorded,
            std::string(manifest_hash),
            verified.manifest.package.object_hash,
            verified.manifest.result.object_hash,
            std::move(verified.result_bytes),
            verified.manifest.backend_id,
            verified.manifest.backend_contract_version,
            std::nullopt};
  } catch (const ReplayError &failure) {
    return inspection_failed(manifest_hash, failure.stage(), failure.code(),
                             failure.what());
  } catch (const integrity::CanonicalJsonError &failure) {
    return inspection_failed(manifest_hash, "integrity", failure.code(),
                             failure.what());
  } catch (const std::exception &failure) {
    return inspection_failed(manifest_hash, "recorded_run_verification",
                             "verification_failed", failure.what());
  } catch (...) {
    return inspection_failed(manifest_hash, "recorded_run_verification",
                             "verification_failed",
                             "unknown recorded-run verification failure");
  }
}

ReplayReport replay_exact(const std::filesystem::path &project_path,
                          const std::string_view manifest_hash,
                          run_store::TransactionOptions options) noexcept {
  try {
    auto verified =
        verify_recorded_run(project_path, manifest_hash, options, true);
    const auto &manifest = verified.manifest;

    if (manifest.backend_id != execution::motor_arm_backend_id ||
        manifest.backend_contract_version !=
            execution::motor_arm_backend_contract_version) {
      return failed(ReplayStatus::execution_unavailable, manifest_hash,
                    "backend_identity", "backend_identity_unavailable",
                    "recorded authoritative backend is unavailable",
                    manifest.result.object_hash);
    }
    const auto request =
        execution::parse_analysis_request(verified.request_bytes);
    if (!request.has_value()) {
      const auto status =
          request.diagnostic().code.starts_with("unsupported_")
              ? ReplayStatus::execution_unavailable
              : ReplayStatus::verification_failed;
      return failed(status, manifest_hash, request.diagnostic().stage,
                    request.diagnostic().code, request.diagnostic().message,
                    manifest.result.object_hash);
    }
    const auto profile = execution::collect_numeric_profile();
    if (!profile.has_value()) {
      return failed(ReplayStatus::execution_unavailable, manifest_hash,
                    profile.diagnostic().stage, profile.diagnostic().code,
                    profile.diagnostic().message,
                    manifest.result.object_hash);
    }
    if (profile.value() != manifest.numeric_profile) {
      return failed(ReplayStatus::execution_unavailable, manifest_hash,
                    "numeric_profile", "numeric_profile_mismatch",
                    "current numeric identity differs from the recorded run",
                    manifest.result.object_hash);
    }

    const execution::ExecutionInput input{
        verified.package_bytes, manifest.package.object_hash,
        verified.scenario_bytes, manifest.scenario.object_hash,
        verified.request_bytes, manifest.request.object_hash};
    const auto outcome = execution::execute(input);
    if (const auto *failure =
            std::get_if<execution::ExecutionFailure>(&outcome)) {
      const auto diagnostic = failure->diagnostics.empty()
                                  ? execution::Diagnostic{
                                        "execution", "execution_failed",
                                        "execution returned no diagnostic",
                                        std::nullopt, std::nullopt}
                                  : failure->diagnostics.front();
      const auto status = failure->disposition ==
                                  execution::ExecutionDisposition::unsupported
                              ? ReplayStatus::execution_unavailable
                              : ReplayStatus::execution_failed;
      return failed(status, manifest_hash, diagnostic.stage, diagnostic.code,
                    diagnostic.message, manifest.result.object_hash);
    }
    const auto &completed = std::get<execution::CompletedExecution>(outcome);
    if (completed.result.object_hash != manifest.result.object_hash ||
        completed.result.bytes != verified.result_bytes) {
      return failed(ReplayStatus::mismatch, manifest_hash,
                    "exact_comparison", "result_mismatch",
                    "replayed canonical result bytes or hash differ",
                    manifest.result.object_hash,
                    completed.result.object_hash);
    }
    if (completed.manifest.object_hash != manifest_hash ||
        completed.manifest.bytes != verified.manifest_bytes) {
      return failed(ReplayStatus::mismatch, manifest_hash,
                    "exact_comparison", "manifest_mismatch",
                    "replayed manifest bytes or hash differ",
                    manifest.result.object_hash,
                    completed.result.object_hash);
    }
    return ReplayReport{ReplayStatus::exact_match,
                        std::string(manifest_hash),
                        manifest.result.object_hash,
                        completed.result.object_hash,
                        std::nullopt};
  } catch (const ReplayError &failure) {
    return failed(ReplayStatus::verification_failed, manifest_hash,
                  failure.stage(), failure.code(), failure.what());
  } catch (const integrity::CanonicalJsonError &failure) {
    return failed(ReplayStatus::verification_failed, manifest_hash,
                  "integrity", failure.code(), failure.what());
  } catch (const std::exception &failure) {
    return failed(ReplayStatus::verification_failed, manifest_hash,
                  "replay_verification", "verification_failed",
                  failure.what());
  } catch (...) {
    return failed(ReplayStatus::verification_failed, manifest_hash,
                  "replay_verification", "verification_failed",
                  "unknown replay verification failure");
  }
}

} // namespace prometheus::replay
