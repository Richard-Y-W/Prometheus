#include <prometheus/run_store/project_evidence_archive.hpp>

#include "platform_io.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <random>
#include <stdexcept>

namespace prometheus::run_store {
namespace {
using Json = nlohmann::json;

template <typename T> Result<T> failure(std::string code, std::string message) {
  return Result<T>::failure(
      detail::store_diagnostic(std::move(code), std::move(message)));
}

Json referenceJson(const StoredObjectReference &reference) {
  return {{"object_hash", reference.object_hash},
          {"byte_length", reference.byte_length},
          {"media_type", reference.media_type},
          {"schema_id", reference.schema_id},
          {"schema_version", reference.schema_version}};
}

StoredObjectReference parseReference(const Json &value) {
  return {value.at("object_hash").get<std::string>(),
          value.at("byte_length").get<std::uint64_t>(),
          value.at("media_type").get<std::string>(),
          value.at("schema_id").get<std::string>(),
          value.at("schema_version").get<std::string>()};
}

ObjectToStore object(std::string bytes, std::string mediaType,
                     std::string schemaId) {
  bytes = integrity::canonicalize_json_bytes(bytes);
  return {{integrity::sha256_bytes(bytes), bytes.size(), std::move(mediaType),
           std::move(schemaId), "1.0.0"},
          std::move(bytes)};
}

std::string encode(const std::string_view bytes) {
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((bytes.size() + 2U) / 3U) * 4U);
  for (std::size_t offset = 0; offset < bytes.size(); offset += 3U) {
    const auto remaining = bytes.size() - offset;
    const std::uint32_t a = static_cast<unsigned char>(bytes[offset]);
    const std::uint32_t b = remaining > 1U
                                ? static_cast<unsigned char>(bytes[offset + 1U])
                                : 0U;
    const std::uint32_t c = remaining > 2U
                                ? static_cast<unsigned char>(bytes[offset + 2U])
                                : 0U;
    const auto value = (a << 16U) | (b << 8U) | c;
    result.push_back(alphabet[(value >> 18U) & 63U]);
    result.push_back(alphabet[(value >> 12U) & 63U]);
    result.push_back(remaining > 1U ? alphabet[(value >> 6U) & 63U] : '=');
    result.push_back(remaining > 2U ? alphabet[value & 63U] : '=');
  }
  return result;
}

std::string decode(const std::string_view text) {
  if (text.size() % 4U) throw std::runtime_error("invalid base64 length");
  const auto digit = [](const char value) -> int {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
  };
  std::string bytes;
  for (std::size_t offset = 0; offset < text.size(); offset += 4U) {
    const int a = digit(text[offset]), b = digit(text[offset + 1U]);
    const int c = text[offset + 2U] == '=' ? 0 : digit(text[offset + 2U]);
    const int d = text[offset + 3U] == '=' ? 0 : digit(text[offset + 3U]);
    if (a < 0 || b < 0 || c < 0 || d < 0 ||
        (text[offset + 2U] == '=' && text[offset + 3U] != '=') ||
        (offset + 4U != text.size() &&
         (text[offset + 2U] == '=' || text[offset + 3U] == '=')))
      throw std::runtime_error("invalid base64");
    const auto packed = (static_cast<std::uint32_t>(a) << 18U) |
                        (static_cast<std::uint32_t>(b) << 12U) |
                        (static_cast<std::uint32_t>(c) << 6U) |
                        static_cast<std::uint32_t>(d);
    bytes.push_back(static_cast<char>((packed >> 16U) & 255U));
    if (text[offset + 2U] != '=')
      bytes.push_back(static_cast<char>((packed >> 8U) & 255U));
    if (text[offset + 3U] != '=') bytes.push_back(static_cast<char>(packed & 255U));
  }
  return bytes;
}

bool internalPath(const std::string &path) {
  return path.ends_with(".prometheus") ||
         path.find(".prometheus.data/") != std::string::npos;
}

bool safeRelative(const std::string &text) {
  const auto path = std::filesystem::path(text);
  if (text.empty() || text.size() > 4096U || path.is_absolute() ||
      path.has_root_path() || path != path.lexically_normal())
    return false;
  return std::ranges::none_of(path, [](const auto &part) {
    return part == "." || part == "..";
  });
}
} // namespace

