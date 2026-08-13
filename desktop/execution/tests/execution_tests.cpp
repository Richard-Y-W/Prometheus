#include "test_support.hpp"

#include <prometheus/execution/contracts.hpp>
#include <prometheus/execution/execute.hpp>
#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cfenv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace execution = prometheus::execution;
namespace integrity = prometheus::integrity;
using Json = nlohmann::json;
using execution::test::require;
using execution::test::require_success;

const std::filesystem::path repository_root{PROMETHEUS_REPOSITORY_ROOT};
const std::filesystem::path process_probe{PROMETHEUS_EXECUTION_PROCESS_PROBE};

constexpr std::string_view result_schema_id =
    "urn:prometheus:schema:analysis-result:1.0.0";
constexpr std::string_view result_media_type =
    "application/vnd.prometheus.analysis-result+json;version=1.0.0";
constexpr std::string_view manifest_schema_id =
    "urn:prometheus:schema:run-manifest:1.0.0";
constexpr std::string_view manifest_media_type =
    "application/vnd.prometheus.run-manifest+json;version=1.0.0";

template <typename T>
concept HasResultMember = requires(T value) { value.result; };

template <typename T>
concept HasManifestMember = requires(T value) { value.manifest; };

static_assert(!HasResultMember<execution::ExecutionFailure>);
static_assert(!HasManifestMember<execution::ExecutionFailure>);
static_assert(std::variant_size_v<execution::ExecutionOutcome> == 2U);

std::string read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  require(static_cast<bool>(stream), "open fixture: " + path.string());
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(stream), "open temporary execution input");
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(static_cast<bool>(stream), "write temporary execution input");
}

