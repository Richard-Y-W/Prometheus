#include "platform_io.hpp"

#include <prometheus/run_store/project_v2.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <utility>
#include <unistd.h>
#include <fcntl.h>

namespace prometheus::run_store::detail {
namespace {

using namespace std::chrono_literals;

class FileDescriptor final {
public:
  explicit FileDescriptor(const int value = -1) noexcept : value_(value) {}
  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;
  FileDescriptor(FileDescriptor &&other) noexcept
      : value_(std::exchange(other.value_, -1)) {}
  FileDescriptor &operator=(FileDescriptor &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, -1);
    }
    return *this;
  }
  ~FileDescriptor() { reset(); }

  [[nodiscard]] int get() const noexcept { return value_; }
  [[nodiscard]] bool valid() const noexcept { return value_ >= 0; }
  [[nodiscard]] int release() noexcept { return std::exchange(value_, -1); }
  void reset() noexcept {
    if (value_ >= 0) {
      static_cast<void>(::close(value_));
      value_ = -1;
    }
  }

private:
  int value_;
};

class TemporaryEntry final {
public:
  TemporaryEntry(const int directory, std::string name)
      : directory_(directory), name_(std::move(name)) {}
  TemporaryEntry(const TemporaryEntry &) = delete;
  TemporaryEntry &operator=(const TemporaryEntry &) = delete;
  ~TemporaryEntry() {
    if (armed_) {
      static_cast<void>(::unlinkat(directory_, name_.c_str(), 0));
    }
  }
  void release() noexcept { armed_ = false; }

private:
  int directory_;
  std::string name_;
  bool armed_{true};
};

struct AnchoredProject final {
  FileDescriptor parent;
  std::string project_name;
  std::string sidecar_name;
  std::filesystem::path project_path;
};

template <typename T>
Result<T> fail(std::string code, std::string message,
               std::optional<std::filesystem::path> path = std::nullopt) {
  return Result<T>::failure(store_diagnostic(
      std::move(code), std::move(message), std::nullopt, std::move(path)));
}

std::string errno_message() { return std::strerror(errno); }

bool safe_name(const std::string_view value) {
  return !value.empty() && value != "." && value != ".." &&
         value.find('/') == std::string_view::npos &&
         value.find('\0') == std::string_view::npos;
}

Result<AnchoredProject>
anchor_project(const std::filesystem::path &project_path) {
  const auto filename = project_path.filename().string();
  if (!safe_name(filename)) {
    return fail<AnchoredProject>("unsafe_project_path",
                                 "project path has no safe filename",
                                 project_path);
  }
  auto parent_path = project_path.parent_path();
  if (parent_path.empty()) {
    parent_path = ".";
  }
  FileDescriptor parent(::open(parent_path.c_str(),
                               O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                   O_CLOEXEC));
  if (!parent.valid()) {
    return fail<AnchoredProject>("unsafe_project_path", errno_message(),
                                 parent_path);
  }
  struct stat information {};
  if (::fstat(parent.get(), &information) != 0 ||
      !S_ISDIR(information.st_mode)) {
    return fail<AnchoredProject>("unsafe_project_path",
                                 "project parent is not a real directory",
                                 parent_path);
  }
  return Result<AnchoredProject>::success(
      AnchoredProject{std::move(parent), filename, filename + ".data",
                      project_path});
}

