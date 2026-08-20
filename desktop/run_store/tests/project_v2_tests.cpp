#include <prometheus/run_store/project_v2.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace {

using Json = nlohmann::json;
namespace run_store = prometheus::run_store;

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

std::filesystem::path fixture(const std::string_view relative) {
  return std::filesystem::path(PROMETHEUS_REPOSITORY_ROOT) /
         "fixtures/contracts/project-v2" / relative;
}

Json valid_project() {
  return Json::parse(read_file(fixture("valid.minimal.json")));
}

void expect_rejected(const std::string &bytes, const std::string_view code,
                     const std::string_view context) {
  const auto parsed = run_store::parse_project_v2(bytes);
  check(!parsed.has_value(), context);
  if (!parsed.has_value()) {
    check(parsed.diagnostic().code == code,
          std::string(context) + " diagnostic code");
  }
}

Json package_reference(const char digit) {
  return Json{{"object_hash",
               "sha256:" + std::string(64U, digit)},
              {"byte_length", 1},
              {"media_type",
               "application/vnd.prometheus.execution-component+json;version=2.0.0"},
              {"schema_id",
               "urn:prometheus:schema:execution-component:2.0.0"},
              {"schema_version", "2.0.0"}};
}

Json scenario_reference(const char digit) {
  return Json{{"object_hash", "sha256:" + std::string(64U, digit)},
              {"byte_length", 1},
              {"media_type",
               "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0"},
              {"schema_id",
               "urn:prometheus:schema:motor-arm-scenario:1.0.0"},
              {"schema_version", "1.0.0"}};
}

Json manifest_reference(const char digit) {
  return Json{{"object_hash", "sha256:" + std::string(64U, digit)},
              {"byte_length", 1},
              {"media_type",
               "application/vnd.prometheus.run-manifest+json;version=1.0.0"},
              {"schema_id", "urn:prometheus:schema:run-manifest:1.0.0"},
              {"schema_version", "1.0.0"}};
}

Json event(const std::uint64_t sequence) {
  return Json{{"sequence", sequence},
              {"event_kind", "run_invoked"},
              {"status", "completed"},
              {"related_hash", nullptr},
              {"occurred_at_utc", "2026-08-12T12:00:00Z"},
              {"diagnostic_code", "none"}};
}

void valid_round_trip_is_deterministic() {
  const std::string link_libraries = PROMETHEUS_RUN_STORE_LINK_LIBRARIES;
  check(link_libraries.find("Qt") == std::string::npos &&
            link_libraries.find("Python") == std::string::npos &&
            link_libraries.find("Network") == std::string::npos &&
            link_libraries.find("prometheus_cad") == std::string::npos &&
            link_libraries.find("prometheus_execution") == std::string::npos &&
            link_libraries.find("prometheus_core") == std::string::npos,
        "run store has no Qt, Python, network, CAD, or physics dependency");

  const auto bytes = read_file(fixture("valid.minimal.json"));
  const auto parsed = run_store::parse_project_v2(bytes);
  check(parsed.has_value(), "valid shared fixture parses");
  if (!parsed.has_value()) {
    return;
  }
  check(parsed.value().name == "Motor arm", "project name is typed");
  check(parsed.value().execution.package_bindings.size() == 1U,
        "package binding is retained");
  check(parsed.value().execution.committed_runs.empty(),
        "empty run history is retained");

  const auto serialized = run_store::serialize_project_v2(parsed.value());
  check(serialized.has_value(), "valid project serializes");
  if (!serialized.has_value()) {
    return;
  }
  check(serialized.value().ends_with('\n'), "writer emits one final newline");
  check(!serialized.value().ends_with("\n\n"),
        "writer emits only one final newline");
  check(serialized.value().starts_with(
            "{\n  \"$schema\": \"urn:prometheus:schema:project:2.0.0\",\n"
            "  \"assembly_artifact_hash\":"),
        "writer sorts object keys and uses two-space indentation");

  const auto reopened = run_store::parse_project_v2(serialized.value());
  check(reopened.has_value(), "writer output reopens");
  if (reopened.has_value()) {
    const auto second = run_store::serialize_project_v2(reopened.value());
    check(second.has_value() && second.value() == serialized.value(),
          "second serialization is byte deterministic");
  }

  const auto compact = valid_project().dump();
  check(run_store::parse_project_v2(" \n\t" + compact + "\r\n").has_value(),
        "reader accepts noncanonical whitespace");

  auto history = valid_project();
  history["execution"]["current_scenario"] = scenario_reference('c');
  history["execution"]["committed_runs"].push_back(manifest_reference('d'));
  history["execution"]["events"].push_back(event(41U));
  const auto history_parsed = run_store::parse_project_v2(history.dump());
  check(history_parsed.has_value(),
        "full scenario, manifest, and retained event references parse");
  if (history_parsed.has_value()) {
    check(history_parsed.value().execution.current_scenario.has_value() &&
              history_parsed.value().execution.committed_runs.size() == 1U &&
              history_parsed.value().execution.events.front().sequence == 41U,
          "typed execution history is retained without renumbering");
  }
}

