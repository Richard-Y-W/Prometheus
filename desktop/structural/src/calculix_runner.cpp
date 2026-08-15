#include "prometheus/structural/calculix_runner.hpp"

#include "solver_process.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace prometheus::structural {
namespace {

std::optional<std::string> read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

bool safe_job_name(const std::string &value) {
  return !value.empty() && value.size() <= 128 &&
         std::ranges::all_of(value, [](const unsigned char c) {
           return std::isalnum(c) || c == '_' || c == '-';
         });
}

} // namespace

SolverRunResult run_calculix(const SolverRunOptions &options) {
  SolverRunResult result;
  if (options.executable.empty() || options.working_directory.empty() ||
      !safe_job_name(options.job_name) || options.timeout.count() <= 0) {
    result.detail = "invalid_solver_run_options";
    return result;
  }
  const auto inputPath = options.working_directory / (options.job_name + ".inp");
  if (!std::filesystem::is_regular_file(inputPath)) {
    result.detail = "input_deck_missing";
    return result;
  }
  const auto datPath = options.working_directory / (options.job_name + ".dat");
  const auto frdPath = options.working_directory / (options.job_name + ".frd");
  const auto staPath = options.working_directory / (options.job_name + ".sta");
  if (std::filesystem::exists(datPath) || std::filesystem::exists(frdPath) ||
      std::filesystem::exists(staPath)) {
    result.status = SolverRunStatus::output_conflict;
    result.detail = "preexisting_solver_output";
    return result;
  }
  const auto process = detail::run_process(options.executable,
                                            {options.job_name},
                                            options.working_directory,
                                            options.timeout);
  result.exit_code = process.exit_code;
  result.elapsed = process.elapsed;
  result.standard_output = process.standard_output;
  result.standard_error = process.standard_error;
  result.detail = process.detail;
  if (!process.launched) return result;
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
  const auto dat = read_file(datPath);
  if (!dat || !std::filesystem::is_regular_file(frdPath) ||
      !std::filesystem::is_regular_file(staPath)) {
    result.status = SolverRunStatus::output_missing;
    result.detail = "required_solver_output_missing";
    return result;
  }
  try {
    result.metrics = parse_calculix_dat(*dat);
    result.status = SolverRunStatus::completed;
    result.detail = "completed";
  } catch (const std::exception &error) {
    result.status = SolverRunStatus::result_invalid;
    result.detail = error.what();
  }
  return result;
}

} // namespace prometheus::structural
