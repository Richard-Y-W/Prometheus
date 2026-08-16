#include <prometheus/run_store/project_bundle.hpp>

#include <prometheus/run_store/object_store.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace prometheus::run_store {
namespace {

using Json = nlohmann::json;
constexpr auto bundleSchema = "urn:prometheus:schema:portable-project-bundle:1.0.0";
constexpr auto bundleProjectName = "project.prometheus";
constexpr auto bundleManifestName = "prometheus-project-bundle.json";

template <typename T> Result<T> failure(std::string code, std::string message,
                                       const std::filesystem::path &path = {}) {
  return Result<T>::failure(Diagnostic{
      "store", std::move(code), std::move(message), std::nullopt,
      path.empty() ? std::nullopt
                   : std::optional<std::string>(path.generic_string())});
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to open bundle file");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path &path, const std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) throw std::runtime_error("unable to write bundle file");
}

bool safeFilename(const std::string &name) {
  return !name.empty() && name.size() <= 128U && name != "." && name != ".." &&
         name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos;
}

bool safeRelativePath(const std::filesystem::path &path) {
  if (path.empty() || path.is_absolute() || path.has_root_path() ||
      path != path.lexically_normal())
    return false;
  for (const auto &component : path)
    if (component == "." || component == "..") return false;
  return true;
}

StoredObjectReference parseReference(const Json &value) {
  return {value.at("object_hash").get<std::string>(),
          value.at("byte_length").get<std::uint64_t>(),
          value.at("media_type").get<std::string>(),
          value.at("schema_id").get<std::string>(),
          value.at("schema_version").get<std::string>()};
}

Json referenceJson(const StoredObjectReference &reference) {
  return {{"object_hash", reference.object_hash},
          {"byte_length", reference.byte_length},
          {"media_type", reference.media_type},
          {"schema_id", reference.schema_id},
          {"schema_version", reference.schema_version}};
}

Result<std::unordered_map<std::string, StoredObjectReference>> reachableObjects(
    const std::filesystem::path &projectPath, const ProjectV2 &project) {
  try {
    std::queue<StoredObjectReference> pending;
    for (const auto &binding : project.execution.package_bindings)
      pending.push(binding.package);
    if (project.execution.current_scenario)
      pending.push(*project.execution.current_scenario);
    for (const auto &run : project.execution.committed_runs) pending.push(run);
    std::unordered_map<std::string, StoredObjectReference> result;
    while (!pending.empty()) {
      auto reference = std::move(pending.front());
      pending.pop();
      if (const auto found = result.find(reference.object_hash);
          found != result.end()) {
        if (found->second != reference)
          return failure<std::unordered_map<std::string, StoredObjectReference>>(
              "object_reference_conflict",
              "one reachable object hash has conflicting metadata");
        continue;
      }
      const auto bytes = read_object(projectPath, reference);
      if (!bytes.has_value())
        return Result<std::unordered_map<std::string, StoredObjectReference>>::failure(
            bytes.diagnostic());
      result.emplace(reference.object_hash, reference);
      const auto document = Json::parse(bytes.value());
      if (reference.schema_id == "urn:prometheus:schema:run-manifest:1.0.0") {
        for (const auto member : {"package", "scenario", "request", "result"})
          pending.push(parseReference(document.at(member)));
      } else if (reference.schema_id == structural_project_run_schema_id) {
        pending.push(parseReference(document.at("archive_manifest")));
        for (const auto &artifact : document.at("artifacts"))
          for (const auto &chunk : artifact.at("chunks"))
            pending.push(parseReference(chunk));
      } else if (reference.schema_id == execution_project_snapshot_schema_id) {
        if (document.size() != 7U ||
            document.value("$schema", "") !=
                execution_project_snapshot_schema_id ||
            document.value("schema_version", "") != "1.0.0" ||
            document.value("snapshot_kind", "") !=
                "pre_execution_project" ||
            !document.contains("project_index") ||
            !document.at("project_index").is_string() ||
            !document.contains("pending_manifest_hash") ||
            !document.at("pending_manifest_hash").is_string())
          return failure<std::unordered_map<std::string, StoredObjectReference>>(
              "execution_snapshot_invalid",
              "execution project snapshot contract is invalid");
        const auto nestedBytes =
            document.at("project_index").get<std::string>();
        if (integrity::sha256_bytes(nestedBytes) !=
            document.value("project_index_sha256", ""))
          return failure<std::unordered_map<std::string, StoredObjectReference>>(
              "execution_snapshot_changed",
              "execution project snapshot identity differs");
        const auto nested = parse_project_v2(nestedBytes);
        if (!nested.has_value())
          return Result<std::unordered_map<std::string, StoredObjectReference>>::failure(
              nested.diagnostic());
        const auto pendingHash =
            document.at("pending_manifest_hash").get<std::string>();
        if (!std::ranges::any_of(
                project.execution.committed_runs, [&](const auto &candidate) {
                  return candidate.object_hash == pendingHash &&
                         candidate.schema_id !=
                             execution_project_snapshot_schema_id;
                }))
          return failure<std::unordered_map<std::string, StoredObjectReference>>(
              "execution_snapshot_orphaned",
              "execution snapshot does not bind a committed run");
        for (const auto &binding : nested.value().execution.package_bindings)
          pending.push(binding.package);
        if (nested.value().execution.current_scenario)
          pending.push(*nested.value().execution.current_scenario);
        for (const auto &nestedReference :
             nested.value().execution.committed_runs)
          pending.push(nestedReference);
      }
    }
    return Result<std::unordered_map<std::string, StoredObjectReference>>::success(
        std::move(result));
  } catch (const std::exception &error) {
    return failure<std::unordered_map<std::string, StoredObjectReference>>(
        "reachable_object_graph_invalid", error.what(), projectPath);
  }
}

} // namespace

