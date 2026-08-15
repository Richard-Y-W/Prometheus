#pragma once

#include <prometheus/run_store/run_store.hpp>

#include <filesystem>
#include <vector>

namespace prometheus::run_store {

inline constexpr std::size_t structural_artifact_chunk_bytes =
    700U * 1024U;

struct StructuralArchiveObjects final {
  ObjectToStore archive_manifest;
  std::vector<ObjectToStore> chunks;
  ObjectToStore project_manifest;
};

[[nodiscard]] Result<StructuralArchiveObjects> build_structural_archive_objects(
    const std::filesystem::path &archive_manifest_path) noexcept;

// Reconstructs into a destination that must not exist and publishes by atomic
// directory rename only after every decoded artifact matches the archive.
[[nodiscard]] Result<std::filesystem::path> reconstruct_structural_archive(
    const std::filesystem::path &project_path,
    const StoredObjectReference &project_manifest,
    const std::filesystem::path &destination_directory) noexcept;

} // namespace prometheus::run_store
