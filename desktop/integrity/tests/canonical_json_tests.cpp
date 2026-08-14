#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using prometheus::integrity::CanonicalJsonError;
using prometheus::integrity::Limits;
using prometheus::integrity::canonicalize_json_bytes;
using prometheus::integrity::object_hash;
using prometheus::integrity::sha256_bytes;
using prometheus::integrity::sha256_file;
using prometheus::integrity::verify_canonical_bytes;
using prometheus::integrity::verify_execution_component;

const fs::path repository_root{PROMETHEUS_REPOSITORY_ROOT};
const fs::path corpus_root =
    repository_root / "fixtures/conformance/rfc8785";

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    const auto prefix =
        "prometheus-integrity-tests-" +
        std::to_string(std::hash<std::string>{}(fs::current_path().string())) +
        "-";
    for (std::size_t index = 0U; index < 10000U; ++index) {
      std::error_code error;
      const auto candidate =
          fs::temp_directory_path() / (prefix + std::to_string(index));
      if (fs::create_directory(candidate, error)) {
        path_ = candidate;
        return;
      }
      if (error) {
        throw std::runtime_error("unable to create temporary test directory: " +
                                 error.message());
      }
    }
    throw std::runtime_error("unable to reserve a temporary test directory");
  }

  TemporaryDirectory(const TemporaryDirectory &) = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path &path() const noexcept { return path_; }

private:
  fs::path path_;
};

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string read_file(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("unable to open fixture: " + path.string());
  }
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string trim_ascii_whitespace(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1U);
}

unsigned int hex_digit(const char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<unsigned int>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<unsigned int>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<unsigned int>(value - 'A' + 10);
  }
  throw std::runtime_error("invalid hexadecimal fixture byte");
}

std::string decode_hex(const std::string &encoded) {
  require(encoded.size() % 2U == 0U, "hex fixture has an odd length");
  std::string decoded;
  decoded.reserve(encoded.size() / 2U);
  for (std::size_t index = 0; index < encoded.size(); index += 2U) {
    const auto byte = static_cast<char>((hex_digit(encoded[index]) << 4U) |
                                        hex_digit(encoded[index + 1U]));
    decoded.push_back(byte);
  }
  return decoded;
}

std::string repeated_array(const std::size_t count) {
  std::string result;
  result.reserve(count * 2U + 1U);
  result.push_back('[');
  for (std::size_t index = 0; index < count; ++index) {
    if (index != 0U) {
      result.push_back(',');
    }
    result.push_back('0');
  }
  result.push_back(']');
  return result;
}

std::string node_tree(std::size_t scalar_count) {
  std::string result{"["};
  bool first = true;
  while (scalar_count != 0U) {
    const auto branch_size = std::min<std::size_t>(scalar_count, 10000U);
    if (!first) {
      result.push_back(',');
    }
    result += repeated_array(branch_size);
    first = false;
    scalar_count -= branch_size;
  }
  result.push_back(']');
  return result;
}

std::string generated_input(const json &spec) {
  const auto generator = spec.at("generator").get<std::string>();
  if (generator == "nested_arrays") {
    const auto depth = spec.at("depth").get<std::size_t>();
    return std::string(depth, '[') + "0" + std::string(depth, ']');
  }
  if (generator == "node_tree") {
    return node_tree(spec.at("scalar_count").get<std::size_t>());
  }
  if (generator == "object_members") {
    const auto count = spec.at("count").get<std::size_t>();
    std::ostringstream result;
    result << '{';
    for (std::size_t index = 0; index < count; ++index) {
      if (index != 0U) {
        result << ',';
      }
      result << "\"k" << std::setfill('0') << std::setw(5) << index
             << "\":0";
    }
    result << '}';
    return result.str();
  }
  if (generator == "array_elements") {
    return repeated_array(spec.at("count").get<std::size_t>());
  }
  if (generator == "string_bytes") {
    return '"' + std::string(spec.at("count").get<std::size_t>(), 'a') +
           '"';
  }
  if (generator == "raw_bytes") {
    const auto count = spec.at("count").get<std::size_t>();
    require(count >= 2U, "raw byte generator requires at least two bytes");
    return "{}" + std::string(count - 2U, ' ');
  }
  throw std::runtime_error("unknown corpus generator: " + generator);
}

