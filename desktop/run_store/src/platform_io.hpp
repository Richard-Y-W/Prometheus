#pragma once

#include <prometheus/run_store/object_store.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace prometheus::run_store::detail {

struct Unit final {};

enum class LockMode { shared, exclusive };

class ProjectLock final {
public:
  explicit ProjectLock(std::intptr_t native_handle) noexcept;
  ProjectLock(const ProjectLock &) = delete;
  ProjectLock &operator=(const ProjectLock &) = delete;
  ProjectLock(ProjectLock &&other) noexcept;
  ProjectLock &operator=(ProjectLock &&other) noexcept;
  ~ProjectLock();

private:
  std::intptr_t native_handle_{-1};
};

[[nodiscard]] Diagnostic
store_diagnostic(std::string code, std::string message,
                 std::optional<std::string> field = std::nullopt,
                 std::optional<std::filesystem::path> path = std::nullopt);

[[nodiscard]] std::optional<Diagnostic>
check_boundary(const TransactionOptions &options,
               TransactionBoundary boundary) noexcept;

[[nodiscard]] std::optional<Diagnostic>
check_cancelled(const TransactionOptions &options) noexcept;

[[nodiscard]] Result<Unit>
verify_stored_object(const StoredObjectReference &reference,
                     std::string_view bytes) noexcept;

[[nodiscard]] Result<ProjectLock>
acquire_project_lock(const std::filesystem::path &project_path,
                     LockMode mode, bool create_sidecar,
                     std::chrono::milliseconds timeout) noexcept;

[[nodiscard]] Result<std::string>
read_project_index_file(const std::filesystem::path &project_path) noexcept;

[[nodiscard]] Result<Unit>
replace_project_index_file(const std::filesystem::path &project_path,
                           std::string_view bytes, bool replace_existing,
                           const TransactionOptions &options) noexcept;

[[nodiscard]] Result<InstalledObject>
install_object_file(const std::filesystem::path &project_path,
                    const StoredObjectReference &reference,
                    std::string_view bytes,
                    const TransactionOptions &options) noexcept;

[[nodiscard]] Result<std::string>
read_object_file(const std::filesystem::path &project_path,
                 const StoredObjectReference &reference) noexcept;

} // namespace prometheus::run_store::detail