Result<ProjectEvidenceArchiveObjects> build_project_evidence_archive(
    const StoredObjectReference &inventorySnapshot,
    const std::vector<ProjectEvidenceInput> &inputFiles) noexcept {
  try {
    ProjectEvidenceArchiveObjects result;
    Json files = Json::array();
    std::uint64_t retainedTotal = 0U;
    for (const auto &file : inputFiles) {
      std::string disposition = "external_only";
      std::string reason = "identity_unavailable";
      Json chunks = Json::array();
      const bool readable = file.sha256.has_value() &&
                            file.analysis_state != "unreadable";
      if (file.category == "geometry") reason = "cad_transported_separately";
      else if (internalPath(file.relative_path)) reason = "prometheus_internal_state";
      else if (!readable) reason = "unreadable_or_unsafe_source";
      else if (file.byte_length > project_evidence_file_limit)
        reason = "per_file_size_limit";
      else if (retainedTotal + file.byte_length > project_evidence_total_limit)
        reason = "archive_total_size_limit";
      else {
        disposition = file.category == "other" || file.category == "source_code"
                          ? "quarantined"
                          : "retained";
        reason = disposition == "quarantined" ? "inert_untrusted_content"
                                                : "portable_evidence";
        std::ifstream stream(file.absolute_path, std::ios::binary);
        if (!stream) throw std::runtime_error("retained evidence cannot be opened");
        std::vector<char> buffer(project_evidence_chunk_bytes);
        std::uint64_t offset = 0U;
        while (offset < file.byte_length) {
          const auto wanted = static_cast<std::streamsize>(std::min<std::uint64_t>(
              buffer.size(), file.byte_length - offset));
          stream.read(buffer.data(), wanted);
          if (stream.gcount() != wanted)
            throw std::runtime_error("retained evidence changed while archiving");
          const std::string_view bytes(buffer.data(), static_cast<std::size_t>(wanted));
          const Json chunk{{"$schema", project_evidence_chunk_schema_id},
                           {"schema_version", "1.0.0"},
                           {"encoding", "base64"},
                           {"file_sha256", *file.sha256},
                           {"byte_offset", offset},
                           {"decoded_length", static_cast<std::uint64_t>(wanted)},
                           {"data", encode(bytes)}};
          result.chunks.push_back(object(
              chunk.dump(), std::string(project_evidence_chunk_media_type),
              std::string(project_evidence_chunk_schema_id)));
          chunks.push_back(referenceJson(result.chunks.back().reference));
          offset += static_cast<std::uint64_t>(wanted);
        }
        if (integrity::sha256_file(file.absolute_path) != *file.sha256)
          throw std::runtime_error("retained evidence identity changed while archiving");
        retainedTotal += file.byte_length;
      }
      files.push_back({{"relative_path", file.relative_path},
                       {"byte_length", file.byte_length},
                       {"sha256", file.sha256 ? Json(*file.sha256) : Json(nullptr)},
                       {"category", file.category},
                       {"disposition", disposition}, {"reason", reason},
                       {"chunks", std::move(chunks)}});
    }
    const Json manifest{{"$schema", project_evidence_archive_schema_id},
                        {"schema_version", "1.0.0"},
                        {"archive_kind", "bounded_inert_project_evidence"},
                        {"inventory_snapshot", referenceJson(inventorySnapshot)},
                        {"file_limit_bytes", project_evidence_file_limit},
                        {"total_limit_bytes", project_evidence_total_limit},
                        {"files", std::move(files)}};
    result.manifest = object(manifest.dump(),
                             std::string(project_evidence_archive_media_type),
                             std::string(project_evidence_archive_schema_id));
    return Result<ProjectEvidenceArchiveObjects>::success(std::move(result));
  } catch (const std::exception &error) {
    return failure<ProjectEvidenceArchiveObjects>("project_evidence_pack_failed",
                                                   error.what());
  }
}

