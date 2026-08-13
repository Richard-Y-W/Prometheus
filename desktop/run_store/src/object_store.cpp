#include <prometheus/run_store/object_store.hpp>

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace prometheus::run_store {
namespace {

using Json = nlohmann::json;

constexpr std::size_t maximum_message_bytes = 4096U;

std::string bounded(std::string value, const std::size_t maximum_bytes) {
  if (value.size() <= maximum_bytes) {
    return value;
  }
  auto boundary = maximum_bytes;
  while (boundary > 0U && boundary < value.size() &&
         (static_cast<unsigned char>(value[boundary]) & 0xC0U) == 0x80U) {
    --boundary;
  }
  value.resize(boundary);
  return value;
}

Diagnostic diagnostic(std::string code, std::string message,
                      std::optional<std::filesystem::path> path = std::nullopt) {
  std::optional<std::string> path_text;
  if (path.has_value()) {
    path_text = bounded(path->generic_string(), 4096U);
  }
  return Diagnostic{"object_store", bounded(std::move(code), 128U),
                    bounded(std::move(message), maximum_message_bytes),
                    std::nullopt, std::move(path_text)};
}

template <typename T>
Result<T> fail(std::string code, std::string message,
               std::optional<std::filesystem::path> path = std::nullopt) {
  return Result<T>::failure(
      diagnostic(std::move(code), std::move(message), std::move(path)));
}

bool is_regular_nosymlink(const std::filesystem::path &path,
                          std::error_code &error) {
  const auto status = std::filesystem::symlink_status(path, error);
  return !error && std::filesystem::is_regular_file(status) &&
         !std::filesystem::is_symlink(status);
}

Result<bool> ensure_regular_project(const std::filesystem::path &path) {
  std::error_code error;
  if (!is_regular_nosymlink(path, error)) {
    return fail<bool>("unsafe_project_path",
                      "project path must be an existing regular file and not a symlink",
                      path);
  }
  return Result<bool>::success(true);
}

Result<bool> ensure_directory_component(const std::filesystem::path &path) {
  std::error_code error;
  auto status = std::filesystem::symlink_status(path, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    return fail<bool>("store_path_error", error.message(), path);
  }
  if (!error && status.type() != std::filesystem::file_type::not_found) {
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return fail<bool>("unsafe_store_path",
                        "object-store path component is not a real directory",
                        path);
    }
    return Result<bool>::success(false);
  }
  error.clear();
  if (!std::filesystem::create_directory(path, error) || error) {
    status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return fail<bool>("store_directory_create_failed",
                        error ? error.message()
                              : "new object-store component is unsafe",
                        path);
    }
    return Result<bool>::success(false);
  }
  status = std::filesystem::symlink_status(path, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_directory(status)) {
    return fail<bool>("unsafe_store_path",
                      "created object-store component is unsafe", path);
  }
  return Result<bool>::success(true);
}

Result<bool> ensure_store_directories(const std::filesystem::path &sidecar,
                                      const std::string_view digest) {
  const std::array<std::filesystem::path, 4> components{
      sidecar, sidecar / "objects", sidecar / "objects" / "sha256",
      sidecar / "objects" / "sha256" / std::string(digest.substr(0U, 2U))};
  for (const auto &component : components) {
    const auto created = ensure_directory_component(component);
    if (!created.has_value()) {
      return Result<bool>::failure(created.diagnostic());
    }
  }
  return Result<bool>::success(true);
}

Result<bool> validate_store_directories(const std::filesystem::path &sidecar,
                                        const std::string_view digest) {
  const std::array<std::filesystem::path, 4> components{
      sidecar, sidecar / "objects", sidecar / "objects" / "sha256",
      sidecar / "objects" / "sha256" / std::string(digest.substr(0U, 2U))};
  for (const auto &component : components) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(component, error);
    if (error || status.type() == std::filesystem::file_type::not_found) {
      return fail<bool>("object_store_missing",
                        error ? error.message()
                              : "object-store directory component is missing",
                        component);
    }
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return fail<bool>("unsafe_store_path",
                        "object-store directory component is unsafe",
                        component);
    }
  }
  return Result<bool>::success(true);
}

