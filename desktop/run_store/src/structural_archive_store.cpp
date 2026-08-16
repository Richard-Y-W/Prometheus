#include <prometheus/run_store/structural_archive_store.hpp>

#include "platform_io.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>

namespace prometheus::run_store {
namespace {

using Json = nlohmann::json;
constexpr std::array<std::string_view, 7> artifactKeys{
    "setup", "deck", "dat", "frd", "sta", "stdout", "stderr"};

template <typename T> Result<T> failure(std::string code, std::string message,
                                       const std::filesystem::path &path = {}) {
  return Result<T>::failure(detail::store_diagnostic(
      std::move(code), std::move(message), std::nullopt,
      path.empty() ? std::nullopt
                   : std::optional<std::filesystem::path>(path)));
}

bool safeFile(const std::string &name) {
  return !name.empty() && name.size() <= 128U && name != "." && name != ".." &&
         name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos;
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to open structural archive file");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path &path, const std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) throw std::runtime_error("unable to write reconstructed archive file");
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
                     std::string schemaId,
                     std::string schemaVersion = "1.0.0") {
  bytes = integrity::canonicalize_json_bytes(bytes);
  return {{integrity::object_hash(bytes),
           static_cast<std::uint64_t>(bytes.size()), std::move(mediaType),
           std::move(schemaId), std::move(schemaVersion)},
          std::move(bytes)};
}

std::string base64Encode(const std::string_view bytes) {
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((bytes.size() + 2U) / 3U) * 4U);
  for (std::size_t offset = 0; offset < bytes.size(); offset += 3U) {
    const auto remaining = bytes.size() - offset;
    const std::uint32_t first = static_cast<unsigned char>(bytes[offset]);
    const std::uint32_t second = remaining > 1U
        ? static_cast<unsigned char>(bytes[offset + 1U]) : 0U;
    const std::uint32_t third = remaining > 2U
        ? static_cast<unsigned char>(bytes[offset + 2U]) : 0U;
    const auto value = (first << 16U) | (second << 8U) | third;
    result.push_back(alphabet[(value >> 18U) & 63U]);
    result.push_back(alphabet[(value >> 12U) & 63U]);
    result.push_back(remaining > 1U ? alphabet[(value >> 6U) & 63U] : '=');
    result.push_back(remaining > 2U ? alphabet[value & 63U] : '=');
  }
  return result;
}

std::string base64Decode(const std::string_view text) {
  if (text.size() % 4U != 0U) throw std::runtime_error("invalid base64 length");
  const auto value = [](const char character) -> int {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
  };
  std::string result;
  result.reserve(text.size() / 4U * 3U);
  for (std::size_t offset = 0; offset < text.size(); offset += 4U) {
    const int a = value(text[offset]);
    const int b = value(text[offset + 1U]);
    const int c = text[offset + 2U] == '=' ? 0 : value(text[offset + 2U]);
    const int d = text[offset + 3U] == '=' ? 0 : value(text[offset + 3U]);
    if (a < 0 || b < 0 || c < 0 || d < 0 ||
        (text[offset + 2U] == '=' && text[offset + 3U] != '=') ||
        (offset + 4U != text.size() &&
         (text[offset + 2U] == '=' || text[offset + 3U] == '=')))
      throw std::runtime_error("invalid base64 data");
    const auto packed = (static_cast<std::uint32_t>(a) << 18U) |
                        (static_cast<std::uint32_t>(b) << 12U) |
                        (static_cast<std::uint32_t>(c) << 6U) |
                        static_cast<std::uint32_t>(d);
    result.push_back(static_cast<char>((packed >> 16U) & 255U));
    if (text[offset + 2U] != '=')
      result.push_back(static_cast<char>((packed >> 8U) & 255U));
    if (text[offset + 3U] != '=')
      result.push_back(static_cast<char>(packed & 255U));
  }
  return result;
}

} // namespace

