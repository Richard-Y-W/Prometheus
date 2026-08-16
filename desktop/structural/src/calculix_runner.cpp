#include "prometheus/structural/calculix_runner.hpp"

#include "solver_process.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace prometheus::structural {
namespace {

std::optional<std::string> read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

bool write_file(const std::filesystem::path &path,
                const std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  return static_cast<bool>(output);
}

bool safe_job_name(const std::string &value) {
  return !value.empty() && value.size() <= 128U &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return std::isalnum(character) || character == '_' ||
                  character == '-';
         });
}

std::string solver_version(const std::string_view standardOutput) {
  std::istringstream input{std::string(standardOutput)};
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos)
      continue;
    const auto last = line.find_last_not_of(" \t");
    const auto trimmed = line.substr(first, last - first + 1U);
    if (trimmed.starts_with("CalculiX Version "))
      return trimmed;
  }
  return {};
}

} // namespace

SolverRunResult run_calculix(const SolverRunOptions &options,
                             const CompiledStructuralSetup &setup) {
  SolverRunResult result;
  if (options.executable.empty() || options.working_directory.empty() ||
      !safe_job_name(options.job_name) || options.timeout.count() <= 0 ||
      setup.identity.empty() || setup.calculix_deck.empty()) {
    result.detail = "invalid_solver_run_options";
    return result;
  }
  const auto inputPath =
      options.working_directory / (options.job_name + ".inp");
  const auto datPath =
      options.working_directory / (options.job_name + ".dat");
  const auto frdPath =
      options.working_directory / (options.job_name + ".frd");
  const auto staPath =
      options.working_directory / (options.job_name + ".sta");
  if (std::filesystem::exists(inputPath) ||
      std::filesystem::exists(datPath) ||
      std::filesystem::exists(frdPath) ||
      std::filesystem::exists(staPath)) {
    result.status = SolverRunStatus::output_conflict;
    result.detail = "preexisting_solver_artifact";
    return result;
  }
  if (!std::filesystem::is_regular_file(options.executable) ||
      !std::filesystem::is_directory(options.working_directory) ||
      !write_file(inputPath, setup.calculix_deck)) {
    result.detail = "solver_input_write_failed";
    return result;
  }

  std::string executableSha256;
  try {
    executableSha256 = integrity::sha256_file(options.executable);
  } catch (const std::exception &error) {
    result.detail = std::string("solver_identity_failed: ") + error.what();
    return result;
  }

  const auto process = detail::run_process(
      options.executable, {options.job_name}, options.working_directory,
      options.timeout);
  result.exit_code = process.exit_code;
  result.elapsed = process.elapsed;
  result.standard_output = process.standard_output;
  result.standard_error = process.standard_error;
  result.detail = process.detail;
  if (!process.launched)
    return result;
  if (process.timed_out) {
    result.status = SolverRunStatus::timed_out;
    result.detail = "solver_timeout";
    return result;
  }
  if (process.exit_code != 0) {
    result.status = SolverRunStatus::nonzero_exit;
    result.detail = "solver_nonzero_exit";
    return result;
  }

  const auto deck = read_file(inputPath);
  const auto dat = read_file(datPath);
  const auto sta = read_file(staPath);
  std::error_code sizeError;
  const auto frdBytes = std::filesystem::file_size(frdPath, sizeError);
  if (!deck || !dat || !sta || sizeError || frdBytes == 0U ||
      !std::filesystem::is_regular_file(frdPath)) {
    result.status = SolverRunStatus::output_missing;
    result.detail = "required_solver_output_missing";
    return result;
  }

  std::string frdSha256;
  try {
    frdSha256 = integrity::sha256_file(frdPath);
  } catch (const std::exception &error) {
    result.status = SolverRunStatus::result_invalid;
    result.detail = std::string("frd_identity_failed: ") + error.what();
    return result;
  }
  const CalculixRunEvidence evidence{
      .process_exit_code = process.exit_code,
      .solver_executable_sha256 = std::move(executableSha256),
      .solver_version = solver_version(process.standard_output),
      .deck_bytes = *deck,
      .standard_output = process.standard_output,
      .standard_error = process.standard_error,
      .status_bytes = *sta,
      .data_bytes = *dat,
      .frd_sha256 = std::move(frdSha256),
      .frd_byte_length = frdBytes};
  result.validated_result = compile_calculix_result(setup, evidence);
  if (!result.validated_result->complete()) {
    result.status = SolverRunStatus::result_invalid;
    result.detail = result.validated_result->issues.empty()
                        ? "solver_evidence_incomplete"
                        : result.validated_result->issues.front().code;
    return result;
  }
  result.metrics = result.validated_result->metrics;
  result.status = SolverRunStatus::completed;
  result.detail = "completed";
  return result;
}

} // namespace prometheus::structural