std::string case_input(const json &test_case) {
  const auto &spec = test_case.at("input");
  if (spec.is_string()) {
    return read_file(corpus_root / spec.get<std::string>());
  }
  const auto kind = spec.at("kind").get<std::string>();
  if (kind == "hex") {
    return decode_hex(spec.at("data").get<std::string>());
  }
  if (kind == "generated") {
    return generated_input(spec);
  }
  throw std::runtime_error("unknown corpus input kind: " + kind);
}

template <typename Callable>
void expect_error(Callable &&callable, const std::string &expected_code,
                  const std::string &context) {
  try {
    callable();
  } catch (const CanonicalJsonError &error) {
    require(error.code() == expected_code,
            context + ": expected " + expected_code + ", received " +
                error.code() + " (" + error.what() + ")");
    return;
  }
  throw std::runtime_error(context + ": expected CanonicalJsonError " +
                           expected_code);
}

void replace_once(std::string &source, const std::string_view before,
                  const std::string_view after) {
  require(before.size() == after.size(),
          "fixture mutation must preserve byte length");
  const auto position = source.find(before);
  require(position != std::string::npos, "fixture mutation target not found");
  require(source.find(before, position + before.size()) == std::string::npos,
          "fixture mutation target is not unique");
  source.replace(position, before.size(), after);
}

void test_shared_corpus() {
  const auto manifest =
      json::parse(read_file(corpus_root / "manifest.json"));
  const auto limits = Limits{};
  const auto &manifest_limits = manifest.at("limits");
  require(manifest_limits.at("max_raw_bytes") == limits.raw_bytes,
          "manifest raw byte limit");
  require(manifest_limits.at("max_depth") == limits.depth,
          "manifest depth limit");
  require(manifest_limits.at("max_nodes") == limits.nodes,
          "manifest node limit");
  require(manifest_limits.at("max_object_members") == limits.object_members,
          "manifest object member limit");
  require(manifest_limits.at("max_array_elements") == limits.array_elements,
          "manifest array element limit");
  require(manifest_limits.at("max_string_bytes") == limits.string_bytes,
          "manifest string byte limit");

  for (const auto &test_case : manifest.at("success_cases")) {
    const auto id = test_case.at("id").get<std::string>();
    const auto input = case_input(test_case);
    const auto expected = read_file(
        corpus_root / test_case.at("canonical").get<std::string>());
    const auto expected_hash = test_case.at("sha256").get<std::string>();

    const auto canonical = canonicalize_json_bytes(input);
    require(canonical == expected, id + " canonical bytes");
    require(verify_canonical_bytes(expected) == expected,
            id + " stored canonical verification");
    require(object_hash(expected) == expected_hash, id + " SHA-256 identity");
  }

  for (const auto &test_case : manifest.at("failure_cases")) {
    const auto id = test_case.at("id").get<std::string>();
    const auto input = case_input(test_case);
    const auto error_code = test_case.at("error_code").get<std::string>();
    expect_error([&] { static_cast<void>(canonicalize_json_bytes(input)); },
                 error_code, id);
  }
}

void test_limits_are_inclusive() {
  const auto limits = Limits{};
  require(!canonicalize_json_bytes(std::string(limits.depth, '[') + "0" +
                                   std::string(limits.depth, ']'))
               .empty(),
          "maximum depth is inclusive");
  require(!canonicalize_json_bytes(node_tree(limits.nodes - 11U)).empty(),
          "maximum node count is inclusive");
  require(!canonicalize_json_bytes(generated_input(
               {{"generator", "object_members"},
                {"count", limits.object_members}}))
               .empty(),
          "maximum object member count is inclusive");
  require(!canonicalize_json_bytes(generated_input(
               {{"generator", "array_elements"},
                {"count", limits.array_elements}}))
               .empty(),
          "maximum array element count is inclusive");
  require(!canonicalize_json_bytes(generated_input(
               {{"generator", "string_bytes"},
                {"count", limits.string_bytes}}))
               .empty(),
          "maximum string byte count is inclusive");
  require(canonicalize_json_bytes(generated_input(
              {{"generator", "raw_bytes"}, {"count", limits.raw_bytes}})) ==
              "{}",
          "maximum raw byte count is inclusive");
}