Result<StructuralArchiveObjects> build_structural_archive_objects(
    const std::filesystem::path &manifestPath,
    std::string assemblyArtifactHash) noexcept {
  try {
    if (!is_valid_object_hash(assemblyArtifactHash))
      return failure<StructuralArchiveObjects>(
          "assembly_artifact_hash_invalid",
          "structural publication requires the exact project assembly identity");
    auto manifestBytes = integrity::verify_canonical_bytes(readFile(manifestPath));
    const auto archive = Json::parse(manifestBytes);
    const auto schemaId = archive.value("$schema", "");
    const auto schemaVersion = archive.value("schema_version", "");
    const bool version1 = schemaId == structural_manifest_schema_id_v1 &&
                          schemaVersion == "1.0.0";
    const bool version2 = schemaId == structural_manifest_schema_id_v2 &&
                          schemaVersion == "2.0.0";
    if ((!version1 && !version2) ||
        archive.value("archive_kind", "") != "completed_linear_static_run" ||
        !archive.contains("artifacts") || !archive.at("artifacts").is_object())
      return failure<StructuralArchiveObjects>(
          "structural_manifest_contract_invalid",
          "source is not a completed structural archive manifest", manifestPath);

    StructuralArchiveObjects result;
    result.archive_manifest = object(
        manifestBytes, std::string(structural_manifest_media_type),
        schemaId, schemaVersion);
    Json storedArtifacts = Json::array();
    for (const auto key : artifactKeys) {
      const auto &declared = archive.at("artifacts").at(std::string(key));
      const auto filename = declared.at("file").get<std::string>();
      const auto byteLength = declared.at("byte_length").get<std::uint64_t>();
      const auto sha256 = declared.at("sha256").get<std::string>();
      if (!safeFile(filename))
        return failure<StructuralArchiveObjects>(
            "unsafe_artifact_path", "archive artifact filename is unsafe", manifestPath);
      const auto artifactPath = manifestPath.parent_path() / filename;
      std::error_code sizeError;
      const auto actualLength = std::filesystem::file_size(artifactPath, sizeError);
      if (sizeError || actualLength != byteLength ||
          integrity::sha256_file(artifactPath) != sha256)
        return failure<StructuralArchiveObjects>(
            "artifact_identity_mismatch", filename + " bytes changed", artifactPath);

      std::ifstream input(artifactPath, std::ios::binary);
      if (!input) throw std::runtime_error("unable to open structural artifact");
      const auto chunkCount = static_cast<std::size_t>(
          (byteLength + structural_artifact_chunk_bytes - 1U) /
          structural_artifact_chunk_bytes);
      Json chunkReferences = Json::array();
      std::vector<char> buffer(structural_artifact_chunk_bytes);
      for (std::size_t index = 0; index < chunkCount; ++index) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) throw std::runtime_error("structural artifact ended early");
        const std::string_view chunk(buffer.data(), static_cast<std::size_t>(count));
        const Json chunkDocument{
            {"$schema", structural_artifact_chunk_schema_id},
            {"schema_version", "1.0.0"}, {"encoding", "base64"},
            {"artifact_sha256", sha256}, {"chunk_index", index},
            {"chunk_count", chunkCount},
            {"byte_offset", index * structural_artifact_chunk_bytes},
            {"decoded_length", static_cast<std::uint64_t>(count)},
            {"data", base64Encode(chunk)}};
        result.chunks.push_back(object(
            chunkDocument.dump(),
            std::string(structural_artifact_chunk_media_type),
            std::string(structural_artifact_chunk_schema_id)));
        chunkReferences.push_back(referenceJson(result.chunks.back().reference));
      }
      if (input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("structural artifact exceeds declared length");
      storedArtifacts.push_back(
          {{"role", key}, {"file", filename}, {"byte_length", byteLength},
           {"sha256", sha256}, {"chunks", std::move(chunkReferences)}});
    }
    const Json projectManifest{
        {"$schema", structural_project_run_schema_id},
        {"schema_version", "1.0.0"},
        {"manifest_kind", "embedded_structural_run"},
        {"assembly_artifact_hash", std::move(assemblyArtifactHash)},
        {"archive_manifest", referenceJson(result.archive_manifest.reference)},
        {"artifacts", std::move(storedArtifacts)}};
    result.project_manifest = object(
        projectManifest.dump(), std::string(structural_project_run_media_type),
        std::string(structural_project_run_schema_id));
    return Result<StructuralArchiveObjects>::success(std::move(result));
  } catch (const integrity::CanonicalJsonError &error) {
    return failure<StructuralArchiveObjects>(error.code(), error.what(), manifestPath);
  } catch (const std::exception &error) {
    return failure<StructuralArchiveObjects>(
        "structural_archive_pack_failed", error.what(), manifestPath);
  }
}

