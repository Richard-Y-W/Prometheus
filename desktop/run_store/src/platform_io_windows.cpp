#define NOMINMAX
#include "platform_io.hpp"

#include <prometheus/run_store/project_v2.hpp>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace prometheus::run_store::detail {
namespace {

using namespace std::chrono_literals;
constexpr std::wstring_view previous_project_index_name =
    L".project-index.previous";

class NativeHandle final {
public:
  explicit NativeHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  NativeHandle(const NativeHandle &) = delete;
  NativeHandle &operator=(const NativeHandle &) = delete;
  NativeHandle(NativeHandle &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
  NativeHandle &operator=(NativeHandle &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
    }
    return *this;
  }
  ~NativeHandle() { reset(); }

  [[nodiscard]] HANDLE get() const noexcept { return value_; }
  [[nodiscard]] bool valid() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }
  [[nodiscard]] HANDLE release() noexcept {
    return std::exchange(value_, INVALID_HANDLE_VALUE);
  }
  void reset() noexcept {
    if (valid()) {
      static_cast<void>(::CloseHandle(value_));
    }
    value_ = INVALID_HANDLE_VALUE;
  }

private:
  HANDLE value_;
};

class TemporaryPath final {
public:
  TemporaryPath(std::filesystem::path path, NativeHandle &handle)
      : path_(std::move(path)), handle_(&handle) {}
  TemporaryPath(const TemporaryPath &) = delete;
  TemporaryPath &operator=(const TemporaryPath &) = delete;
  ~TemporaryPath() {
    if (armed_) {
      handle_->reset();
      static_cast<void>(::DeleteFileW(path_.c_str()));
    }
  }
  void release() noexcept { armed_ = false; }

private:
  std::filesystem::path path_;
  NativeHandle *handle_;
  bool armed_{true};
};

struct SafeDirectory final {
  NativeHandle handle;
  std::filesystem::path path;
  std::wstring resolved;
};

struct AnchoredProject final {
  SafeDirectory parent;
  std::wstring project_name;
  std::wstring sidecar_name;
  std::filesystem::path project_path;
};

struct ObjectDirectory final {
  SafeDirectory fanout;
  std::wstring destination_name;
  std::filesystem::path destination_path;
};

template <typename T>
Result<T> fail(std::string code, std::string message,
               std::optional<std::filesystem::path> path = std::nullopt) {
  return Result<T>::failure(store_diagnostic(
      std::move(code), std::move(message), std::nullopt, std::move(path)));
}

std::string windows_error(const DWORD error = ::GetLastError()) {
  return "Windows error " + std::to_string(error);
}

std::wstring normalize_path(std::wstring value) {
  std::replace(value.begin(), value.end(), L'/', L'\\');
  while (value.size() > 4U && value.back() == L'\\') {
    value.pop_back();
  }
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const wchar_t character) {
                   return static_cast<wchar_t>(std::towlower(character));
                 });
  return value;
}

Result<std::wstring> resolved_path(const HANDLE handle,
                                   const std::filesystem::path &display_path) {
  const DWORD required = ::GetFinalPathNameByHandleW(
      handle, nullptr, 0U, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (required == 0U) {
    return fail<std::wstring>("path_resolution_failed", windows_error(),
                              display_path);
  }
  std::wstring value(required, L'\0');
  const DWORD written = ::GetFinalPathNameByHandleW(
      handle, value.data(), required, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (written == 0U || written >= required) {
    return fail<std::wstring>("path_resolution_failed", windows_error(),
                              display_path);
  }
  value.resize(written);
  return Result<std::wstring>::success(normalize_path(std::move(value)));
}

std::wstring child_resolved_path(const SafeDirectory &parent,
                                 const std::wstring_view name) {
  return normalize_path(parent.resolved + L"\\" + std::wstring(name));
}

bool safe_name(const std::wstring_view name) {
  return !name.empty() && name != L"." && name != L".." &&
         name.find(L'\\') == std::wstring_view::npos &&
         name.find(L'/') == std::wstring_view::npos &&
         name.find(L'\0') == std::wstring_view::npos &&
         name.find(L':') == std::wstring_view::npos;
}

Result<SafeDirectory>
open_safe_directory(const std::filesystem::path &path, const bool create,
                    std::string missing_code, std::string unsafe_code) {
  DWORD attributes = ::GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = ::GetLastError();
    if (!create ||
        (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)) {
      return fail<SafeDirectory>(std::move(missing_code),
                                 windows_error(error), path);
    }
    if (::CreateDirectoryW(path.c_str(), nullptr) == 0) {
      const DWORD create_error = ::GetLastError();
      if (create_error != ERROR_ALREADY_EXISTS) {
        return fail<SafeDirectory>("store_directory_create_failed",
                                   windows_error(create_error), path);
      }
    }
    attributes = ::GetFileAttributesW(path.c_str());
  }
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return fail<SafeDirectory>(std::move(unsafe_code),
                               "directory is missing, non-directory, or a reparse point",
                               path);
  }
  NativeHandle handle(::CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES | GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!handle.valid()) {
    return fail<SafeDirectory>(std::move(unsafe_code), windows_error(), path);
  }
  BY_HANDLE_FILE_INFORMATION information{};
  if (::GetFileInformationByHandle(handle.get(), &information) == 0 ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return fail<SafeDirectory>(std::move(unsafe_code),
                               "opened directory handle is unsafe", path);
  }
  auto resolved = resolved_path(handle.get(), path);
  if (!resolved.has_value()) {
    return Result<SafeDirectory>::failure(resolved.diagnostic());
  }
  return Result<SafeDirectory>::success(
      SafeDirectory{std::move(handle), path, std::move(resolved.value())});
}