void test_policy_edges() {
  for (const std::string source : {"-0", "-0.0", "-0e0", "-0E+10"}) {
    expect_error(
        [&] { static_cast<void>(canonicalize_json_bytes(source)); },
        "negative_zero", "negative-zero spelling " + source);
  }

  expect_error(
      [] { static_cast<void>(verify_canonical_bytes("{\"b\":2, \"a\":1}")); },
      "noncanonical_bytes", "valid but noncanonical JSON");
  expect_error([] { static_cast<void>(object_hash("{}\n")); },
               "noncanonical_bytes", "hashing noncanonical JSON");
  expect_error([] { static_cast<void>(canonicalize_json_bytes("1e20")); },
               "unsafe_integer", "canonical output must remain parseable");
}

void test_raw_sha256_and_file_hashing() {
  require(sha256_bytes("") ==
              "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty SHA-256 vector");
  require(sha256_bytes("abc") ==
              "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "abc SHA-256 vector");
  require(sha256_bytes(std::string(1000000U, 'a')) ==
              "sha256:cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "million-a SHA-256 vector");

  std::string all_bytes;
  all_bytes.reserve(256U);
  for (unsigned int value = 0U; value <= 255U; ++value) {
    all_bytes.push_back(static_cast<char>(value));
  }
  require(sha256_bytes(all_bytes) ==
              "sha256:40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880",
          "all-byte SHA-256 vector");

  const TemporaryDirectory temporary_directory;
  const auto temporary =
      temporary_directory.path() / "prometheus-integrity-sha256-vector.bin";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(stream), "create temporary SHA-256 vector");
    stream.write(all_bytes.data(), static_cast<std::streamsize>(all_bytes.size()));
    require(static_cast<bool>(stream), "write temporary SHA-256 vector");
  }
  require(sha256_file(temporary) == sha256_bytes(all_bytes),
          "file and raw byte hashing agree");

  auto symlink = temporary;
  symlink += ".symlink";
  std::error_code cleanup_error;
  fs::remove(symlink, cleanup_error);
  std::error_code symlink_error;
  fs::create_symlink(temporary, symlink, symlink_error);
#ifndef _WIN32
  require(!symlink_error, "create symbolic-link rejection fixture");
#endif
  if (!symlink_error) {
    expect_error([&] { static_cast<void>(sha256_file(symlink)); },
                 "file_not_regular", "symbolic link cannot be hashed");
    fs::remove(symlink);
  }

  const std::string larger_than_json_limit(Limits{}.raw_bytes + 1U, 'x');
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(stream), "create oversized SHA-256 vector");
    stream.write(larger_than_json_limit.data(),
                 static_cast<std::streamsize>(larger_than_json_limit.size()));
    require(static_cast<bool>(stream), "write oversized SHA-256 vector");
  }
  require(sha256_file(temporary) == sha256_bytes(larger_than_json_limit),
          "file hashing must not inherit the JSON byte limit");
  fs::remove(temporary);

  expect_error(
      [&] { static_cast<void>(sha256_file(fs::temp_directory_path())); },
      "file_not_regular", "directory cannot be hashed as a file");
  expect_error(
      [&] { static_cast<void>(sha256_file(temporary)); }, "file_read_failed",
      "missing file cannot be hashed");
}

