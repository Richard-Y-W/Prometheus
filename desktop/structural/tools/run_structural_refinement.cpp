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

struct Run final {
  ps::SolverRunOptions options;
  ps::SolverRunResult solver;
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
      ccx, root, job, std::chrono::minutes(5)};
  auto solver = ps::run_calculix(options, reference.setup);
  std::cout << solver.standard_output;
  std::cerr << solver.standard_error;
  if (solver.status != ps::SolverRunStatus::completed ||
      !solver.validated_result || !solver.validated_result->metrics)
    throw std::runtime_error(job + " failed: " + solver.detail);
  return {options, std::move(solver)};
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
  if (argc < 3 || argc > 4 ||
      (argc == 4 && std::string_view(argv[3]) != "--smoke")) {
    std::cerr << "usage: prometheus_run_structural_refinement CCX "
                 "OUTPUT_DIRECTORY [--smoke]\n";
    return 2;
  }
  try {
    const fs::path ccx = fs::absolute(argv[1]);
    const fs::path root = fs::absolute(argv[2]);
    const bool smoke = argc == 4;
    const auto meshes = smoke
                            ? ps::CantileverValidationMeshPair{
                                  .coarse = {20, 3, 3},
                                  .fine = {40, 6, 6}}
                            : ps::cantilever_validation_mesh_pair();
    const auto coarseReference = ps::cantilever_benchmark(
        meshes.coarse.length_divisions, meshes.coarse.width_divisions,
        meshes.coarse.height_divisions);
    const auto fineReference = ps::cantilever_benchmark(
        meshes.fine.length_divisions, meshes.fine.width_divisions,
        meshes.fine.height_divisions);
    const auto coarse = execute(ccx, root, "cantilever_coarse",
                                coarseReference);
    const auto fine = execute(ccx, root, "cantilever_fine",
                              fineReference);
    const auto criterion = ps::compile_structural_refinement_criterion(
        ps::cantilever_validation_observable_specs());
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
    const auto analytic =
        ps::compare_cantilever_validation(*compiled.value());
    const bool scopedPass =
        evaluation.declared_obligations == 2 &&
        evaluation.evaluated_obligations == 1 &&
        evaluation.findings.size() == 1U &&
        evaluation.findings.front().obligation == "maximum_displacement" &&
        evaluation.findings.front().disposition ==
            ps::StructuralFindingDisposition::no_violation_detected_within_scope &&
        evaluation.unknowns.size() == 1U &&
        evaluation.unknowns.front().obligation ==
            "maximum_von_mises_stress" &&
        evaluation.unknowns.front().code ==
            "matching_converged_scope_missing";
    const auto &displacement = observable(
        *compiled.value(), "cantilever.maximum_displacement");
    const auto &sectionStress = observable(
        *compiled.value(), "cantilever.section_von_mises");
    const auto &globalStress = globalStressDiagnostic(*compiled.value());
    std::cout << std::scientific << std::setprecision(10)
              << "fine_displacement_relative_error="
              << analytic.displacement_relative_error << '\n'
              << "fine_section_stress_relative_error="
              << analytic.stress_relative_error << '\n'
              << "observable.cantilever.maximum_displacement.change_fraction="
              << displacement.change_fraction << '\n'
              << "observable.cantilever.section_von_mises.change_fraction="
              << sectionStress.change_fraction << '\n'
              << "global.maximum_von_mises_stress.change_fraction="
              << globalStress.change_fraction << '\n'
              << "global.maximum_von_mises_stress.participated_in_acceptance="
              << std::boolalpha << globalStress.participated_in_acceptance
              << '\n'
              << "global.maximum_von_mises_stress.status="
              << (globalStress.within_threshold
                      ? "converged_diagnostic_only"
                      : "not_converged_in_this_study")
              << '\n';
    if (!analytic.passed() ||
        compiled.value()->status() != ps::StructuralRefinementStatus::accepted ||
        globalStress.participated_in_acceptance || !scopedPass) {
      std::cerr << "cantilever refinement gate failed\n";
      return 4;
    }
    const auto archive = ps::write_structural_refinement_archive(
        *compiled.value(), evaluation);
    if (smoke)
      std::cout << "validation_profile=smoke_non_authoritative\n";
    std::cout << "refinement="
              << (smoke ? "smoke_passed" : "passed") << '\n'
              << "archive_schema_version=" << archive.schema_version << '\n'
              << "archive_manifest=" << archive.manifest_path.string() << '\n'
              << "archive_sha256=" << archive.manifest_sha256 << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}
