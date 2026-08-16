#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/structural/structural_findings.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace ps = prometheus::structural;
namespace fs = std::filesystem;

namespace {

struct Run final {
  ps::SolverRunResult solver;
  ps::BenchmarkComparison analytic;
};

Run execute(const fs::path &ccx, const fs::path &root,
            const std::string &job,
            const ps::BenchmarkReference &reference) {
  fs::create_directories(root);
  for (const auto extension : {"inp", "dat", "frd", "sta", "cvg", "12d",
                               "eig", "fin", "hrn", "mas", "msh", "nam",
                               "rout", "stm"}) {
    std::error_code ignored;
    fs::remove(root / (job + "." + extension), ignored);
  }
  auto solver = ps::run_calculix(
      {ccx, root, job, std::chrono::seconds(120)}, reference.setup);
  std::cout << solver.standard_output;
  std::cerr << solver.standard_error;
  if (solver.status != ps::SolverRunStatus::completed ||
      !solver.validated_result || !solver.validated_result->metrics)
    throw std::runtime_error(job + " failed: " + solver.detail);
  const auto comparison =
      ps::compare_benchmark(reference, *solver.validated_result->metrics);
  return {std::move(solver), comparison};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: prometheus_run_structural_refinement CCX "
                 "OUTPUT_DIRECTORY\n";
    return 2;
  }
  try {
    const fs::path ccx = fs::absolute(argv[1]);
    const fs::path root = fs::absolute(argv[2]);
    const auto coarseReference = ps::cantilever_benchmark(20, 3, 3);
    const auto fineReference = ps::cantilever_benchmark(40, 6, 6);
    const auto coarse = execute(ccx, root / "coarse", "cantilever_coarse",
                                coarseReference);
    const auto fine = execute(ccx, root / "fine", "cantilever_fine",
                              fineReference);
    const auto refinement = ps::compile_structural_refinement_evidence(
        *coarse.solver.validated_result, *fine.solver.validated_result, 0.10);
    const auto findings = ps::compile_structural_findings(
        fineReference.setup.request, fine.solver.validated_result, refinement);
    const bool scopedPass = findings.evaluated_obligations == 2 &&
        std::ranges::all_of(findings.findings, [](const auto &finding) {
          return finding.disposition ==
              ps::StructuralFindingDisposition::no_violation_detected_within_scope;
        });
    std::cout << std::scientific << std::setprecision(10)
              << "coarse_displacement_relative_error="
              << coarse.analytic.displacement_relative_error << '\n'
              << "fine_displacement_relative_error="
              << fine.analytic.displacement_relative_error << '\n'
              << "coarse_stress_relative_error="
              << coarse.analytic.stress_relative_error << '\n'
              << "fine_stress_relative_error="
              << fine.analytic.stress_relative_error << '\n'
              << "coarse_to_fine_change_fraction="
              << refinement.coarse_to_fine_change_fraction << '\n';
    if (!fine.analytic.passed() || !refinement.criteria_satisfied ||
        !scopedPass) {
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
