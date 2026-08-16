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
};

struct StructuralArchiveVerification final {
  bool valid{};
  std::string code;
  std::string detail;
  std::optional<CalculixMetrics> metrics;
  int declared_obligations{};
  int evaluated_obligations{};
};

[[nodiscard]] std::string serialize_structural_setup_evidence(
    const StructuralSetup &setup);

[[nodiscard]] StructuralArchive write_structural_archive(
    const std::filesystem::path &working_directory, std::string job_name,
    std::string solver_identity, std::string reviewed_setup_bytes,
    const StructuralRequest &request,
    const SolverRunResult &run, const StructuralEvaluation &evaluation);

[[nodiscard]] StructuralArchiveVerification verify_structural_archive(
    const std::filesystem::path &manifest_path) noexcept;

// Copies a verified archive into a new directory through a sibling temporary
// directory, verifies the copy, then atomically publishes it by rename.
[[nodiscard]] StructuralArchive export_structural_archive(
    const std::filesystem::path &manifest_path,
    const std::filesystem::path &destination_directory);

} // namespace prometheus::structural
