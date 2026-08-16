#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_archive.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_refinement.hpp"

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
  ps::SolverRunOptions options;
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
  for (const auto suffix : {".reviewed-structural-setup.json",
                            ".stdout.txt", ".stderr.txt"}) {
    std::error_code ignored;
    fs::remove(root / (job + suffix), ignored);
  }
  {
    std::error_code ignored;
    fs::remove(root / "prometheus-structural-run.json", ignored);
  }
  const ps::SolverRunOptions options{
      ccx, root, job, std::chrono::seconds(120)};
  auto solver = ps::run_calculix(options, reference.setup);
  std::cout << solver.standard_output;
  std::cerr << solver.standard_error;
  if (solver.status != ps::SolverRunStatus::completed ||
      !solver.validated_result || !solver.validated_result->metrics)
    throw std::runtime_error(job + " failed: " + solver.detail);
  const auto comparison =
      ps::compare_benchmark(reference, *solver.validated_result->metrics);
  return {options, std::move(solver), comparison};
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
    const auto coarse = execute(ccx, root, "cantilever_coarse",
                                coarseReference);
    const auto fine = execute(ccx, root, "cantilever_fine",
                              fineReference);
    const auto criterion =
        ps::compile_structural_refinement_criterion(0.10);
    const auto coarseSample = ps::compile_completed_structural_sample(
        ps::StructuralSampleRole::coarse, criterion, coarse.options,
        coarseReference.setup, coarse.solver);
    const auto fineSample = ps::compile_completed_structural_sample(
        ps::StructuralSampleRole::fine, criterion, fine.options,
        fineReference.setup, fine.solver);
    const auto boundaryReview =
        ps::review_structural_boundary_correspondence(
            coarseReference.setup, fineReference.setup, true, true);
    const auto compiled = ps::compile_structural_refinement(
        coarseSample, fineSample, boundaryReview);
    if (!compiled.complete())
      throw std::runtime_error(compiled.issues().front().code + ": " +
                               compiled.issues().front().message);
    const auto evaluation =
        ps::compile_structural_findings(*compiled.value());
    const bool scopedPass = evaluation.evaluated_obligations == 2 &&
        std::ranges::all_of(evaluation.findings, [](const auto &finding) {
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
              << "displacement_change_fraction="
              << compiled.value()->displacement_change_fraction() << '\n'
              << "stress_change_fraction="
              << compiled.value()->stress_change_fraction() << '\n'
              << "maximum_change_fraction="
              << compiled.value()->maximum_change_fraction() << '\n';
    if (!fine.analytic.passed() ||
        compiled.value()->status() != ps::StructuralRefinementStatus::accepted ||
        !scopedPass) {
      std::cerr << "cantilever refinement gate failed\n";
      return 4;
    }
    const auto archive = ps::write_structural_refinement_archive(
        *compiled.value(), evaluation);
    std::cout << "refinement=passed\n"
              << "archive_manifest=" << archive.manifest_path.string() << '\n'
              << "archive_sha256=" << archive.manifest_sha256 << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}
