#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/replay/replay.hpp>
#include <prometheus/run_store/object_store.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
namespace integrity = prometheus::integrity;
namespace replay = prometheus::replay;
namespace run_store = prometheus::run_store;
using Json = nlohmann::json;
using namespace std::chrono_literals;

const fs::path repository_root{PROMETHEUS_REPOSITORY_ROOT};
const fs::path replay_executable{PROMETHEUS_REPLAY_EXECUTABLE};
const fs::path fixture_creator{PROMETHEUS_REPLAY_FIXTURE_CREATOR};
const fs::path contention_helper{PROMETHEUS_RUN_STORE_CONTENTION_HELPER};

static_assert(std::is_same_v<decltype(&replay::replay_exact),
                             replay::ReplayReport (*)(
                                 const fs::path &, std::string_view,
                                 run_store::TransactionOptions) noexcept>);
static_assert(std::is_same_v<decltype(&replay::inspect_recorded),
                             replay::RecordedRunReport (*)(
                                 const fs::path &, std::string_view,
                                 run_store::TransactionOptions) noexcept>);

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string read_file(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  require(static_cast<bool>(stream), "open file: " + path.string());
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void write_file(const fs::path &path, const std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(stream), "open file for writing: " + path.string());
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(static_cast<bool>(stream), "write file: " + path.string());
}

class TemporaryRoot final {
public:
  TemporaryRoot() {
    static std::atomic<std::uint64_t> counter{0U};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() /
            ("prometheus-replay-cli-" + std::to_string(stamp) + "-" +
             std::to_string(counter.fetch_add(1U)));
    require(fs::create_directory(path_), "create replay test root");
    capture_root_ = path_ / "captures";
    require(fs::create_directory(capture_root_), "create process capture root");
  }

  TemporaryRoot(const TemporaryRoot &) = delete;
  TemporaryRoot &operator=(const TemporaryRoot &) = delete;

  ~TemporaryRoot() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }

  [[nodiscard]] const fs::path &path() const noexcept { return path_; }
  [[nodiscard]] const fs::path &capture_root() const noexcept {
    return capture_root_;
  }

private:
  fs::path path_;
  fs::path capture_root_;
};

struct ProcessResult final {
  int exit_code;
  std::string standard_output;
  std::string standard_error;
};

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string &value) {
  if (value.empty()) {
    return {};
  }
  const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          value.data(),
                                          static_cast<int>(value.size()),
                                          nullptr, 0);
  require(length > 0, "convert UTF-8 process argument");
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), result.data(),
                              length) == length,
          "complete UTF-8 process argument conversion");
  return result;
}

std::wstring quote_windows_argument(const std::wstring &value) {
  std::wstring result{L"\""};
  std::size_t backslashes = 0U;
  for (const auto character : value) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'\"') {
      result.append(backslashes * 2U + 1U, L'\\');
      result.push_back(L'\"');
    } else {
      result.append(backslashes, L'\\');
      result.push_back(character);
    }
    backslashes = 0U;
  }
  result.append(backslashes * 2U, L'\\');
  result.push_back(L'\"');
  return result;
}

struct SavedEnvironment final {
  std::wstring key;
  std::optional<std::wstring> value;
};

std::optional<std::wstring> read_environment(const std::wstring &key) {
  SetLastError(ERROR_SUCCESS);
  const auto required = GetEnvironmentVariableW(key.c_str(), nullptr, 0U);
  if (required == 0U) {
    return std::nullopt;
  }
  std::wstring value(required, L'\0');
  const auto written =
      GetEnvironmentVariableW(key.c_str(), value.data(), required);
  require(written < required, "read inherited process environment");
  value.resize(written);
  return value;
}
#endif

