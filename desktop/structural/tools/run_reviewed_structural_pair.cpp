#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/reviewed_pair.hpp"
#include "prometheus/structural/structural_archive.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_refinement.hpp"

#include <charconv>
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

std::chrono::milliseconds timeout_from(const std::string_view value) {
  unsigned int seconds{};
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), seconds);
  if (error != std::errc{} || end != value.data() + value.size() ||
      seconds == 0U || seconds > 3600U)
    throw std::invalid_argument(
        "timeout must be an integer from 1 through 3600 seconds");
  return std::chrono::seconds(seconds);
}

std::string evaluation_disposition(const ps::StructuralEvaluation &evaluation) {
  if (evaluation.findings.empty()) return "indeterminate";
  for (const auto &finding : evaluation.findings)
    if (finding.disposition == ps::StructuralFindingDisposition::violated)
      return "violated";
  return "no_violation_detected_within_scope";
}

ps::CompletedStructuralSamplePtr execute_sample(
    const ps::StructuralSampleRole role,
    const ps::StructuralRefinementCriterion &criterion,
    ps::SolverRunOptions options,
    ps::CompiledStructuralSetup setup) {
  auto run = ps::run_calculix(options, setup);
  std::cout << run.standard_output;
  std::cerr << run.standard_error;
  if (run.status != ps::SolverRunStatus::completed ||
      !run.validated_result || !run.validated_result->complete())
    throw std::runtime_error(options.job_name + " failed: " + run.detail);
  return ps::compile_completed_structural_sample(
      role, criterion, std::move(options), std::move(setup), std::move(run));
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 4 || argc > 5) {
    std::cerr <<
        "usage: prometheus_run_reviewed_structural_pair "
        "REVIEWED_PAIR_JSON CCX OUTPUT_DIRECTORY [TIMEOUT_SECONDS]\n";
    return 2;
  }
  try {
    const auto manifest = fs::absolute(argv[1]);
    const auto ccx = fs::absolute(argv[2]);
    const auto output = fs::absolute(argv[3]);
    const auto timeout = argc == 5 ? timeout_from(argv[4])
                                   : std::chrono::minutes(5);

    // Complete both setup compilations before creating any run output or
    // starting the authoritative backend.
    auto pair = ps::preflight_reviewed_structural_pair(manifest);
    if (!fs::is_regular_file(ccx))
      throw std::invalid_argument("CalculiX executable must be a regular file");
    if (fs::exists(output))
      throw std::invalid_argument("output directory must not already exist");
    const auto parent = output.parent_path();
    if (parent.empty() || !fs::is_directory(parent) ||
        !fs::create_directory(output))
      throw std::invalid_argument(
          "output parent must exist and a new output directory must be creatable");

    const auto coarseOptions = ps::SolverRunOptions{
        ccx, output, pair.coarse_job_name, timeout};
    const auto fineOptions = ps::SolverRunOptions{
        ccx, output, pair.fine_job_name, timeout};
    auto coarse = execute_sample(
        ps::StructuralSampleRole::coarse, pair.criterion, coarseOptions,
        std::move(pair.coarse_setup));
    auto fine = execute_sample(
        ps::StructuralSampleRole::fine, pair.criterion, fineOptions,
        std::move(pair.fine_setup));

    const auto compiled = ps::compile_structural_refinement(
        std::move(coarse), std::move(fine), pair.boundary_correspondence);
    if (!compiled.complete()) {
      const auto detail = compiled.issues().empty()
                              ? std::string("unknown refinement failure")
                              : compiled.issues().front().code + ": " +
                                    compiled.issues().front().message;
      throw std::runtime_error(detail);
    }
    const auto evaluation =
        ps::compile_structural_findings(*compiled.value());
    const auto archive = ps::write_structural_refinement_archive(
        *compiled.value(), evaluation);

    std::cout << std::scientific << std::setprecision(10)
              << "status=completed\n"
              << "reviewed_pair_manifest_identity="
              << pair.manifest_identity << '\n'
              << "coarse_setup_identity="
              << compiled.value()->coarse().setup().identity << '\n'
              << "coarse_result_identity="
              << compiled.value()->coarse().run().validated_result->identity
              << '\n'
              << "fine_setup_identity="
              << compiled.value()->fine().setup().identity << '\n'
              << "fine_result_identity="
              << compiled.value()->fine().run().validated_result->identity
              << '\n';
    for (const auto &observable :
         compiled.value()->observable_comparisons())
      std::cout << "observable." << observable.definition.spec.id
                << ".coarse=" << observable.coarse_value << '\n'
                << "observable." << observable.definition.spec.id
                << ".fine=" << observable.fine_value << '\n'
                << "observable." << observable.definition.spec.id
                << ".change_fraction=" << observable.change_fraction << '\n';
    std::cout << "refinement="
              << (compiled.value()->status() ==
                          ps::StructuralRefinementStatus::accepted
                      ? "accepted"
                      : "indeterminate")
              << '\n'
              << "evaluation=" << evaluation_disposition(evaluation) << '\n'
              << "coverage=" << evaluation.evaluated_obligations << '/'
              << evaluation.declared_obligations << '\n'
              << "archive_schema_version=" << archive.schema_version << '\n'
              << "archive_manifest=" << archive.manifest_path.string() << '\n'
              << "archive_sha256=" << archive.manifest_sha256 << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}