Result<SafeDirectory> open_child_directory(const SafeDirectory &parent,
                                           const std::wstring_view name,
                                           const bool create,
                                           std::string missing_code =
                                               "object_store_missing") {
  if (!safe_name(name)) {
    return fail<SafeDirectory>("unsafe_store_path",
                               "directory component name is unsafe",
                               parent.path / std::wstring(name));
  }
  auto current_parent =
      open_safe_directory(parent.path, false, "object_store_missing",
                          "unsafe_store_path");
  if (!current_parent.has_value() ||
      current_parent.value().resolved != parent.resolved) {
    return fail<SafeDirectory>(
        "unsafe_store_path",
        "anchored parent directory identity changed before mutation",
        parent.path);
  }
  auto child = open_safe_directory(parent.path / std::wstring(name), create,
                                   std::move(missing_code),
                                   "unsafe_store_path");
  if (!child.has_value()) {
    return child;
  }
  if (child.value().resolved != child_resolved_path(parent, name)) {
    return fail<SafeDirectory>(
        "unsafe_store_path",
        "resolved child directory escapes its anchored parent",
        child.value().path);
  }
  return child;
}

Result<AnchoredProject>
anchor_project(const std::filesystem::path &project_path) {
  const auto filename = project_path.filename().wstring();
  if (!safe_name(filename)) {
    return fail<AnchoredProject>("unsafe_project_path",
                                 "project path has no safe filename",
                                 project_path);
  }
  auto parent_path = project_path.parent_path();
  if (parent_path.empty()) {
    parent_path = L".";
  }
  auto parent = open_safe_directory(parent_path, false, "project_parent_missing",
                                    "unsafe_project_path");
  if (!parent.has_value()) {
    return Result<AnchoredProject>::failure(parent.diagnostic());
  }
  return Result<AnchoredProject>::success(AnchoredProject{
      std::move(parent.value()), filename, filename + L".data", project_path});
}

Result<SafeDirectory> open_sidecar(const AnchoredProject &anchor,
                                   const bool create) {
  return open_child_directory(anchor.parent, anchor.sidecar_name, create,
                              "execution_store_missing");
}

Result<Unit> require_project_state(const AnchoredProject &anchor,
                                   const bool must_exist) {
  const auto path = anchor.parent.path / anchor.project_name;
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = ::GetLastError();
    if (!must_exist &&
        (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
      return Result<Unit>::success(Unit{});
    }
    return fail<Unit>(must_exist ? "project_missing" : "project_path_error",
                      windows_error(error), path);
  }
  if (!must_exist) {
    return fail<Unit>("project_exists", "Save As destination already exists",
                      path);
  }
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return fail<Unit>("unsafe_project_path",
                      "project is not a regular non-reparse-point file", path);
  }
  return Result<Unit>::success(Unit{});
}