Result<FileDescriptor> open_sidecar(const AnchoredProject &anchor,
                                    const bool create) {
  struct stat information {};
  if (::fstatat(anchor.parent.get(), anchor.sidecar_name.c_str(), &information,
                AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno != ENOENT) {
      return fail<FileDescriptor>("unsafe_store_path", errno_message(),
                                  sidecar_path_for_project(anchor.project_path));
    }
    if (!create) {
      return fail<FileDescriptor>(
          "execution_store_missing", "execution sidecar directory is missing",
          sidecar_path_for_project(anchor.project_path));
    }
    if (::mkdirat(anchor.parent.get(), anchor.sidecar_name.c_str(),
                  S_IRWXU) != 0 &&
        errno != EEXIST) {
      return fail<FileDescriptor>("store_directory_create_failed",
                                  errno_message(),
                                  sidecar_path_for_project(anchor.project_path));
    }
    if (::fsync(anchor.parent.get()) != 0) {
      return fail<FileDescriptor>("store_directory_flush_failed",
                                  errno_message(),
                                  sidecar_path_for_project(anchor.project_path));
    }
    if (::fstatat(anchor.parent.get(), anchor.sidecar_name.c_str(), &information,
                  AT_SYMLINK_NOFOLLOW) != 0) {
      return fail<FileDescriptor>("unsafe_store_path", errno_message(),
                                  sidecar_path_for_project(anchor.project_path));
    }
  }
  if (!S_ISDIR(information.st_mode)) {
    return fail<FileDescriptor>("unsafe_store_path",
                                "execution sidecar is not a real directory",
                                sidecar_path_for_project(anchor.project_path));
  }
  FileDescriptor sidecar(::openat(anchor.parent.get(),
                                  anchor.sidecar_name.c_str(),
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                      O_CLOEXEC));
  if (!sidecar.valid()) {
    return fail<FileDescriptor>("unsafe_store_path", errno_message(),
                                sidecar_path_for_project(anchor.project_path));
  }
  return Result<FileDescriptor>::success(std::move(sidecar));
}

Result<FileDescriptor> open_child_directory(const int parent,
                                            const std::string_view name,
                                            const bool create,
                                            const std::filesystem::path &path) {
  if (!safe_name(name)) {
    return fail<FileDescriptor>("unsafe_store_path",
                                "object-store directory name is unsafe", path);
  }
  struct stat information {};
  const std::string owned(name);
  if (::fstatat(parent, owned.c_str(), &information, AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno != ENOENT) {
      return fail<FileDescriptor>("unsafe_store_path", errno_message(), path);
    }
    if (!create) {
      return fail<FileDescriptor>("object_store_missing",
                                  "object-store directory is missing", path);
    }
    if (::mkdirat(parent, owned.c_str(), S_IRWXU) != 0 && errno != EEXIST) {
      return fail<FileDescriptor>("store_directory_create_failed",
                                  errno_message(), path);
    }
    if (::fsync(parent) != 0) {
      return fail<FileDescriptor>("store_directory_flush_failed",
                                  errno_message(), path);
    }
    if (::fstatat(parent, owned.c_str(), &information,
                  AT_SYMLINK_NOFOLLOW) != 0) {
      return fail<FileDescriptor>("unsafe_store_path", errno_message(), path);
    }
  }
  if (!S_ISDIR(information.st_mode)) {
    return fail<FileDescriptor>("unsafe_store_path",
                                "object-store component is not a real directory",
                                path);
  }
  FileDescriptor child(::openat(parent, owned.c_str(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                    O_CLOEXEC));
  if (!child.valid()) {
    return fail<FileDescriptor>("unsafe_store_path", errno_message(), path);
  }
  return Result<FileDescriptor>::success(std::move(child));
}

Result<Unit> require_project_state(const AnchoredProject &anchor,
                                   const bool must_exist) {
  struct stat information {};
  if (::fstatat(anchor.parent.get(), anchor.project_name.c_str(), &information,
                AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT && !must_exist) {
      return Result<Unit>::success(Unit{});
    }
    return fail<Unit>(must_exist ? "project_missing" : "project_path_error",
                      errno_message(), anchor.project_path);
  }
  if (!must_exist) {
    return fail<Unit>("project_exists",
                      "Save As destination already exists",
                      anchor.project_path);
  }
  if (!S_ISREG(information.st_mode)) {
    return fail<Unit>("unsafe_project_path",
                      "project path is not a regular non-symlink file",
                      anchor.project_path);
  }
  return Result<Unit>::success(Unit{});
}

Result<std::string> read_descriptor(const int descriptor,
                                    const std::size_t maximum_bytes,
                                    const std::filesystem::path &path,
                                    const std::string_view code) {
  struct stat before {};
  if (::fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode)) {
    return fail<std::string>(std::string(code),
                             "opened path is not a regular file", path);
  }
  if (before.st_size < 0 ||
      static_cast<std::uint64_t>(before.st_size) > maximum_bytes) {
    return fail<std::string>(std::string(code),
                             "file exceeds its configured byte limit", path);
  }
  std::string bytes(static_cast<std::size_t>(before.st_size), '\0');
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const auto received =
        ::pread(descriptor, bytes.data() + offset, bytes.size() - offset,
                static_cast<off_t>(offset));
    if (received < 0 && errno == EINTR) {
      continue;
    }
    if (received <= 0) {
      return fail<std::string>(std::string(code),
                               received == 0 ? "file was truncated during read"
                                             : errno_message(),
                               path);
    }
    offset += static_cast<std::size_t>(received);
  }
  char extra = '\0';
  const auto extra_count =
      ::pread(descriptor, &extra, 1U, static_cast<off_t>(bytes.size()));
  if (extra_count != 0) {
    return fail<std::string>(std::string(code),
                             "file changed during bounded read", path);
  }
  struct stat after {};
  if (::fstat(descriptor, &after) != 0 || before.st_dev != after.st_dev ||
      before.st_ino != after.st_ino || before.st_size != after.st_size) {
    return fail<std::string>(std::string(code),
                             "file identity changed during read", path);
  }
  return Result<std::string>::success(std::move(bytes));
}