void syntax_and_identity_fail_closed() {
  const auto source = read_file(fixture("valid.minimal.json"));
  const auto duplicate = source.substr(0U, source.find('\n') + 1U) +
                         "  \"name\": \"duplicate\",\n" +
                         source.substr(source.find('\n') + 1U);
  expect_rejected(duplicate, "duplicate_key", "duplicate key rejects");

  auto invalid_utf8 = source;
  const auto name = invalid_utf8.find("Motor arm");
  invalid_utf8.replace(name, std::string("Motor arm").size(),
                       std::string("bad") + static_cast<char>(0xC3));
  expect_rejected(invalid_utf8, "invalid_utf8", "invalid UTF-8 rejects");

  auto unsafe = valid_project();
  unsafe["execution"]["package_bindings"][0]["binding_revision"] =
      9007199254740992ULL;
  expect_rejected(unsafe.dump(), "unsafe_integer", "unsafe integer rejects");

  auto bad_hash = valid_project();
  bad_hash["assembly_artifact_hash"] = "sha256:ABC";
  expect_rejected(bad_hash.dump(), "invalid_hash", "bad hash rejects");

  expect_rejected(read_file(fixture("invalid/unknown-top-level.json")),
                  "unknown_field", "unknown top-level field rejects");
  expect_rejected(read_file(fixture("invalid/bare-scenario-hash.json")),
                  "invalid_type", "bare scenario hash rejects");
  expect_rejected(read_file(fixture("invalid/motor-finding-in-geometry.json")),
                  "invalid_geometry_finding_kind",
                  "motor finding cannot enter geometry state");

  expect_rejected(std::string(run_store::maximum_project_bytes + 1U, ' '),
                  "max_raw_bytes_exceeded", "project over 8 MiB rejects");
}

void binding_revision_graph_is_strict() {
  auto duplicate = valid_project();
  duplicate["execution"]["package_bindings"].push_back(
      Json{{"binding_revision", 1},
           {"supersedes_binding_revision", 1},
           {"cad_entity_id", "motor"},
           {"package", package_reference('c')}});
  expect_rejected(duplicate.dump(), "binding_revision_order_invalid",
                  "duplicate binding revision rejects");

  auto gap = valid_project();
  gap["execution"]["package_bindings"].push_back(
      Json{{"binding_revision", 3},
           {"supersedes_binding_revision", 1},
           {"cad_entity_id", "motor"},
           {"package", package_reference('c')}});
  expect_rejected(gap.dump(), "binding_revision_order_invalid",
                  "non-contiguous binding revision rejects");

  auto cross_entity = valid_project();
  cross_entity["execution"]["package_bindings"].push_back(
      Json{{"binding_revision", 2},
           {"supersedes_binding_revision", 1},
           {"cad_entity_id", "driver"},
           {"package", package_reference('c')}});
  expect_rejected(cross_entity.dump(), "binding_supersession_cross_entity",
                  "cross-entity supersession rejects");

  auto two_active = valid_project();
  two_active["execution"]["package_bindings"].push_back(
      Json{{"binding_revision", 2},
           {"supersedes_binding_revision", nullptr},
           {"cad_entity_id", "motor"},
           {"package", package_reference('c')}});
  expect_rejected(two_active.dump(), "multiple_active_bindings",
                  "multiple unsuperseded bindings reject");

  auto valid_chain = valid_project();
  valid_chain["execution"]["package_bindings"].push_back(
      Json{{"binding_revision", 2},
           {"supersedes_binding_revision", 1},
           {"cad_entity_id", "motor"},
           {"package", package_reference('c')}});
  check(run_store::parse_project_v2(valid_chain.dump()).has_value(),
        "same-entity supersession chain parses");
}

