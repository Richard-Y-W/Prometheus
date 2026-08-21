#include "prometheus/structural/reviewed_pair.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ps = prometheus::structural;
namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string_view message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const std::string_view message) {
  if (!condition) fail(message);
}

std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("test fixture could not be read");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void write(const fs::path &path, const std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) throw std::runtime_error("test fixture could not be written");
}

class TemporaryFixture final {
public:
  explicit TemporaryFixture(const fs::path &source) {
    std::mt19937_64 random{std::random_device{}()};
    root_ = fs::temp_directory_path() /
            ("prometheus-reviewed-pair-tests-" + std::to_string(random()));
    fs::copy(source, root_, fs::copy_options::recursive);
  }

  ~TemporaryFixture() {
    std::error_code ignored;
    fs::remove_all(root_, ignored);
  }

  [[nodiscard]] fs::path manifest() const {
    return root_ / "reviewed-pair.json";
  }

private:
  fs::path root_;
};

template <typename Mutation>
void expectManifestError(const fs::path &fixture, Mutation mutate,
                         const std::string_view code,
                         const std::string_view message) {
  TemporaryFixture temporary(fixture);
  auto document = nlohmann::json::parse(read(temporary.manifest()));
  mutate(document);
  write(temporary.manifest(), document.dump(2) + "\n");
  try {
    static_cast<void>(
        ps::preflight_reviewed_structural_pair(temporary.manifest()));
  } catch (const std::exception &error) {
    require(std::string_view(error.what()).find(code) != std::string_view::npos,
            message);
    return;
  }
  fail(message);
}

double forceComponent(const ps::CompiledStructuralSetup &setup,
                      const std::size_t axis) {
  double result = 0.0;
  for (const auto &force : setup.request.nodal_forces)
    result += force.force_n.at(axis);
  return result;
}

void testValidSmokePair(const fs::path &fixture) {
  const auto pair = ps::preflight_reviewed_structural_pair(
      fixture / "reviewed-pair.json");
  require(pair.manifest_identity.starts_with("sha256:") &&
              pair.manifest_identity.size() == 71U,
          "manifest receives a strict semantic identity");
  require(pair.coarse_setup.request.nodes.size() == 4U,
          "coarse setup compiles");
  require(pair.fine_setup.request.elements.size() == 6U,
          "fine setup compiles");
  require(pair.coarse_setup.request.nodal_forces.size() > 0U,
          "coarse reviewed force compiles");
  require(pair.fine_setup.request.nodal_forces.size() > 0U,
          "fine reviewed force compiles");
  require(std::abs(forceComponent(pair.coarse_setup, 0U)) < 1e-12 &&
              std::abs(forceComponent(pair.coarse_setup, 1U) + 100.0) <
                  1e-10 &&
              std::abs(forceComponent(pair.coarse_setup, 2U)) < 1e-12,
          "compiled coarse nodal forces retain the reviewed resultant");
  require(pair.criterion.observables().size() == 2U,
          "global displacement and stress observables are locked");
  require(std::abs(pair.criterion.maximum_change_fraction() - 0.1) < 1e-15,
          "refinement threshold is retained");
  require(pair.coarse_job_name != pair.fine_job_name,
          "pair jobs are distinct");
  require(pair.coarse_setup.request.displacement_limit_m == 0.0005,
          "informational displacement threshold compiles");
  require(!pair.coarse_setup.request.von_mises_limit_pa,
          "no stress allowable is invented");
}

void testStrictManifestFailures(const fs::path &fixture) {
  expectManifestError(
      fixture,
      [](auto &document) { document["unexpected"] = true; },
      "reviewed_pair_contract_invalid",
      "unknown top-level members fail closed");
  expectManifestError(
      fixture,
      [](auto &document) {
        document["samples"]["coarse"]["mesh"]["sha256"] =
            "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
      },
      "reviewed_pair_artifact_hash_mismatch",
      "changed mesh bytes fail before setup compilation");
  expectManifestError(
      fixture,
      [](auto &document) {
        document["material"]["youngs_modulus_pa"] = 71000000000.0;
      },
      "reviewed_pair_material_candidate_mismatch",
      "material values must match the selected evidence candidate");
  expectManifestError(
      fixture,
      [](auto &document) {
        document["samples"]["coarse"]["load_selection"]
                ["expected_area_m2"] = 0.25;
      },
      "reviewed_pair_patch_drift",
      "changed reviewed patch geometry fails closed");
  expectManifestError(
      fixture,
      [](auto &document) {
        document["samples"]["coarse"]["load_selection"]["patch_ids"] =
            nlohmann::json::array({99});
      },
      "reviewed_pair_patch_invalid",
      "an absent patch ID fails closed");
  expectManifestError(
      fixture,
      [](auto &document) {
        auto &coarse = document["samples"]["coarse"];
        auto &fine = document["samples"]["fine"];
        fine["mesh"] = coarse["mesh"];
        fine["mesh"]["minimum_size_m"] = 0.0005;
        fine["mesh"]["maximum_size_m"] = 0.0015;
        fine["mesh"]["target_size_m"] = 0.001;
        fine["load_selection"] = coarse["load_selection"];
        fine["restraint_selection"] = coarse["restraint_selection"];
      },
      "reviewed_pair_meshes_not_distinct",
      "coarse and fine must be different immutable meshes");
  expectManifestError(
      fixture,
      [](auto &document) {
        document["samples"]["fine"]["mesh"]["maximum_size_m"] = 0.003;
        document["samples"]["fine"]["mesh"]["target_size_m"] = 0.002;
      },
      "reviewed_pair_refinement_order_invalid",
      "fine target size must be smaller");
}

void testDuplicateJsonMemberFails(const fs::path &fixture) {
  TemporaryFixture temporary(fixture);
  auto bytes = read(temporary.manifest());
  bytes.insert(bytes.find('{') + 1U,
               "\n  \"schema_version\": \"1.0.0\",");
  write(temporary.manifest(), bytes);
  try {
    static_cast<void>(
        ps::preflight_reviewed_structural_pair(temporary.manifest()));
  } catch (const std::exception &error) {
    require(std::string_view(error.what()).find(
                "reviewed_pair_json_invalid") != std::string_view::npos,
            "duplicate JSON members use the stable parse error");
    return;
  }
  fail("duplicate JSON members fail closed");
}

} // namespace

int main() {
  const auto fixture = fs::path(PROMETHEUS_REPOSITORY_ROOT) /
                       "fixtures/structural/reviewed-pair-smoke";
  testValidSmokePair(fixture);
  testStrictManifestFailures(fixture);
  testDuplicateJsonMemberFails(fixture);
  std::cout << "reviewed structural pair tests passed\n";
  return 0;
}
