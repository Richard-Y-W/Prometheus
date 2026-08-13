#include <prometheus/execution/execute.hpp>
#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

namespace execution = prometheus::execution;
namespace fs = std::filesystem;
namespace integrity = prometheus::integrity;
namespace run_store = prometheus::run_store;
using Json = nlohmann::json;

std::string read_file(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open fixture " + path.string());
  }
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

run_store::StoredObjectReference
reference_for(const std::string_view bytes, std::string media_type,
              std::string schema_id, std::string schema_version) {
  return {integrity::object_hash(bytes),
          static_cast<std::uint64_t>(bytes.size()), std::move(media_type),
          std::move(schema_id), std::move(schema_version)};
}

run_store::ObjectToStore stored(const execution::CanonicalObject &object) {
  return {run_store::StoredObjectReference{
              object.object_hash, static_cast<std::uint64_t>(object.bytes.size()),
              object.media_type, object.schema_id, object.schema_version},
          object.bytes};
}

run_store::ObjectToStore package_object(std::string bytes) {
  auto reference = reference_for(
      bytes,
      "application/vnd.prometheus.execution-component+json;version=2.0.0",
      "urn:prometheus:schema:execution-component:2.0.0", "2.0.0");
  return {std::move(reference), std::move(bytes)};
}

run_store::ObjectToStore scenario_object(std::string bytes) {
  auto reference = reference_for(bytes,
                                 std::string(execution::motor_arm_scenario_media_type),
                                 std::string(execution::motor_arm_scenario_schema_id),
                                 "1.0.0");
  return {std::move(reference), std::move(bytes)};
}

run_store::ObjectToStore request_object(std::string bytes) {
  auto reference = reference_for(bytes,
                                 std::string(execution::analysis_request_media_type),
                                 std::string(execution::analysis_request_schema_id),
                                 "1.0.0");
  return {std::move(reference), std::move(bytes)};
}

template <typename T>
T require_success(run_store::Result<T> result, const std::string &context) {
  if (!result.has_value()) {
    throw std::runtime_error(context + ": " + result.diagnostic().stage + "/" +
                             result.diagnostic().code + " " +
                             result.diagnostic().message);
  }
  return std::move(result.value());
}

execution::CompletedExecution execute_run(const run_store::ObjectToStore &package,
                                          const run_store::ObjectToStore &scenario,
                                          const run_store::ObjectToStore &request) {
  const execution::ExecutionInput input{
      package.bytes, package.reference.object_hash,
      scenario.bytes, scenario.reference.object_hash,
      request.bytes, request.reference.object_hash};
  auto outcome = execution::execute(input);
  if (!std::holds_alternative<execution::CompletedExecution>(outcome)) {
    const auto &failure = std::get<execution::ExecutionFailure>(outcome);
    const auto detail = failure.diagnostics.empty()
                            ? std::string("no diagnostic")
                            : failure.diagnostics.front().stage + "/" +
                                  failure.diagnostics.front().code;
    throw std::runtime_error("fixture execution failed: " + detail);
  }
  return std::get<execution::CompletedExecution>(std::move(outcome));
}

run_store::CompletedRunObjects
completed_objects(const run_store::ObjectToStore &package,
                  const run_store::ObjectToStore &scenario,
                  const run_store::ObjectToStore &request,
                  const execution::CompletedExecution &completed) {
  return {package, scenario, request, stored(completed.result),
          stored(completed.manifest)};
}

} // namespace

int main(const int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: prometheus_create_replay_fixture <project>\n";
    return 2;
  }
  try {
    const fs::path repository{PROMETHEUS_REPOSITORY_ROOT};
    const fs::path project_path{argv[1]};
    auto project_parent = project_path.parent_path();
    if (project_parent.empty()) {
      project_parent = ".";
    }
    const auto cad_path = project_parent / "motor-arm.step";
    fs::copy_file(repository / "fixtures/assemblies/motor-arm.step", cad_path,
                  fs::copy_options::none);
    const auto assembly_hash = integrity::sha256_file(cad_path);

    const auto package_a = package_object(read_file(
        repository / "fixtures/contracts/execution-component-v2.motor-a.jcs"));
    const auto package_b = package_object(read_file(
        repository / "fixtures/contracts/execution-component-v2.motor-b.jcs"));
    const auto scenario = scenario_object(read_file(
        repository /
        "fixtures/contracts/program-01b/motor-arm-scenario-v1.acceptance.jcs"));
    const auto request_a = request_object(read_file(
        repository /
        "fixtures/contracts/program-01b/analysis-request-v1.motor-a.jcs"));
    const auto request_b = request_object(read_file(
        repository /
        "fixtures/contracts/program-01b/analysis-request-v1.motor-b.jcs"));

    run_store::ProjectV2 project{
        "Motor arm",
        "motor-arm.step",
        assembly_hash,
        "right-handed Z-up",
        "m",
        {},
        {},
        {},
        {},
        run_store::EngineeringState{std::nullopt, {}, "not_evaluated"},
        std::nullopt,
        run_store::ExecutionIndex{{}, std::nullopt, {}, {}},
    };
    require_success(run_store::create_project_v2(project_path, project),
                    "create replay fixture project");
    require_success(run_store::install_package_binding(
                        project_path, "motor", package_a.reference,
                        package_a.bytes),
                    "bind Motor A");
    require_success(run_store::set_current_scenario(
                        project_path, scenario.reference, scenario.bytes),
                    "set reviewed scenario");
    const auto completed_a = execute_run(package_a, scenario, request_a);
    const auto run_a = completed_objects(package_a, scenario, request_a,
                                         completed_a);
    require_success(run_store::publish_completed_run(project_path, run_a),
                    "publish Motor A");

    require_success(run_store::install_package_binding(
                        project_path, "motor", package_b.reference,
                        package_b.bytes),
                    "bind Motor B");
    const auto completed_b = execute_run(package_b, scenario, request_b);
    const auto run_b = completed_objects(package_b, scenario, request_b,
                                         completed_b);
    require_success(run_store::publish_completed_run(project_path, run_b),
                    "publish Motor B");

    std::cout << Json{{"motor_a_manifest_hash",
                       completed_a.manifest.object_hash},
                      {"motor_b_manifest_hash",
                       completed_b.manifest.object_hash},
                      {"project", project_path.generic_string()}}
                     .dump()
              << '\n';
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << failure.what() << '\n';
    return 3;
  }
}
