#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/object_store.hpp>
#include <prometheus/run_store/run_store.hpp>
#include <prometheus/run_store/structural_archive_store.hpp>
#include <prometheus/run_store/project_bundle.hpp>
#include <prometheus/run_store/project_evidence_archive.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
namespace integrity = prometheus::integrity;
namespace run_store = prometheus::run_store;
using namespace std::chrono_literals;

const fs::path repository_root{PROMETHEUS_REPOSITORY_ROOT};
const fs::path contention_helper{PROMETHEUS_RUN_STORE_CONTENTION_HELPER};

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string read_file(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  require(static_cast<bool>(stream), "open fixture: " + path.string());
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void write_file(const fs::path &path, const std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(stream), "open test file: " + path.string());
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(static_cast<bool>(stream), "write test file: " + path.string());
}

class TemporaryRoot final {
public:
  TemporaryRoot() {
    static std::atomic<std::uint64_t> counter{0U};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() /
            ("prometheus-run-transaction-" + std::to_string(stamp) + "-" +
             std::to_string(counter.fetch_add(1U)));
    require(fs::create_directory(path_), "create transaction test root");
  }

  TemporaryRoot(const TemporaryRoot &) = delete;
  TemporaryRoot &operator=(const TemporaryRoot &) = delete;

  ~TemporaryRoot() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }

  [[nodiscard]] const fs::path &path() const noexcept { return path_; }

private:
  fs::path path_;
};

run_store::StoredObjectReference
reference_for(const std::string_view bytes, std::string media_type,
              std::string schema_id, std::string schema_version) {
  return {integrity::object_hash(bytes),
          static_cast<std::uint64_t>(bytes.size()), std::move(media_type),
          std::move(schema_id), std::move(schema_version)};
}

run_store::ObjectToStore object_fixture(const std::string_view relative,
                                        std::string media_type,
                                        std::string schema_id,
                                        std::string schema_version) {
  auto bytes = read_file(repository_root / relative);
  auto reference = reference_for(bytes, std::move(media_type),
                                 std::move(schema_id),
                                 std::move(schema_version));
  return {std::move(reference), std::move(bytes)};
}

run_store::CompletedRunObjects motor_a_run() {
  return {
      object_fixture(
          "fixtures/contracts/execution-component-v2.motor-a.jcs",
          "application/vnd.prometheus.execution-component+json;version=2.0.0",
          "urn:prometheus:schema:execution-component:2.0.0", "2.0.0"),
      object_fixture(
          "fixtures/contracts/program-01b/motor-arm-scenario-v1.acceptance.jcs",
          "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0",
          "urn:prometheus:schema:motor-arm-scenario:1.0.0", "1.0.0"),
      object_fixture(
          "fixtures/contracts/program-01b/analysis-request-v1.motor-a.jcs",
          "application/vnd.prometheus.analysis-request+json;version=1.0.0",
          "urn:prometheus:schema:analysis-request:1.0.0", "1.0.0"),
      object_fixture(
          "fixtures/contracts/program-01b/analysis-result-v1.motor-a.jcs",
          "application/vnd.prometheus.analysis-result+json;version=1.0.0",
          "urn:prometheus:schema:analysis-result:1.0.0", "1.0.0"),
      object_fixture(
          "fixtures/contracts/program-01b/run-manifest-v1.motor-a.jcs",
          "application/vnd.prometheus.run-manifest+json;version=1.0.0",
          "urn:prometheus:schema:run-manifest:1.0.0", "1.0.0"),
  };
}

run_store::ObjectToStore structural_manifest_fixture() {
  const auto artifact = [](const std::string &file, const char hashCharacter) {
    return std::string{"{\"byte_length\":1,\"file\":\""} + file +
           "\",\"sha256\":\"sha256:" + std::string(64U, hashCharacter) + "\"}";
  };
  const auto document =
      std::string{"{\"$schema\":\"urn:prometheus:schema:structural-run-archive:1.0.0\","}
      + "\"analysis_id\":\"analysis\",\"archive_kind\":\"completed_linear_static_run\","
      + "\"artifacts\":{\"dat\":" + artifact("run.dat", '1') +
      ",\"deck\":" + artifact("run.inp", '2') +
      ",\"frd\":" + artifact("run.frd", '3') +
      ",\"setup\":" + artifact("setup.json", '4') +
      ",\"sta\":" + artifact("run.sta", '5') +
      ",\"stderr\":" + artifact("stderr.txt", '6') +
      ",\"stdout\":" + artifact("stdout.txt", '7') + "},"
      + "\"component_name\":\"component\",\"coverage\":{},\"execution\":{},"
      + "\"findings\":[],\"geometry_sha256\":\"sha256:" + std::string(64U, '8') +
      "\",\"job_name\":\"run\",\"limitation\":\"bounded\",\"metrics\":{},"
      + "\"requirements\":{},\"schema_version\":\"1.0.0\","
      + "\"solver_identity\":\"fixture\"}";
  auto bytes = integrity::canonicalize_json_bytes(document);
  return {reference_for(bytes,
                        std::string(run_store::structural_manifest_media_type),
                        std::string(run_store::structural_manifest_schema_id),
                        "1.0.0"),
          std::move(bytes)};
}

run_store::ObjectToStore inventory_snapshot_fixture(
    const std::string &cadHash, const std::uint64_t cadLength,
    const std::string &documentHash, const std::uint64_t documentLength,
    const std::string &toolHash, const std::uint64_t toolLength) {
  nlohmann::json artifacts = nlohmann::json::array({
      {{"relative_path", "docs/spec.pdf"}, {"byte_length", documentLength},
       {"sha256", documentHash}, {"category", "document"},
       {"analysis_state", "not_evaluated"}, {"detail", "Not interpreted"}},
      {{"relative_path", "portable-bracket.step"}, {"byte_length", cadLength},
       {"sha256", cadHash}, {"category", "geometry"},
       {"analysis_state", "ready"}, {"detail", "STEP import ready"}},
      {{"relative_path", "tools/check.exe"}, {"byte_length", toolLength},
       {"sha256", toolHash}, {"category", "other"},
       {"analysis_state", "unsupported"}, {"detail", "Not executable"}}});
  const auto bytes = integrity::canonicalize_json_bytes(
      nlohmann::json{{"$schema", run_store::project_inventory_schema_id},
                     {"artifacts", std::move(artifacts)},
                     {"root_label", "portable-source"},
                     {"schema_version", "1.0.0"},
                     {"snapshot_kind", "accounted_project_folder"}}
          .dump());
  return {reference_for(bytes,
                        std::string(run_store::project_inventory_media_type),
                        std::string(run_store::project_inventory_schema_id),
                        "1.0.0"),
          bytes};
}

