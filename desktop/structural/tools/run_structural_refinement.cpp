#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace ps = prometheus::structural;
namespace fs = std::filesystem;

struct Run final {
  ps::BenchmarkComparison comparison;
  ps::CalculixMetrics metrics;
};

Run execute(const fs::path &ccx, const fs::path &root,
            const std::string &job, const ps::BenchmarkReference &reference) {
  fs::create_directories(root);
  for (const auto extension : {"dat", "frd", "sta", "cvg", "12d", "eig",
                               "fin", "hrn", "mas", "msh", "nam", "rout",
                               "stm"}) {
    std::error_code ignored;
    fs::remove(root / (job + "." + extension), ignored);
  }
  std::ofstream deck(root / (job + ".inp"), std::ios::binary);
  deck << ps::generate_calculix_deck(reference.request);
  deck.close();
  const auto result = ps::run_calculix(
      {ccx, root, job, std::chrono::seconds(120)});
  if (result.status != ps::SolverRunStatus::completed || !result.metrics)
    throw std::runtime_error(job + " failed: " + result.detail);
  return {ps::compare_benchmark(reference, *result.metrics), *result.metrics};
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: prometheus_run_structural_refinement CCX OUTPUT_DIRECTORY\n";
    return 2;
  }
  try {
    const fs::path ccx = fs::absolute(argv[1]);
    const fs::path root = fs::absolute(argv[2]);
    const auto coarseReference = ps::cantilever_benchmark(20, 3, 3);
    const auto refinedReference = ps::cantilever_benchmark(40, 6, 6);
    const auto coarse = execute(ccx, root / "coarse", "cantilever_coarse", coarseReference);
    const auto refined = execute(ccx, root / "refined", "cantilever_refined", refinedReference);
    std::cout << std::scientific << std::setprecision(10)
              << "coarse_displacement_relative_error="
              << coarse.comparison.displacement_relative_error << '\n'
              << "refined_displacement_relative_error="
              << refined.comparison.displacement_relative_error << '\n'
              << "coarse_stress_relative_error="
              << coarse.comparison.stress_relative_error << '\n'
              << "refined_stress_relative_error="
              << refined.comparison.stress_relative_error << '\n';
    const bool convergedDirection =
        refined.comparison.displacement_relative_error <
            coarse.comparison.displacement_relative_error &&
        refined.comparison.stress_relative_error <
            coarse.comparison.stress_relative_error;
    if (!convergedDirection || !refined.comparison.passed()) {
      std::cerr << "cantilever refinement gate failed\n";
      return 4;
    }
    std::cout << "refinement=passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}