Result<std::string> read_regular_file(const std::filesystem::path &path,
                                      const std::size_t maximum_bytes,
                                      const std::string_view unsafe_code,
                                      const std::string_view read_code) {
  std::error_code error;
  if (!is_regular_nosymlink(path, error)) {
    return fail<std::string>(std::string(unsafe_code),
                             "object path is not a regular non-symlink file",
                             path);
  }
  const auto length = std::filesystem::file_size(path, error);
  if (error) {
    return fail<std::string>(std::string(read_code), error.message(), path);
  }
  if (length > maximum_bytes) {
    return fail<std::string>(std::string(read_code),
                             "stored object exceeds its byte limit", path);
  }

#ifdef _WIN32
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return fail<std::string>(std::string(read_code),
                             "cannot open stored object", path);
  }
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
    CloseHandle(handle);
    return fail<std::string>(std::string(unsafe_code),
                             "opened object path is unsafe", path);
  }
  std::string bytes(static_cast<std::size_t>(length), '\0');
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const auto remaining = bytes.size() - offset;
    const DWORD request = static_cast<DWORD>(
        std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
    DWORD received = 0U;
    if (!ReadFile(handle, bytes.data() + offset, request, &received, nullptr) ||
        received == 0U) {
      CloseHandle(handle);
      return fail<std::string>(std::string(read_code),
                               "stored-object read failed", path);
    }
    offset += received;
  }
  CloseHandle(handle);
  return Result<std::string>::success(std::move(bytes));
#else
  const int descriptor =
      ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0) {
    return fail<std::string>(std::string(read_code), std::strerror(errno), path);
  }
  struct stat information {};
  if (::fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode)) {
    const auto message = std::strerror(errno);
    ::close(descriptor);
    return fail<std::string>(std::string(unsafe_code), message, path);
  }
  std::string bytes(static_cast<std::size_t>(length), '\0');
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const auto received =
        ::read(descriptor, bytes.data() + offset, bytes.size() - offset);
    if (received < 0 && errno == EINTR) {
      continue;
    }
    if (received <= 0) {
      const auto message = received == 0 ? "stored object truncated"
                                         : std::strerror(errno);
      ::close(descriptor);
      return fail<std::string>(std::string(read_code), message, path);
    }
    offset += static_cast<std::size_t>(received);
  }
  ::close(descriptor);
  return Result<std::string>::success(std::move(bytes));
#endif
}

Result<bool> write_exclusive_file(const std::filesystem::path &path,
                                  const std::string_view bytes) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (!error && status.type() != std::filesystem::file_type::not_found) {
    return fail<bool>("unsafe_temporary_path",
                      "object temporary path already exists", path);
  }
  if (error && error != std::errc::no_such_file_or_directory) {
    return fail<bool>("temporary_path_error", error.message(), path);
  }

#ifdef _WIN32
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0U, nullptr,
                              CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return fail<bool>("temporary_create_failed",
                      "cannot exclusively create object temporary file", path);
  }
  const auto remove_created_file = [&path] {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  };
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, std::numeric_limits<DWORD>::max()));
    DWORD written = 0U;
    if (!WriteFile(handle, bytes.data() + offset, request, &written, nullptr) ||
        written == 0U) {
      CloseHandle(handle);
      remove_created_file();
      return fail<bool>("temporary_write_failed",
                        "object temporary write failed", path);
    }
    offset += written;
  }
  if (!FlushFileBuffers(handle)) {
    CloseHandle(handle);
    remove_created_file();
    return fail<bool>("temporary_write_failed",
                      "object temporary flush failed", path);
  }
  CloseHandle(handle);
#else
  const int descriptor = ::open(path.c_str(),
                                O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                                    O_NOFOLLOW,
                                S_IRUSR | S_IWUSR);
  if (descriptor < 0) {
    return fail<bool>("temporary_create_failed", std::strerror(errno), path);
  }
  const auto remove_created_file = [&path] {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  };
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const auto written =
        ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      const auto message = std::strerror(errno);
      ::close(descriptor);
      remove_created_file();
      return fail<bool>("temporary_write_failed", message, path);
    }
    offset += static_cast<std::size_t>(written);
  }
  if (::fsync(descriptor) != 0) {
    const auto message = std::strerror(errno);
    ::close(descriptor);
    remove_created_file();
    return fail<bool>("temporary_write_failed", message, path);
  }
  if (::close(descriptor) != 0) {
    remove_created_file();
    return fail<bool>("temporary_write_failed", std::strerror(errno), path);
  }
#endif
  return Result<bool>::success(true);
}

Result<bool> install_without_replacement(const std::filesystem::path &temporary,
                                         const std::filesystem::path &destination) {
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                   MOVEFILE_WRITE_THROUGH)) {
    return fail<bool>("object_install_failed",
                      "cannot install object without replacement", destination);
  }
#else
  if (::link(temporary.c_str(), destination.c_str()) != 0) {
    return fail<bool>(errno == EEXIST ? "object_collision"
                                     : "object_install_failed",
                      std::strerror(errno), destination);
  }
  if (::unlink(temporary.c_str()) != 0) {
    return fail<bool>("temporary_cleanup_failed", std::strerror(errno),
                      temporary);
  }
