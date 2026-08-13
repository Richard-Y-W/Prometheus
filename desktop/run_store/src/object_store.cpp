#include <prometheus/run_store/object_store.hpp>

#include "platform_io.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace prometheus::run_store {
namespace detail {

Result<Unit>
verify_stored_object(const StoredObjectReference &reference,
                     const std::string_view bytes) noexcept {
  if (!is_valid_object_hash(reference.object_hash)) {
    return Result<Unit>::failure(store_diagnostic(
        "invalid_hash",
        "object hash must be sha256 plus 64 lowercase hexadecimal digits"));
  }
  if (!is_supported_object_reference(reference)) {
    return Result<Unit>::failure(store_diagnostic(
        "unsupported_object_contract",
        "media type, schema ID, and version are not registered"));
  }
  if (bytes.size() > maximum_object_bytes ||
      reference.byte_length > maximum_object_bytes) {
    return Result<Unit>::failure(store_diagnostic(
        "object_too_large", "object exceeds the 8 MiB limit"));
  }
  if (reference.byte_length != bytes.size()) {
    return Result<Unit>::failure(store_diagnostic(
        "object_length_mismatch",
        "object bytes differ from the declared byte length"));
  }
  try {
    const auto canonical = integrity::verify_canonical_bytes(bytes);
    if (reference.schema_id ==
        "urn:prometheus:schema:execution-component:2.0.0") {
      static_cast<void>(integrity::verify_execution_component(
          bytes, reference.object_hash));
    }
    if (integrity::object_hash(canonical) != reference.object_hash) {
      return Result<Unit>::failure(store_diagnostic(
          "object_hash_mismatch",
          "object bytes differ from the declared object hash"));
    }
    const auto root = nlohmann::json::parse(canonical);
    if (!root.is_object() || !root.contains("$schema") ||
        !root.contains("schema_version") ||
        !root.at("$schema").is_string() ||
        !root.at("schema_version").is_string() ||
        root.at("$schema").get<std::string>() != reference.schema_id ||
        root.at("schema_version").get<std::string>() !=
            reference.schema_version) {
      return Result<Unit>::failure(store_diagnostic(
          "object_contract_mismatch",
          "object body disagrees with its schema reference"));
    }
    return Result<Unit>::success(Unit{});
  } catch (const integrity::CanonicalJsonError &failure) {
    return Result<Unit>::failure(
        store_diagnostic(failure.code(), failure.what()));
  } catch (const std::exception &failure) {
    return Result<Unit>::failure(
        store_diagnostic("object_verification_failed", failure.what()));
  } catch (...) {
    return Result<Unit>::failure(store_diagnostic(
        "object_verification_failed", "unknown object verification failure"));
  }
}

} // namespace detail

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
    return Result<std::filesystem::path>::failure(detail::store_diagnostic(
        "invalid_hash",
        "object hash must be sha256 plus 64 lowercase hexadecimal digits"));
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
  return detail::install_object_file(project_path, reference, bytes,
                                     TransactionOptions{});
}

Result<std::string>
read_object(const std::filesystem::path &project_path,
            const StoredObjectReference &reference) noexcept {
  return detail::read_object_file(project_path, reference);
}

} // namespace prometheus::run_store