Result<std::string>
read_safe_file(const std::filesystem::path &path,
               const SafeDirectory &expected_parent,
               const std::wstring_view expected_name,
               const std::size_t maximum_bytes, std::string unsafe_code,
               std::string read_code) {
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return fail<std::string>(std::move(read_code), windows_error(), path);
  }
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return fail<std::string>(std::move(unsafe_code),
                             "file is a directory or reparse point", path);
  }
  NativeHandle file(::CreateFileW(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  if (!file.valid()) {
    return fail<std::string>(std::move(unsafe_code), windows_error(), path);
  }
  BY_HANDLE_FILE_INFORMATION before{};
  if (::GetFileInformationByHandle(file.get(), &before) == 0 ||
      (before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
      (before.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return fail<std::string>(std::move(unsafe_code),
                             "opened file handle is unsafe", path);
  }
  auto resolved = resolved_path(file.get(), path);
  if (!resolved.has_value()) {
    return Result<std::string>::failure(resolved.diagnostic());
  }
  if (resolved.value() !=
      child_resolved_path(expected_parent, expected_name)) {
    return fail<std::string>(std::move(unsafe_code),
                             "resolved file escapes its anchored directory",
                             path);
  }
  LARGE_INTEGER size{};
  if (::GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart < 0 ||
      static_cast<std::uint64_t>(size.QuadPart) > maximum_bytes) {
    return fail<std::string>(std::move(read_code),
                             "file size is unavailable or over limit", path);
  }
  std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, std::numeric_limits<DWORD>::max()));
    DWORD received = 0U;
    if (::ReadFile(file.get(), bytes.data() + offset, requested, &received,
                   nullptr) == 0 ||
        received == 0U) {
      return fail<std::string>(std::move(read_code), windows_error(), path);
    }
    offset += received;
  }
  LARGE_INTEGER after_size{};
  BY_HANDLE_FILE_INFORMATION after{};
  if (::GetFileSizeEx(file.get(), &after_size) == 0 ||
      ::GetFileInformationByHandle(file.get(), &after) == 0 ||
      after_size.QuadPart != size.QuadPart ||
      after.dwVolumeSerialNumber != before.dwVolumeSerialNumber ||
      after.nFileIndexHigh != before.nFileIndexHigh ||
      after.nFileIndexLow != before.nFileIndexLow) {
    return fail<std::string>(std::move(read_code),
                             "file identity changed during read", path);
  }
  return Result<std::string>::success(std::move(bytes));
}

Result<Unit> validate_directory_identity(const SafeDirectory &directory) {
  auto current = open_safe_directory(directory.path, false,
                                     "object_store_missing",
                                     "unsafe_store_path");
  if (!current.has_value()) {
    return Result<Unit>::failure(current.diagnostic());
  }
  if (current.value().resolved != directory.resolved) {
    return fail<Unit>("unsafe_store_path",
                      "anchored directory identity changed", directory.path);
  }
  return Result<Unit>::success(Unit{});
}

Result<Unit> validate_regular_handle(const HANDLE handle,
                                     const SafeDirectory &parent,
                                     const std::wstring_view name,
                                     const std::filesystem::path &path,
                                     std::string code) {
  BY_HANDLE_FILE_INFORMATION information{};
  if (::GetFileInformationByHandle(handle, &information) == 0 ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return fail<Unit>(std::move(code), "opened file handle is unsafe", path);
  }
  const auto resolved = resolved_path(handle, path);
  if (!resolved.has_value()) {
    return Result<Unit>::failure(resolved.diagnostic());
  }
  if (resolved.value() != child_resolved_path(parent, name)) {
    return fail<Unit>(std::move(code),
                      "opened file escapes its anchored directory", path);
  }
  return Result<Unit>::success(Unit{});
}

Result<Unit> write_all(const HANDLE handle, const std::string_view bytes,
                       const std::filesystem::path &path) {
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, std::numeric_limits<DWORD>::max()));
    DWORD written = 0U;
    if (::WriteFile(handle, bytes.data() + offset, requested, &written,
                    nullptr) == 0 ||
        written == 0U) {
      return fail<Unit>("temporary_write_failed", windows_error(), path);
    }
    offset += written;
  }
  return Result<Unit>::success(Unit{});
}

std::wstring unique_temporary_name(const std::wstring_view base) {
  static std::atomic<std::uint64_t> counter{0U};
  return std::wstring(base) + L".tmp." +
         std::to_wstring(::GetCurrentProcessId()) + L"." +
         std::to_wstring(counter.fetch_add(1U));
}

Result<Unit> boundary(const TransactionOptions &options,
                      const TransactionBoundary point) {
  if (const auto failure = check_cancelled(options); failure.has_value()) {
    return Result<Unit>::failure(*failure);
  }
  if (const auto failure = check_boundary(options, point);
      failure.has_value()) {
    return Result<Unit>::failure(*failure);
  }
  if (const auto failure = check_cancelled(options); failure.has_value()) {
    return Result<Unit>::failure(*failure);
  }
  return Result<Unit>::success(Unit{});
}