fs::path create_structural_archive_fixture(const fs::path &root) {
  const auto archive = root / "structural-archive";
  require(fs::create_directory(archive), "create structural archive fixture");
  const std::array<std::pair<std::string, std::string>, 7> smallFiles{{
      {"setup.json", "setup\n"}, {"run.inp", "deck\n"},
      {"run.dat", ""}, {"run.frd", "frd\n"}, {"run.sta", "sta\n"},
      {"stdout.txt", "stdout\n"}, {"stderr.txt", "stderr\n"}}};
  for (const auto &[name, bytes] : smallFiles) write_file(archive / name, bytes);
  std::string large(run_store::structural_artifact_chunk_bytes + 17U, 'D');
  write_file(archive / "run.dat", large);
  const auto artifact = [&](const std::string &name) {
    const auto bytes = read_file(archive / name);
    return std::string{"{\"byte_length\":"} + std::to_string(bytes.size()) +
           ",\"file\":\"" + name + "\",\"sha256\":\"" +
           integrity::sha256_bytes(bytes) + "\"}";
  };
  const auto document =
      std::string{"{\"$schema\":\"urn:prometheus:schema:structural-run-archive:1.0.0\","}
      + "\"analysis_id\":\"embedded-analysis\",\"archive_kind\":\"completed_linear_static_run\","
      + "\"artifacts\":{\"dat\":" + artifact("run.dat") +
      ",\"deck\":" + artifact("run.inp") +
      ",\"frd\":" + artifact("run.frd") +
      ",\"setup\":" + artifact("setup.json") +
      ",\"sta\":" + artifact("run.sta") +
      ",\"stderr\":" + artifact("stderr.txt") +
      ",\"stdout\":" + artifact("stdout.txt") + "},"
      + "\"component_name\":\"component\",\"coverage\":{},\"execution\":{},"
      + "\"findings\":[],\"geometry_sha256\":\"sha256:" + std::string(64U, '9') +
      "\",\"job_name\":\"run\",\"limitation\":\"bounded\",\"metrics\":{},"
      + "\"requirements\":{},\"schema_version\":\"1.0.0\","
      + "\"solver_identity\":\"fixture\"}";
  write_file(archive / "prometheus-structural-run.json",
             integrity::canonicalize_json_bytes(document));
  return archive / "prometheus-structural-run.json";
}

run_store::ObjectToStore motor_b_package() {
  return object_fixture(
      "fixtures/contracts/execution-component-v2.motor-b.jcs",
      "application/vnd.prometheus.execution-component+json;version=2.0.0",
      "urn:prometheus:schema:execution-component:2.0.0", "2.0.0");
}

run_store::ProjectV2 initial_project() {
  return run_store::ProjectV2{
      "Motor arm",
      "motor-arm.step",
      "sha256:377302b669b12b89e2c020dc4c29e1c63c4920587920eb8f02ff54ca73bf977d",
      "right-handed Z-up",
      "m",
      {},
      {},
      {},
      {},
      run_store::EngineeringState{std::nullopt, {}, "not_evaluated"},
      std::nullopt,
      run_store::ExecutionIndex{{}, std::nullopt, {}, {}},
  };
}

template <typename T>
T require_success(run_store::Result<T> result, const std::string &context) {
  require(result.has_value(),
          context + ": " +
              (result.has_value() ? std::string{}
                                  : result.diagnostic().stage + "/" +
                                        result.diagnostic().code + " " +
                                        result.diagnostic().message));
  return std::move(result.value());
}

void require_failure(const auto &result, const std::string_view code,
                     const std::string &context) {
  require(!result.has_value(), context + ": expected failure");
  require(result.diagnostic().stage == "store", context + ": failure stage");
  require(result.diagnostic().code == code,
          context + ": expected " + std::string(code) + ", received " +
              result.diagnostic().code);
  require(!result.diagnostic().message.empty(),
          context + ": bounded diagnostic message");
}

run_store::TransactionOptions
fail_at(const run_store::TransactionBoundary target) {
  auto fired = std::make_shared<bool>(false);
  run_store::TransactionOptions options;
  options.boundary_hook = [target, fired](const auto boundary) {
    if (!*fired && boundary == target) {
      *fired = true;
      return true;
    }
    return false;
  };
  return options;
}

const std::vector<run_store::TransactionBoundary> object_boundaries{
    run_store::TransactionBoundary::before_object_temporary_create,
    run_store::TransactionBoundary::after_object_temporary_create,
    run_store::TransactionBoundary::before_object_write,
    run_store::TransactionBoundary::after_object_write,
    run_store::TransactionBoundary::before_object_flush,
    run_store::TransactionBoundary::after_object_flush,
    run_store::TransactionBoundary::before_object_verification,
    run_store::TransactionBoundary::after_object_verification,
    run_store::TransactionBoundary::before_object_rename,
    run_store::TransactionBoundary::after_object_rename,
};

const std::vector<run_store::TransactionBoundary> project_boundaries{
    run_store::TransactionBoundary::before_project_temporary_create,
    run_store::TransactionBoundary::after_project_temporary_create,
    run_store::TransactionBoundary::before_project_write,
    run_store::TransactionBoundary::after_project_write,
    run_store::TransactionBoundary::before_project_flush,
    run_store::TransactionBoundary::after_project_flush,
    run_store::TransactionBoundary::before_project_verification,
    run_store::TransactionBoundary::after_project_verification,
    run_store::TransactionBoundary::before_project_replacement,
    run_store::TransactionBoundary::after_project_replacement,
};

std::vector<run_store::TransactionBoundary> all_boundaries() {
  auto result = object_boundaries;
  result.insert(result.end(), project_boundaries.begin(),
                project_boundaries.end());
  return result;
}

void require_no_temporary_files(const fs::path &root,
                                const std::string &context) {
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    require(entry.path().filename().string().find(".tmp.") == std::string::npos,
            context + ": temporary file was not cleaned: " +
                entry.path().string());
  }
}