#endif
  return Result<bool>::success(true);
}

Result<bool> verify_reference_and_bytes(
    const StoredObjectReference &reference, const std::string_view bytes) {
  if (!is_valid_object_hash(reference.object_hash)) {
    return fail<bool>("invalid_hash",
                      "object hash must be sha256 plus 64 lowercase hex digits");
  }
  if (!is_supported_object_reference(reference)) {
    return fail<bool>("unsupported_object_contract",
                      "media type, schema ID, and version are not registered");
  }
  if (bytes.size() > maximum_object_bytes ||
      reference.byte_length > maximum_object_bytes) {
    return fail<bool>("object_too_large", "object exceeds the 8 MiB limit");
  }
  if (reference.byte_length != bytes.size()) {
    return fail<bool>("object_length_mismatch",
                      "object bytes differ from the declared byte length");
  }
  try {
    const auto canonical = integrity::verify_canonical_bytes(bytes);
    if (integrity::object_hash(canonical) != reference.object_hash) {
      return fail<bool>("object_hash_mismatch",
                        "object bytes differ from the declared object hash");
    }
    const auto root = Json::parse(canonical);
    if (!root.is_object() || !root.contains("$schema") ||
        !root.contains("schema_version") ||
        !root.at("$schema").is_string() ||
        !root.at("schema_version").is_string() ||
        root.at("$schema").get<std::string>() != reference.schema_id ||
        root.at("schema_version").get<std::string>() !=
            reference.schema_version) {
      return fail<bool>("object_contract_mismatch",
                        "object body disagrees with its schema reference");
    }
  } catch (const integrity::CanonicalJsonError &failure) {
    return fail<bool>(failure.code(), failure.what());
  } catch (const std::exception &failure) {
    return fail<bool>("object_verification_failed", failure.what());
  }
  return Result<bool>::success(true);
}

Result<bool> verify_destination(const std::filesystem::path &destination,
                                const StoredObjectReference &reference,
                                const std::string_view expected_bytes,
                                const std::string_view collision_code) {
  const auto loaded = read_regular_file(destination, maximum_object_bytes,
                                        "unsafe_object_path",
                                        "object_read_failed");
  if (!loaded.has_value()) {
    return Result<bool>::failure(loaded.diagnostic());
  }
  if (loaded.value() != expected_bytes) {
    return fail<bool>(std::string(collision_code),
                      "different bytes already occupy the digest path",
                      destination);
  }
  const auto verified = verify_reference_and_bytes(reference, loaded.value());
  if (!verified.has_value()) {
    return verified;
  }
  return Result<bool>::success(true);
}

class TemporaryCleanup final {
public:
  explicit TemporaryCleanup(std::filesystem::path path)
      : path_(std::move(path)) {}
  TemporaryCleanup(const TemporaryCleanup &) = delete;
  TemporaryCleanup &operator=(const TemporaryCleanup &) = delete;
  ~TemporaryCleanup() {
    if (armed_) {
      std::error_code ignored;
      std::filesystem::remove(path_, ignored);
    }
  }
  void release() noexcept { armed_ = false; }

private:
  std::filesystem::path path_;
  bool armed_{true};
};

} // namespace

std::filesystem::path
sidecar_path_for_project(const std::filesystem::path &project_path) {
  auto result = project_path;
  result += ".data";
  return result;
}

Result<std::filesystem::path>
object_path_for_hash(const std::filesystem::path &sidecar_root,
                     const std::string_view object_hash) noexcept {
  if (!is_valid_object_hash(object_hash)) {
    return fail<std::filesystem::path>(
        "invalid_hash",
        "object hash must be sha256 plus 64 lowercase hex digits");
  }
  const auto digest = object_hash.substr(7U);
  return Result<std::filesystem::path>::success(
      sidecar_root / "objects" / "sha256" /
      std::string(digest.substr(0U, 2U)) / std::string(digest.substr(2U)));
}

std::filesystem::path
temporary_path_for_object(const std::filesystem::path &object_path) {
  auto result = object_path;
  result += ".tmp";
  return result;
}

