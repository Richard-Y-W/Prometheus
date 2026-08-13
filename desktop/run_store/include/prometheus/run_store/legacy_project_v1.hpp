#pragma once

#include <prometheus/run_store/project_v2.hpp>

#include <filesystem>
#include <string_view>

namespace prometheus::run_store {

struct LegacyProjectV1 final {
  // A strict conversion snapshot. The assembly hash is an explicit placeholder
  // until the desktop project owner hashes the referenced CAD artifact during
  // Save As. Execution references are always empty.
  ProjectV2 project;
};

[[nodiscard]] Result<LegacyProjectV1>
parse_legacy_project_v1(std::string_view bytes) noexcept;

[[nodiscard]] Result<LegacyProjectV1>
open_legacy_project_v1(const std::filesystem::path &project_path) noexcept;

} // namespace prometheus::run_store