void create_project(const fs::path &project) {
  require_success(run_store::create_project_v2(project, initial_project()),
                  "create project v2");
}

void prepare_for_publication(const fs::path &project,
                             const run_store::CompletedRunObjects &run) {
  create_project(project);
  require_success(run_store::install_package_binding(
                      project, "motor", run.package.reference,
                      run.package.bytes),
                  "install package binding");
  require_success(run_store::set_current_scenario(
                      project, run.scenario.reference, run.scenario.bytes),
                  "set current scenario");
}

void require_complete_run_objects(const fs::path &project,
                                  const run_store::CompletedRunObjects &run) {
  for (const auto *object : {&run.package, &run.scenario, &run.request,
                             &run.result, &run.manifest}) {
    const auto bytes = run_store::read_object(project, object->reference);
    require(bytes.has_value() && bytes.value() == object->bytes,
            "committed reference resolves to exact immutable bytes");
  }
}

void test_normal_operations_and_idempotency() {
  TemporaryRoot root;
  const auto project_path = root.path() / "arm.prometheus";
  const auto run = motor_a_run();

  const auto &created = require_success(
      run_store::create_project_v2(project_path, initial_project()),
      "normal create");
  require(created.execution.package_bindings.empty(),
          "new project has no package binding");
  require(fs::is_regular_file(project_path), "project is a regular file");
  require(fs::is_regular_file(run_store::sidecar_path_for_project(project_path) /
                              ".writer.lock"),
          "persistent writer lock is a regular file");

  const auto &bound = require_success(
      run_store::install_package_binding(project_path, "motor",
                                         run.package.reference,
                                         run.package.bytes),
      "normal package binding");
  require(bound.execution.package_bindings.size() == 1U &&
              bound.execution.package_bindings.front().binding_revision == 1U,
          "first binding appends revision one");

  const auto &scenario = require_success(
      run_store::set_current_scenario(project_path, run.scenario.reference,
                                      run.scenario.bytes),
      "normal scenario update");
  require(scenario.execution.current_scenario == run.scenario.reference,
          "scenario update stores a five-field reference");

  const auto &published = require_success(
      run_store::publish_completed_run(project_path, run), "normal publish");
  require(!published.already_committed &&
              published.project.execution.committed_runs.size() == 2U &&
              published.project.execution.committed_runs.front() ==
                  run.manifest.reference &&
              published.project.execution.committed_runs.back().schema_id ==
                  run_store::execution_project_snapshot_schema_id,
          "first publication commits its manifest and pre-execution snapshot");
  require_complete_run_objects(project_path, run);
  const auto snapshotReference =
      published.project.execution.committed_runs.back();
  const auto snapshotBytes = require_success(
      run_store::read_object(project_path, snapshotReference),
      "read motor execution project snapshot");
  const auto snapshot = nlohmann::json::parse(snapshotBytes);
  require(snapshot.at("execution_kind") == "motor_analysis" &&
              snapshot.at("pending_manifest_hash") ==
                  run.manifest.reference.object_hash &&
              integrity::sha256_bytes(
                  snapshot.at("project_index").get<std::string>()) ==
                  snapshot.at("project_index_sha256").get<std::string>(),
          "execution snapshot closes over its intended run and exact project bytes");
  const auto preExecution = run_store::parse_project_v2(
      snapshot.at("project_index").get<std::string>());
  require(preExecution.has_value() &&
              preExecution.value().execution.committed_runs.empty() &&
              preExecution.value().execution.package_bindings.size() == 1U &&
              preExecution.value().execution.current_scenario ==
                  run.scenario.reference,
          "execution snapshot is the complete project state before publication");

  const auto &repeated = require_success(
      run_store::publish_completed_run(project_path, run),
      "idempotent publish");
  require(repeated.already_committed &&
              repeated.project.execution.committed_runs.size() == 2U,
          "idempotent publish does not duplicate a manifest");
  require(repeated.project.execution.events.size() == 3U &&
              repeated.project.execution.events.back().event_kind ==
                  "run_invoked" &&
              repeated.project.execution.events.back().status ==
                  "already_committed",
          "idempotent invocation adds only display metadata");

  const auto reopened = run_store::open_read_only(project_path);
  require(reopened.has_value() &&
              reopened.value().execution.committed_runs.size() == 2U,
          "read-only reopen retains the committed run");

  const auto package_b = motor_b_package();
  const auto &rebound = require_success(
      run_store::install_package_binding(project_path, "motor",
                                         package_b.reference, package_b.bytes),
      "superseding package binding");
  require(rebound.execution.package_bindings.size() == 2U &&
              rebound.execution.package_bindings.back()
                      .supersedes_binding_revision == 1U &&
              rebound.execution.committed_runs.size() == 2U,
          "new package supersedes the active binding without altering history");
  const auto historical_retry = require_success(
      run_store::publish_completed_run(project_path, run),
      "historical idempotent publish after package switch");
  require(historical_retry.already_committed &&
              historical_retry.project.execution.committed_runs.size() == 2U,
          "already-committed manifest remains idempotent after binding changes");
}

void test_structural_manifest_anchor() {
  TemporaryRoot root;
  const auto projectPath = root.path() / "structural.prometheus";
  require_success(run_store::create_project_v2(projectPath, initial_project()),
                  "create structural project");
  const auto manifest = structural_manifest_fixture();
  const auto &anchored = require_success(
      run_store::commit_structural_archive_manifest(projectPath, manifest),
      "anchor structural manifest");
  require(!anchored.already_committed &&
              anchored.project.execution.committed_runs.size() == 1U &&
              anchored.project.execution.committed_runs.front() ==
                  manifest.reference &&
              anchored.project.execution.events.back().event_kind ==
                  "structural_run_anchored",
          "structural manifest gets a distinct immutable project history entry");
  const auto retained = run_store::read_object(projectPath, manifest.reference);
  require(retained.has_value() && retained.value() == manifest.bytes,
          "anchored structural manifest resolves to exact canonical bytes");
  const auto &repeated = require_success(
      run_store::commit_structural_archive_manifest(projectPath, manifest),
      "repeat structural manifest anchor");
  require(repeated.already_committed &&
              repeated.project.execution.committed_runs.size() == 1U &&
              repeated.project.execution.events.back().event_kind ==
                  "structural_run_invoked",
          "structural manifest anchoring is idempotent");
  const auto reopened = run_store::open_read_only(projectPath);
  require(reopened.has_value() &&
              reopened.value().execution.committed_runs.front() ==
                  manifest.reference,
          "structural manifest reference survives close and reopen");

  auto invalid = manifest;
  invalid.bytes.replace(invalid.bytes.find("run.dat"), 7U, "../xdat");
  invalid.reference = reference_for(
      invalid.bytes, std::string(run_store::structural_manifest_media_type),
      std::string(run_store::structural_manifest_schema_id), "1.0.0");
  const auto rejected =
      run_store::commit_structural_archive_manifest(projectPath, invalid);
  require_failure(rejected, "structural_manifest_contract_invalid",
                  "unsafe structural artifact identity");
  require(run_store::open_read_only(projectPath)
              .value()
              .execution.committed_runs.size() == 1U,
          "rejected structural manifest does not alter project history");
}

