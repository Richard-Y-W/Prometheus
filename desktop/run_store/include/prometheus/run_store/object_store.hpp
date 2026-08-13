#pragma once

#include <prometheus/run_store/project_v2.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace prometheus::run_store {

struct InstalledObject final {
  std::filesystem::path object_path;
  bool already_present;
};

[[nodiscard]] std::filesystem::path
sidecar_path_for_project(const std::filesystem::path &project_path);
[[nodiscard]] Result<std::filesystem::path>
object_path_for_hash(const std::filesystem::path &sidecar_root,
                     std::string_view object_hash) noexcept;
[[nodiscard]] std::filesystem::path
temporary_path_for_object(const std::filesystem::path &object_path);

[[nodiscard]] Result<InstalledObject>
install_object(const std::filesystem::path &project_path,
               const StoredObjectReference &reference,
               std::string_view bytes) noexcept;
[[nodiscard]] Result<std::string>
read_object(const std::filesystem::path &project_path,
            const StoredObjectReference &reference) noexcept;

} // namespace prometheus::run_store
