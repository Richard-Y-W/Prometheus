#include "prometheus/structural/calculix_runner.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 5) {
    std::cerr << "usage: prometheus_run_calculix_job EXECUTABLE WORK_DIRECTORY "
                 "JOB_NAME TIMEOUT_SECONDS\n";
    return 2;
  }
  try {
    const auto seconds = std::stoll(argv[4]);
    const auto result = prometheus::structural::run_calculix({
        std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]),
        argv[3], std::chrono::seconds(seconds)});
    std::cout << result.standard_output;
    std::cerr << result.standard_error;
    if (result.status != prometheus::structural::SolverRunStatus::completed) {
      std::cerr << "Prometheus solver status: " << result.detail << '\n';
      return result.status == prometheus::structural::SolverRunStatus::timed_out
                 ? 124
                 : 3;
    }
    std::cout << "Prometheus metrics: max_displacement_m="
              << result.metrics->maximum_displacement_m
              << " max_von_mises_pa=" << result.metrics->maximum_von_mises_pa
              << " elapsed_ms=" << result.elapsed.count() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