void test_embedded_structural_archive_round_trip() {
  TemporaryRoot root;
  const auto sourceManifest = create_structural_archive_fixture(root.path());
  auto packed = run_store::build_structural_archive_objects(
      sourceManifest, initial_project().assembly_artifact_hash);
  if (!packed.has_value())
    throw std::runtime_error("pack structural archive: " +
                             packed.diagnostic().code + ": " +
                             packed.diagnostic().message);
  require(packed.value().chunks.size() == 8U,
          "structural archive packs all artifacts and splits a large DAT file");
  require_failure(run_store::build_structural_archive_objects(
                      sourceManifest, "sha256:invalid"),
                  "assembly_artifact_hash_invalid",
                  "structural archive without exact assembly identity");
  const auto projectPath = root.path() / "embedded.prometheus";
  require_success(run_store::create_project_v2(projectPath, initial_project()),
                  "create embedded structural project");
  const auto &published = require_success(
      run_store::publish_structural_archive(projectPath, packed.value()),
      "publish embedded structural archive");
  require(!published.already_committed &&
              published.project.execution.committed_runs.size() == 2U &&
              published.project.execution.committed_runs.front() ==
                  packed.value().project_manifest.reference &&
              published.project.execution.committed_runs.back().schema_id ==
                  run_store::execution_project_snapshot_schema_id &&
              published.project.execution.events.back().event_kind ==
                  "structural_run_published",
          "embedded structural graph commits its manifest and pre-execution snapshot");
  for (const auto *object : {&packed.value().archive_manifest,
                             &packed.value().project_manifest}) {
    const auto retained = run_store::read_object(projectPath, object->reference);
    require(retained.has_value() && retained.value() == object->bytes,
            "embedded structural manifest object resolves exactly");
  }
  for (const auto &chunk : packed.value().chunks) {
    const auto retained = run_store::read_object(projectPath, chunk.reference);
    require(retained.has_value() && retained.value() == chunk.bytes,
            "embedded structural chunk resolves exactly");
  }
  const auto destination = root.path() / "reconstructed";
  const auto reconstructed = run_store::reconstruct_structural_archive(
      projectPath, packed.value().project_manifest.reference, destination);
  require(reconstructed.has_value(), "embedded structural archive reconstructs");
  for (const auto &entry : fs::directory_iterator(sourceManifest.parent_path())) {
    require(read_file(entry.path()) ==
                read_file(destination / entry.path().filename()),
            "reconstructed structural file is byte-identical");
  }
  const auto &repeated = require_success(
      run_store::publish_structural_archive(projectPath, packed.value()),
      "repeat embedded structural publication");
  require(repeated.already_committed &&
              repeated.project.execution.committed_runs.size() == 2U,
          "embedded structural publication is idempotent");

  auto forged = packed.value();
  const auto oldChunkHash = forged.chunks.front().reference.object_hash;
  const auto data = forged.chunks.front().bytes.find("\"data\":\"");
  require(data != std::string::npos, "structural chunk contains encoded data");
  auto &encodedByte = forged.chunks.front().bytes[data + 8U];
  encodedByte = encodedByte == 'A' ? 'B' : 'A';
  forged.chunks.front().reference.object_hash =
      integrity::object_hash(forged.chunks.front().bytes);
  const auto hashPosition =
      forged.project_manifest.bytes.find(oldChunkHash);
  require(hashPosition != std::string::npos,
          "project manifest references first structural chunk");
  forged.project_manifest.bytes.replace(
      hashPosition, oldChunkHash.size(),
      forged.chunks.front().reference.object_hash);
  forged.project_manifest.reference.object_hash =
      integrity::object_hash(forged.project_manifest.bytes);
  const auto forgedRejected =
      run_store::publish_structural_archive(projectPath, forged);
  require_failure(forgedRejected, "structural_chunk_identity_mismatch",
                  "forged structural chunk payload");

  auto changed = packed.value();
  changed.chunks.pop_back();
  const auto rejected =
      run_store::publish_structural_archive(projectPath, changed);
  require_failure(rejected, "structural_chunk_reference_mismatch",
                  "missing structural chunk graph");
  require(run_store::open_read_only(projectPath)
              .value()
              .execution.committed_runs.size() == 2U,
          "rejected embedded graph does not alter committed history");

  const auto cadPath = root.path() / "portable-bracket.step";
  write_file(cadPath, "portable CAD source bytes\n");
  const auto documentPath = root.path() / "docs" / "spec.pdf";
  const auto toolPath = root.path() / "tools" / "check.exe";
  fs::create_directories(documentPath.parent_path());
  fs::create_directories(toolPath.parent_path());
  write_file(documentPath, "portable document evidence\n");
  write_file(toolPath, "inert untrusted tool bytes\n");
  const auto cadHash = integrity::sha256_file(cadPath);
  auto portableProject = initial_project();
  portableProject.cad_source = cadPath.string();
  portableProject.assembly_artifact_hash = cadHash;
  const auto portableProjectPath = root.path() / "portable-source.prometheus";
  require_success(run_store::create_project_v2(portableProjectPath,
                                                portableProject),
                  "create portable structural project");
  auto portableObjects = run_store::build_structural_archive_objects(
      sourceManifest, cadHash);
  require(portableObjects.has_value(), "pack portable structural archive");
  require_success(run_store::publish_structural_archive(
                      portableProjectPath, portableObjects.value()),
                  "publish portable structural archive");
  const auto inventory = inventory_snapshot_fixture(
      cadHash, fs::file_size(cadPath), integrity::sha256_file(documentPath),
      fs::file_size(documentPath), integrity::sha256_file(toolPath),
      fs::file_size(toolPath));
  const std::vector<run_store::ProjectEvidenceInput> evidenceInputs{
      {"docs/spec.pdf", documentPath, fs::file_size(documentPath),
       integrity::sha256_file(documentPath), "document", "not_evaluated"},
      {"portable-bracket.step", cadPath, fs::file_size(cadPath), cadHash,
       "geometry", "ready"},
      {"tools/check.exe", toolPath, fs::file_size(toolPath),
       integrity::sha256_file(toolPath), "other", "unsupported"}};
  const auto evidence = run_store::build_project_evidence_archive(
      inventory.reference, evidenceInputs);
  require(evidence.has_value() && evidence.value().chunks.size() == 2U,
          "bounded evidence retains a document and quarantines unknown bytes");
  const auto evidenceManifest =
      nlohmann::json::parse(evidence.value().manifest.bytes);
  require(evidenceManifest.at("files").at(0).at("disposition") == "retained" &&
              evidenceManifest.at("files").at(1).at("disposition") ==
                  "external_only" &&
              evidenceManifest.at("files").at(2).at("disposition") ==
                  "quarantined",
          "evidence policy retains documents, separates CAD, and quarantines unknown content");
  auto forgedEvidence = evidence.value();
  forgedEvidence.chunks.front().bytes.push_back(' ');
  require_failure(run_store::validate_project_evidence_archive(
                  inventory, forgedEvidence),
                  "project_evidence_chunk_invalid",
                  "changed evidence chunk bytes");
  const auto interruptedEvidenceProject =
      root.path() / "interrupted-evidence.prometheus";
  require_success(run_store::create_project_v2(interruptedEvidenceProject,
                                                portableProject),
                  "create interrupted evidence project");
  const auto interruptedEvidence = run_store::publish_project_inventory_archive(
      interruptedEvidenceProject, inventory, evidence.value(),
      fail_at(run_store::TransactionBoundary::before_project_replacement));
  require_failure(interruptedEvidence, "injected_failure",
                  "evidence publication interruption");
  require(run_store::open_read_only(interruptedEvidenceProject)
              .value().execution.committed_runs.empty(),
          "interrupted evidence publication cites no partial archive");
  const auto evidenceRetry = run_store::publish_project_inventory_archive(
      interruptedEvidenceProject, inventory, evidence.value());
  require(evidenceRetry.has_value() &&
              evidenceRetry.value().project.execution.committed_runs.size() ==
                  2U,
          "evidence publication recovers from retained immutable objects");
  require_success(run_store::publish_project_inventory_archive(
                      portableProjectPath, inventory, evidence.value()),
                  "publish portable project evidence archive");
  const auto bundleDirectory = root.path() / "portable-bundle";
  const auto bundle = run_store::export_project_bundle(
      portableProjectPath, bundleDirectory);
  require(bundle.has_value(),
          "portable bundle exports: " +
              (bundle.has_value() ? std::string{} :
                                    bundle.diagnostic().code + " " +
                                        bundle.diagnostic().message));
  require(bundle.value().object_count ==
              portableObjects.value().chunks.size() + 7U,
          "portable bundle contains exactly the reachable structural object graph");
  const auto movedBundle = root.path() / "moved-clean-machine-bundle";
  fs::rename(bundleDirectory, movedBundle);
  const auto moved = run_store::verify_project_bundle(movedBundle);
  require(moved.has_value(), "whole project bundle verifies after directory relocation");
  const auto restoredBundlePath = root.path() / "restored-portable-bundle";
  const auto restoredBundle = run_store::restore_project_bundle(
      movedBundle, restoredBundlePath);
  require(restoredBundle.has_value() &&
              restoredBundle.value().object_count == moved.value().object_count &&
              run_store::open_read_only(restoredBundle.value().project_path)
                  .has_value(),
          "verified portable backup restores atomically into a new usable project");
  write_file(restoredBundlePath / "undeclared-tool.exe", "unexpected bytes");
  require_failure(run_store::verify_project_bundle(restoredBundlePath),
                  "bundle_file_set_invalid",
                  "portable bundle with undeclared filesystem payload");
  const auto rejectedRestorePath = root.path() / "rejected-bundle-restore";
  require_failure(run_store::restore_project_bundle(
                      restoredBundlePath, rejectedRestorePath),
                  "bundle_file_set_invalid", "restore from changed bundle");
  require(!fs::exists(rejectedRestorePath),
          "rejected restore publishes no destination");
  const auto movedProject = run_store::open_read_only(moved.value().project_path);
  require(movedProject.has_value() &&
              movedProject.value().cad_source == "sources/portable-bracket.step" &&
              movedProject.value().execution.committed_runs.size() == 4U,
          "relocated bundle opens with relative CAD and structural history");
  require(run_store::read_object(moved.value().project_path, inventory.reference)
              .value() == inventory.bytes,
          "relocated bundle retains exact project inventory snapshot");
  const auto movedEvidence = std::ranges::find_if(
      movedProject.value().execution.committed_runs, [](const auto &reference) {
        return reference.schema_id ==
               run_store::project_evidence_archive_schema_id;
      });
  require(movedEvidence != movedProject.value().execution.committed_runs.end(),
          "relocated project retains its evidence archive root");
  const auto reconstructedEvidence =
      run_store::reconstruct_project_evidence_archive(
          moved.value().project_path, *movedEvidence,
          root.path() / "reconstructed-project-evidence");
  require(reconstructedEvidence.has_value() &&
              read_file(reconstructedEvidence.value() / "retained" / "docs" /
                        "spec.pdf") == read_file(documentPath) &&
              read_file(reconstructedEvidence.value() / "quarantine" / "tools" /
                        "check.exe.prometheus-quarantined") ==
                  read_file(toolPath) &&
              !fs::exists(reconstructedEvidence.value() / "retained" /
                          "portable-bracket.step"),
          "relocated evidence reconstructs retained bytes, neutralizes quarantine names, and leaves CAD separate");
  const auto bundleRestored = run_store::reconstruct_structural_archive(
      moved.value().project_path,
      movedProject.value().execution.committed_runs.front(),
      root.path() / "bundle-restored-run");
  require(bundleRestored.has_value(),
          "relocated bundle reconstructs its embedded structural archive");
  for (const auto &entry : fs::directory_iterator(sourceManifest.parent_path())) {
    require(read_file(entry.path()) ==
                read_file(bundleRestored.value().parent_path() /
                          entry.path().filename()),
            "relocated bundle retains exact structural artifact bytes");
  }
  write_file(cadPath, "changed CAD source bytes\n");
  const auto changedSourceBundle = run_store::export_project_bundle(
      portableProjectPath, root.path() / "changed-source-bundle");
  require_failure(changedSourceBundle, "bundle_source_changed",
                  "portable export with changed CAD source");
  require(!fs::exists(root.path() / "changed-source-bundle"),
          "rejected portable export publishes no destination");

  write_file(sourceManifest.parent_path() / "run.frd", "changed\n");
  const auto corrupt = run_store::build_structural_archive_objects(
      sourceManifest, initial_project().assembly_artifact_hash);
  require_failure(corrupt, "artifact_identity_mismatch",
                  "changed source artifact cannot be packed");

  const auto interruptedPath = root.path() / "interrupted-structural.prometheus";
  require_success(run_store::create_project_v2(interruptedPath, initial_project()),
                  "create interrupted structural project");
  const auto interrupted = run_store::publish_structural_archive(
      interruptedPath, packed.value(),
      fail_at(run_store::TransactionBoundary::before_project_replacement));
  require_failure(interrupted, "injected_failure",
                  "structural publication interruption");
  require(run_store::open_read_only(interruptedPath)
              .value()
              .execution.committed_runs.empty(),
          "interrupted structural publication leaves last valid project index");
  const auto retry = run_store::publish_structural_archive(
      interruptedPath, packed.value());
  require(retry.has_value() &&
              retry.value().project.execution.committed_runs.size() == 2U,
          "structural publication recovers by retrying retained immutable objects");

  const auto changedAssemblyPath = root.path() / "changed-assembly.prometheus";
  auto changedAssemblyProject = initial_project();
  changedAssemblyProject.assembly_artifact_hash =
      "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
  require_success(run_store::create_project_v2(changedAssemblyPath,
                                                changedAssemblyProject),
                  "create changed-assembly structural project");
  const auto stalePublication = run_store::publish_structural_archive(
      changedAssemblyPath, packed.value());
  require_failure(stalePublication, "assembly_artifact_mismatch",
                  "structural run built against another assembly");
  require(run_store::open_read_only(changedAssemblyPath)
              .value().execution.committed_runs.empty(),
          "assembly mismatch cannot enter structural project history");
}