Result<std::filesystem::path> reconstruct_structural_archive(
    const std::filesystem::path &projectPath,
    const StoredObjectReference &projectManifestReference,
    const std::filesystem::path &destinationDirectory) noexcept {
  std::filesystem::path temporary;
  try {
    if (std::filesystem::exists(destinationDirectory) ||
        !std::filesystem::is_directory(destinationDirectory.parent_path()))
      return failure<std::filesystem::path>(
          "archive_destination_invalid",
          "archive destination must not exist and its parent must exist",
          destinationDirectory);
    const auto manifestBytes = read_object(projectPath, projectManifestReference);
    if (!manifestBytes.has_value())
      return Result<std::filesystem::path>::failure(manifestBytes.diagnostic());
    const auto manifest = Json::parse(manifestBytes.value());
    if (manifest.value("$schema", "") != structural_project_run_schema_id ||
        manifest.value("manifest_kind", "") != "embedded_structural_run")
      return failure<std::filesystem::path>(
          "structural_project_manifest_invalid",
          "committed object is not an embedded structural run");
    std::mt19937_64 random{std::random_device{}()};
    temporary = destinationDirectory;
    temporary += ".partial-" + std::to_string(random());
    std::filesystem::create_directory(temporary);

    const auto archiveReference = parseReference(manifest.at("archive_manifest"));
    const auto archiveBytes = read_object(projectPath, archiveReference);
    if (!archiveBytes.has_value())
      return Result<std::filesystem::path>::failure(archiveBytes.diagnostic());
    writeFile(temporary / "prometheus-structural-run.json", archiveBytes.value());
    for (const auto &artifact : manifest.at("artifacts")) {
      const auto filename = artifact.at("file").get<std::string>();
      if (!safeFile(filename)) throw std::runtime_error("unsafe stored artifact filename");
      std::string decoded;
      decoded.reserve(artifact.at("byte_length").get<std::size_t>());
      std::size_t expectedIndex = 0U;
      for (const auto &chunkReferenceJson : artifact.at("chunks")) {
        const auto chunkReference = parseReference(chunkReferenceJson);
        const auto chunkBytes = read_object(projectPath, chunkReference);
        if (!chunkBytes.has_value())
          return Result<std::filesystem::path>::failure(chunkBytes.diagnostic());
        const auto chunk = Json::parse(chunkBytes.value());
        if (chunk.value("$schema", "") != structural_artifact_chunk_schema_id ||
            chunk.value("encoding", "") != "base64" ||
            chunk.at("chunk_index").get<std::size_t>() != expectedIndex ||
            chunk.at("artifact_sha256").get<std::string>() !=
                artifact.at("sha256").get<std::string>() ||
            chunk.at("byte_offset").get<std::size_t>() != decoded.size())
          throw std::runtime_error("stored structural chunk graph is invalid");
        auto raw = base64Decode(chunk.at("data").get<std::string>());
        if (raw.size() != chunk.at("decoded_length").get<std::size_t>())
          throw std::runtime_error("stored structural chunk length is invalid");
        decoded += raw;
        ++expectedIndex;
      }
      if (decoded.size() != artifact.at("byte_length").get<std::size_t>() ||
          integrity::sha256_bytes(decoded) != artifact.at("sha256").get<std::string>())
        throw std::runtime_error("reconstructed structural artifact identity differs");
      writeFile(temporary / filename, decoded);
    }
    std::filesystem::rename(temporary, destinationDirectory);
    return Result<std::filesystem::path>::success(
        destinationDirectory / "prometheus-structural-run.json");
  } catch (const std::exception &error) {
    std::error_code ignored;
    if (!temporary.empty()) std::filesystem::remove_all(temporary, ignored);
    return failure<std::filesystem::path>(
        "structural_archive_reconstruction_failed", error.what(),
        destinationDirectory);
  }
}

} // namespace prometheus::run_store