ProcessResult run_process(
    const fs::path &executable, const std::vector<std::string> &arguments,
    const fs::path &capture_root,
    const std::vector<std::pair<std::string, std::string>> &environment = {}) {
  static std::atomic<std::uint64_t> counter{0U};
  const auto identity = std::to_string(counter.fetch_add(1U));
  const auto stdout_path = capture_root / (identity + ".stdout");
  const auto stderr_path = capture_root / (identity + ".stderr");
  int exit_code = 127;

#ifdef _WIN32
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE stdout_handle = CreateFileW(
      stdout_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE stderr_handle = CreateFileW(
      stderr_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  require(stdout_handle != INVALID_HANDLE_VALUE &&
              stderr_handle != INVALID_HANDLE_VALUE,
          "create Windows process capture files");

  std::wstring command = quote_windows_argument(executable.wstring());
  for (const auto &argument : arguments) {
    command.push_back(L' ');
    command += quote_windows_argument(utf8_to_wide(argument));
  }
  std::vector<SavedEnvironment> saved_environment;
  for (const auto &[key, value] : environment) {
    auto wide_key = utf8_to_wide(key);
    saved_environment.push_back(
        SavedEnvironment{wide_key, read_environment(wide_key)});
    require(SetEnvironmentVariableW(wide_key.c_str(),
                                    utf8_to_wide(value).c_str()) != 0,
            "set child process environment");
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = stdout_handle;
  startup.hStdError = stderr_handle;
  PROCESS_INFORMATION process{};
  const auto created = CreateProcessW(
      executable.c_str(), command.data(), nullptr, nullptr, TRUE,
      CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process);
  for (const auto &saved : saved_environment) {
    static_cast<void>(SetEnvironmentVariableW(
        saved.key.c_str(),
        saved.value.has_value() ? saved.value->c_str() : nullptr));
  }
  CloseHandle(stdout_handle);
  CloseHandle(stderr_handle);
  require(created != 0, "create Windows child process");
  CloseHandle(process.hThread);
  require(WaitForSingleObject(process.hProcess, 60000U) == WAIT_OBJECT_0,
          "Windows child process timeout");
  DWORD process_code = 127U;
  require(GetExitCodeProcess(process.hProcess, &process_code) != 0,
          "read Windows child exit code");
  CloseHandle(process.hProcess);
  exit_code = static_cast<int>(process_code);
#else
  const auto child = ::fork();
  require(child >= 0, "fork child process");
  if (child == 0) {
    const auto stdout_descriptor =
        ::open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const auto stderr_descriptor =
        ::open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (stdout_descriptor < 0 || stderr_descriptor < 0 ||
        ::dup2(stdout_descriptor, STDOUT_FILENO) < 0 ||
        ::dup2(stderr_descriptor, STDERR_FILENO) < 0) {
      ::_exit(126);
    }
    static_cast<void>(::close(stdout_descriptor));
    static_cast<void>(::close(stderr_descriptor));
    for (const auto &[key, value] : environment) {
      if (::setenv(key.c_str(), value.c_str(), 1) != 0) {
        ::_exit(126);
      }
    }
    std::vector<std::string> owned;
    owned.reserve(arguments.size() + 1U);
    owned.push_back(executable.string());
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    argv.reserve(owned.size() + 1U);
    for (auto &argument : owned) {
      argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    ::execv(executable.c_str(), argv.data());
    ::_exit(127);
  }
  int status = 0;
  require(::waitpid(child, &status, 0) == child, "wait for child process");
  exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#endif

  const auto standard_output = read_file(stdout_path);
  const auto standard_error = read_file(stderr_path);
  std::error_code ignored;
  fs::remove(stdout_path, ignored);
  fs::remove(stderr_path, ignored);
  return {exit_code, standard_output, standard_error};
}

using Snapshot = std::map<std::string, std::string>;

Snapshot snapshot_tree(const fs::path &root) {
  Snapshot snapshot;
  if (!fs::exists(root)) {
    return snapshot;
  }
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    std::error_code error;
    const auto status = entry.symlink_status(error);
    require(!error, "inspect replay immutability snapshot");
    const auto relative = fs::relative(entry.path(), root, error);
    require(!error, "derive replay immutability snapshot path");
    const auto key = relative.generic_string();
    if (fs::is_symlink(status)) {
      const auto target = fs::read_symlink(entry.path(), error);
      require(!error, "read replay snapshot symbolic link");
      snapshot.emplace(key, "symlink:" + target.generic_string());
    } else if (fs::is_directory(status)) {
      snapshot.emplace(key, "directory");
    } else if (fs::is_regular_file(status)) {
      snapshot.emplace(key, "file:" + integrity::sha256_bytes(read_file(entry.path())));
    } else {
      snapshot.emplace(key, "other");
    }
  }
  return snapshot;
}

struct Fixture final {
  fs::path root;
  fs::path project;
  std::string motor_a_manifest_hash;
  std::string motor_b_manifest_hash;
};

Fixture create_fixture(const fs::path &root, const fs::path &capture_root) {
  require(fs::create_directory(root), "create replay fixture directory");
  const auto project = root / "arm.prometheus";
  const auto created = run_process(fixture_creator, {project.string()},
                                   capture_root);
  require(created.exit_code == 0,
          "fixture creator failed: " + created.standard_error);
  const auto value = Json::parse(created.standard_output);
  return Fixture{root,
                 project,
                 value.at("motor_a_manifest_hash").get<std::string>(),
                 value.at("motor_b_manifest_hash").get<std::string>()};
}

Fixture clone_fixture(const Fixture &source, const fs::path &destination) {
  std::error_code error;
  fs::copy(source.root, destination,
           fs::copy_options::recursive | fs::copy_options::copy_symlinks,
           error);
  require(!error, "clone replay fixture: " + error.message());
  return Fixture{destination, destination / source.project.filename(),
                 source.motor_a_manifest_hash,
                 source.motor_b_manifest_hash};
}

ProcessResult invoke_immutable(
    const Fixture &fixture, const std::vector<std::string> &arguments,
    const fs::path &capture_root,
    const std::vector<std::pair<std::string, std::string>> &environment = {}) {
  const auto before = snapshot_tree(fixture.root);
  const auto result =
      run_process(replay_executable, arguments, capture_root, environment);
  const auto after = snapshot_tree(fixture.root);
  if (before != after) {
    std::string difference;
    for (const auto &[path, value] : before) {
      const auto found = after.find(path);
      if (found == after.end()) {
        difference += " removed=" + path;
      } else if (found->second != value) {
        difference += " changed=" + path;
      }
    }
    for (const auto &[path, value] : after) {
      static_cast<void>(value);
      if (!before.contains(path)) {
        difference += " added=" + path;
      }
    }
    require(false,
            "replay invocation mutated project, sidecar, CAD, object, or event bytes:" +
                difference + " exit=" + std::to_string(result.exit_code) +
                " stderr=" + result.standard_error);
  }
  return result;
}

ProcessResult invoke_run(
    const Fixture &fixture, const std::string &hash,
    const fs::path &capture_root,
    const std::vector<std::pair<std::string, std::string>> &environment = {}) {
  return invoke_immutable(
      fixture,
      {"--project", fixture.project.string(), "--run", hash}, capture_root,
      environment);
}

void test_recorded_inspection_is_verified_and_read_only(const Fixture &fixture) {
  const auto before = snapshot_tree(fixture.root);
  const auto inspected = replay::inspect_recorded(
      fixture.project, fixture.motor_a_manifest_hash);
  const auto after = snapshot_tree(fixture.root);
  require(before == after, "recorded inspection is read-only");
  require(inspected.status == replay::RecordedRunStatus::recorded &&
              inspected.package_hash.has_value() &&
              inspected.result_hash.has_value() &&
              inspected.result_bytes.has_value() &&
              !inspected.result_bytes->empty() &&
              !inspected.diagnostic.has_value(),
          "recorded inspection verifies and returns exact display bytes");
  const auto result = Json::parse(*inspected.result_bytes);
  require(result.at("package_hash") == *inspected.package_hash &&
              result.at("execution_disposition") == "completed",
          "recorded inspection binds display bytes to the verified graph");
}

void test_absolute_desktop_cad_path_replays(const Fixture &base,
                                            const fs::path &variant_root,
                                            const fs::path &capture_root) {
  const auto fixture =
      clone_fixture(base, variant_root / "absolute-desktop-cad");
  const auto opened = run_store::open_read_only(fixture.project);
  require(opened.has_value(), "open absolute-CAD replay fixture");
  auto project = opened.value();
  project.cad_source = fs::absolute(fixture.root / "motor-arm.step").string();
  const auto saved =
      run_store::save_project_snapshot(fixture.project, project);
  require(saved.has_value(), "save absolute desktop CAD source");
  const auto replayed =
      invoke_run(fixture, fixture.motor_a_manifest_hash, capture_root);
  require(replayed.exit_code == 0,
          "an ordinary absolute desktop CAD path supports exact replay: " +
              replayed.standard_error);
}

void require_failure_report(const ProcessResult &result, const int exit_code,
                            const std::string_view status,
                            const std::optional<std::string_view> code =
                                std::nullopt) {
  require(result.exit_code == exit_code,
          "unexpected replay exit code: " + std::to_string(result.exit_code) +
              " stderr=" + result.standard_error);
  const auto value = Json::parse(result.standard_output);
  require(value.is_object() && value.at("status") == status &&
              value.contains("stage") && value.contains("code"),
          "failure stdout is the closed machine-readable report");
  if (code.has_value()) {
    require(value.at("code") == *code,
            "unexpected replay diagnostic code: expected " +
                std::string(*code) + ", received " +
                value.at("code").get<std::string>());
  }
  require(!result.standard_error.empty(),
          "failure human detail is written to stderr");
}

run_store::StoredObjectReference
reference_from_json(const Json &value) {
  return run_store::StoredObjectReference{
      value.at("object_hash").get<std::string>(),
      value.at("byte_length").get<std::uint64_t>(),
      value.at("media_type").get<std::string>(),
      value.at("schema_id").get<std::string>(),
      value.at("schema_version").get<std::string>()};
}

Json reference_json(const run_store::StoredObjectReference &reference) {
  return Json{{"object_hash", reference.object_hash},
              {"byte_length", reference.byte_length},
              {"media_type", reference.media_type},
              {"schema_id", reference.schema_id},
              {"schema_version", reference.schema_version}};
}

run_store::StoredObjectReference
reference_for(const std::string_view bytes, std::string media_type,
              std::string schema_id, std::string schema_version) {
  return {integrity::object_hash(bytes),
          static_cast<std::uint64_t>(bytes.size()), std::move(media_type),
          std::move(schema_id), std::move(schema_version)};
}

template <typename T>
T require_success(run_store::Result<T> result, const std::string &context) {
  require(result.has_value(),
          context + ": " +
              (result.has_value()
                   ? std::string{}
                   : result.diagnostic().stage + "/" +
                         result.diagnostic().code + " " +
                         result.diagnostic().message));
  return std::move(result.value());
}

run_store::StoredObjectReference
find_committed(const Fixture &fixture, const std::string_view hash) {
  const auto project = require_success(run_store::open_read_only(fixture.project),
                                       "open fixture project");
  const auto found = std::find_if(
      project.execution.committed_runs.begin(),
      project.execution.committed_runs.end(), [&](const auto &reference) {
        return reference.object_hash == hash;
      });
  require(found != project.execution.committed_runs.end(),
          "find committed replay manifest");
  return *found;
}

std::string object_bytes(const Fixture &fixture,
                         const run_store::StoredObjectReference &reference) {
  return require_success(run_store::read_object(fixture.project, reference),
                         "read fixture object");
}

fs::path object_path(const Fixture &fixture,
                     const run_store::StoredObjectReference &reference) {
  return require_success(
      run_store::object_path_for_hash(
          run_store::sidecar_path_for_project(fixture.project),
          reference.object_hash),
      "derive fixture object path");
}

void replace_committed_reference(
    const Fixture &fixture, const std::string_view old_hash,
    const run_store::StoredObjectReference &replacement) {
  auto project = require_success(run_store::open_read_only(fixture.project),
                                 "open project for test mutation");
  const auto found = std::find_if(
      project.execution.committed_runs.begin(),
      project.execution.committed_runs.end(), [&](const auto &reference) {
        return reference.object_hash == old_hash;
      });
  require(found != project.execution.committed_runs.end(),
          "replace committed manifest reference");
  *found = replacement;
  const auto serialized = require_success(run_store::serialize_project_v2(project),
                                          "serialize test-mutated project");
  write_file(fixture.project, serialized);
}

run_store::StoredObjectReference
install_canonical_object(const Fixture &fixture, const std::string &bytes,
                         std::string media_type, std::string schema_id,
                         std::string schema_version) {
  auto reference = reference_for(bytes, std::move(media_type),
                                 std::move(schema_id),
                                 std::move(schema_version));
  const auto installed =
      run_store::install_object(fixture.project, reference, bytes);
  require(installed.has_value(),
          "install test object: " +
              (installed.has_value()
                   ? std::string{}
                   : installed.diagnostic().stage + "/" +
                         installed.diagnostic().code));
  return reference;
}

run_store::StoredObjectReference
install_manifest_variant(const Fixture &fixture,
                         const std::string_view original_hash,
                         const Json &manifest) {
  const auto bytes = integrity::canonicalize_json_bytes(manifest.dump());
  auto reference = install_canonical_object(
      fixture, bytes,
      "application/vnd.prometheus.run-manifest+json;version=1.0.0",
      "urn:prometheus:schema:run-manifest:1.0.0", "1.0.0");
  replace_committed_reference(fixture, original_hash, reference);
  return reference;
}

Json manifest_json(const Fixture &fixture, const std::string_view hash) {
  const auto reference = find_committed(fixture, hash);
  return Json::parse(object_bytes(fixture, reference));
}

std::string recorded_result_hash(const Fixture &fixture,
                                 const std::string_view manifest_hash) {
  return manifest_json(fixture, manifest_hash)
      .at("result")
      .at("object_hash")
      .get<std::string>();
}

void test_exact_cli_and_offline_linkage(const Fixture &base,
                                        const fs::path &capture_root) {
  const std::string cli_links{PROMETHEUS_REPLAY_LINK_LIBRARIES};
  const std::string support_links{PROMETHEUS_REPLAY_SUPPORT_LINK_LIBRARIES};
  const std::string complete_link_graph =
      cli_links + "|" + support_links + "|" +
      PROMETHEUS_EXECUTION_LINK_LIBRARIES + "|" +
      PROMETHEUS_RUN_STORE_LINK_LIBRARIES + "|" +
      PROMETHEUS_CORE_LINK_LIBRARIES + "|" +
      PROMETHEUS_INTEGRITY_LINK_LIBRARIES;
  require(cli_links == "prometheus_replay_support",
          "CLI is only an adapter over shared replay support");
  require(support_links.find("prometheus_execution") != std::string::npos &&
              support_links.find("prometheus_run_store") != std::string::npos,
          "shared replay support links the authoritative execution and store");
  for (const auto forbidden : {"Qt", "Python", "curl", "CURL", "http",
                               "HTTP", "database", "postgres"}) {
    require(complete_link_graph.find(forbidden) == std::string::npos,
            "replay target has no network/service dependency: " +
                std::string(forbidden));
  }

  const std::vector<std::pair<std::string, std::string>> offline_environment{
      {"HTTP_PROXY", "http://127.0.0.1:1"},
      {"HTTPS_PROXY", "http://127.0.0.1:1"},
      {"ALL_PROXY", "http://127.0.0.1:1"},
      {"NO_PROXY", ""},
      {"RES_OPTIONS", "attempts:1 timeout:1"},
  };
  for (const auto &hash : {base.motor_a_manifest_hash,
                           base.motor_b_manifest_hash}) {
    const auto result =
        invoke_run(base, hash, capture_root, offline_environment);
    require(result.exit_code == 0 && result.standard_error.empty(),
            "offline exact replay succeeds without stderr");
    const auto expected =
        Json{{"manifest_hash", hash},
             {"recorded_result_hash", recorded_result_hash(base, hash)},
             {"replayed_result_hash", recorded_result_hash(base, hash)},
             {"status", "exact_match"}}
            .dump() +
        "\n";
    require(result.standard_output == expected,
            "exact-match stdout is one compact closed JSON object");
  }
}

void test_strict_arguments(const Fixture &base,
                           const fs::path &capture_root) {
  const auto help = invoke_immutable(base, {"--help"}, capture_root);
  require(help.exit_code == 0 &&
              help.standard_output ==
                  "Usage: prometheus_replay --project <project.prometheus> "
                  "--run <sha256:...>\n" &&
              help.standard_error.empty(),
          "help is the sole non-replay CLI mode");

  const std::vector<std::vector<std::string>> invalid{
      {},
      {"--help", "extra"},
      {"--project", base.project.string(), "--run"},
      {"--run", base.motor_a_manifest_hash, "--project",
       base.project.string()},
      {"--project", base.project.string(), "--run",
       "sha256:" + std::string(64U, 'A')},
      {"--project", base.project.string(), "--run", "sha256:1234"},
      {"--project", base.project.string(), "--run",
       base.motor_a_manifest_hash, "extra"},
  };
  for (const auto &arguments : invalid) {
    require_failure_report(
        invoke_immutable(base, arguments, capture_root), 2,
        "invalid_arguments", "invalid_arguments");
  }
}

void test_project_store_and_commit_failures(const Fixture &base,
                                            const fs::path &variant_root,
                                            const fs::path &capture_root) {
  {
    auto fixture = clone_fixture(base, variant_root / "missing-project");
    std::error_code error;
    fs::remove(fixture.project, error);
    require(!error, "remove project while retaining its sidecar");
    require_failure_report(invoke_run(fixture, fixture.motor_a_manifest_hash,
                                      capture_root),
                           3, "verification_failed", "project_missing");
  }
  {
    const auto fixture = clone_fixture(base, variant_root / "corrupt-project");
    write_file(fixture.project, "{}\n");
    require_failure_report(invoke_run(fixture, fixture.motor_a_manifest_hash,
                                      capture_root),
                           3, "verification_failed");
  }
  {
    const auto fixture = clone_fixture(base, variant_root / "missing-sidecar");
    std::error_code error;
    fs::remove_all(run_store::sidecar_path_for_project(fixture.project), error);
    require(!error, "remove test sidecar");
    require_failure_report(invoke_run(fixture, fixture.motor_a_manifest_hash,
                                      capture_root),
                           3, "verification_failed");
  }
  {
    const auto fixture = clone_fixture(base, variant_root / "uncommitted");
    auto project = require_success(run_store::open_read_only(fixture.project),
                                   "open uncommitted test project");
    project.execution.committed_runs.erase(
        std::remove_if(project.execution.committed_runs.begin(),
                       project.execution.committed_runs.end(),
                       [&](const auto &reference) {
                         return reference.object_hash ==
                                fixture.motor_a_manifest_hash;
                       }),
        project.execution.committed_runs.end());
    write_file(fixture.project,
               require_success(run_store::serialize_project_v2(project),
                               "serialize uncommitted test project"));
    require_failure_report(invoke_run(fixture, fixture.motor_a_manifest_hash,
                                      capture_root),
                           3, "verification_failed",
                           "manifest_not_committed");
    require(fs::is_regular_file(object_path(
                fixture, run_store::StoredObjectReference{
                             fixture.motor_a_manifest_hash,
                             0U,
                             "application/vnd.prometheus.run-manifest+json;version=1.0.0",
                             "urn:prometheus:schema:run-manifest:1.0.0",
                             "1.0.0"})),
            "uncommitted manifest remains physically present");
  }
}

void test_manifest_reference_metadata_failures(
    const Fixture &base, const fs::path &variant_root,
    const fs::path &capture_root) {
  struct Mutation final {
    std::string name;
    std::string expected_code;
    void (*apply)(Json &);
  };
  const std::vector<Mutation> mutations{
      {"wrong-length", "object_length_mismatch",
       [](Json &manifest) {
         manifest["package"]["byte_length"] =
             manifest["package"]["byte_length"].get<std::uint64_t>() + 1U;
       }},
      {"wrong-media", "unsupported_object_contract",
       [](Json &manifest) {
         manifest["package"]["media_type"] = "application/json";
       }},
      {"wrong-schema", "unsupported_object_contract",
       [](Json &manifest) {
         manifest["package"]["schema_id"] =
             "urn:prometheus:schema:not-supported:1.0.0";
       }},
      {"wrong-hash", "object_store_missing",
       [](Json &manifest) {
         manifest["package"]["object_hash"] =
             "sha256:" + std::string(64U, '0');
       }},
  };
  for (const auto &mutation : mutations) {
    const auto fixture = clone_fixture(base, variant_root / mutation.name);
    auto manifest = manifest_json(fixture, fixture.motor_a_manifest_hash);
    mutation.apply(manifest);
    const auto replacement = install_manifest_variant(
        fixture, fixture.motor_a_manifest_hash, manifest);
    require_failure_report(invoke_run(fixture, replacement.object_hash,
                                      capture_root),
                           3, "verification_failed", mutation.expected_code);
  }
}

void test_missing_corrupt_and_symlinked_objects(
    const Fixture &base, const fs::path &variant_root,
    const fs::path &capture_root) {
  const auto mutate_result = [&](const std::string &name,
                                 const auto &mutation,
                                 const std::optional<std::string_view> code) {
    const auto fixture = clone_fixture(base, variant_root / name);
    const auto manifest = manifest_json(fixture, fixture.motor_a_manifest_hash);
    const auto result_reference = reference_from_json(manifest.at("result"));
    mutation(fixture, result_reference);
    require_failure_report(
        invoke_run(fixture, fixture.motor_a_manifest_hash, capture_root), 3,
        "verification_failed", code);
  };

  mutate_result(
      "missing-object",
      [](const Fixture &fixture,
         const run_store::StoredObjectReference &reference) {
        std::error_code error;
        fs::remove(object_path(fixture, reference), error);
        require(!error, "remove replay result object");
      },
      "object_read_failed");
  mutate_result(
      "corrupt-object",
      [](const Fixture &fixture,
         const run_store::StoredObjectReference &reference) {
        auto bytes = object_bytes(fixture, reference);
        require(!bytes.empty(), "result bytes are nonempty before corruption");
        bytes.front() = bytes.front() == '{' ? '[' : '{';
        write_file(object_path(fixture, reference), bytes);
      },
      std::nullopt);
  mutate_result(
      "symlink-object",
      [](const Fixture &fixture,
         const run_store::StoredObjectReference &reference) {
        const auto destination = object_path(fixture, reference);
        const auto decoy = fixture.root / "result-decoy.jcs";
        write_file(decoy, object_bytes(fixture, reference));
        std::error_code error;
        fs::remove(destination, error);
        require(!error, "remove object before symlink substitution");
        fs::create_symlink(decoy, destination, error);
        require(!error, "create result-object symlink substitution");
      },
      "unsafe_object_path");
}

void test_external_cad_failures(const Fixture &base,
                                const fs::path &variant_root,
                                const fs::path &capture_root) {
  {
    const auto fixture = clone_fixture(base, variant_root / "missing-cad");
    std::error_code error;
    fs::remove(fixture.root / "motor-arm.step", error);
    require(!error, "remove external CAD fixture");
    require_failure_report(invoke_run(fixture, fixture.motor_a_manifest_hash,
                                      capture_root),
                           3, "verification_failed", "assembly_missing");
  }
  {
    const auto fixture = clone_fixture(base, variant_root / "changed-cad");
    auto bytes = read_file(fixture.root / "motor-arm.step");
    bytes += "changed";
    write_file(fixture.root / "motor-arm.step", bytes);
    require_failure_report(invoke_run(fixture, fixture.motor_a_manifest_hash,
                                      capture_root),
                           3, "verification_failed",
                           "assembly_hash_mismatch");
  }
  {
    const auto fixture = clone_fixture(base, variant_root / "symlink-cad");
    const auto cad = fixture.root / "motor-arm.step";
    const auto decoy = fixture.root / "motor-arm-decoy.step";
    write_file(decoy, read_file(cad));
    std::error_code error;
    fs::remove(cad, error);
    require(!error, "remove CAD before symlink substitution");
    fs::create_symlink(decoy, cad, error);
    require(!error, "create CAD symlink substitution");
    require_failure_report(invoke_run(fixture, fixture.motor_a_manifest_hash,
                                      capture_root),
                           3, "verification_failed",
                           "unsafe_assembly_path");
  }
}

void test_numeric_identity_and_exact_mismatch(
    const Fixture &base, const fs::path &variant_root,
    const fs::path &capture_root) {
  {
    const auto fixture = clone_fixture(base, variant_root / "numeric-profile");
    auto manifest = manifest_json(fixture, fixture.motor_a_manifest_hash);
    const auto original_result = reference_from_json(manifest.at("result"));
    auto result = Json::parse(object_bytes(fixture, original_result));
    const auto unavailable_fingerprint = "sha256:" + std::string(64U, 'f');
    manifest["numeric_profile"]["backend_build_fingerprint"] =
        unavailable_fingerprint;
    result["backend"]["numeric_profile"]["backend_build_fingerprint"] =
        unavailable_fingerprint;
    const auto result_bytes = integrity::canonicalize_json_bytes(result.dump());
    const auto result_reference = install_canonical_object(
        fixture, result_bytes,
        "application/vnd.prometheus.analysis-result+json;version=1.0.0",
        "urn:prometheus:schema:analysis-result:1.0.0", "1.0.0");
    manifest["result"] = reference_json(result_reference);
    const auto replacement = install_manifest_variant(
        fixture, fixture.motor_a_manifest_hash, manifest);
    require_failure_report(invoke_run(fixture, replacement.object_hash,
                                      capture_root),
                           4, "execution_unavailable",
                           "numeric_profile_mismatch");
  }
  {
    const auto fixture = clone_fixture(base, variant_root / "result-mismatch");
    auto manifest = manifest_json(fixture, fixture.motor_a_manifest_hash);
    const auto original_result = reference_from_json(manifest.at("result"));
    auto result = Json::parse(object_bytes(fixture, original_result));
    result["calculations"][0]["value"] =
        result["calculations"][0]["value"].get<double>() + 0.125;
    const auto result_bytes = integrity::canonicalize_json_bytes(result.dump());
    const auto result_reference = install_canonical_object(
        fixture, result_bytes,
        "application/vnd.prometheus.analysis-result+json;version=1.0.0",
        "urn:prometheus:schema:analysis-result:1.0.0", "1.0.0");
    manifest["result"] = reference_json(result_reference);
    const auto replacement = install_manifest_variant(
        fixture, fixture.motor_a_manifest_hash, manifest);
    const auto replayed = invoke_run(fixture, replacement.object_hash,
                                     capture_root);
    require_failure_report(replayed, 5, "mismatch", "result_mismatch");
    const auto report = Json::parse(replayed.standard_output);
    require(report.at("manifest_hash") == replacement.object_hash,
            "mismatch report identifies the requested manifest");
  }
}

void test_backend_unavailable_and_execution_failure(
    const Fixture &base, const fs::path &variant_root,
    const fs::path &capture_root) {
  {
    const auto fixture = clone_fixture(base, variant_root / "backend-unavailable");
    auto manifest = manifest_json(fixture, fixture.motor_a_manifest_hash);
    const auto original_request = reference_from_json(manifest.at("request"));
    auto request = Json::parse(object_bytes(fixture, original_request));
    request["backend_id"] = "retired_motor_backend_v0";
    const auto request_bytes =
        integrity::canonicalize_json_bytes(request.dump());
    const auto request_reference = install_canonical_object(
        fixture, request_bytes,
        "application/vnd.prometheus.analysis-request+json;version=1.0.0",
        "urn:prometheus:schema:analysis-request:1.0.0", "1.0.0");

    const auto original_result = reference_from_json(manifest.at("result"));
    auto result = Json::parse(object_bytes(fixture, original_result));
    result["request_hash"] = request_reference.object_hash;
    result["backend"]["backend_id"] = "retired_motor_backend_v0";
    const auto result_bytes = integrity::canonicalize_json_bytes(result.dump());
    const auto result_reference = install_canonical_object(
        fixture, result_bytes,
        "application/vnd.prometheus.analysis-result+json;version=1.0.0",
        "urn:prometheus:schema:analysis-result:1.0.0", "1.0.0");

    manifest["request"] = reference_json(request_reference);
    manifest["result"] = reference_json(result_reference);
    manifest["backend_id"] = "retired_motor_backend_v0";
    const auto replacement = install_manifest_variant(
        fixture, fixture.motor_a_manifest_hash, manifest);
    require_failure_report(invoke_run(fixture, replacement.object_hash,
                                      capture_root),
                           4, "execution_unavailable",
                           "backend_identity_unavailable");
  }
  {
    const auto fixture = clone_fixture(base, variant_root / "execution-failure");
    auto manifest = manifest_json(fixture, fixture.motor_a_manifest_hash);
    const auto original_scenario = reference_from_json(manifest.at("scenario"));
    auto scenario = Json::parse(object_bytes(fixture, original_scenario));
    scenario["payload_mass"]["value"] = 1.0e308;
    const auto scenario_bytes =
        integrity::canonicalize_json_bytes(scenario.dump());
    const auto scenario_reference = install_canonical_object(
        fixture, scenario_bytes,
        "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0",
        "urn:prometheus:schema:motor-arm-scenario:1.0.0", "1.0.0");

    const auto original_request = reference_from_json(manifest.at("request"));
    auto request = Json::parse(object_bytes(fixture, original_request));
    request["scenario_hash"] = scenario_reference.object_hash;
    const auto request_bytes =
        integrity::canonicalize_json_bytes(request.dump());
    const auto request_reference = install_canonical_object(
        fixture, request_bytes,
        "application/vnd.prometheus.analysis-request+json;version=1.0.0",
        "urn:prometheus:schema:analysis-request:1.0.0", "1.0.0");

    const auto original_result = reference_from_json(manifest.at("result"));
    auto result = Json::parse(object_bytes(fixture, original_result));
    result["request_hash"] = request_reference.object_hash;
    const auto result_bytes = integrity::canonicalize_json_bytes(result.dump());
    const auto result_reference = install_canonical_object(
        fixture, result_bytes,
        "application/vnd.prometheus.analysis-result+json;version=1.0.0",
        "urn:prometheus:schema:analysis-result:1.0.0", "1.0.0");

    manifest["scenario"] = reference_json(scenario_reference);
    manifest["request"] = reference_json(request_reference);
    manifest["result"] = reference_json(result_reference);
    const auto replacement = install_manifest_variant(
        fixture, fixture.motor_a_manifest_hash, manifest);
    require_failure_report(invoke_run(fixture, replacement.object_hash,
                                      capture_root),
                           4, "execution_failed", "non_finite_output");
  }
}

void wait_for_file(const fs::path &path) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (!fs::exists(path) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  require(fs::exists(path), "writer helper did not signal readiness");
}

class ChildProcess final {
public:
  ChildProcess(const fs::path &executable,
               const std::vector<std::string> &arguments) {
#ifdef _WIN32
    std::wstring command = quote_windows_argument(executable.wstring());
    for (const auto &argument : arguments) {
      command.push_back(L' ');
      command += quote_windows_argument(utf8_to_wide(argument));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(executable.c_str(), command.data(), nullptr,
                           nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                           nullptr, &startup, &process) != 0,
            "spawn replay contention helper");
    CloseHandle(process.hThread);
    process_ = process.hProcess;
#else
    std::vector<std::string> owned;
    owned.reserve(arguments.size() + 1U);
    owned.push_back(executable.string());
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    argv.reserve(owned.size() + 1U);
    for (auto &argument : owned) {
      argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    pid_ = ::fork();
    require(pid_ >= 0, "fork replay contention helper");
    if (pid_ == 0) {
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
      static_cast<void>(TerminateProcess(process_, 1U));
      static_cast<void>(WaitForSingleObject(process_, INFINITE));
      static_cast<void>(CloseHandle(process_));
#else
      static_cast<void>(::kill(pid_, SIGKILL));
      int status = 0;
      static_cast<void>(::waitpid(pid_, &status, 0));
#endif
    }
  }

  int wait() {
    require(!waited_, "wait for replay contention helper only once");
#ifdef _WIN32
    require(WaitForSingleObject(process_, 60000U) == WAIT_OBJECT_0,
            "replay contention helper timeout");
    DWORD code = 127U;
    require(GetExitCodeProcess(process_, &code) != 0,
            "read replay contention helper exit code");
    CloseHandle(process_);
    waited_ = true;
    return static_cast<int>(code);
#else
    int status = 0;
    require(::waitpid(pid_, &status, 0) == pid_,
            "wait for replay contention helper");
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

void test_active_writer_rejected(const Fixture &base,
                                 const fs::path &variant_root,
                                 const fs::path &capture_root) {
  const auto fixture = clone_fixture(base, variant_root / "active-writer");
  const auto ready = fixture.root / "writer.ready";
  const auto release = fixture.root / "writer.release";
  ChildProcess writer(
      contention_helper,
      {"hold", fixture.project.string(), ready.string(), release.string(),
       "replay-contention"});
  wait_for_file(ready);
  const auto replayed =
      invoke_run(fixture, fixture.motor_a_manifest_hash, capture_root);
  require_failure_report(replayed, 3, "verification_failed", "project_busy");
  write_file(release, "release\n");
  require(writer.wait() == 0,
          "contention writer completed after replay rejection");
}

} // namespace

int main() {
  try {
    TemporaryRoot root;
    const auto base = create_fixture(root.path() / "base", root.capture_root());
    const auto variants = root.path() / "variants";
    require(fs::create_directory(variants), "create replay variant root");

    test_exact_cli_and_offline_linkage(base, root.capture_root());
    test_recorded_inspection_is_verified_and_read_only(base);
    test_absolute_desktop_cad_path_replays(base, variants,
                                           root.capture_root());
    test_strict_arguments(base, root.capture_root());
    test_project_store_and_commit_failures(base, variants,
                                           root.capture_root());
    test_manifest_reference_metadata_failures(base, variants,
                                              root.capture_root());
    test_missing_corrupt_and_symlinked_objects(base, variants,
                                               root.capture_root());
    test_external_cad_failures(base, variants, root.capture_root());
    test_numeric_identity_and_exact_mismatch(base, variants,
                                             root.capture_root());
    test_backend_unavailable_and_execution_failure(base, variants,
                                                   root.capture_root());
    test_active_writer_rejected(base, variants, root.capture_root());
    std::cout << "All exact replay CLI tests passed.\n";
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << "FAIL: " << failure.what() << '\n';
    return 1;
  }
}