void cleanup_project_temporaries(const AnchoredProject &anchor) {
  const auto pattern = anchor.parent.path /
                       (anchor.project_name + L".tmp.*");
  WIN32_FIND_DATAW data{};
  HANDLE search = ::FindFirstFileW(pattern.c_str(), &data);
  if (search == INVALID_HANDLE_VALUE) {
    return;
  }
  do {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U &&
        (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U) {
      static_cast<void>(
          ::DeleteFileW((anchor.parent.path / data.cFileName).c_str()));
    }
  } while (::FindNextFileW(search, &data) != 0);
  static_cast<void>(::FindClose(search));
}

Result<ObjectDirectory>
open_object_directory(const std::filesystem::path &project_path,
                      const std::string_view object_hash, const bool create) {
  auto anchor = anchor_project(project_path);
  if (!anchor.has_value()) {
    return Result<ObjectDirectory>::failure(anchor.diagnostic());
  }
  const auto project = require_project_state(anchor.value(), true);
  if (!project.has_value()) {
    return Result<ObjectDirectory>::failure(project.diagnostic());
  }
  auto sidecar = open_sidecar(anchor.value(), create);
  if (!sidecar.has_value()) {
    return Result<ObjectDirectory>::failure(sidecar.diagnostic());
  }
  auto objects = open_child_directory(sidecar.value(), L"objects", create);
  if (!objects.has_value()) {
    return Result<ObjectDirectory>::failure(objects.diagnostic());
  }
  auto sha256 = open_child_directory(objects.value(), L"sha256", create);
  if (!sha256.has_value()) {
    return Result<ObjectDirectory>::failure(sha256.diagnostic());
  }
  const auto digest = object_hash.substr(7U);
  const auto fanout_name =
      std::filesystem::path(std::string(digest.substr(0U, 2U))).wstring();
  auto fanout =
      open_child_directory(sha256.value(), fanout_name, create);
  if (!fanout.has_value()) {
    return Result<ObjectDirectory>::failure(fanout.diagnostic());
  }
  const auto destination_name =
      std::filesystem::path(std::string(digest.substr(2U))).wstring();
  const auto destination_path = fanout.value().path / destination_name;
  return Result<ObjectDirectory>::success(ObjectDirectory{
      std::move(fanout.value()), destination_name, destination_path});
}

Result<Unit> compare_and_verify(const ObjectDirectory &directory,
                                const std::wstring_view name,
                                const std::filesystem::path &path,
                                const StoredObjectReference &reference,
                                const std::string_view expected,
                                const std::string_view mismatch_code) {
  const auto loaded =
      read_safe_file(path, directory.fanout, name, maximum_object_bytes,
                     "unsafe_object_path", "object_read_failed");
  if (!loaded.has_value()) {
    return Result<Unit>::failure(loaded.diagnostic());
  }
  if (loaded.value() != expected) {
    return fail<Unit>(std::string(mismatch_code),
                      "different bytes occupy the digest path", path);
  }
  return verify_stored_object(reference, loaded.value());
}

bool path_is_missing(const DWORD error) {
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

} // namespace

ProjectLock::ProjectLock(const std::intptr_t native_handle) noexcept
    : native_handle_(native_handle) {}

ProjectLock::ProjectLock(ProjectLock &&other) noexcept
    : native_handle_(std::exchange(other.native_handle_, -1)) {}

ProjectLock &ProjectLock::operator=(ProjectLock &&other) noexcept {
  if (this != &other) {
    if (native_handle_ != -1) {
      OVERLAPPED overlap{};
      static_cast<void>(::UnlockFileEx(
          reinterpret_cast<HANDLE>(native_handle_), 0U, MAXDWORD, MAXDWORD,
          &overlap));
      static_cast<void>(
          ::CloseHandle(reinterpret_cast<HANDLE>(native_handle_)));
    }
    native_handle_ = std::exchange(other.native_handle_, -1);
  }
  return *this;
}

ProjectLock::~ProjectLock() {
  if (native_handle_ != -1) {
    OVERLAPPED overlap{};
    static_cast<void>(::UnlockFileEx(
        reinterpret_cast<HANDLE>(native_handle_), 0U, MAXDWORD, MAXDWORD,
        &overlap));
    static_cast<void>(::CloseHandle(reinterpret_cast<HANDLE>(native_handle_)));
  }
}