Result<ProjectBundle> export_project_bundle(
    const std::filesystem::path &projectPath,
    const std::filesystem::path &destinationDirectory) noexcept {
  std::filesystem::path temporary;
  try {
    if (std::filesystem::exists(destinationDirectory) ||
        !std::filesystem::is_directory(destinationDirectory.parent_path()))
      return failure<ProjectBundle>(
          "bundle_destination_invalid",
          "bundle destination must not exist and its parent must exist",
          destinationDirectory);
    const auto opened = open_read_only(projectPath);
    if (!opened.has_value()) return Result<ProjectBundle>::failure(opened.diagnostic());
    auto snapshot = opened.value();
    auto sourcePath = std::filesystem::path(snapshot.cad_source);
    if (sourcePath.is_relative()) sourcePath = projectPath.parent_path() / sourcePath;
    std::error_code sourceError;
    const auto sourceStatus = std::filesystem::symlink_status(sourcePath, sourceError);
    if (sourceError || !std::filesystem::is_regular_file(sourceStatus) ||
        std::filesystem::is_symlink(sourceStatus))
      return failure<ProjectBundle>(
          "bundle_source_unavailable",
          "project CAD source must be a regular available file", sourcePath);
    if (integrity::sha256_file(sourcePath) != snapshot.assembly_artifact_hash)
      return failure<ProjectBundle>(
          "bundle_source_changed",
          "project CAD source identity differs from its saved snapshot", sourcePath);
    const auto sourceName = sourcePath.filename().string();
    if (!safeFilename(sourceName))
      return failure<ProjectBundle>(
          "bundle_source_name_unsafe", "CAD source filename is unsafe", sourcePath);
    const auto objects = reachableObjects(projectPath, snapshot);
    if (!objects.has_value())
      return Result<ProjectBundle>::failure(objects.diagnostic());

    std::mt19937_64 random{std::random_device{}()};
    temporary = destinationDirectory;
    temporary += ".partial-" + std::to_string(random());
    std::filesystem::create_directories(temporary / "sources");
    const auto bundledProject = temporary / bundleProjectName;
    const auto bundledSidecar = sidecar_path_for_project(bundledProject);
    std::filesystem::create_directories(bundledSidecar / "objects" / "sha256");
    writeFile(bundledSidecar / ".writer.lock", "");
    std::filesystem::copy_file(sourcePath, temporary / "sources" / sourceName,
                               std::filesystem::copy_options::none);
    if (integrity::sha256_file(temporary / "sources" / sourceName) !=
        snapshot.assembly_artifact_hash)
      throw std::runtime_error("bundled CAD source changed during copy");
    snapshot.cad_source = "sources/" + sourceName;
    const auto serialized = serialize_project_v2(snapshot);
    if (!serialized.has_value())
      throw std::runtime_error(serialized.diagnostic().code + ": " +
                               serialized.diagnostic().message);
    writeFile(bundledProject, serialized.value());

    std::vector<StoredObjectReference> orderedObjects;
    orderedObjects.reserve(objects.value().size());
    for (const auto &[hash, reference] : objects.value()) {
      (void)hash;
      const auto bytes = read_object(projectPath, reference);
      if (!bytes.has_value())
        throw std::runtime_error(bytes.diagnostic().code + ": " +
                                 bytes.diagnostic().message);
      const auto target = object_path_for_hash(bundledSidecar, reference.object_hash);
      if (!target.has_value())
        throw std::runtime_error(target.diagnostic().code + ": " +
                                 target.diagnostic().message);
      std::filesystem::create_directories(target.value().parent_path());
      writeFile(target.value(), bytes.value());
      orderedObjects.push_back(reference);
    }
    std::ranges::sort(orderedObjects, {}, &StoredObjectReference::object_hash);
    Json references = Json::array();
    for (const auto &reference : orderedObjects)
      references.push_back(referenceJson(reference));
    const Json manifest{
        {"$schema", bundleSchema}, {"schema_version", "1.0.0"},
        {"bundle_kind", "portable_prometheus_project"},
        {"project_file", bundleProjectName},
        {"project_sha256", integrity::sha256_bytes(serialized.value())},
        {"cad_source", "sources/" + sourceName},
        {"cad_source_sha256", snapshot.assembly_artifact_hash},
        {"objects", std::move(references)}};
    writeFile(temporary / bundleManifestName,
              integrity::canonicalize_json_bytes(manifest.dump()));
    const auto verified = verify_project_bundle(temporary);
    if (!verified.has_value())
      throw std::runtime_error(verified.diagnostic().code + ": " +
                               verified.diagnostic().message);
    std::filesystem::rename(temporary, destinationDirectory);
    return Result<ProjectBundle>::success(
        {destinationDirectory, destinationDirectory / bundleProjectName,
         destinationDirectory / bundleManifestName, orderedObjects.size()});
  } catch (const std::exception &error) {
    std::error_code ignored;
    if (!temporary.empty()) std::filesystem::remove_all(temporary, ignored);
    return failure<ProjectBundle>("bundle_export_failed", error.what(),
                                  destinationDirectory);
  }
}

