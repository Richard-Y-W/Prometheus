#pragma once

#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/types.hpp"
#include "prometheus/structural/structural_setup.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace prometheus::structural {

struct StructuralArchive final {
  std::filesystem::path manifest_path;
  std::string manifest_sha256;
  std::string schema_version;
  std::string validated_result_identity;
};

struct StructuralArchiveVerification final {
  bool valid{};
  std::string code;
  std::string detail;
  std::optional<CalculixMetrics> metrics;
  int declared_obligations{};
  int evaluated_obligations{};
  std::string schema_version;
  std::string validated_result_identity;
  std::optional<CalculixDat> normalized;
  std::optional<StructuralSetup> reviewed_setup;
  std::optional<CompiledStructuralSetup> compiled_setup;
  std::optional<StructuralEvaluation> evaluation;
};

[[nodiscard]] StructuralArchive write_structural_archive(
    const std::filesystem::path &working_directory, std::string job_name,
    const CompiledStructuralSetup &setup,
    const SolverRunResult &run, const StructuralEvaluation &evaluation);

[[nodiscard]] StructuralArchiveVerification verify_structural_archive(
    const std::filesystem::path &manifest_path) noexcept;

// Copies a verified archive into a new directory through a sibling temporary
// directory, verifies the copy, then atomically publishes it by rename.
[[nodiscard]] StructuralArchive export_structural_archive(
    const std::filesystem::path &manifest_path,
    const std::filesystem::path &destination_directory);

} // namespace prometheus::structural
