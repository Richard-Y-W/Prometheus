#pragma once

#include <prometheus/run_store/project_v2.hpp>

#include <filesystem>

namespace prometheus::run_store {

struct ProjectBundle final {
  std::filesystem::path bundle_directory;
  std::filesystem::path project_path;
  std::filesystem::path manifest_path;
  std::size_t object_count{};
};

[[nodiscard]] Result<ProjectBundle> export_project_bundle(
    const std::filesystem::path &project_path,
    const std::filesystem::path &destination_directory) noexcept;

[[nodiscard]] Result<ProjectBundle> verify_project_bundle(
    const std::filesystem::path &bundle_directory) noexcept;

} // namespace prometheus::run_store
