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
#include <string_view>

namespace ps = prometheus::structural;
namespace fs = std::filesystem;

namespace {

struct ExecutedBenchmark final {
  ps::SolverRunOptions options;
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
  for (const auto suffix : {".reviewed-structural-setup.json",
                            ".stdout.txt", ".stderr.txt"}) {
    std::error_code ignored;
    fs::remove(root / (job + suffix), ignored);
  }
}

ExecutedBenchmark execute(const fs::path &ccx, const fs::path &root,
                          const std::string &job,
                          const ps::BenchmarkReference &reference) {
  fs::create_directories(root);
  clear_job_artifacts(root, job);
  const ps::SolverRunOptions options{
      ccx, root, job, std::chrono::seconds(120)};
  auto run = ps::run_calculix(options, reference.setup);
  std::cout << run.standard_output;
  std::cerr << run.standard_error;
  if (run.status != ps::SolverRunStatus::completed ||
      !run.validated_result || !run.validated_result->metrics)
    throw std::runtime_error(job + " failed: " + run.detail);
  const auto comparison =
      ps::compare_benchmark(reference, *run.validated_result->metrics);
  return {options, std::move(run), comparison};
}

const ps::StructuralObservableComparison &observable(
    const ps::VerifiedStructuralRefinement &refinement,
    const std::string_view id) {
  const auto found = std::ranges::find_if(
      refinement.observable_comparisons(), [&](const auto &comparison) {
        return comparison.definition.spec.id == id;
      });
  if (found == refinement.observable_comparisons().end())
    throw std::runtime_error("required refinement observable is missing");
  return *found;
}

const ps::StructuralGlobalExtremumDiagnostic &globalStressDiagnostic(
    const ps::VerifiedStructuralRefinement &refinement) {
  const auto found = std::ranges::find_if(
      refinement.global_extremum_diagnostics(), [](const auto &diagnostic) {
        return diagnostic.quantity ==
            ps::StructuralObservableQuantity::von_mises_stress_pa;
      });
  if (found == refinement.global_extremum_diagnostics().end())
    throw std::runtime_error("global stress diagnostic is missing");
  return *found;
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
      const auto meshes = ps::cantilever_validation_mesh_pair();
      coarseReference = ps::cantilever_benchmark(
          meshes.coarse.length_divisions, meshes.coarse.width_divisions,
          meshes.coarse.height_divisions);
      fineReference = ps::cantilever_benchmark(
          meshes.fine.length_divisions, meshes.fine.width_divisions,
          meshes.fine.height_divisions);
    } else {
      throw std::invalid_argument("unknown benchmark");
    }

    const auto coarse = execute(ccx, output,
                                "prometheus_" + benchmark + "_coarse",
                                coarseReference);
    const auto fine = execute(ccx, output,
                              "prometheus_" + benchmark + "_fine",
                              fineReference);
    const auto criterion = ps::compile_structural_refinement_criterion(
        benchmark == "axial"
            ? ps::global_structural_observable_specs(0.10)
            : ps::cantilever_validation_observable_specs());
    const auto coarseSample = ps::compile_completed_structural_sample(
        ps::StructuralSampleRole::coarse, criterion, coarse.options,
        coarseReference.setup, coarse.run);
    const auto fineSample = ps::compile_completed_structural_sample(
        ps::StructuralSampleRole::fine, criterion, fine.options,
        fineReference.setup, fine.run);
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
    const bool axial = benchmark == "axial";
    const bool scopedPass =
        axial
            ? evaluation.evaluated_obligations == 2 &&
                  evaluation.unknowns.empty() &&
                  std::ranges::all_of(
                      evaluation.findings, [](const auto &finding) {
                        return finding.disposition ==
                            ps::StructuralFindingDisposition::no_violation_detected_within_scope;
                      })
            : evaluation.declared_obligations == 2 &&
                  evaluation.evaluated_obligations == 1 &&
                  evaluation.findings.size() == 1U &&
                  evaluation.findings.front().obligation ==
                      "maximum_displacement" &&
                  evaluation.findings.front().disposition ==
                      ps::StructuralFindingDisposition::no_violation_detected_within_scope &&
                  evaluation.unknowns.size() == 1U &&
                  evaluation.unknowns.front().code ==
                      "matching_converged_scope_missing";
    const auto analytic =
        axial ? fine.analytic_comparison
              : ps::compare_cantilever_validation(*compiled.value());

    std::cout << std::scientific << std::setprecision(10)
              << "fine_displacement_relative_error="
              << analytic.displacement_relative_error << '\n'
              << "fine_stress_relative_error="
              << analytic.stress_relative_error << '\n';
    bool diagnosticPass = true;
    if (!axial) {
      const auto &displacement = observable(
          *compiled.value(), "cantilever.maximum_displacement");
      const auto &sectionStress = observable(
          *compiled.value(), "cantilever.section_von_mises");
      const auto &globalStress = globalStressDiagnostic(*compiled.value());
      diagnosticPass = !globalStress.participated_in_acceptance;
      std::cout
          << "observable.cantilever.maximum_displacement.change_fraction="
          << displacement.change_fraction << '\n'
          << "observable.cantilever.section_von_mises.change_fraction="
          << sectionStress.change_fraction << '\n'
          << "global.maximum_von_mises_stress.change_fraction="
          << globalStress.change_fraction << '\n'
          << "global.maximum_von_mises_stress.participated_in_acceptance="
          << std::boolalpha << globalStress.participated_in_acceptance << '\n'
          << "global.maximum_von_mises_stress.status="
          << (globalStress.within_threshold
                  ? "converged_diagnostic_only"
                  : "not_converged_in_this_study")
          << '\n';
    }
    if (!analytic.passed() ||
        compiled.value()->status() != ps::StructuralRefinementStatus::accepted ||
        !diagnosticPass || !scopedPass) {
      std::cerr << "benchmark validation gate failed\n";
      return 5;
    }
    const auto archive = ps::write_structural_refinement_archive(
        *compiled.value(), evaluation);
    std::cout << "benchmark=passed\n"
              << "archive_schema_version=" << archive.schema_version << '\n'
              << "archive_manifest=" << archive.manifest_path.string() << '\n'
              << "archive_sha256=" << archive.manifest_sha256 << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}
