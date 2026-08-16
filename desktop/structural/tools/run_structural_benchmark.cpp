#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_archive.hpp"
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

struct ExecutedBenchmark final {
  ps::SolverRunResult run;
  ps::BenchmarkComparison analytic_comparison;
};

void clear_job_artifacts(const fs::path &root, const std::string &job) {
  for (const auto extension : {"inp", "dat", "frd", "sta", "cvg", "12d",
                               "eig", "fin", "hrn", "mas", "msh", "nam",
                               "rout", "stm"}) {
    std::error_code ignored;
    fs::remove(root / (job + "." + extension), ignored);
  }
  for (const auto name : {"prometheus-structural-run.json",
                          "reviewed-structural-setup.json",
                          "solver.stdout.txt", "solver.stderr.txt"}) {
    std::error_code ignored;
    fs::remove(root / name, ignored);
  }
}

ExecutedBenchmark execute(const fs::path &ccx, const fs::path &root,
                          const std::string &job,
                          const ps::BenchmarkReference &reference) {
  fs::create_directories(root);
  clear_job_artifacts(root, job);
  auto run = ps::run_calculix(
      {ccx, root, job, std::chrono::seconds(120)}, reference.setup);
  std::cout << run.standard_output;
  std::cerr << run.standard_error;
  if (run.status != ps::SolverRunStatus::completed ||
      !run.validated_result || !run.validated_result->metrics)
    throw std::runtime_error(job + " failed: " + run.detail);
  const auto comparison =
      ps::compare_benchmark(reference, *run.validated_result->metrics);
  return {std::move(run), comparison};
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "usage: prometheus_run_structural_benchmark CCX "
                 "OUTPUT_DIRECTORY [axial|cantilever]\n";
    return 2;
  }
  try {
    const fs::path ccx = fs::absolute(argv[1]);
    const fs::path output = fs::absolute(argv[2]);
    const std::string benchmark = argc == 4 ? argv[3] : "axial";
    ps::BenchmarkReference coarseReference;
    ps::BenchmarkReference fineReference;
    if (benchmark == "axial") {
      coarseReference = ps::axial_tension_bar_benchmark(1, 1, 1);
      fineReference = ps::axial_tension_bar_benchmark(4, 2, 2);
    } else if (benchmark == "cantilever") {
      coarseReference = ps::cantilever_benchmark(20, 3, 3);
      fineReference = ps::cantilever_benchmark(40, 6, 6);
    } else {
      throw std::invalid_argument("unknown benchmark");
    }

    const auto coarse = execute(ccx, output / "coarse",
                                "prometheus_" + benchmark + "_coarse",
                                coarseReference);
    const auto fine = execute(ccx, output / "fine",
                              "prometheus_" + benchmark + "_fine",
                              fineReference);
    const auto refinement = ps::compile_structural_refinement_evidence(
        *coarse.run.validated_result, *fine.run.validated_result, 0.10);
    const auto findings = ps::compile_structural_findings(
        fineReference.setup.request, fine.run.validated_result, refinement);
    const bool scopedPass = findings.evaluated_obligations == 2 &&
        std::ranges::all_of(findings.findings, [](const auto &finding) {
          return finding.disposition ==
              ps::StructuralFindingDisposition::no_violation_detected_within_scope;
        });

    std::cout << std::scientific << std::setprecision(10)
              << "fine_displacement_relative_error="
              << fine.analytic_comparison.displacement_relative_error << '\n'
              << "fine_stress_relative_error="
              << fine.analytic_comparison.stress_relative_error << '\n'
              << "coarse_to_fine_change_fraction="
              << refinement.coarse_to_fine_change_fraction << '\n';
    if (!fine.analytic_comparison.passed() ||
        !refinement.criteria_satisfied || !scopedPass) {
      std::cerr << "benchmark validation gate failed\n";
      return 5;
    }
    const auto archive = ps::write_structural_archive(
        output / "fine", "prometheus_" + benchmark + "_fine",
        fineReference.setup, fine.run, findings);
    std::cout << "benchmark=passed\n"
              << "archive_manifest=" << archive.manifest_path.string() << '\n'
              << "archive_sha256=" << archive.manifest_sha256 << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}