void test_qt_free_integrity_boundary() {
#ifndef PROMETHEUS_INTEGRITY_LINK_LIBRARIES
#error "integrity link libraries must be visible to the boundary test"
#endif
  const std::string link_libraries = PROMETHEUS_INTEGRITY_LINK_LIBRARIES;
  require(link_libraries.find("Qt") == std::string::npos,
          "prometheus_integrity must not link a Qt target");

  const auto source_root = repository_root / "desktop/integrity";
  const auto production = read_file(source_root / "CMakeLists.txt") +
                          read_file(source_root / "src/canonical_json.cpp") +
                          read_file(source_root /
                                    "include/prometheus/integrity/canonical_json.hpp");
  for (const std::string_view forbidden : {"QCryptographicHash", "QByteArray",
                                           "Qt6::Core"}) {
    require(production.find(forbidden) == std::string::npos,
            "Qt symbol remains under desktop/integrity: " +
                std::string(forbidden));
  }
}

void test_program_01b_assembly_fixture_identity() {
  const auto assembly =
      repository_root / "fixtures/assemblies/motor-arm.step";
  const auto assembly_hash = sha256_file(assembly);
  for (const std::string_view motor : {"motor-a", "motor-b"}) {
    const auto request = json::parse(read_file(
        repository_root / "fixtures/contracts/program-01b" /
        ("analysis-request-v1." + std::string(motor) + ".jcs")));
    require(assembly_hash ==
                request.at("assembly_artifact_hash").get<std::string>(),
            "Program 01B assembly bytes match the frozen " +
                std::string(motor) + " request identity");
  }
}

void test_complete_execution_component() {
  const auto package_path =
      repository_root / "fixtures/contracts/execution-component-v2.pm-36-gm.jcs";
  const auto hash_path = repository_root /
                         "fixtures/contracts/execution-component-v2.pm-36-gm.sha256";
  const auto package = read_file(package_path);
  const auto expected_hash = trim_ascii_whitespace(read_file(hash_path));

  require(verify_canonical_bytes(package) == package,
          "complete package canonical bytes");
  require(object_hash(package) == expected_hash, "complete package hash");
  require(verify_execution_component(package, expected_hash) == expected_hash,
          "complete package verification");

  auto flipped = package;
  replace_once(flipped,
               "\"package_kind\":\"component_execution_input\"",
               "\"package_kind\":\"component_execution_inpuu\"");
  expect_error(
      [&] {
        static_cast<void>(
            verify_execution_component(flipped, expected_hash));
      },
      "object_hash_mismatch", "byte-flipped package");

  auto unsupported_schema_id = package;
  replace_once(unsupported_schema_id,
               "\"$schema\":\"urn:prometheus:schema:execution-component:2.0.0\"",
               "\"$schema\":\"urn:prometheus:schema:execution-component:9.0.0\"");
  const auto unsupported_schema_id_hash = object_hash(unsupported_schema_id);
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            unsupported_schema_id, unsupported_schema_id_hash));
      },
      "unsupported_schema", "unsupported execution-component schema ID");

  auto unsupported_schema_version = package;
  replace_once(unsupported_schema_version,
               "\"schema_version\":\"2.0.0\"",
               "\"schema_version\":\"9.0.0\"");
  const auto unsupported_schema_version_hash =
      object_hash(unsupported_schema_version);
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            unsupported_schema_version, unsupported_schema_version_hash));
      },
      "unsupported_schema", "unsupported execution-component schema version");
}