Result<std::string> read_named_file(const int directory,
                                    const std::string &name,
                                    const std::size_t maximum_bytes,
                                    const std::filesystem::path &path,
                                    const std::string_view unsafe_code,
                                    const std::string_view read_code) {
  struct stat path_information {};
  if (::fstatat(directory, name.c_str(), &path_information,
                AT_SYMLINK_NOFOLLOW) != 0) {
    return fail<std::string>(std::string(read_code), errno_message(), path);
  }
  if (!S_ISREG(path_information.st_mode)) {
    return fail<std::string>(std::string(unsafe_code),
                             "path is not a regular non-symlink file", path);
  }
  FileDescriptor file(
      ::openat(directory, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  if (!file.valid()) {
    return fail<std::string>(std::string(unsafe_code), errno_message(), path);
  }
  return read_descriptor(file.get(), maximum_bytes, path, read_code);
}

Result<Unit> write_all(const int descriptor, const std::string_view bytes,
                       const std::filesystem::path &path) {
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const auto written =
        ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return fail<Unit>("temporary_write_failed", errno_message(), path);
    }
    offset += static_cast<std::size_t>(written);
  }
  return Result<Unit>::success(Unit{});
}

std::string unique_temporary_name(const std::string_view base) {
  static std::atomic<std::uint64_t> counter{0U};
  return std::string(base) + ".tmp." + std::to_string(::getpid()) + "." +
         std::to_string(counter.fetch_add(1U));
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
  const auto prefix = anchor.project_name + ".tmp.";
  const int duplicate = ::dup(anchor.parent.get());
  if (duplicate < 0) {
    return;
  }
  DIR *directory = ::fdopendir(duplicate);
  if (directory == nullptr) {
    static_cast<void>(::close(duplicate));
    return;
  }
  while (const auto *entry = ::readdir(directory)) {
    const std::string_view name(entry->d_name);
    if (!name.starts_with(prefix)) {
      continue;
    }
    struct stat information {};
    if (::fstatat(anchor.parent.get(), entry->d_name, &information,
                  AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(information.st_mode)) {
      static_cast<void>(
          ::unlinkat(anchor.parent.get(), entry->d_name, 0));
    }
  }
  static_cast<void>(::closedir(directory));
}

struct ObjectDirectory final {
  FileDescriptor fanout;
  std::string destination_name;
  std::filesystem::path destination_path;
};

Result<ObjectDirectory>
open_object_directory(const std::filesystem::path &project_path,
                      const std::string_view object_hash, const bool create) {
  auto anchor_result = anchor_project(project_path);
  if (!anchor_result.has_value()) {
    return Result<ObjectDirectory>::failure(anchor_result.diagnostic());
  }
  auto anchor = std::move(anchor_result.value());
  const auto project = require_project_state(anchor, true);
  if (!project.has_value()) {
    return Result<ObjectDirectory>::failure(project.diagnostic());
  }
  auto sidecar_result = open_sidecar(anchor, create);
  if (!sidecar_result.has_value()) {
    return Result<ObjectDirectory>::failure(sidecar_result.diagnostic());
  }
  auto sidecar = std::move(sidecar_result.value());
  const auto root_path = sidecar_path_for_project(project_path);
  auto objects_result =
      open_child_directory(sidecar.get(), "objects", create,
                           root_path / "objects");
  if (!objects_result.has_value()) {
    return Result<ObjectDirectory>::failure(objects_result.diagnostic());
  }
  auto objects = std::move(objects_result.value());
  auto sha_result = open_child_directory(objects.get(), "sha256", create,
                                         root_path / "objects" / "sha256");
  if (!sha_result.has_value()) {
    return Result<ObjectDirectory>::failure(sha_result.diagnostic());
  }
  auto sha = std::move(sha_result.value());
  const auto digest = object_hash.substr(7U);
  const auto fanout_name = std::string(digest.substr(0U, 2U));
  auto fanout_result = open_child_directory(
      sha.get(), fanout_name, create,
      root_path / "objects" / "sha256" / fanout_name);
  if (!fanout_result.has_value()) {
    return Result<ObjectDirectory>::failure(fanout_result.diagnostic());
  }
  auto destination_path =
      object_path_for_hash(root_path, object_hash).value();
  return Result<ObjectDirectory>::success(
      ObjectDirectory{std::move(fanout_result.value()),
                      std::string(digest.substr(2U)),
                      std::move(destination_path)});
}

Result<Unit> compare_and_verify(const int directory, const std::string &name,
                                const std::filesystem::path &path,
                                const StoredObjectReference &reference,
                                const std::string_view expected,
                                const std::string_view mismatch_code) {
  const auto loaded = read_named_file(directory, name, maximum_object_bytes,
                                      path, "unsafe_object_path",
                                      "object_read_failed");
  if (!loaded.has_value()) {
    return Result<Unit>::failure(loaded.diagnostic());
  }
  if (loaded.value() != expected) {
    return fail<Unit>(std::string(mismatch_code),
                      "different bytes occupy the digest path", path);
  }
  return verify_stored_object(reference, loaded.value());
}

} // namespace