void test_create_failure_boundaries() {
  for (const auto boundary : project_boundaries) {
    TemporaryRoot root;
    const auto project_path = root.path() / "arm.prometheus";
    const auto created = run_store::create_project_v2(
        project_path, initial_project(), fail_at(boundary));
    require_failure(created, "injected_failure", "create failure boundary");
    if (fs::exists(project_path)) {
      const auto reopened = run_store::open_read_only(project_path);
      require(reopened.has_value() &&
                  reopened.value().execution.committed_runs.empty(),
              "failed create leaves a complete new index if replacement occurred");
    }
    require_no_temporary_files(root.path(), "create failure cleanup");
  }

  TemporaryRoot rejected_root;
  const auto rejected_path = rejected_root.path() / "dangling.prometheus";
  auto dangling = initial_project();
  dangling.execution.committed_runs.push_back(
      motor_a_run().manifest.reference);
  const auto rejected = run_store::create_project_v2(rejected_path, dangling);
  require_failure(rejected, "initial_execution_references_not_empty",
                  "new project with dangling execution reference");
  require(!fs::exists(rejected_path) &&
              !fs::exists(run_store::sidecar_path_for_project(rejected_path)),
          "dangling initial execution reference causes no filesystem mutation");
}

void test_binding_failure_boundaries() {
  const auto run = motor_a_run();
  for (const auto boundary : all_boundaries()) {
    TemporaryRoot root;
    const auto project_path = root.path() / "arm.prometheus";
    create_project(project_path);
    const auto updated = run_store::install_package_binding(
        project_path, "motor", run.package.reference, run.package.bytes,
        fail_at(boundary));
    require_failure(updated, "injected_failure", "binding failure boundary");
    const auto reopened = require_success(
        run_store::open_read_only(project_path), "reopen failed binding");
    require(reopened.execution.package_bindings.size() <= 1U,
            "failed binding leaves old or complete new index");
    if (!reopened.execution.package_bindings.empty()) {
      require(reopened.execution.package_bindings.front().package ==
                  run.package.reference,
              "new binding references the exact installed package");
      require(run_store::read_object(project_path, run.package.reference)
                  .has_value(),
              "new binding never references a partial object");
    }
    require_no_temporary_files(root.path(), "binding failure cleanup");
  }
}