Result<bool> validate_project_evidence_archive(
    const ObjectToStore &inventorySnapshot,
    const ProjectEvidenceArchiveObjects &archive) noexcept {
  try {
    const auto manifest = Json::parse(
        integrity::verify_canonical_bytes(archive.manifest.bytes));
    if (inventorySnapshot.reference.schema_id != project_inventory_schema_id ||
        inventorySnapshot.reference.object_hash !=
            integrity::sha256_bytes(inventorySnapshot.bytes) ||
        inventorySnapshot.reference.byte_length != inventorySnapshot.bytes.size() ||
        archive.manifest.reference.schema_id != project_evidence_archive_schema_id ||
        archive.manifest.reference.media_type !=
            project_evidence_archive_media_type ||
        archive.manifest.reference.schema_version != "1.0.0" ||
        archive.manifest.reference.byte_length != archive.manifest.bytes.size() ||
        archive.manifest.reference.object_hash !=
            integrity::sha256_bytes(archive.manifest.bytes) ||
        manifest.value("$schema", "") != project_evidence_archive_schema_id ||
        manifest.value("schema_version", "") != "1.0.0" ||
        manifest.value("archive_kind", "") !=
            "bounded_inert_project_evidence" ||
        manifest.value("file_limit_bytes", 0ULL) !=
            project_evidence_file_limit ||
        manifest.value("total_limit_bytes", 0ULL) !=
            project_evidence_total_limit ||
        parseReference(manifest.at("inventory_snapshot")) !=
            inventorySnapshot.reference)
      return failure<bool>("project_evidence_manifest_invalid",
                           "evidence archive manifest identity is invalid");
    const auto inventory = Json::parse(
        integrity::verify_canonical_bytes(inventorySnapshot.bytes));
    std::map<std::string, Json> accounted;
    for (const auto &artifact : inventory.at("artifacts"))
      accounted.emplace(artifact.at("relative_path").get<std::string>(), artifact);
    if (manifest.at("files").size() != accounted.size())
      return failure<bool>("project_evidence_inventory_mismatch",
                           "evidence archive does not account for every inventory file");
    std::map<std::string, const ObjectToStore *> supplied;
    for (const auto &chunk : archive.chunks)
      if (!supplied.emplace(chunk.reference.object_hash, &chunk).second ||
          chunk.reference.schema_id != project_evidence_chunk_schema_id ||
          chunk.reference.media_type != project_evidence_chunk_media_type ||
          chunk.reference.schema_version != "1.0.0" ||
          chunk.reference.byte_length != chunk.bytes.size() ||
          chunk.reference.object_hash != integrity::sha256_bytes(chunk.bytes))
        return failure<bool>("project_evidence_chunk_invalid",
                             "evidence archive chunk registration is invalid");
    std::size_t referenced = 0U;
    std::string previousPath;
    for (const auto &file : manifest.at("files")) {
      const auto path = file.at("relative_path").get<std::string>();
      if (!safeRelative(path) || (!previousPath.empty() && path <= previousPath))
        return failure<bool>("project_evidence_path_invalid",
                             "evidence paths must be safe and sorted");
      previousPath = path;
      const auto foundAccounted = accounted.find(path);
      if (foundAccounted == accounted.end() ||
          foundAccounted->second.at("byte_length") != file.at("byte_length") ||
          foundAccounted->second.at("sha256") != file.at("sha256") ||
          foundAccounted->second.at("category") != file.at("category"))
        return failure<bool>("project_evidence_inventory_mismatch",
                             "evidence archive file differs from its inventory");
      const auto disposition = file.at("disposition").get<std::string>();
      if ((file.at("chunks").empty() && disposition != "external_only") ||
          (!file.at("chunks").empty() && disposition != "retained" &&
           disposition != "quarantined"))
        return failure<bool>("project_evidence_disposition_invalid",
                             "evidence disposition conflicts with retained bytes");
      std::string reconstructed;
      std::uint64_t offset = 0U;
      for (const auto &referenceValue : file.at("chunks")) {
        const auto reference = parseReference(referenceValue);
        const auto found = supplied.find(reference.object_hash);
        if (found == supplied.end() || found->second->reference != reference)
          return failure<bool>("project_evidence_chunk_missing",
                               "evidence archive chunk graph is incomplete");
        const auto document = Json::parse(
            integrity::verify_canonical_bytes(found->second->bytes));
        if (document.value("$schema", "") != project_evidence_chunk_schema_id ||
            document.value("schema_version", "") != "1.0.0" ||
            document.value("encoding", "") != "base64")
          return failure<bool>("project_evidence_chunk_invalid",
                               "evidence archive chunk contract is invalid");
        const auto decoded = decode(document.at("data").get<std::string>());
        if (document.at("byte_offset").get<std::uint64_t>() != offset ||
            document.at("decoded_length").get<std::uint64_t>() != decoded.size() ||
            document.at("file_sha256") != file.at("sha256"))
          return failure<bool>("project_evidence_chunk_invalid",
                               "evidence archive chunk identity is invalid");
        reconstructed += decoded;
        offset += decoded.size();
        ++referenced;
      }
      if (!file.at("chunks").empty() &&
          (offset != file.at("byte_length").get<std::uint64_t>() ||
           integrity::sha256_bytes(reconstructed) !=
               file.at("sha256").get<std::string>()))
        return failure<bool>("project_evidence_file_changed",
                             "reconstructed evidence identity differs");
    }
    if (referenced != archive.chunks.size())
      return failure<bool>("project_evidence_chunk_unreferenced",
                           "evidence archive contains an unreferenced chunk");
    return Result<bool>::success(true);
  } catch (const std::exception &error) {
    return failure<bool>("project_evidence_validation_failed", error.what());
  }
}