ProjectLock::ProjectLock(const std::intptr_t native_handle) noexcept
    : native_handle_(native_handle) {}

ProjectLock::ProjectLock(ProjectLock &&other) noexcept
    : native_handle_(std::exchange(other.native_handle_, -1)) {}

ProjectLock &ProjectLock::operator=(ProjectLock &&other) noexcept {
  if (this != &other) {
    if (native_handle_ >= 0) {
      static_cast<void>(::flock(static_cast<int>(native_handle_), LOCK_UN));
      static_cast<void>(::close(static_cast<int>(native_handle_)));
    }
    native_handle_ = std::exchange(other.native_handle_, -1);
  }
  return *this;
}

ProjectLock::~ProjectLock() {
  if (native_handle_ >= 0) {
    static_cast<void>(::flock(static_cast<int>(native_handle_), LOCK_UN));
    static_cast<void>(::close(static_cast<int>(native_handle_)));
  }
}

Result<ProjectLock>
acquire_project_lock(const std::filesystem::path &project_path,
                     const LockMode mode, const bool create_sidecar,
                     std::chrono::milliseconds timeout) noexcept {
  try {
    auto anchor_result = anchor_project(project_path);
    if (!anchor_result.has_value()) {
      return Result<ProjectLock>::failure(anchor_result.diagnostic());
    }
    auto anchor = std::move(anchor_result.value());
    auto sidecar_result = open_sidecar(anchor, create_sidecar);
    if (!sidecar_result.has_value()) {
      return Result<ProjectLock>::failure(sidecar_result.diagnostic());
    }
    auto sidecar = std::move(sidecar_result.value());
    const int create_flag = mode == LockMode::exclusive ? O_CREAT : 0;
    FileDescriptor lock_file(::openat(sidecar.get(), ".writer.lock",
                                      O_RDWR | create_flag | O_NOFOLLOW |
                                          O_CLOEXEC,
                                      S_IRUSR | S_IWUSR));
    if (!lock_file.valid()) {
      const auto code = mode == LockMode::shared && errno == ENOENT
                            ? "execution_store_missing"
                            : "unsafe_lock_path";
      return fail<ProjectLock>(code, errno_message(),
                               sidecar_path_for_project(project_path) /
                                   ".writer.lock");
    }
    struct stat lock_information {};
    if (::fstat(lock_file.get(), &lock_information) != 0 ||
        !S_ISREG(lock_information.st_mode)) {
      return fail<ProjectLock>("unsafe_lock_path",
                               "writer lock path is not a regular file",
                               sidecar_path_for_project(project_path) /
                                   ".writer.lock");
    }
    if (mode == LockMode::exclusive) {
      if (::fsync(lock_file.get()) != 0) {
        return fail<ProjectLock>("lock_flush_failed", errno_message(),
                                 sidecar_path_for_project(project_path) /
                                     ".writer.lock");
      }
      if (::fsync(sidecar.get()) != 0) {
        return fail<ProjectLock>("lock_directory_flush_failed",
                                 errno_message(),
                                 sidecar_path_for_project(project_path));
      }
    }
    timeout = std::clamp(timeout, 0ms, maximum_lock_wait);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const int operation =
        (mode == LockMode::exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB;
    while (::flock(lock_file.get(), operation) != 0) {
      if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
        return fail<ProjectLock>("lock_failed", errno_message(),
                                 sidecar_path_for_project(project_path) /
                                     ".writer.lock");
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return fail<ProjectLock>("project_busy",
                                 "project lock was not available within five seconds",
                                 sidecar_path_for_project(project_path) /
                                     ".writer.lock");
      }
      std::this_thread::sleep_for(10ms);
    }
    if (mode == LockMode::exclusive) {
      cleanup_project_temporaries(anchor);
    }
    return Result<ProjectLock>::success(
        ProjectLock(static_cast<std::intptr_t>(lock_file.release())));
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
    auto anchor_result = anchor_project(project_path);
    if (!anchor_result.has_value()) {
      return Result<std::string>::failure(anchor_result.diagnostic());
    }
    auto anchor = std::move(anchor_result.value());
    const auto state = require_project_state(anchor, true);
    if (!state.has_value()) {
      return Result<std::string>::failure(state.diagnostic());
    }
    return read_named_file(anchor.parent.get(), anchor.project_name,
                           maximum_project_bytes, project_path,
                           "unsafe_project_path", "project_read_failed");
  } catch (const std::exception &failure) {
    return fail<std::string>("project_read_failed", failure.what(),
                             project_path);
  } catch (...) {
    return fail<std::string>("project_read_failed",
                             "unknown project read failure", project_path);
  }
}