void test_scenario_failure_is_atomic() {
  const auto run = motor_a_run();
  for (const auto boundary : all_boundaries()) {
    TemporaryRoot root;
    const auto project_path = root.path() / "arm.prometheus";
    create_project(project_path);
    const auto updated = run_store::set_current_scenario(
        project_path, run.scenario.reference, run.scenario.bytes,
        fail_at(boundary));
    require_failure(updated, "injected_failure", "scenario failure boundary");
    const auto reopened = require_success(
        run_store::open_read_only(project_path), "reopen failed scenario");
    if (reopened.execution.current_scenario.has_value()) {
      require(*reopened.execution.current_scenario == run.scenario.reference &&
                  run_store::read_object(project_path, run.scenario.reference)
                      .has_value(),
              "scenario reference is absent or fully installed");
    }
    require_no_temporary_files(root.path(), "scenario failure cleanup");
  }
}

void test_publication_failure_is_atomic() {
  const auto run = motor_a_run();
  for (const auto boundary : all_boundaries()) {
    TemporaryRoot root;
    const auto project_path = root.path() / "arm.prometheus";
    prepare_for_publication(project_path, run);
    const auto published = run_store::publish_completed_run(
        project_path, run, fail_at(boundary));
    require_failure(published, "injected_failure",
                    "publication failure boundary");
    const auto reopened = require_success(
        run_store::open_read_only(project_path), "reopen failed publication");
    require(reopened.execution.committed_runs.empty() ||
                reopened.execution.committed_runs.size() == 2U,
            "failed publication leaves old or complete new run history");
    if (!reopened.execution.committed_runs.empty()) {
      require(reopened.execution.committed_runs.front() ==
                  run.manifest.reference,
              "new publication commits the expected manifest");
      require_complete_run_objects(project_path, run);
    }
    require_no_temporary_files(root.path(), "publication failure cleanup");
  }
}

