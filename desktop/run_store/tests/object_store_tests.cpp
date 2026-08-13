#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/object_store.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace run_store = prometheus::run_store;
namespace integrity = prometheus::integrity;

int failures = 0;

void check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path &path,
                const std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

class TemporaryRoot final {
public:
  TemporaryRoot() {
    static std::atomic<std::uint64_t> counter{0U};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("prometheus-run-store-" + std::to_string(stamp) + "-" +
             std::to_string(counter.fetch_add(1U)));
    std::filesystem::create_directory(path_);
  }

  TemporaryRoot(const TemporaryRoot &) = delete;
  TemporaryRoot &operator=(const TemporaryRoot &) = delete;

  ~TemporaryRoot() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

struct StoreFixture final {
  TemporaryRoot root;
  std::filesystem::path project{root.path() / "arm.prometheus"};

  StoreFixture() { write_file(project, "{}\n"); }
};

std::filesystem::path repository_file(const std::string_view relative) {
  return std::filesystem::path(PROMETHEUS_REPOSITORY_ROOT) / relative;
}

run_store::StoredObjectReference
reference_for(const std::string_view bytes, std::string media_type,
              std::string schema_id, std::string schema_version) {
  return {integrity::object_hash(bytes),
          static_cast<std::uint64_t>(bytes.size()), std::move(media_type),
          std::move(schema_id), std::move(schema_version)};
}

run_store::StoredObjectReference package_reference(const std::string &bytes) {
  return reference_for(
      bytes,
      "application/vnd.prometheus.execution-component+json;version=2.0.0",
      "urn:prometheus:schema:execution-component:2.0.0", "2.0.0");
}

void expect_install_failure(const std::filesystem::path &project,
                            const run_store::StoredObjectReference &reference,
                            const std::string_view bytes,
                            const std::string_view code,
                            const std::string_view context) {
  const auto installed = run_store::install_object(project, reference, bytes);
  check(!installed.has_value(), context);
  if (!installed.has_value()) {
    check(installed.diagnostic().code == code,
          std::string(context) + " diagnostic code");
  }
}

void derivation_registry_and_installation() {
  StoreFixture store;
  check(run_store::sidecar_path_for_project("/work/arm.prometheus") ==
            std::filesystem::path("/work/arm.prometheus.data"),
        "sidecar is a sibling with .data appended");

  struct ContractCase final {
    std::string path;
    std::string media_type;
    std::string schema_id;
    std::string schema_version;
  };
  const std::vector<ContractCase> contracts{
      {"fixtures/contracts/execution-component-v2.motor-a.jcs",
       "application/vnd.prometheus.execution-component+json;version=2.0.0",
       "urn:prometheus:schema:execution-component:2.0.0", "2.0.0"},
      {"fixtures/contracts/program-01b/motor-arm-scenario-v1.acceptance.jcs",
       "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0",
       "urn:prometheus:schema:motor-arm-scenario:1.0.0", "1.0.0"},
      {"fixtures/contracts/program-01b/analysis-request-v1.motor-a.jcs",
       "application/vnd.prometheus.analysis-request+json;version=1.0.0",
       "urn:prometheus:schema:analysis-request:1.0.0", "1.0.0"},
      {"fixtures/contracts/program-01b/analysis-result-v1.motor-a.jcs",
       "application/vnd.prometheus.analysis-result+json;version=1.0.0",
       "urn:prometheus:schema:analysis-result:1.0.0", "1.0.0"},
      {"fixtures/contracts/program-01b/run-manifest-v1.motor-a.jcs",
       "application/vnd.prometheus.run-manifest+json;version=1.0.0",
       "urn:prometheus:schema:run-manifest:1.0.0", "1.0.0"},
  };

  for (const auto &contract : contracts) {
    const auto bytes = read_file(repository_file(contract.path));
    const auto reference =
        reference_for(bytes, contract.media_type, contract.schema_id,
                      contract.schema_version);
    const auto destination = run_store::object_path_for_hash(
        run_store::sidecar_path_for_project(store.project),
        reference.object_hash);
    check(destination.has_value(), "valid hash derives an object path");
    if (destination.has_value()) {
      const auto digest = reference.object_hash.substr(7U);
      check(destination.value().parent_path().filename() == digest.substr(0U, 2U),
            "object path uses two-character digest fan-out");
      check(destination.value().filename() == digest.substr(2U),
            "object filename uses the remaining digest");
    }

    const auto installed =
        run_store::install_object(store.project, reference, bytes);
    check(installed.has_value(), "supported immutable object installs");
    if (!installed.has_value()) {
      continue;
    }
    check(!installed.value().already_present,
          "first installation reports newly installed");
    check(std::filesystem::is_regular_file(installed.value().object_path),
          "installed destination is a regular file");
    check(!std::filesystem::exists(
              run_store::temporary_path_for_object(installed.value().object_path)),
          "temporary file is removed after installation");

    const auto loaded = run_store::read_object(store.project, reference);
    check(loaded.has_value() && loaded.value() == bytes,
          "stored object reopens with exact bytes");
    const auto repeated =
        run_store::install_object(store.project, reference, bytes);
    check(repeated.has_value() && repeated.value().already_present,
          "same-byte installation is idempotent");
  }
}

void metadata_and_collision_fail_closed() {
  const auto bytes = read_file(repository_file(
      "fixtures/contracts/execution-component-v2.motor-a.jcs"));

  {
    StoreFixture store;
    auto reference = package_reference(bytes);
    ++reference.byte_length;
    expect_install_failure(store.project, reference, bytes,
                           "object_length_mismatch",
                           "declared byte-length mismatch rejects");
  }
  {
    StoreFixture store;
    auto reference = package_reference(bytes);
    reference.media_type = "application/json";
    expect_install_failure(store.project, reference, bytes,
                           "unsupported_object_contract",
                           "unsupported media/schema pair rejects");
  }
  {
    StoreFixture store;
    auto reference = package_reference(bytes);
    reference.schema_id = "urn:prometheus:schema:analysis-result:1.0.0";
    expect_install_failure(store.project, reference, bytes,
                           "unsupported_object_contract",
                           "mismatched media/schema pair rejects");
  }
  {
    StoreFixture store;
    auto reference = package_reference(bytes);
    reference.object_hash = "sha256:" + std::string(64U, 'A');
    expect_install_failure(store.project, reference, bytes, "invalid_hash",
                           "uppercase hash rejects");
    reference.object_hash = "/absolute/object";
    expect_install_failure(store.project, reference, bytes, "invalid_hash",
                           "absolute object reference rejects");
    reference.object_hash = "sha256:../../object";
    expect_install_failure(store.project, reference, bytes, "invalid_hash",
                           "traversal object reference rejects");
  }
  {
    StoreFixture store;
    const std::string over_limit(run_store::maximum_object_bytes + 1U, 'x');
    auto reference = package_reference(bytes);
    reference.byte_length = over_limit.size();
    expect_install_failure(store.project, reference, over_limit,
                           "object_too_large", "object over 8 MiB rejects");
  }
  {
    StoreFixture store;
    const auto reference = package_reference(bytes);
    const auto installed =
        run_store::install_object(store.project, reference, bytes);
    check(installed.has_value(), "collision setup installs object");
    if (installed.has_value()) {
      write_file(installed.value().object_path, "different bytes");
      expect_install_failure(store.project, reference, bytes,
                             "object_collision",
                             "different bytes at digest path are fatal");
      check(!std::filesystem::exists(run_store::temporary_path_for_object(
                installed.value().object_path)),
            "collision leaves no temporary file");
    }
  }
}

bool create_directory_symlink_checked(const std::filesystem::path &target,
                                      const std::filesystem::path &link,
                                      const std::string_view context) {
  std::error_code error;
  std::filesystem::create_directory_symlink(target, link, error);
  check(!error, std::string(context) + " test symlink creation");
  return !error;
}

bool create_file_symlink_checked(const std::filesystem::path &target,
                                 const std::filesystem::path &link,
                                 const std::string_view context) {
  std::error_code error;
  std::filesystem::create_symlink(target, link, error);
  check(!error, std::string(context) + " test symlink creation");
  return !error;
}

void symlink_positions_fail_closed() {
  const auto bytes = read_file(repository_file(
      "fixtures/contracts/execution-component-v2.motor-a.jcs"));
  const auto reference = package_reference(bytes);
  const auto digest = reference.object_hash.substr(7U);

  {
    TemporaryRoot root;
    const auto real = root.path() / "real.prometheus";
    const auto link = root.path() / "link.prometheus";
    write_file(real, "{}\n");
    if (create_file_symlink_checked(real, link, "project")) {
      expect_install_failure(link, reference, bytes, "unsafe_project_path",
                             "symlink project rejects");
    }
  }
  {
    StoreFixture store;
    const auto decoy = store.root.path() / "decoy";
    std::filesystem::create_directory(decoy);
    if (create_directory_symlink_checked(
            decoy, run_store::sidecar_path_for_project(store.project),
            "sidecar")) {
      expect_install_failure(store.project, reference, bytes,
                             "unsafe_store_path", "symlink sidecar rejects");
    }
  }
  {
    StoreFixture store;
    const auto sidecar = run_store::sidecar_path_for_project(store.project);
    const auto decoy = store.root.path() / "decoy";
    std::filesystem::create_directories(sidecar);
    std::filesystem::create_directory(decoy);
    if (create_directory_symlink_checked(decoy, sidecar / "objects",
                                         "objects")) {
      expect_install_failure(store.project, reference, bytes,
                             "unsafe_store_path", "symlink objects rejects");
    }
  }
  {
    StoreFixture store;
    const auto sidecar = run_store::sidecar_path_for_project(store.project);
    const auto decoy = store.root.path() / "decoy";
    std::filesystem::create_directories(sidecar / "objects");
    std::filesystem::create_directory(decoy);
    if (create_directory_symlink_checked(decoy, sidecar / "objects/sha256",
                                         "sha256")) {
      expect_install_failure(store.project, reference, bytes,
                             "unsafe_store_path", "symlink sha256 rejects");
    }
  }
  {
    StoreFixture store;
    const auto sidecar = run_store::sidecar_path_for_project(store.project);
    const auto decoy = store.root.path() / "decoy";
    std::filesystem::create_directories(sidecar / "objects/sha256");
    std::filesystem::create_directory(decoy);
    if (create_directory_symlink_checked(
            decoy, sidecar / "objects/sha256" / digest.substr(0U, 2U),
            "fanout")) {
      expect_install_failure(store.project, reference, bytes,
                             "unsafe_store_path", "symlink fanout rejects");
    }
  }
  {
    StoreFixture store;
    const auto destination =
        run_store::object_path_for_hash(
            run_store::sidecar_path_for_project(store.project),
            reference.object_hash)
            .value();
    std::filesystem::create_directories(destination.parent_path());
    const auto decoy = store.root.path() / "decoy-file";
    write_file(decoy, "decoy");
    if (create_file_symlink_checked(decoy, destination, "destination")) {
      expect_install_failure(store.project, reference, bytes,
                             "unsafe_object_path",
                             "symlink object destination rejects");
    }
  }
  {
    StoreFixture store;
    const auto destination =
        run_store::object_path_for_hash(
            run_store::sidecar_path_for_project(store.project),
            reference.object_hash)
            .value();
    std::filesystem::create_directories(destination.parent_path());
    const auto decoy = store.root.path() / "decoy-file";
    write_file(decoy, "decoy");
    if (create_file_symlink_checked(
            decoy, run_store::temporary_path_for_object(destination),
            "temporary")) {
      const auto temporary = run_store::temporary_path_for_object(destination);
      expect_install_failure(store.project, reference, bytes,
                             "unsafe_temporary_path",
                             "symlink temporary file rejects");
      check(std::filesystem::is_symlink(
                std::filesystem::symlink_status(temporary)),
            "rejected pre-existing temporary symlink is not removed");
    }
  }
}

void read_path_substitution_fails_closed() {
  StoreFixture store;
  const auto bytes = read_file(repository_file(
      "fixtures/contracts/execution-component-v2.motor-a.jcs"));
  const auto reference = package_reference(bytes);
  const auto installed = run_store::install_object(store.project, reference, bytes);
  check(installed.has_value(), "read substitution setup installs object");
  if (!installed.has_value()) {
    return;
  }

  const auto sidecar = run_store::sidecar_path_for_project(store.project);
  const auto objects = sidecar / "objects";
  const auto real_objects = sidecar / "objects-real";
  std::error_code error;
  std::filesystem::rename(objects, real_objects, error);
  check(!error, "read substitution setup moves objects directory");
  if (error || !create_directory_symlink_checked(real_objects, objects,
                                                  "read objects")) {
    return;
  }
  const auto loaded = run_store::read_object(store.project, reference);
  check(!loaded.has_value(), "read rejects a symlinked objects directory");
  if (!loaded.has_value()) {
    check(loaded.diagnostic().code == "unsafe_store_path",
          "read-path substitution has an unsafe-store diagnostic");
  }
}

} // namespace

int main() {
  derivation_registry_and_installation();
  metadata_and_collision_fail_closed();
  symlink_positions_fail_closed();
  read_path_substitution_fails_closed();
  return failures == 0 ? 0 : 1;
}