Result<Unit>
replace_project_index_file(const std::filesystem::path &project_path,
                           const std::string_view bytes,
                           const bool replace_existing,
                           const TransactionOptions &options) noexcept {
  try {
    const auto validated = parse_project_v2(bytes);
    if (!validated.has_value()) {
      return Result<Unit>::failure(store_diagnostic(
          validated.diagnostic().code, validated.diagnostic().message,
          validated.diagnostic().field, project_path));
    }
    auto anchor_result = anchor_project(project_path);
    if (!anchor_result.has_value()) {
      return Result<Unit>::failure(anchor_result.diagnostic());
    }
    auto anchor = std::move(anchor_result.value());
    const auto state = require_project_state(anchor, replace_existing);
    if (!state.has_value()) {
      return state;
    }
    if (const auto step = boundary(
            options, TransactionBoundary::before_project_temporary_create);
        !step.has_value()) {
      return step;
    }
    const auto temporary_name = unique_temporary_name(anchor.project_name);
    const auto temporary_path = project_path.parent_path() / temporary_name;
    FileDescriptor temporary(::openat(anchor.parent.get(), temporary_name.c_str(),
                                      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
                                          O_CLOEXEC,
                                      S_IRUSR | S_IWUSR));
    if (!temporary.valid()) {
      return fail<Unit>("project_temporary_create_failed", errno_message(),
                        temporary_path);
    }
    TemporaryEntry cleanup(anchor.parent.get(), temporary_name);
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
    if (::fsync(temporary.get()) != 0) {
      return fail<Unit>("project_flush_failed", errno_message(), temporary_path);
    }
    if (const auto step =
            boundary(options, TransactionBoundary::after_project_flush);
        !step.has_value()) {
      return step;
    }
    if (const auto step = boundary(
            options, TransactionBoundary::before_project_verification);
        !step.has_value()) {
      return step;
    }
    const auto reloaded = read_named_file(
        anchor.parent.get(), temporary_name, maximum_project_bytes,
        temporary_path, "unsafe_project_path", "project_verification_failed");
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
    const auto replacement_state =
        require_project_state(anchor, replace_existing);
    if (!replacement_state.has_value()) {
      return replacement_state;
    }
    if (replace_existing) {
      if (::renameat(anchor.parent.get(), temporary_name.c_str(),
                     anchor.parent.get(), anchor.project_name.c_str()) != 0) {
        return fail<Unit>("project_replacement_failed", errno_message(),
                          project_path);
      }
    } else {
      if (::linkat(anchor.parent.get(), temporary_name.c_str(),
                   anchor.parent.get(), anchor.project_name.c_str(), 0) != 0) {
        return fail<Unit>(errno == EEXIST ? "project_exists"
                                         : "project_replacement_failed",
                          errno_message(), project_path);
      }
      if (::unlinkat(anchor.parent.get(), temporary_name.c_str(), 0) != 0) {
        return fail<Unit>("project_temporary_cleanup_failed", errno_message(),
                          temporary_path);
      }
    }
    cleanup.release();
    if (::fsync(anchor.parent.get()) != 0) {
      return fail<Unit>("project_directory_flush_failed", errno_message(),
                        project_path.parent_path());
    }
    const auto final_bytes = read_named_file(
        anchor.parent.get(), anchor.project_name, maximum_project_bytes,
        project_path, "unsafe_project_path", "project_verification_failed");
    if (!final_bytes.has_value() || final_bytes.value() != bytes ||
        !parse_project_v2(final_bytes.has_value() ? final_bytes.value()
                                                  : std::string{})
             .has_value()) {
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
    auto directory_result =
        open_object_directory(project_path, reference.object_hash, true);
    if (!directory_result.has_value()) {
      return Result<InstalledObject>::failure(directory_result.diagnostic());
    }
    auto directory = std::move(directory_result.value());
    struct stat existing {};
    if (::fstatat(directory.fanout.get(), directory.destination_name.c_str(),
                  &existing, AT_SYMLINK_NOFOLLOW) == 0) {
      if (!S_ISREG(existing.st_mode)) {
        return fail<InstalledObject>(
            "unsafe_object_path",
            "object destination is not a regular non-symlink file",
            directory.destination_path);
      }
      const auto verified = compare_and_verify(
          directory.fanout.get(), directory.destination_name,
          directory.destination_path, reference, bytes, "object_collision");
      if (!verified.has_value()) {
        return Result<InstalledObject>::failure(verified.diagnostic());
      }
      return Result<InstalledObject>::success(
          InstalledObject{directory.destination_path, true});
    }
    if (errno != ENOENT) {
      return fail<InstalledObject>("object_path_error", errno_message(),
                                   directory.destination_path);
    }

    const auto reserved_temporary = directory.destination_name + ".tmp";
    if (::fstatat(directory.fanout.get(), reserved_temporary.c_str(), &existing,
                  AT_SYMLINK_NOFOLLOW) == 0) {
      return fail<InstalledObject>(
          "unsafe_temporary_path",
          "reserved object temporary path already exists",
          temporary_path_for_object(directory.destination_path));
    }
    if (errno != ENOENT) {
      return fail<InstalledObject>("temporary_path_error", errno_message(),
                                   temporary_path_for_object(
                                       directory.destination_path));
    }

    if (const auto step = boundary(
            options, TransactionBoundary::before_object_temporary_create);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    const auto temporary_name =
        unique_temporary_name(directory.destination_name);
    const auto temporary_path = directory.destination_path.parent_path() /
                                temporary_name;
    FileDescriptor temporary(::openat(
        directory.fanout.get(), temporary_name.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        S_IRUSR | S_IWUSR));
    if (!temporary.valid()) {
      return fail<InstalledObject>("temporary_create_failed", errno_message(),
                                   temporary_path);
    }
    TemporaryEntry cleanup(directory.fanout.get(), temporary_name);
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
    if (::fsync(temporary.get()) != 0) {
      return fail<InstalledObject>("temporary_flush_failed", errno_message(),
                                   temporary_path);
    }
    if (const auto step =
            boundary(options, TransactionBoundary::after_object_flush);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    if (const auto step = boundary(
            options, TransactionBoundary::before_object_verification);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    const auto temporary_verified = compare_and_verify(
        directory.fanout.get(), temporary_name, temporary_path, reference,
        bytes, "object_collision");
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
    if (::linkat(directory.fanout.get(), temporary_name.c_str(),
                 directory.fanout.get(), directory.destination_name.c_str(),
                 0) != 0) {
      if (errno != EEXIST) {
        return fail<InstalledObject>("object_install_failed", errno_message(),
                                     directory.destination_path);
      }
      const auto concurrent = compare_and_verify(
          directory.fanout.get(), directory.destination_name,
          directory.destination_path, reference, bytes, "object_collision");
      if (!concurrent.has_value()) {
        return Result<InstalledObject>::failure(concurrent.diagnostic());
      }
      return Result<InstalledObject>::success(
          InstalledObject{directory.destination_path, true});
    }
    if (::unlinkat(directory.fanout.get(), temporary_name.c_str(), 0) != 0) {
      return fail<InstalledObject>("temporary_cleanup_failed", errno_message(),
                                   temporary_path);
    }
    cleanup.release();
    if (::fsync(directory.fanout.get()) != 0) {
      return fail<InstalledObject>("object_directory_flush_failed",
                                   errno_message(),
                                   directory.destination_path.parent_path());
    }
    if (const auto step =
            boundary(options, TransactionBoundary::after_object_rename);
        !step.has_value()) {
      return Result<InstalledObject>::failure(step.diagnostic());
    }
    const auto final = compare_and_verify(
        directory.fanout.get(), directory.destination_name,
        directory.destination_path, reference, bytes, "object_collision");
    if (!final.has_value()) {
      return Result<InstalledObject>::failure(final.diagnostic());
    }
    return Result<InstalledObject>::success(
        InstalledObject{directory.destination_path, false});
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
    auto directory_result =
        open_object_directory(project_path, reference.object_hash, false);
    if (!directory_result.has_value()) {
      return Result<std::string>::failure(directory_result.diagnostic());
    }
    auto directory = std::move(directory_result.value());
    const auto loaded = read_named_file(
        directory.fanout.get(), directory.destination_name,
        maximum_object_bytes, directory.destination_path,
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