Result<ProjectBundle> verify_project_bundle(
    const std::filesystem::path &bundleDirectory) noexcept {
  try {
    const auto manifestPath = bundleDirectory / bundleManifestName;
    const auto manifestBytes = integrity::verify_canonical_bytes(readFile(manifestPath));
    const auto manifest = Json::parse(manifestBytes);
    if (manifest.value("$schema", "") != bundleSchema ||
        manifest.value("schema_version", "") != "1.0.0" ||
        manifest.value("bundle_kind", "") != "portable_prometheus_project" ||
        manifest.value("project_file", "") != bundleProjectName ||
        !manifest.contains("objects") || !manifest.at("objects").is_array())
      return failure<ProjectBundle>("bundle_manifest_invalid",
                                    "portable bundle manifest is invalid",
                                    manifestPath);
    const auto projectPath = bundleDirectory / bundleProjectName;
    const auto projectBytes = readFile(projectPath);
    if (integrity::sha256_bytes(projectBytes) !=
        manifest.at("project_sha256").get<std::string>())
      return failure<ProjectBundle>("bundle_project_changed",
                                    "bundled project index identity differs",
                                    projectPath);
    const auto parsed = parse_project_v2(projectBytes);
    if (!parsed.has_value()) return Result<ProjectBundle>::failure(parsed.diagnostic());
    const auto cadRelative = manifest.at("cad_source").get<std::string>();
    if (cadRelative != parsed.value().cad_source ||
        !safeRelativePath(std::filesystem::path(cadRelative)))
      return failure<ProjectBundle>("bundle_source_path_invalid",
                                    "bundled CAD path is not safe and relative");
    const auto cadPath = bundleDirectory / std::filesystem::path(cadRelative);
    if (integrity::sha256_file(cadPath) !=
            manifest.at("cad_source_sha256").get<std::string>() ||
        manifest.at("cad_source_sha256").get<std::string>() !=
            parsed.value().assembly_artifact_hash)
      return failure<ProjectBundle>("bundle_source_changed",
                                    "bundled CAD source identity differs", cadPath);
    const auto reachable = reachableObjects(projectPath, parsed.value());
    if (!reachable.has_value())
      return Result<ProjectBundle>::failure(reachable.diagnostic());
    std::set<std::string> declared;
    for (const auto &value : manifest.at("objects")) {
      const auto reference = parseReference(value);
      if (!declared.insert(reference.object_hash).second ||
          !reachable.value().contains(reference.object_hash) ||
          reachable.value().at(reference.object_hash) != reference)
        return failure<ProjectBundle>("bundle_object_set_invalid",
                                      "bundle object set is duplicated or changed");
    }
    if (declared.size() != reachable.value().size())
      return failure<ProjectBundle>("bundle_object_set_invalid",
                                    "bundle object set is incomplete or unreachable");
    return Result<ProjectBundle>::success(
        {bundleDirectory, projectPath, manifestPath, declared.size()});
  } catch (const integrity::CanonicalJsonError &error) {
    return failure<ProjectBundle>(error.code(), error.what(), bundleDirectory);
  } catch (const std::exception &error) {
    return failure<ProjectBundle>("bundle_verification_failed", error.what(),
                                  bundleDirectory);
  }
}

} // namespace prometheus::run_store
