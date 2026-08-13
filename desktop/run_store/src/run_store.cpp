#include <prometheus/run_store/run_store.hpp>

#include "platform_io.hpp"

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
      if (project.execution.committed_runs.size() >= maximum_committed_runs) {
        return Result<Publication>::failure(detail::store_diagnostic(
            "committed_run_limit_exceeded",
            "project already has the maximum committed runs"));
      }
      project.execution.committed_runs.push_back(objects.manifest.reference);
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
