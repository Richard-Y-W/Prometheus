#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/structural/structural_request.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "usage: prometheus_run_structural_benchmark CCX OUTPUT_DIRECTORY "
                 "[axial|cantilever]\n";
    return 2;
  }
  namespace ps = prometheus::structural;
  namespace fs = std::filesystem;
  const std::string benchmark = argc == 4 ? argv[3] : "axial";
  const auto reference = benchmark == "axial"
      ? ps::axial_tension_bar_benchmark()
      : benchmark == "cantilever"
          ? ps::cantilever_benchmark(40, 6, 6)
          : throw std::invalid_argument("unknown benchmark");
  const auto issues = ps::validate_request(reference.request);
  if (!issues.empty()) {
    std::cerr << issues.front().code << ": " << issues.front().message << '\n';
    return 3;
  }
  const fs::path output = fs::absolute(argv[2]);
  fs::create_directories(output);
  const std::string job = benchmark == "axial"
      ? "prometheus_axial_tension_bar_v1"
      : "prometheus_cantilever_v1";
  for (const auto extension : {"dat", "frd", "sta", "cvg", "12d", "eig",
                               "fin", "hrn", "mas", "msh", "nam", "rout",
                               "stm"}) {
    std::error_code ignored;
    fs::remove(output / (job + "." + extension), ignored);
  }
  std::ofstream deck(output / (job + ".inp"), std::ios::binary);
  deck << ps::generate_calculix_deck(reference.request);
  deck.close();
  if (!deck) { std::cerr << "could not write benchmark deck\n"; return 3; }
  const auto run = ps::run_calculix(
      {fs::absolute(argv[1]), output, job, std::chrono::seconds(120)});
  std::cout << run.standard_output;
  std::cerr << run.standard_error;
  if (run.status != ps::SolverRunStatus::completed || !run.metrics) {
    std::cerr << "benchmark execution failed: " << run.detail << '\n';
    return 4;
  }
  const auto comparison = ps::compare_benchmark(reference, *run.metrics);
  std::cout << std::scientific << std::setprecision(10)
            << "expected_displacement_m=" << reference.expected_maximum_displacement_m
            << " actual_displacement_m=" << run.metrics->maximum_displacement_m
            << " relative_error=" << comparison.displacement_relative_error << '\n'
            << "expected_von_mises_pa=" << reference.expected_maximum_von_mises_pa
            << " actual_von_mises_pa=" << run.metrics->maximum_von_mises_pa
            << " relative_error=" << comparison.stress_relative_error << '\n';
  if (!comparison.passed()) {
    std::cerr << "benchmark tolerance failed\n";
    return 5;
  }
  std::cout << "benchmark=passed\n";
  return 0;
}
