#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace ps = prometheus::structural;

int main(int argc, char **argv) {
  if (argc != 6 || std::string_view(argv[1]) != "--axial-smoke") {
    std::cerr
        << "usage: prometheus_run_calculix_job --axial-smoke EXECUTABLE "
           "WORK_DIRECTORY JOB_NAME TIMEOUT_SECONDS\n";
    return 2;
  }
  try {
    const auto seconds = std::stoll(argv[5]);
    const auto setup = ps::axial_tension_bar_benchmark().setup;
    const auto result = ps::run_calculix(
        {std::filesystem::absolute(argv[2]),
         std::filesystem::absolute(argv[3]), argv[4],
         std::chrono::seconds(seconds)},
        setup);
    std::cout << result.standard_output;
    std::cerr << result.standard_error;
    if (result.status != ps::SolverRunStatus::completed ||
        !result.validated_result || !result.validated_result->complete()) {
      std::cerr << "status=failed evidence=indeterminate detail="
                << result.detail << '\n';
      return result.status == ps::SolverRunStatus::timed_out ? 124 : 3;
    }
    std::cout << "status=completed evidence=validated"
              << " compiled_setup_identity=" << setup.identity
              << " validated_result_identity="
              << result.validated_result->identity
              << " max_displacement_m="
              << result.validated_result->metrics->maximum_displacement_m
              << " max_von_mises_pa="
              << result.validated_result->metrics->maximum_von_mises_pa
              << " elapsed_ms=" << result.elapsed.count() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "status=failed evidence=indeterminate detail="
              << error.what() << '\n';
    return 2;
  }
}