void test_execution_component_graph_verification() {
  for (const std::string_view stem : {"execution-component-v2.motor-a",
                                      "execution-component-v2.motor-b"}) {
    const auto package_path =
        repository_root / "fixtures/contracts" / (std::string(stem) + ".jcs");
    const auto hash_path = repository_root / "fixtures/contracts" /
                           (std::string(stem) + ".sha256");
    const auto package = read_file(package_path);
    const auto expected_hash = trim_ascii_whitespace(read_file(hash_path));
    require(verify_execution_component(package, expected_hash) == expected_hash,
            std::string(stem) + " graph verification");
  }

  const auto source = read_file(
      repository_root /
      "fixtures/contracts/execution-component-v2.motor-a.jcs");
  const auto mutate = [&](const auto &mutation) {
    auto value = json::parse(source);
    mutation(value);
    return canonicalize_json_bytes(value.dump());
  };

  const auto stale_review = mutate([](json &value) {
    value["claim_reviews"][0]["reviewed_claim_fingerprint"] =
        "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            stale_review, object_hash(stale_review)));
      },
      "stale_review", "stale selected-claim review");

  const auto duplicate_slot = mutate([](json &value) {
    value["parameter_slots"][1]["name"] =
        value["parameter_slots"][0]["name"];
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            duplicate_slot, object_hash(duplicate_slot)));
      },
      "duplicate_slot_name", "duplicate package slot name");

  const auto readiness_mismatch = mutate([](json &value) {
    value["execution_readiness"] = "blocked";
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            readiness_mismatch, object_hash(readiness_mismatch)));
      },
      "readiness_gate_mismatch", "readiness and gate disagreement");

  const auto unknown_member = mutate([](json &value) {
    value["unreviewed_extension"] = true;
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            unknown_member, object_hash(unknown_member)));
      },
      "unknown_field", "closed package top-level contract");

  const auto missing_value_kind = mutate([](json &value) {
    value["claims"][0]["value"].erase("kind");
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            missing_value_kind, object_hash(missing_value_kind)));
      },
      "missing_field", "engineering value discriminator");

  const auto invalid_review_version = mutate([](json &value) {
    value["claim_reviews"][0]["applied_draft_version"] = 0;
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            invalid_review_version, object_hash(invalid_review_version)));
      },
      "invalid_review_version", "positive applied review version");

  const auto empty_satisfied_gate = mutate([](json &value) {
    value["gates"].back()["satisfying_reference_ids"] = json::array();
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            empty_satisfied_gate, object_hash(empty_satisfied_gate)));
      },
      "gate_state_metadata_mismatch", "satisfied gate reference requirement");

  const auto unordered_gate_references = mutate([](json &value) {
    auto &references = value["gates"][2]["satisfying_reference_ids"];
    std::swap(references[0], references[1]);
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            unordered_gate_references,
            object_hash(unordered_gate_references)));
      },
      "invalid_contract_order", "gate reference contract order");

  const auto unordered_artifacts = mutate([](json &value) {
    std::swap(value["artifacts"][0], value["artifacts"][1]);
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            unordered_artifacts, object_hash(unordered_artifacts)));
      },
      "invalid_contract_order", "artifact contract order");

  const auto duplicate_claim_evidence = mutate([](json &value) {
    auto &evidence_ids = value["claims"][0]["evidence_ids"];
    evidence_ids.push_back(evidence_ids[0]);
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            duplicate_claim_evidence,
            object_hash(duplicate_claim_evidence)));
      },
      "duplicate_claim_evidence", "claim evidence uniqueness");

  const auto unordered_limitations = mutate([](json &value) {
    std::swap(value["limitations"][0], value["limitations"][1]);
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            unordered_limitations, object_hash(unordered_limitations)));
      },
      "invalid_contract_order", "limitation contract order");

  const auto empty_review_note = mutate([](json &value) {
    value["claim_reviews"][0]["note"] = "";
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            empty_review_note, object_hash(empty_review_note)));
      },
      "invalid_review_note", "effective review note");

  const auto changed_claim_semantics = mutate([](json &value) {
    value["claims"][0]["original_value"] = "tampered";
  });
  expect_error(
      [&] {
        static_cast<void>(verify_execution_component(
            changed_claim_semantics, object_hash(changed_claim_semantics)));
      },
      "claim_fingerprint_mismatch", "semantic claim fingerprint");
}

} // namespace

int main() {
  try {
    test_shared_corpus();
    test_limits_are_inclusive();
    test_policy_edges();
    test_raw_sha256_and_file_hashing();
    test_qt_free_integrity_boundary();
    test_program_01b_assembly_fixture_identity();
    test_complete_execution_component();
    test_execution_component_graph_verification();
    std::cout << "All independent canonical JSON tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Independent canonical JSON test failed: " << error.what()
              << '\n';
    return 1;
  }
}