void test_publication_object_order_is_explicit() {
  TemporaryRoot root;
  const auto project_path = root.path() / "arm.prometheus";
  const auto run = motor_a_run();
  prepare_for_publication(project_path, run);
  const auto published = run_store::publish_completed_run(
      project_path, run,
      fail_at(run_store::TransactionBoundary::after_object_rename));
  require_failure(published, "injected_failure",
                  "ordered object publication failure");
  require(run_store::read_object(project_path, run.request.reference).has_value(),
          "request is the first newly installed publication object");
  require(!run_store::read_object(project_path, run.result.reference).has_value(),
          "result is not installed after request-boundary failure");
  require(!run_store::read_object(project_path, run.manifest.reference)
               .has_value(),
          "manifest remains last and is not installed early");
  require(require_success(run_store::open_read_only(project_path),
                          "reopen ordered publication failure")
              .execution.committed_runs.empty(),
          "an orphan request is never cited by project history");
}

void test_cancelled_and_invalid_publications_do_not_commit() {
  TemporaryRoot root;
  const auto project_path = root.path() / "arm.prometheus";
  const auto run = motor_a_run();
  prepare_for_publication(project_path, run);

  std::stop_source stop;
  stop.request_stop();
  run_store::TransactionOptions cancelled_options;
  cancelled_options.stop_token = stop.get_token();
  const auto cancelled = run_store::publish_completed_run(
      project_path, run, cancelled_options);
  require_failure(cancelled, "operation_cancelled", "cancelled publication");
  require(require_success(run_store::open_read_only(project_path),
                          "reopen cancelled publication")
              .execution.committed_runs.empty(),
          "cancellation before replacement commits no run");

  std::stop_source late_stop;
  run_store::TransactionOptions late_cancel;
  late_cancel.stop_token = late_stop.get_token();
  late_cancel.boundary_hook = [&](const auto boundary) {
    if (boundary ==
        run_store::TransactionBoundary::before_project_replacement) {
      late_stop.request_stop();
    }
    return false;
  };
  const auto cancelled_at_commit =
      run_store::publish_completed_run(project_path, run, late_cancel);
  require_failure(cancelled_at_commit, "operation_cancelled",
                  "last-gate cancellation");
  require(require_success(run_store::open_read_only(project_path),
                          "reopen last-gate cancellation")
              .execution.committed_runs.empty(),
          "cancellation raised at the final gate prevents replacement");

  auto mismatched = run;
  mismatched.manifest.reference.object_hash =
      "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  const auto rejected =
      run_store::publish_completed_run(project_path, mismatched);
  require(!rejected.has_value(), "manifest hash mismatch rejects");
  require(require_success(run_store::open_read_only(project_path),
                          "reopen rejected publication")
              .execution.committed_runs.empty(),
          "invalid manifest graph commits no run");
}

void test_event_log_trims_only_display_metadata() {
  TemporaryRoot root;
  const auto project_path = root.path() / "arm.prometheus";
  auto project = initial_project();
  for (std::uint64_t sequence = 1U; sequence <= run_store::maximum_events;
       ++sequence) {
    project.execution.events.push_back(run_store::Event{
        sequence, "prior_event", "completed", std::nullopt,
        "2026-08-12T12:00:00Z", "none"});
  }
  require_success(run_store::create_project_v2(project_path, project),
                  "create full-event project");
  const auto run = motor_a_run();
  require_success(run_store::install_package_binding(
                      project_path, "motor", run.package.reference,
                      run.package.bytes),
                  "bind full-event project");
  require_success(run_store::set_current_scenario(
                      project_path, run.scenario.reference,
                      run.scenario.bytes),
                  "set full-event scenario");
  const auto published = require_success(
      run_store::publish_completed_run(project_path, run),
      "publish with full display event log");
  require(published.project.execution.events.size() ==
                  run_store::maximum_events &&
              published.project.execution.events.front().sequence == 3U &&
              published.project.execution.events.back().sequence == 258U &&
              published.project.execution.committed_runs.size() == 2U,
          "event append trims only the oldest display event, not run history");
}

class ChildProcess final {
public:
  ChildProcess(const std::vector<std::string> &arguments) {
#ifdef _WIN32
    std::string command = '"' + contention_helper.string() + '"';
    for (const auto &argument : arguments) {
      command += " \"" + argument + "\"";
    }
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                           nullptr, nullptr, &startup, &process) != 0,
            "spawn contention helper");
    CloseHandle(process.hThread);
    process_ = process.hProcess;
#else
    pid_ = ::fork();
    require(pid_ >= 0, "fork contention helper");
    if (pid_ == 0) {
      std::vector<char *> argv;
      auto executable = contention_helper.string();
      argv.push_back(executable.data());
      std::vector<std::string> owned = arguments;
      for (auto &argument : owned) {
        argv.push_back(argument.data());
      }
      argv.push_back(nullptr);
      ::execv(executable.c_str(), argv.data());
      ::_exit(127);
    }
#endif
  }

  ChildProcess(const ChildProcess &) = delete;
  ChildProcess &operator=(const ChildProcess &) = delete;

  ~ChildProcess() {
    if (!waited_) {
#ifdef _WIN32
      TerminateProcess(process_, 1U);
      WaitForSingleObject(process_, INFINITE);
      CloseHandle(process_);
#else
      ::kill(pid_, SIGKILL);
      int status = 0;
      static_cast<void>(::waitpid(pid_, &status, 0));
#endif
    }
  }

  int wait() {
    require(!waited_, "child process waited twice");
#ifdef _WIN32
    require(WaitForSingleObject(process_, 10000U) == WAIT_OBJECT_0,
            "contention helper exit timeout");
    DWORD code = 1U;
    require(GetExitCodeProcess(process_, &code) != 0,
            "read contention helper exit code");
    CloseHandle(process_);
    waited_ = true;
    return static_cast<int>(code);
#else
    int status = 0;
    require(::waitpid(pid_, &status, 0) == pid_, "wait contention helper");
    waited_ = true;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#endif
  }

