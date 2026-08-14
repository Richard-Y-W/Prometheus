#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

namespace {

namespace fs = std::filesystem;
namespace integrity = prometheus::integrity;
namespace run_store = prometheus::run_store;
using namespace std::chrono_literals;

std::string read_file(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void write_file(const fs::path &path, const std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 6) {
    return 2;
  }
  const std::string mode = argv[1];
  const fs::path project = argv[2];
  const fs::path ready = argv[3];
  const fs::path release = argv[4];
  const std::string entity = argv[5];
  if (mode != "hold" && mode != "crash") {
    return 2;
  }

  const auto bytes = read_file(
      fs::path(PROMETHEUS_REPOSITORY_ROOT) /
      "fixtures/contracts/execution-component-v2.motor-a.jcs");
  if (bytes.empty()) {
    return 3;
  }
  const run_store::StoredObjectReference reference{
      integrity::object_hash(bytes),
      static_cast<std::uint64_t>(bytes.size()),
      "application/vnd.prometheus.execution-component+json;version=2.0.0",
      "urn:prometheus:schema:execution-component:2.0.0", "2.0.0"};

  run_store::TransactionOptions options;
  options.boundary_hook = [&](const auto boundary) {
    if (boundary !=
        run_store::TransactionBoundary::before_project_replacement) {
      return false;
    }
    write_file(ready, "ready\n");
    if (mode == "crash") {
      std::_Exit(0);
    }
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (!fs::exists(release) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(10ms);
    }
    return false;
  };
  const auto updated = run_store::install_package_binding(
      project, entity, reference, bytes, options);
  return updated.has_value() ? 0 : 4;
}
