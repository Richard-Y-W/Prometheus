#include <prometheus/execution/contracts.hpp>
#include <prometheus/execution/execute.hpp>
#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace execution = prometheus::execution;
namespace integrity = prometheus::integrity;
using Json = nlohmann::json;

const std::filesystem::path repository_root{PROMETHEUS_REPOSITORY_ROOT};

std::string read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("unable to open vector source: " + path.string());
  }
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string read_hash(const std::filesystem::path &path) {
  auto value = read_file(path);
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

void write_file(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("unable to open vector output: " + path.string());
  }
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    throw std::runtime_error("unable to write vector output: " + path.string());
  }
}

template <typename T>
T require_value(execution::Result<T> result, const std::string_view context) {
  if (!result.has_value()) {
    const auto &failure = result.diagnostic();
    throw std::runtime_error(std::string(context) + " failed at " +
                             failure.stage + "/" + failure.code + ": " +
                             failure.message);
  }
  return std::move(result.value());
}

execution::CanonicalObject build_scenario() {
  auto preview = require_value(
      execution::preview_motor_arm_scenario(execution::ScenarioDraftDegrees{
          8.0, 0.2, 90.0, 1.2, 4.0, 10.0, 35.0}),
      "scenario preview");
  preview.confirmed_by_user = true;
  return require_value(
      execution::confirm_motor_arm_scenario(
          preview,
          "Evaluate the bound motor for the reviewed motor-arm operating cycle."),
      "scenario confirmation");
}

execution::CanonicalObject
build_request(const std::string &package_hash,
              const execution::CanonicalObject &scenario,
              const std::string &assembly_hash,
              const std::string &consumer_hash) {
  std::vector<std::string> obligations;
  obligations.reserve(execution::motor_arm_obligation_ids.size());
  std::transform(execution::motor_arm_obligation_ids.begin(),
                 execution::motor_arm_obligation_ids.end(),
                 std::back_inserter(obligations),
                 [](const std::string_view value) { return std::string(value); });
  return require_value(
      execution::build_analysis_request(execution::AnalysisRequestDraft{
          package_hash,
          scenario.object_hash,
          assembly_hash,
          "motor",
          std::string(execution::motor_arm_backend_id),
          std::string(execution::motor_arm_backend_contract_version),
          consumer_hash,
          std::move(obligations),
      }),
      "analysis request");
}

void emit_object(const std::filesystem::path &directory,
                 const std::string_view stem,
                 const execution::CanonicalObject &object) {
  const auto verified = integrity::verify_canonical_bytes(object.bytes);
  if (integrity::object_hash(verified) != object.object_hash) {
    throw std::runtime_error("refusing to export an object with mismatched bytes");
  }
  const auto human = Json::parse(verified).dump(2) + "\n";
  const auto base = directory / std::string(stem);
  write_file(base.string() + ".json", human);
  write_file(base.string() + ".jcs", verified);
  write_file(base.string() + ".sha256", object.object_hash + "\n");
}

execution::CompletedExecution
run_motor(const std::string &package_bytes, const std::string &package_hash,
          const execution::CanonicalObject &scenario,
          const execution::CanonicalObject &request) {
  const execution::ExecutionInput input{
      package_bytes, package_hash, scenario.bytes, scenario.object_hash,
      request.bytes, request.object_hash,
  };
  const auto outcome = execution::execute(input);
  if (const auto *completed =
          std::get_if<execution::CompletedExecution>(&outcome)) {
    return *completed;
  }
  const auto &failure = std::get<execution::ExecutionFailure>(outcome);
  if (failure.diagnostics.empty()) {
    throw std::runtime_error("execution failed without a diagnostic");
  }
  throw std::runtime_error(
      "execution failed at " + failure.diagnostics.front().stage + "/" +
      failure.diagnostics.front().code + ": " +
      failure.diagnostics.front().message);
}

void require_output_directory(const std::filesystem::path &directory) {
  if (directory.empty()) {
    throw std::runtime_error("--output requires a non-empty directory");
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory, error);
  if (error && status.type() != std::filesystem::file_type::not_found) {
    throw std::runtime_error("unable to inspect output directory");
  }
  if (std::filesystem::is_symlink(status)) {
    throw std::runtime_error("output directory must not be a symlink");
  }
  if (status.type() == std::filesystem::file_type::not_found) {
    if (!std::filesystem::create_directories(directory)) {
      throw std::runtime_error("unable to create output directory");
    }
  } else if (!std::filesystem::is_directory(status)) {
    throw std::runtime_error("output path must be a directory");
  }
}

void export_vectors(const std::filesystem::path &output_directory) {
  require_output_directory(output_directory);
  const auto contracts = repository_root / "fixtures/contracts";
  const auto scenario = build_scenario();
  const auto assembly_hash = integrity::sha256_file(
      repository_root / "fixtures/assemblies/motor-arm.step");
  const auto consumer_hash = read_hash(
      contracts / "package-consumer.motor-arm-builtin-v1.sha256");
  emit_object(output_directory, "motor-arm-scenario-v1.acceptance", scenario);

  for (const auto motor : {std::string_view("motor-a"),
                           std::string_view("motor-b")}) {
    const auto package_stem =
        "execution-component-v2." + std::string(motor);
    const auto package_bytes = read_file(contracts / (package_stem + ".jcs"));
    const auto package_hash =
        read_hash(contracts / (package_stem + ".sha256"));
    const auto request = build_request(package_hash, scenario, assembly_hash,
                                       consumer_hash);
    const auto completed =
        run_motor(package_bytes, package_hash, scenario, request);
    emit_object(output_directory,
                "analysis-request-v1." + std::string(motor), request);
    emit_object(output_directory,
                "analysis-result-v1." + std::string(motor),
                completed.result);
    emit_object(output_directory,
                "run-manifest-v1." + std::string(motor),
                completed.manifest);
  }
}

} // namespace

int main(const int argc, char **argv) {
  try {
    if (argc != 3 || std::string_view(argv[1]) != "--output") {
      std::cerr << "usage: prometheus_export_program_01b_vectors --output DIR\n";
      return 64;
    }
    export_vectors(argv[2]);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