Json joint_binding(const std::uint64_t revision,
                   const std::optional<std::uint64_t> supersedes,
                   const std::string &source, const std::string &target) {
  return Json{{"binding_revision", revision},
              {"supersedes_binding_revision",
               supersedes.has_value() ? Json(*supersedes) : Json(nullptr)},
              {"source_cad_entity_id", source},
              {"target_cad_entity_id", target},
              {"type", "revolute"},
              {"axis", "Z"},
              {"minimum_deg", 0},
              {"maximum_deg", 90},
              {"pivot_x", 0},
              {"pivot_y", 0},
              {"pivot_z", 0}};
}

void joint_binding_revision_graph_is_strict() {
  auto duplicate = valid_project();
  duplicate["execution"]["joint_bindings"].push_back(
      joint_binding(1, std::nullopt, "arm", "base"));
  duplicate["execution"]["joint_bindings"].push_back(
      joint_binding(1, 1, "arm", "base"));
  expect_rejected(duplicate.dump(), "joint_binding_revision_order_invalid",
                  "duplicate joint binding revision rejects");

  auto gap = valid_project();
  gap["execution"]["joint_bindings"].push_back(
      joint_binding(1, std::nullopt, "arm", "base"));
  gap["execution"]["joint_bindings"].push_back(
      joint_binding(3, 1, "arm", "base"));
  expect_rejected(gap.dump(), "joint_binding_revision_order_invalid",
                  "non-contiguous joint binding revision rejects");

  auto valid_first = valid_project();
  valid_first["execution"]["joint_bindings"].push_back(
      joint_binding(1, std::nullopt, "arm", "base"));
  check(run_store::parse_project_v2(valid_first.dump()).has_value(),
        "first joint binding parses");

  auto cross_pair = valid_project();
  cross_pair["execution"]["joint_bindings"].push_back(
      joint_binding(1, std::nullopt, "arm", "base"));
  cross_pair["execution"]["joint_bindings"].push_back(
      joint_binding(2, 1, "arm", "driver"));
  expect_rejected(cross_pair.dump(), "joint_binding_supersession_cross_pair",
                  "cross-pair joint supersession rejects");

  auto two_active = valid_project();
  two_active["execution"]["joint_bindings"].push_back(
      joint_binding(1, std::nullopt, "arm", "base"));
  two_active["execution"]["joint_bindings"].push_back(
      joint_binding(2, std::nullopt, "arm", "base"));
  expect_rejected(two_active.dump(), "multiple_active_joint_bindings",
                  "multiple unsuperseded joint bindings reject");

  auto valid_chain = valid_project();
  valid_chain["execution"]["joint_bindings"].push_back(
      joint_binding(1, std::nullopt, "arm", "base"));
  valid_chain["execution"]["joint_bindings"].push_back(
      joint_binding(2, 1, "base", "arm"));
  check(run_store::parse_project_v2(valid_chain.dump()).has_value(),
        "supersession keys on the unordered entity pair, so a swapped "
        "source/target still supersedes the same chain");

  auto self_joint = valid_project();
  self_joint["execution"]["joint_bindings"].push_back(
      joint_binding(1, std::nullopt, "arm", "arm"));
  expect_rejected(self_joint.dump(), "invalid_joint_binding_entities",
                  "joint binding endpoints must differ");
}

void collection_and_event_bounds_are_strict() {
  auto events = valid_project();
  for (std::uint64_t sequence = 1U; sequence <= 257U; ++sequence) {
    events["execution"]["events"].push_back(event(sequence));
  }
  expect_rejected(events.dump(), "event_limit_exceeded",
                  "more than 256 events rejects");

  auto sequence = valid_project();
  sequence["execution"]["events"].push_back(event(1U));
  sequence["execution"]["events"].push_back(event(3U));
  expect_rejected(sequence.dump(), "event_sequence_invalid",
                  "non-contiguous event sequence rejects");

  auto bindings = valid_project();
  bindings["execution"]["package_bindings"] = Json::array();
  for (std::uint64_t revision = 1U;
       revision <= run_store::maximum_package_bindings + 1U; ++revision) {
    bindings["execution"]["package_bindings"].push_back(
        Json{{"binding_revision", revision},
             {"supersedes_binding_revision", nullptr},
             {"cad_entity_id", "entity-" + std::to_string(revision)},
             {"package", package_reference('d')}});
  }
  expect_rejected(bindings.dump(), "max_array_elements_exceeded",
                  "over-limit package-binding array rejects before parse");
}

} // namespace

int main() {
  valid_round_trip_is_deterministic();
  syntax_and_identity_fail_closed();
  binding_revision_graph_is_strict();
  joint_binding_revision_graph_is_strict();
  collection_and_event_bounds_are_strict();
  return failures == 0 ? 0 : 1;
}