private:
#ifdef _WIN32
  HANDLE process_{INVALID_HANDLE_VALUE};
#else
  pid_t pid_{-1};
#endif
  bool waited_{false};
};

void wait_for_file(const fs::path &path) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (!fs::exists(path) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  require(fs::exists(path), "contention helper did not signal readiness");
}

ChildProcess start_helper(const std::string &mode, const fs::path &project,
                          const fs::path &ready, const fs::path &release,
                          const std::string &entity) {
  return ChildProcess({mode, project.string(), ready.string(), release.string(),
                       entity});
}

void test_kernel_lock_contention_and_recovery() {
  TemporaryRoot root;
  const auto project_path = root.path() / "arm.prometheus";
  const auto run = motor_a_run();
  create_project(project_path);

  const auto ready = root.path() / "writer.ready";
  const auto release = root.path() / "writer.release";
  auto writer =
      start_helper("hold", project_path, ready, release, "helper-one");
  wait_for_file(ready);

  run_store::TransactionOptions short_wait;
  short_wait.lock_timeout = 100ms;
  const auto second_writer = run_store::install_package_binding(
      project_path, "parent-writer", run.package.reference, run.package.bytes,
      short_wait);
  require_failure(second_writer, "project_busy", "second writer contention");
  const auto blocked_reader = run_store::open_read_only(project_path, short_wait);
  require_failure(blocked_reader, "project_busy", "reader during writer");

  const auto timeout_started = std::chrono::steady_clock::now();
  const auto timed_out = run_store::open_read_only(project_path);
  const auto timeout_elapsed = std::chrono::steady_clock::now() - timeout_started;
  require_failure(timed_out, "project_busy", "five-second lock timeout");
  require(timeout_elapsed >= 4500ms && timeout_elapsed < 7s,
          "default lock wait is bounded to approximately five seconds");

  write_file(release, "release\n");
  require(writer.wait() == 0, "writer helper completed");
  require(require_success(run_store::open_read_only(project_path),
                          "read after writer release")
              .execution.package_bindings.size() == 1U,
          "released writer publishes one complete update");

  const auto ready_reader = root.path() / "reader-writer.ready";
  const auto release_reader = root.path() / "reader-writer.release";
  auto reader_writer = start_helper("hold", project_path, ready_reader,
                                    release_reader, "helper-two");
  wait_for_file(ready_reader);
  auto reader_future = std::async(std::launch::async, [&] {
    return run_store::open_read_only(project_path);
  });
  require(reader_future.wait_for(100ms) == std::future_status::timeout,
          "reader waits while writer owns the kernel lock");
  write_file(release_reader, "release\n");
  require(reader_writer.wait() == 0, "reader/writer helper completed");
  const auto reader_result = reader_future.get();
  require(reader_result.has_value() &&
              reader_result.value().execution.package_bindings.size() == 2U,
          "waiting reader observes the complete new index");

  const auto crash_ready = root.path() / "crash.ready";
  const auto unused_release = root.path() / "unused.release";
  auto crashing = start_helper("crash", project_path, crash_ready,
                               unused_release, "helper-crash");
  wait_for_file(crash_ready);
  require(crashing.wait() == 0, "crashing helper exits without unlock code");
  const auto after_crash = run_store::install_package_binding(
      project_path, "after-crash", run.package.reference, run.package.bytes);
  require(after_crash.has_value(),
          "kernel releases writer lock after process death");
  require_no_temporary_files(root.path(),
                             "writer cleans crash-left project temporary");

  const auto read_only_temporary =
      root.path() / "arm.prometheus.tmp.read-only-proof";
  write_file(read_only_temporary, "stale\n");
  require(run_store::open_read_only(project_path).has_value(),
          "read-only open succeeds with an unrelated stale temporary");
  require(fs::is_regular_file(read_only_temporary),
          "read-only open does not clean or mutate stale files");
  const auto cleanup_write = run_store::install_package_binding(
      project_path, "cleanup-writer", run.package.reference,
      run.package.bytes);
  require(cleanup_write.has_value(), "writer runs after read-only proof");
  require(!fs::exists(read_only_temporary),
          "exclusive writer owns stale project-temporary cleanup");

  const auto lock_path =
      run_store::sidecar_path_for_project(project_path) / ".writer.lock";
  std::error_code time_error;
  fs::last_write_time(lock_path, fs::file_time_type::clock::now() -
                                     std::chrono::hours(24 * 365),
                      time_error);
  require(!time_error, "set old lock-file timestamp");
  require(run_store::open_read_only(project_path).has_value(),
          "old unlocked lock file does not block or trigger recovery");
  require(fs::is_regular_file(lock_path),
          "persistent old lock file is never deleted as stale");

  const auto lock_decoy = root.path() / "lock-decoy";
  write_file(lock_decoy, "not a lock\n");
  std::error_code link_error;
  fs::remove(lock_path, link_error);
  require(!link_error, "remove lock for substitution test");
  fs::create_symlink(lock_decoy, lock_path, link_error);
  if (link_error == std::errc::operation_not_permitted ||
      link_error == std::errc::permission_denied ||
      link_error == std::errc::function_not_supported ||
      link_error == std::errc::operation_not_supported) {
    std::cerr << "SKIP: lock symlink substitution requires symlink privileges\n";
    return;
  }
  require(!link_error, "create substituted lock symlink");
  const auto substituted_lock = run_store::open_read_only(project_path);
  require_failure(substituted_lock, "unsafe_lock_path",
                  "substituted writer lock");
}

} // namespace

int main() {
  try {
    test_normal_operations_and_idempotency();
    test_structural_manifest_anchor();
    test_embedded_structural_archive_round_trip();
    test_create_failure_boundaries();
    test_binding_failure_boundaries();
    test_scenario_failure_is_atomic();
    test_publication_failure_is_atomic();
    test_publication_object_order_is_explicit();
    test_cancelled_and_invalid_publications_do_not_commit();
    test_event_log_trims_only_display_metadata();
    test_kernel_lock_contention_and_recovery();
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << "FAIL: " << failure.what() << '\n';
    return 1;
  }
}