std::string read_hash(const std::filesystem::path &path) {
  auto value = read_file(path);
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

std::string hash_with(const char value) {
  return "sha256:" + std::string(64U, value);
}

execution::CanonicalObject fixed_scenario(const double payload_mass = 8.0) {
  auto preview = require_success(
      execution::preview_motor_arm_scenario(execution::ScenarioDraftDegrees{
          payload_mass, 0.2, 90.0, 1.2, 4.0, 10.0, 35.0}),
      "preview fixed execution scenario");
  preview.confirmed_by_user = true;
  return require_success(
      execution::confirm_motor_arm_scenario(
          preview,
          "Evaluate the bound motor for the reviewed motor-arm operating cycle."),
      "confirm fixed execution scenario");
}

execution::CanonicalObject
request_for(const std::string &package_hash,
            const execution::CanonicalObject &scenario) {
  std::vector<std::string> obligations;
  obligations.reserve(execution::motor_arm_obligation_ids.size());
  std::transform(execution::motor_arm_obligation_ids.begin(),
                 execution::motor_arm_obligation_ids.end(),
                 std::back_inserter(obligations),
                 [](const std::string_view item) { return std::string(item); });
  return require_success(
      execution::build_analysis_request(execution::AnalysisRequestDraft{
          package_hash,
          scenario.object_hash,
          hash_with('a'),
          "motor",
          std::string(execution::motor_arm_backend_id),
          std::string(execution::motor_arm_backend_contract_version),
          read_hash(repository_root /
                    "fixtures/contracts/package-consumer.motor-arm-builtin-v1.sha256"),
          std::move(obligations),
      }),
      "build fixed execution request");
}

struct Fixture final {
  std::string package_bytes;
  std::string package_hash;
  execution::CanonicalObject scenario;
  execution::CanonicalObject request;

  [[nodiscard]] execution::ExecutionInput input() const {
    return execution::ExecutionInput{package_bytes,
                                     package_hash,
                                     scenario.bytes,
                                     scenario.object_hash,
                                     request.bytes,
                                     request.object_hash};
  }
};

Fixture fixture(const std::string_view motor_name) {
  const auto base = repository_root / "fixtures/contracts";
  const auto stem = "execution-component-v2." + std::string(motor_name);
  auto package_bytes = read_file(base / (stem + ".jcs"));
  auto package_hash = read_hash(base / (stem + ".sha256"));
  auto scenario = fixed_scenario();
  auto request = request_for(package_hash, scenario);
  return Fixture{std::move(package_bytes), std::move(package_hash),
                 std::move(scenario), std::move(request)};
}

const execution::CompletedExecution &
require_completed(const execution::ExecutionOutcome &outcome,
                  const std::string &context) {
  require(std::holds_alternative<execution::CompletedExecution>(outcome),
          context + ": expected completed execution");
  return std::get<execution::CompletedExecution>(outcome);
}

const execution::ExecutionFailure &
require_execution_failure(const execution::ExecutionOutcome &outcome,
                          const execution::ExecutionDisposition disposition,
                          const std::string &stage, const std::string &code) {
  require(std::holds_alternative<execution::ExecutionFailure>(outcome),
          "noncompleted execution must expose only ExecutionFailure");
  const auto &failure = std::get<execution::ExecutionFailure>(outcome);
  require(failure.disposition == disposition,
          "execution failure has unexpected disposition: " +
              std::to_string(static_cast<int>(failure.disposition)));
  require(!failure.diagnostics.empty(),
          "execution failure diagnostics must be non-empty");
  require(failure.diagnostics.front().stage == stage,
          "execution failure has unexpected diagnostic stage: " +
              failure.diagnostics.front().stage);
  require(failure.diagnostics.front().code == code,
          "execution failure has unexpected diagnostic code: " +
              failure.diagnostics.front().code);
  require(!failure.diagnostics.front().message.empty(),
          "execution failure diagnostic message must be non-empty");
  return failure;
}

template <typename Mutation>
execution::CanonicalObject mutate_object(const execution::CanonicalObject &source,
                                         Mutation mutation) {
  auto value = Json::parse(source.bytes);
  mutation(value);
  const auto bytes = integrity::canonicalize_json_bytes(value.dump());
  return execution::CanonicalObject{bytes,
                                    integrity::object_hash(bytes),
                                    source.media_type,
                                    source.schema_id,
                                    source.schema_version};
}

void require_canonical_object(const execution::CanonicalObject &object,
                              const std::string_view schema_id,
                              const std::string_view media_type) {
  require(integrity::verify_canonical_bytes(object.bytes) == object.bytes,
          "completed object contains canonical bytes");
  require(integrity::object_hash(object.bytes) == object.object_hash,
          "completed object hash matches exact bytes");
  require(object.schema_id == schema_id && object.schema_version == "1.0.0" &&
              object.media_type == media_type,
          "completed object carries exact type metadata");
}

void test_completed_a_b_runs_and_determinism() {
  const auto motor_a = fixture("motor-a");
  const auto motor_b = fixture("motor-b");
  const auto first_outcome = execution::execute(motor_a.input());
  const auto second_outcome = execution::execute(motor_a.input());
  const auto motor_b_outcome = execution::execute(motor_b.input());
  const auto &first = require_completed(first_outcome, "first Motor A run");
  const auto &second = require_completed(second_outcome, "second Motor A run");
  const auto &completed_b =
      require_completed(motor_b_outcome, "Motor B run");

  require(first.result.bytes == second.result.bytes &&
              first.result.object_hash == second.result.object_hash &&
              first.manifest.bytes == second.manifest.bytes &&
              first.manifest.object_hash == second.manifest.object_hash,
          "identical executions are byte deterministic in-process");
  require_canonical_object(first.result, result_schema_id, result_media_type);
  require_canonical_object(first.manifest, manifest_schema_id,
                           manifest_media_type);

  const auto result_a = Json::parse(first.result.bytes);
  const auto result_b = Json::parse(completed_b.result.bytes);
  require(result_a.at("execution_disposition") == "completed" &&
              result_a.at("request_hash") == motor_a.request.object_hash &&
              result_a.at("package_hash") == motor_a.package_hash,
          "result binds the exact request and package");
  require(result_a.at("calculations").size() == 8U &&
              result_a.at("obligation_outcomes").size() == 4U,
          "result contains only the fixed calculations and obligations");
  require(result_a.at("obligation_outcomes").at(1).at("outcome") == "fail" &&
              result_b.at("obligation_outcomes").at(1).at("outcome") == "pass",
          "Motor A fails holding while Motor B passes holding");
  require(result_a.at("coverage").at("counts").at("pass") == 3 &&
              result_a.at("coverage").at("counts").at("fail") == 1 &&
              result_b.at("coverage").at("counts").at("pass") == 4 &&
              result_b.at("coverage").at("counts").at("fail") == 0,
          "A/B result coverage matches scoped outcomes");
  require(result_a.at("calculations") == result_b.at("calculations"),
          "A/B calculation records remain equal");
  for (const auto index : std::array<std::size_t, 3>{0U, 2U, 3U}) {
    require(result_a.at("obligation_outcomes").at(index).at("outcome") ==
                result_b.at("obligation_outcomes").at(index).at("outcome") &&
                result_a.at("obligation_outcomes").at(index).at("signed_margin") ==
                    result_b.at("obligation_outcomes")
                        .at(index)
                        .at("signed_margin"),
            "A/B non-holding classifications and margins remain equal");
  }
  require(!result_a.contains("overall_verdict") &&
              !result_a.contains("project_verdict") &&
              !result_a.contains("timestamp") && !result_a.contains("run_id"),
          "result excludes global and volatile fields");

  const auto &profile = result_a.at("backend").at("numeric_profile");
  require(!profile.at("operating_system").at("name").get<std::string>().empty() &&
              !profile.at("compiler").at("id").get<std::string>().empty() &&
              !profile.at("math_runtime").at("version").get<std::string>().empty(),
          "completed result records a concrete numeric profile");
  const auto manifest = Json::parse(first.manifest.bytes);
  require(manifest.at("package").at("object_hash") == motor_a.package_hash &&
              manifest.at("package").at("byte_length") ==
                  motor_a.package_bytes.size() &&
              manifest.at("scenario").at("object_hash") ==
                  motor_a.scenario.object_hash &&
              manifest.at("request").at("object_hash") ==
                  motor_a.request.object_hash &&
              manifest.at("result").at("object_hash") ==
                  first.result.object_hash &&
              manifest.at("result").at("byte_length") ==
                  first.result.bytes.size(),
          "manifest binds exact five-field object references");
  require(manifest.at("numeric_profile") == profile &&
              !manifest.contains("replay_status") &&
              !manifest.contains("timestamp"),
          "manifest freezes execution identity without replay or time state");
}

void test_exact_reverification_and_request_cross_references() {
  const auto value = fixture("motor-a");

  auto wrong_package_hash = value.input();
  wrong_package_hash.expected_package_hash = hash_with('0');
  require_execution_failure(
      execution::execute(wrong_package_hash),
      execution::ExecutionDisposition::rejected_input, "input_integrity",
      "object_hash_mismatch");

  auto noncanonical_scenario = value.input();
  noncanonical_scenario.scenario_bytes += "\n";
  require_execution_failure(
      execution::execute(noncanonical_scenario),
      execution::ExecutionDisposition::rejected_input, "input_integrity",
      "noncanonical_bytes");

  auto wrong_request_hash = value.input();
  wrong_request_hash.expected_request_hash = hash_with('1');
  require_execution_failure(
      execution::execute(wrong_request_hash),
      execution::ExecutionDisposition::rejected_input, "input_integrity",
      "object_hash_mismatch");

  auto oversized_expected_hash = value.input();
  oversized_expected_hash.expected_package_hash = std::string(10000U, 'x');
  const auto oversized_outcome =
      execution::execute(oversized_expected_hash);
  const auto &bounded = require_execution_failure(
      oversized_outcome,
      execution::ExecutionDisposition::rejected_input, "input_integrity",
      "object_hash_mismatch");
  require(bounded.diagnostics.front().object_hash.has_value() &&
              bounded.diagnostics.front().object_hash->size() <= 128U &&
              bounded.diagnostics.front().message.size() <= 4096U,
          "execution-boundary diagnostic context remains bounded");

  auto mismatched_request = request_for(hash_with('2'), value.scenario);
  auto cross_reference = value.input();
  cross_reference.request_bytes = mismatched_request.bytes;
  cross_reference.expected_request_hash = mismatched_request.object_hash;
  require_execution_failure(
      execution::execute(cross_reference),
      execution::ExecutionDisposition::rejected_input, "request_binding",
      "package_hash_mismatch");

  const auto mismatched_scenario_request =
      mutate_object(value.request, [](Json &request) {
        request["scenario_hash"] = hash_with('3');
      });
  auto scenario_cross_reference = value.input();
  scenario_cross_reference.request_bytes = mismatched_scenario_request.bytes;
  scenario_cross_reference.expected_request_hash =
      mismatched_scenario_request.object_hash;
  require_execution_failure(
      execution::execute(scenario_cross_reference),
      execution::ExecutionDisposition::rejected_input, "request_binding",
      "scenario_hash_mismatch");

  const auto unsupported_consumer_request =
      mutate_object(value.request, [](Json &request) {
        request["package_consumer_contract_hash"] = hash_with('4');
      });
  auto unsupported_consumer = value.input();
  unsupported_consumer.request_bytes = unsupported_consumer_request.bytes;
  unsupported_consumer.expected_request_hash =
      unsupported_consumer_request.object_hash;
  require_execution_failure(
      execution::execute(unsupported_consumer),
      execution::ExecutionDisposition::unsupported, "request_binding",
      "unsupported_consumer_contract");
}

void test_unsupported_and_failed_dispositions_have_no_objects() {
  const auto value = fixture("motor-a");
  const auto unsupported_request =
      mutate_object(value.request, [](Json &request) {
        request["backend_id"] = "unavailable_backend";
      });
  auto unsupported_input = value.input();
  unsupported_input.request_bytes = unsupported_request.bytes;
  unsupported_input.expected_request_hash = unsupported_request.object_hash;
  require_execution_failure(
      execution::execute(unsupported_input),
      execution::ExecutionDisposition::unsupported, "request_contract",
      "unsupported_backend");

  auto overflow_scenario = fixed_scenario(1.0e308);
  auto overflow_request = request_for(value.package_hash, overflow_scenario);
  const execution::ExecutionInput overflow_input{
      value.package_bytes,
      value.package_hash,
      overflow_scenario.bytes,
      overflow_scenario.object_hash,
      overflow_request.bytes,
      overflow_request.object_hash,
  };
  require_execution_failure(execution::execute(overflow_input),
                            execution::ExecutionDisposition::failed,
                            "motor_arm_backend", "non_finite_output");

  require(static_cast<int>(execution::ExecutionDisposition::cancelled) == 3,
          "cancelled remains a distinct publication-boundary disposition");
}

void test_unsupported_numeric_environment_has_no_objects() {
  const auto value = fixture("motor-a");
  const auto previous_rounding = std::fegetround();
  require(previous_rounding != -1,
          "read floating-point rounding mode before execution test");
  require(std::fesetround(FE_UPWARD) == 0,
          "set unsupported execution rounding mode");
  const auto outcome = execution::execute(value.input());
  require(std::fesetround(previous_rounding) == 0,
          "restore floating-point rounding mode after execution test");
  require_execution_failure(outcome, execution::ExecutionDisposition::unsupported,
                            "numeric_profile",
                            "unsupported_numeric_profile");
}

std::string shell_quote(const std::string &value) {
#if defined(_WIN32)
  std::string escaped = value;
  std::size_t position = 0U;
  while ((position = escaped.find('"', position)) != std::string::npos) {
    escaped.insert(position, 1U, '\\');
    position += 2U;
  }
  return "\"" + escaped + "\"";
#else
  std::string escaped;
  escaped.reserve(value.size() + 2U);
  escaped.push_back('\'');
  for (const auto character : value) {
    if (character == '\'') {
      escaped += "'\\''";
    } else {
      escaped.push_back(character);
    }
  }
  escaped.push_back('\'');
  return escaped;
#endif
}

void test_fresh_process_repeatability() {
  const auto value = fixture("motor-a");
  const auto outcome = execution::execute(value.input());
  const auto &completed = require_completed(outcome, "parent-process run");
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("prometheus-program-01b-execution-process-probe-" +
       std::to_string(std::hash<std::string>{}(process_probe.string())));
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  require(!error, "clear scoped process-probe directory");
  require(std::filesystem::create_directories(directory),
          "create scoped process-probe directory");

  const auto package_path = directory / "package.jcs";
  const auto scenario_path = directory / "scenario.jcs";
  const auto request_path = directory / "request.jcs";
  const auto result_path = directory / "result.jcs";
  const auto manifest_path = directory / "manifest.jcs";
  write_file(package_path, value.package_bytes);
  write_file(scenario_path, value.scenario.bytes);
  write_file(request_path, value.request.bytes);

  const std::array<std::string, 9> arguments{
      process_probe.string(),
      package_path.string(),
      value.package_hash,
      scenario_path.string(),
      value.scenario.object_hash,
      request_path.string(),
      value.request.object_hash,
      result_path.string(),
      manifest_path.string(),
  };
  std::string command;
  for (const auto &argument : arguments) {
    if (!command.empty()) {
      command.push_back(' ');
    }
    command += shell_quote(argument);
  }
  require(std::system(command.c_str()) == 0,
          "fresh helper process completes exact execution");
  require(read_file(result_path) == completed.result.bytes &&
              read_file(manifest_path) == completed.manifest.bytes,
          "fresh-process result and manifest bytes match in-process output");
  std::filesystem::remove_all(directory, error);
  require(!error, "remove scoped process-probe directory");
}

} // namespace

int main() {
  try {
    test_completed_a_b_runs_and_determinism();
    test_exact_reverification_and_request_cross_references();
    test_unsupported_and_failed_dispositions_have_no_objects();
    test_unsupported_numeric_environment_has_no_objects();
    test_fresh_process_repeatability();
    std::cout << "All end-to-end execution tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
