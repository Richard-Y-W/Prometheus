#pragma once

#include <prometheus/run_store/run_store.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace prometheus::run_store {

inline constexpr std::string_view project_evidence_archive_schema_id =
    "urn:prometheus:schema:project-evidence-archive:1.0.0";
inline constexpr std::string_view project_evidence_archive_media_type =
    "application/vnd.prometheus.project-evidence-archive+json";
inline constexpr std::string_view project_evidence_chunk_schema_id =
    "urn:prometheus:schema:project-evidence-chunk:1.0.0";
inline constexpr std::string_view project_evidence_chunk_media_type =
    "application/vnd.prometheus.project-evidence-chunk+json";
inline constexpr std::size_t project_evidence_chunk_bytes = 700U * 1024U;
inline constexpr std::uint64_t project_evidence_file_limit = 32U * 1024U * 1024U;
inline constexpr std::uint64_t project_evidence_total_limit = 128U * 1024U * 1024U;

struct ProjectEvidenceInput final {
  std::string relative_path;
  std::filesystem::path absolute_path;
  std::uint64_t byte_length{};
  std::optional<std::string> sha256;
  std::string category;
  std::string analysis_state;
};

struct ProjectEvidenceArchiveObjects final {
  ObjectToStore manifest;
  std::vector<ObjectToStore> chunks;
};

[[nodiscard]] Result<ProjectEvidenceArchiveObjects>
build_project_evidence_archive(
    const StoredObjectReference &inventory_snapshot,
    const std::vector<ProjectEvidenceInput> &files) noexcept;

[[nodiscard]] Result<bool> validate_project_evidence_archive(
    const ObjectToStore &inventory_snapshot,
    const ProjectEvidenceArchiveObjects &archive) noexcept;

[[nodiscard]] Result<bool> verify_project_evidence_archive(
    const std::filesystem::path &project_path,
    const StoredObjectReference &archive_manifest) noexcept;

[[nodiscard]] Result<std::filesystem::path> reconstruct_project_evidence_archive(
    const std::filesystem::path &project_path,
    const StoredObjectReference &archive_manifest,
    const std::filesystem::path &destination_directory) noexcept;

} // namespace prometheus::run_store