Result<ProjectLock>
acquire_project_lock(const std::filesystem::path &project_path,
                     const LockMode mode, const bool create_sidecar,
                     std::chrono::milliseconds timeout) noexcept {
  try {
    auto anchor = anchor_project(project_path);
    if (!anchor.has_value()) {
      return Result<ProjectLock>::failure(anchor.diagnostic());
    }
    auto sidecar = open_sidecar(anchor.value(), create_sidecar);
    if (!sidecar.has_value()) {
      return Result<ProjectLock>::failure(sidecar.diagnostic());
    }
    const auto lock_path = sidecar.value().path / L".writer.lock";
    const DWORD disposition =
        mode == LockMode::exclusive ? OPEN_ALWAYS : OPEN_EXISTING;
    NativeHandle lock(::CreateFileW(
        lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        disposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr));
    if (!lock.valid()) {
      const auto code = mode == LockMode::shared &&
                                path_is_missing(::GetLastError())
                            ? "execution_store_missing"
                            : "unsafe_lock_path";
      return fail<ProjectLock>(code, windows_error(), lock_path);
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (::GetFileInformationByHandle(lock.get(), &information) == 0 ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
      return fail<ProjectLock>("unsafe_lock_path",
                               "writer lock is not a regular file", lock_path);
    }
    auto lock_resolved = resolved_path(lock.get(), lock_path);
    if (!lock_resolved.has_value() ||
        lock_resolved.value() !=
            child_resolved_path(sidecar.value(), L".writer.lock")) {
      return fail<ProjectLock>("unsafe_lock_path",
                               "writer lock escapes the sidecar root",
                               lock_path);
    }
    if (mode == LockMode::exclusive && ::FlushFileBuffers(lock.get()) == 0) {
      return fail<ProjectLock>("lock_flush_failed", windows_error(),
                               lock_path);
    }
    timeout = std::clamp(timeout, 0ms, maximum_lock_wait);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    OVERLAPPED overlap{};
    DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
    if (mode == LockMode::exclusive) {
      flags |= LOCKFILE_EXCLUSIVE_LOCK;
    }
    while (::LockFileEx(lock.get(), flags, 0U, MAXDWORD, MAXDWORD, &overlap) ==
           0) {
      const DWORD error = ::GetLastError();
      if (error != ERROR_LOCK_VIOLATION && error != ERROR_IO_PENDING) {
        return fail<ProjectLock>("lock_failed", windows_error(error),
                                 lock_path);
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return fail<ProjectLock>(
            "project_busy",
            "project lock was not available within five seconds", lock_path);
      }
      std::this_thread::sleep_for(10ms);
    }
    if (mode == LockMode::exclusive) {
      cleanup_project_temporaries(anchor.value());
    }
    return Result<ProjectLock>::success(ProjectLock(
        reinterpret_cast<std::intptr_t>(lock.release())));
  } catch (const std::exception &failure) {
    return fail<ProjectLock>("lock_failed", failure.what(), project_path);
  } catch (...) {
    return fail<ProjectLock>("lock_failed", "unknown project lock failure",
                             project_path);
  }
}

Result<std::string>
read_project_index_file(const std::filesystem::path &project_path) noexcept {
  try {
    auto anchor = anchor_project(project_path);
    if (!anchor.has_value()) {
      return Result<std::string>::failure(anchor.diagnostic());
    }
    const auto state = require_project_state(anchor.value(), true);
    if (!state.has_value()) {
      return Result<std::string>::failure(state.diagnostic());
    }
    return read_safe_file(project_path, anchor.value().parent,
                          anchor.value().project_name, maximum_project_bytes,
                          "unsafe_project_path", "project_read_failed");
  } catch (const std::exception &failure) {
    return fail<std::string>("project_read_failed", failure.what(),
                             project_path);
  } catch (...) {
    return fail<std::string>("project_read_failed",
                             "unknown project read failure", project_path);
  }
}

Result<std::string> read_previous_project_index_file(
    const std::filesystem::path &project_path) noexcept {
  try {
    auto anchor = anchor_project(project_path);
    if (!anchor.has_value())
      return Result<std::string>::failure(anchor.diagnostic());
    auto sidecar = open_sidecar(anchor.value(), false);
    if (!sidecar.has_value())
      return Result<std::string>::failure(sidecar.diagnostic());
    const auto path = sidecar.value().path /
                      std::wstring(previous_project_index_name);
    return read_safe_file(path, sidecar.value(), previous_project_index_name,
                          maximum_project_bytes,
                          "unsafe_previous_project_index",
                          "previous_project_index_missing");
  } catch (const std::exception &failure) {
    return fail<std::string>("previous_project_index_read_failed",
                             failure.what(), project_path);
  } catch (...) {
    return fail<std::string>("previous_project_index_read_failed",
                             "unknown previous-index read failure",
                             project_path);
  }
}

Result<Unit>
replace_project_index_file(const std::filesystem::path &project_path,
                           const std::string_view bytes,
                           const bool replace_existing,
                           const TransactionOptions &options,
                           const bool retain_previous) noexcept {
  try {
    const auto parsed = parse_project_v2(bytes);
    if (!parsed.has_value()) {
      return Result<Unit>::failure(store_diagnostic(
          parsed.diagnostic().code, parsed.diagnostic().message,
          parsed.diagnostic().field, project_path));
    }
    auto anchor = anchor_project(project_path);
    if (!anchor.has_value()) {
      return Result<Unit>::failure(anchor.diagnostic());
    }
    const auto initial =
        require_project_state(anchor.value(), replace_existing);
    if (!initial.has_value()) {
      return initial;
    }
    if (const auto step = boundary(
            options, TransactionBoundary::before_project_temporary_create);
        !step.has_value()) {
      return step;
    }
    const auto temporary_name =
        unique_temporary_name(anchor.value().project_name);
    const auto temporary_path = anchor.value().parent.path / temporary_name;
    NativeHandle temporary(::CreateFileW(
        temporary_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr));
    if (!temporary.valid()) {
      return fail<Unit>("project_temporary_create_failed", windows_error(),
                        temporary_path);
    }
    TemporaryPath cleanup(temporary_path, temporary);
    const auto safe_temporary = validate_regular_handle(
        temporary.get(), anchor.value().parent, temporary_name,
        temporary_path, "unsafe_project_path");
    if (!safe_temporary.has_value()) {
      return safe_temporary;
    }
    if (const auto step = boundary(
            options, TransactionBoundary::after_project_temporary_create);
        !step.has_value()) {
      return step;
    }
    if (const auto step =
            boundary(options, TransactionBoundary::before_project_write);
        !step.has_value()) {
      return step;
    }
    const auto written = write_all(temporary.get(), bytes, temporary_path);
    if (!written.has_value()) {
      return written;
    }
    if (const auto step =
            boundary(options, TransactionBoundary::after_project_write);
        !step.has_value()) {
      return step;
    }
    if (const auto step =
            boundary(options, TransactionBoundary::before_project_flush);
        !step.has_value()) {
      return step;
    }
    if (::FlushFileBuffers(temporary.get()) == 0) {
      return fail<Unit>("project_flush_failed", windows_error(),
                        temporary_path);
    }
    if (const auto step =
            boundary(options, TransactionBoundary::after_project_flush);
        !step.has_value()) {
      return step;
    }
    temporary.reset();
    if (const auto step = boundary(
            options, TransactionBoundary::before_project_verification);
        !step.has_value()) {
      return step;
    }
    const auto reloaded = read_safe_file(
        temporary_path, anchor.value().parent, temporary_name,
        maximum_project_bytes, "unsafe_project_path",
        "project_verification_failed");
    if (!reloaded.has_value()) {
      return Result<Unit>::failure(reloaded.diagnostic());
    }
    if (reloaded.value() != bytes ||
        !parse_project_v2(reloaded.value()).has_value()) {
      return fail<Unit>("project_verification_failed",
                        "project temporary bytes failed exact verification",
                        temporary_path);
    }
    if (const auto step = boundary(
            options, TransactionBoundary::after_project_verification);
        !step.has_value()) {
      return step;
    }
    if (const auto step = boundary(
            options, TransactionBoundary::before_project_replacement);
        !step.has_value()) {
      return step;
    }
    const auto final_state =
        require_project_state(anchor.value(), replace_existing);
    if (!final_state.has_value()) {
      return final_state;
    }
    const auto parent_unchanged =
        validate_directory_identity(anchor.value().parent);
    if (!parent_unchanged.has_value()) {
      return parent_unchanged;
    }
    BOOL replaced = FALSE;
    if (replace_existing) {
      std::filesystem::path previous_path;
      if (retain_previous) {
        const auto current_bytes = read_safe_file(
            project_path, anchor.value().parent, anchor.value().project_name,
            maximum_project_bytes, "unsafe_project_path",
            "project_read_failed");
        if (!current_bytes.has_value() ||
            !parse_project_v2(current_bytes.has_value()
                                  ? current_bytes.value()
                                  : std::string{})
                 .has_value())
          return fail<Unit>("previous_project_index_invalid",
                            "current index is not valid enough to retain",
                            project_path);
        auto sidecar = open_sidecar(anchor.value(), true);
        if (!sidecar.has_value())
          return Result<Unit>::failure(sidecar.diagnostic());
        previous_path = sidecar.value().path /
                        std::wstring(previous_project_index_name);
        const DWORD previous_attributes =
            ::GetFileAttributesW(previous_path.c_str());
        if (previous_attributes != INVALID_FILE_ATTRIBUTES &&
            ((previous_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
             (previous_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U))
          return fail<Unit>("unsafe_previous_project_index",
                            "previous index destination is unsafe",
                            previous_path);
      }
      replaced = ::ReplaceFileW(project_path.c_str(), temporary_path.c_str(),
                                retain_previous ? previous_path.c_str()
                                                : nullptr,
                                REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
    } else {
      replaced = ::MoveFileExW(temporary_path.c_str(), project_path.c_str(),
                               MOVEFILE_WRITE_THROUGH);
    }
    if (replaced == FALSE) {
      const DWORD error = ::GetLastError();
      const auto code =
          replace_existing
              ? "project_replacement_failed"
              : ((error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
                     ? "project_exists"
                     : "project_replacement_failed");
      return fail<Unit>(code, windows_error(error), project_path);
    }
    cleanup.release();
    const auto final_bytes = read_safe_file(
        project_path, anchor.value().parent, anchor.value().project_name,
        maximum_project_bytes, "unsafe_project_path",
        "project_verification_failed");
    if (!final_bytes.has_value() || final_bytes.value() != bytes) {
      return fail<Unit>("project_verification_failed",
                        "replaced project bytes failed exact verification",
                        project_path);
    }
    if (const auto step =
            boundary(options, TransactionBoundary::after_project_replacement);
        !step.has_value()) {
      return step;
    }
    return Result<Unit>::success(Unit{});
  } catch (const std::exception &failure) {
    return fail<Unit>("project_write_failed", failure.what(), project_path);
  } catch (...) {
    return fail<Unit>("project_write_failed", "unknown project write failure",
                      project_path);
  }
}

Result<InstalledObject>
install_object_file(const std::filesystem::path &project_path,
                    const StoredObjectReference &reference,
                    const std::string_view bytes,
                    const TransactionOptions &options) noexcept {
  try {
    const auto input = verify_stored_object(reference, bytes);
    if (!input.has_value()) {
      return Result<InstalledObject>::failure(input.diagnostic());
    }
    auto directory =
        open_object_directory(project_path, reference.object_hash, true);
    if (!directory.has_value()) {
      return Result<InstalledObject>::failure(directory.diagnostic());
    }
    DWORD attributes =
        ::GetFileAttributesW(directory.value().destination_path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
      if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
          (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return fail<InstalledObject>(
            "unsafe_object_path",
            "object destination is not a regular non-reparse-point file",
            directory.value().destination_path);
      }
      const auto existing = compare_and_verify(
          directory.value(), directory.value().destination_name,
          directory.value().destination_path, reference, bytes,
          "object_collision");
      if (!existing.has_value()) {
        return Result<InstalledObject>::failure(existing.diagnostic());
      }
      return Result<InstalledObject>::success(
          InstalledObject{directory.value().destination_path, true});
    }
    if (!path_is_missing(::GetLastError())) {
      return fail<InstalledObject>("object_path_error", windows_error(),
                                   directory.value().destination_path);
    }

    const auto reserved_path =
        temporary_path_for_object(directory.value().destination_path);
    attributes = ::GetFileAttributesW(reserved_path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
      return fail<InstalledObject>(
          "unsafe_temporary_path",
          "reserved object temporary path already exists", reserved_path);
    }
    if (!path_is_missing(::GetLastError())) {
      return fail<InstalledObject>("temporary_path_error", windows_error(),
                                   reserved_path);
    }

    if (const auto step = boundary(
            options, TransactionBoundary::before_object_temporary_create);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    const auto temporary_name =
        unique_temporary_name(directory.value().destination_name);
    const auto temporary_path = directory.value().fanout.path / temporary_name;
    NativeHandle temporary(::CreateFileW(
        temporary_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr));
    if (!temporary.valid()) {
      return fail<InstalledObject>("temporary_create_failed", windows_error(),
                                   temporary_path);
    }
    TemporaryPath cleanup(temporary_path, temporary);
    const auto safe_temporary = validate_regular_handle(
        temporary.get(), directory.value().fanout, temporary_name,
        temporary_path, "unsafe_temporary_path");
    if (!safe_temporary.has_value()) {
      return Result<InstalledObject>::failure(safe_temporary.diagnostic());
    }
    if (const auto step = boundary(
            options, TransactionBoundary::after_object_temporary_create);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    if (const auto step =
            boundary(options, TransactionBoundary::before_object_write);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    const auto written = write_all(temporary.get(), bytes, temporary_path);
    if (!written.has_value()) {
      return Result<InstalledObject>::failure(written.diagnostic());
    }
    if (const auto step =
            boundary(options, TransactionBoundary::after_object_write);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    if (const auto step =
            boundary(options, TransactionBoundary::before_object_flush);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    if (::FlushFileBuffers(temporary.get()) == 0) {
      return fail<InstalledObject>("temporary_flush_failed", windows_error(),
                                   temporary_path);
    }
    if (const auto step =
            boundary(options, TransactionBoundary::after_object_flush);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    temporary.reset();
    if (const auto step = boundary(
            options, TransactionBoundary::before_object_verification);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    const auto temporary_bytes = read_safe_file(
        temporary_path, directory.value().fanout, temporary_name,
        maximum_object_bytes, "unsafe_temporary_path",
        "object_verification_failed");
    if (!temporary_bytes.has_value() || temporary_bytes.value() != bytes) {
      return fail<InstalledObject>(
          "object_verification_failed",
          "object temporary bytes failed exact verification", temporary_path);
    }
    const auto temporary_verified =
        verify_stored_object(reference, temporary_bytes.value());
    if (!temporary_verified.has_value()) {
      return Result<InstalledObject>::failure(
          temporary_verified.diagnostic());
    }
    if (const auto step = boundary(
            options, TransactionBoundary::after_object_verification);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    if (const auto step =
            boundary(options, TransactionBoundary::before_object_rename);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    const auto fanout_unchanged =
        validate_directory_identity(directory.value().fanout);
    if (!fanout_unchanged.has_value()) {
      return Result<InstalledObject>::failure(fanout_unchanged.diagnostic());
    }
    if (::MoveFileExW(temporary_path.c_str(),
                      directory.value().destination_path.c_str(),
                      MOVEFILE_WRITE_THROUGH) == 0) {
      const DWORD error = ::GetLastError();
      if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) {
        return fail<InstalledObject>("object_install_failed",
                                     windows_error(error),
                                     directory.value().destination_path);
      }
      const auto concurrent = compare_and_verify(
          directory.value(), directory.value().destination_name,
          directory.value().destination_path, reference, bytes,
          "object_collision");
      if (!concurrent.has_value()) {
        return Result<InstalledObject>::failure(concurrent.diagnostic());
      }
      return Result<InstalledObject>::success(
          InstalledObject{directory.value().destination_path, true});
    }
    cleanup.release();
    if (const auto step =
            boundary(options, TransactionBoundary::after_object_rename);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    const auto final = compare_and_verify(
        directory.value(), directory.value().destination_name,
        directory.value().destination_path, reference, bytes,
        "object_collision");
    if (!final.has_value()) {
      return Result<InstalledObject>::failure(final.diagnostic());
    }
    return Result<InstalledObject>::success(
        InstalledObject{directory.value().destination_path, false});
  } catch (const std::exception &failure) {
    return fail<InstalledObject>("object_install_failed", failure.what(),
                                 project_path);
  } catch (...) {
    return fail<InstalledObject>("object_install_failed",
                                 "unknown object install failure",
                                 project_path);
  }
}

Result<std::string>
read_object_file(const std::filesystem::path &project_path,
                 const StoredObjectReference &reference) noexcept {
  try {
    if (!is_valid_object_hash(reference.object_hash)) {
      return fail<std::string>("invalid_hash", "stored-object hash is invalid");
    }
    if (!is_supported_object_reference(reference)) {
      return fail<std::string>("unsupported_object_contract",
                               "stored-object contract is unsupported");
    }
    auto directory =
        open_object_directory(project_path, reference.object_hash, false);
    if (!directory.has_value()) {
      return Result<std::string>::failure(directory.diagnostic());
    }
    std::error_code existenceError;
    const auto status = std::filesystem::symlink_status(
        directory.value().destination_path, existenceError);
    if (existenceError == std::errc::no_such_file_or_directory ||
        (!existenceError && !std::filesystem::exists(status)))
      return fail<std::string>("object_store_missing",
                               "stored object does not exist",
                               directory.value().destination_path);
    const auto loaded = read_safe_file(
        directory.value().destination_path, directory.value().fanout,
        directory.value().destination_name, maximum_object_bytes,
        "unsafe_object_path", "object_read_failed");
    if (!loaded.has_value()) {
      return loaded;
    }
    const auto verified = verify_stored_object(reference, loaded.value());
    if (!verified.has_value()) {
      return Result<std::string>::failure(verified.diagnostic());
    }
    return loaded;
  } catch (const std::exception &failure) {
    return fail<std::string>("object_read_failed", failure.what(),
                             project_path);
  } catch (...) {
    return fail<std::string>("object_read_failed",
                             "unknown object read failure", project_path);
  }
}

} // namespace prometheus::run_store::detail
