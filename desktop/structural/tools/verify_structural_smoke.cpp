#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/smoke_case.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
namespace ps = prometheus::structural;

namespace {

constexpr std::uintmax_t maximumEvidenceBytes = 128U * 1024U * 1024U;

std::string read_evidence(const fs::path &path) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error)
    throw std::runtime_error("cannot inspect evidence file " + path.string() +
                             ": " + error.message());
  if (size > maximumEvidenceBytes)
    throw std::runtime_error("evidence file exceeds 128 MiB limit: " +
                             path.string());

  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot open evidence file: " + path.string());
  std::string bytes(static_cast<std::size_t>(size), '\0');
  if (size != 0U) {
    stream.read(bytes.data(), static_cast<std::streamsize>(size));
    if (stream.gcount() != static_cast<std::streamsize>(size))
      throw std::runtime_error("evidence file changed while reading: " +
                               path.string());
  }
  char trailing{};
  if (stream.get(trailing))
    throw std::runtime_error("evidence file changed while reading: " +
                             path.string());
  if (!stream.eof())
    throw std::runtime_error("cannot finish reading evidence file: " +
                             path.string());
  return bytes;
}

int parse_exit_code(const std::string_view text) {
  int value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::runtime_error("process exit code is not a base-10 integer");
  return value;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 7) {
    std::cerr << "usage: prometheus_verify_structural_smoke OUTPUT_DIRECTORY "
                 "PROCESS_EXIT_CODE SOLVER_EXECUTABLE_SHA256 SOLVER_VERSION "
                 "STDOUT_FILE STDERR_FILE\n";
    return 2;
  }

  try {
    const fs::path output(argv[1]);
    const auto job = std::string(ps::calculix_smoke_job_name);
    const ps::CalculixRunEvidence evidence{
        .process_exit_code = parse_exit_code(argv[2]),
        .solver_executable_sha256 = argv[3],
        .solver_version = argv[4],
        .deck_bytes = read_evidence(output / (job + ".inp")),
        .standard_output = read_evidence(fs::path(argv[5])),
        .standard_error = read_evidence(fs::path(argv[6])),
        .status_bytes = read_evidence(output / (job + ".sta")),
        .data_bytes = read_evidence(output / (job + ".dat")),
    };
    const auto result =
        ps::compile_calculix_result(ps::structural_smoke_request(), evidence);
    if (!result.complete()) {
      for (const auto &issue : result.issues)
        std::cerr << issue.code << ": " << issue.message << '\n';
      return 9;
    }

    std::cout << std::scientific << std::setprecision(10)
              << "verified CalculiX structural smoke: maximum_displacement_m="
              << result.metrics->maximum_displacement_m
              << " maximum_von_mises_pa="
              << result.metrics->maximum_von_mises_pa
              << " displacement_rows=" << result.metrics->displacement_rows
              << " stress_rows=" << result.metrics->stress_rows << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "structural_smoke_evidence_error: " << error.what() << '\n';
    return 9;
  }
}
