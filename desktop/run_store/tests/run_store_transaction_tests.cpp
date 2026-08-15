#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/object_store.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <atomic>
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
              published.project.execution.committed_runs.size() == 1U &&
              published.project.execution.committed_runs.front() ==
                  run.manifest.reference,
          "first publication commits one manifest reference");
  require_complete_run_objects(project_path, run);

  const auto &repeated = require_success(
      run_store::publish_completed_run(project_path, run),
      "idempotent publish");
  require(repeated.already_committed &&
              repeated.project.execution.committed_runs.size() == 1U,
          "idempotent publish does not duplicate a manifest");
  require(repeated.project.execution.events.size() == 2U &&
              repeated.project.execution.events.back().event_kind ==
                  "run_invoked" &&
              repeated.project.execution.events.back().status ==
                  "already_committed",
          "idempotent invocation adds only display metadata");

  const auto reopened = run_store::open_read_only(project_path);
  require(reopened.has_value() &&
              reopened.value().execution.committed_runs.size() == 1U,
          "read-only reopen retains the committed run");

  const auto package_b = motor_b_package();
  const auto &rebound = require_success(
      run_store::install_package_binding(project_path, "motor",
                                         package_b.reference, package_b.bytes),
      "superseding package binding");
  require(rebound.execution.package_bindings.size() == 2U &&
              rebound.execution.package_bindings.back()
                      .supersedes_binding_revision == 1U &&
              rebound.execution.committed_runs.size() == 1U,
          "new package supersedes the active binding without altering history");
  const auto historical_retry = require_success(
      run_store::publish_completed_run(project_path, run),
      "historical idempotent publish after package switch");
  require(historical_retry.already_committed &&
              historical_retry.project.execution.committed_runs.size() == 1U,
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
    require(reopened.execution.committed_runs.size() <= 1U,
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
              published.project.execution.events.front().sequence == 2U &&
              published.project.execution.events.back().sequence == 257U &&
              published.project.execution.committed_runs.size() == 1U,
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