Result<bool> verify_project_evidence_archive(
    const std::filesystem::path &projectPath,
    const StoredObjectReference &archiveReference) noexcept {
  try {
    const auto manifestBytes = read_object(projectPath, archiveReference);
    if (!manifestBytes.has_value())
      return Result<bool>::failure(manifestBytes.diagnostic());
    ProjectEvidenceArchiveObjects archive;
    archive.manifest = {archiveReference, manifestBytes.value()};
    const auto manifest = Json::parse(manifestBytes.value());
    const auto inventoryReference =
        parseReference(manifest.at("inventory_snapshot"));
    const auto inventoryBytes = read_object(projectPath, inventoryReference);
    if (!inventoryBytes.has_value())
      return Result<bool>::failure(inventoryBytes.diagnostic());
    std::map<std::string, StoredObjectReference> references;
    for (const auto &file : manifest.at("files"))
      for (const auto &value : file.at("chunks")) {
        auto reference = parseReference(value);
        references.emplace(reference.object_hash, std::move(reference));
      }
    for (const auto &[hash, reference] : references) {
      (void)hash;
      const auto bytes = read_object(projectPath, reference);
      if (!bytes.has_value()) return Result<bool>::failure(bytes.diagnostic());
      archive.chunks.push_back({reference, bytes.value()});
    }
    return validate_project_evidence_archive(
        {inventoryReference, inventoryBytes.value()}, archive);
  } catch (const std::exception &error) {
    return failure<bool>("project_evidence_verification_failed", error.what());
  }
}

Result<std::filesystem::path> reconstruct_project_evidence_archive(
    const std::filesystem::path &projectPath,
    const StoredObjectReference &archiveReference,
    const std::filesystem::path &destination) noexcept {
  std::filesystem::path temporary;
  try {
    if (std::filesystem::exists(destination) ||
        !std::filesystem::is_directory(destination.parent_path()))
      return failure<std::filesystem::path>(
          "project_evidence_destination_invalid",
          "evidence destination must not exist and its parent must exist");
    const auto verified = verify_project_evidence_archive(projectPath,
                                                           archiveReference);
    if (!verified.has_value())
      return Result<std::filesystem::path>::failure(verified.diagnostic());
    const auto manifestBytes = read_object(projectPath, archiveReference);
    if (!manifestBytes.has_value())
      return Result<std::filesystem::path>::failure(manifestBytes.diagnostic());
    const auto manifest = Json::parse(manifestBytes.value());
    std::mt19937_64 random{std::random_device{}()};
    temporary = destination;
    temporary += ".partial-" + std::to_string(random());
    std::filesystem::create_directories(temporary / "retained");
    std::filesystem::create_directories(temporary / "quarantine");
    for (const auto &file : manifest.at("files")) {
      const auto disposition = file.at("disposition").get<std::string>();
      if (disposition == "external_only") continue;
      auto relative = std::filesystem::path(
          file.at("relative_path").get<std::string>());
      auto target = temporary /
                    (disposition == "quarantined" ? "quarantine" : "retained") /
                    relative;
      if (disposition == "quarantined")
        target += ".prometheus-quarantined";
      std::filesystem::create_directories(target.parent_path());
      std::ofstream output(target, std::ios::binary | std::ios::trunc);
      if (!output) throw std::runtime_error("unable to create evidence output");
      for (const auto &value : file.at("chunks")) {
        const auto reference = parseReference(value);
        const auto bytes = read_object(projectPath, reference);
        if (!bytes.has_value())
          throw std::runtime_error(bytes.diagnostic().message);
        const auto chunk = Json::parse(bytes.value());
        const auto decoded = decode(chunk.at("data").get<std::string>());
        output.write(decoded.data(), static_cast<std::streamsize>(decoded.size()));
      }
      output.close();
      if (!output || integrity::sha256_file(target) !=
                         file.at("sha256").get<std::string>())
        throw std::runtime_error("reconstructed evidence identity differs");
    }
    std::filesystem::rename(temporary, destination);
    return Result<std::filesystem::path>::success(destination);
  } catch (const std::exception &error) {
    std::error_code ignored;
    if (!temporary.empty()) std::filesystem::remove_all(temporary, ignored);
    return failure<std::filesystem::path>(
        "project_evidence_reconstruction_failed", error.what());
  }
}

} // namespace prometheus::run_store