Result<InstalledObject>
install_object(const std::filesystem::path &project_path,
               const StoredObjectReference &reference,
               const std::string_view bytes) noexcept {
  try {
    const auto project = ensure_regular_project(project_path);
    if (!project.has_value()) {
      return Result<InstalledObject>::failure(project.diagnostic());
    }
    const auto input = verify_reference_and_bytes(reference, bytes);
    if (!input.has_value()) {
      return Result<InstalledObject>::failure(input.diagnostic());
    }
    const auto sidecar = sidecar_path_for_project(project_path);
    const auto destination = object_path_for_hash(sidecar, reference.object_hash);
    if (!destination.has_value()) {
      return Result<InstalledObject>::failure(destination.diagnostic());
    }
    const auto digest = std::string_view(reference.object_hash).substr(7U);
    const auto directories = ensure_store_directories(sidecar, digest);
    if (!directories.has_value()) {
      return Result<InstalledObject>::failure(directories.diagnostic());
    }

    std::error_code error;
    const auto destination_status =
        std::filesystem::symlink_status(destination.value(), error);
    if (!error && destination_status.type() !=
                      std::filesystem::file_type::not_found) {
      if (std::filesystem::is_symlink(destination_status) ||
          !std::filesystem::is_regular_file(destination_status)) {
        return fail<InstalledObject>(
            "unsafe_object_path",
            "object destination exists but is not a regular non-symlink file",
            destination.value());
      }
      const auto existing = verify_destination(destination.value(), reference,
                                               bytes, "object_collision");
      if (!existing.has_value()) {
        return Result<InstalledObject>::failure(existing.diagnostic());
      }
      return Result<InstalledObject>::success(
          InstalledObject{destination.value(), true});
    }
    if (error && error != std::errc::no_such_file_or_directory) {
      return fail<InstalledObject>("object_path_error", error.message(),
                                   destination.value());
    }

    const auto temporary = temporary_path_for_object(destination.value());
    const auto written = write_exclusive_file(temporary, bytes);
    if (!written.has_value()) {
      return Result<InstalledObject>::failure(written.diagnostic());
    }
    TemporaryCleanup cleanup(temporary);
    const auto temporary_verified =
        verify_destination(temporary, reference, bytes, "object_collision");
    if (!temporary_verified.has_value()) {
      return Result<InstalledObject>::failure(temporary_verified.diagnostic());
    }
    const auto installed = install_without_replacement(temporary,
                                                       destination.value());
    if (!installed.has_value()) {
      if (installed.diagnostic().code == "object_collision") {
        const auto concurrent = verify_destination(
            destination.value(), reference, bytes, "object_collision");
        if (concurrent.has_value()) {
          return Result<InstalledObject>::success(
              InstalledObject{destination.value(), true});
        }
      }
      return Result<InstalledObject>::failure(installed.diagnostic());
    }
    cleanup.release();
    const auto final = verify_destination(destination.value(), reference, bytes,
                                          "object_collision");
    if (!final.has_value()) {
      return Result<InstalledObject>::failure(final.diagnostic());
    }
    return Result<InstalledObject>::success(
        InstalledObject{destination.value(), false});
  } catch (const std::exception &failure) {
    return fail<InstalledObject>("object_install_failed", failure.what());
  } catch (...) {
    return fail<InstalledObject>("object_install_failed",
                                 "unknown object installation failure");
  }
}

Result<std::string>
read_object(const std::filesystem::path &project_path,
            const StoredObjectReference &reference) noexcept {
  try {
    const auto project = ensure_regular_project(project_path);
    if (!project.has_value()) {
      return Result<std::string>::failure(project.diagnostic());
    }
    if (!is_valid_object_hash(reference.object_hash)) {
      return fail<std::string>("invalid_hash", "stored-object hash is invalid");
    }
    if (!is_supported_object_reference(reference)) {
      return fail<std::string>("unsupported_object_contract",
                               "stored-object contract is unsupported");
    }
    const auto sidecar = sidecar_path_for_project(project_path);
    const auto destination = object_path_for_hash(sidecar, reference.object_hash);
    if (!destination.has_value()) {
      return Result<std::string>::failure(destination.diagnostic());
    }
    const auto digest = std::string_view(reference.object_hash).substr(7U);
    const auto directories = validate_store_directories(sidecar, digest);
    if (!directories.has_value()) {
      return Result<std::string>::failure(directories.diagnostic());
    }
    const auto loaded = read_regular_file(destination.value(), maximum_object_bytes,
                                          "unsafe_object_path",
                                          "object_read_failed");
    if (!loaded.has_value()) {
      return loaded;
    }
    const auto verified = verify_reference_and_bytes(reference, loaded.value());
    if (!verified.has_value()) {
      return Result<std::string>::failure(verified.diagnostic());
    }
    return loaded;
  } catch (const std::exception &failure) {
    return fail<std::string>("object_read_failed", failure.what());
  } catch (...) {
    return fail<std::string>("object_read_failed",
                             "unknown stored-object read failure");
  }
}

} // namespace prometheus::run_store
