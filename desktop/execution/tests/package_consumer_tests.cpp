#include "test_support.hpp"

#include <prometheus/execution/package_consumer.hpp>
#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
using prometheus::execution::ClaimBinding;
using prometheus::execution::MotorComponentInput;
using prometheus::execution::consume_motor_component;
using prometheus::execution::inspect_execution_component;
using prometheus::execution::test::require;
using prometheus::execution::test::require_failure;
using prometheus::execution::test::require_success;
using prometheus::integrity::canonicalize_json_bytes;
using prometheus::integrity::object_hash;
using prometheus::integrity::sha256_bytes;

const std::string repository_root{PROMETHEUS_REPOSITORY_ROOT};
const std::string fixture_root = repository_root + "/fixtures/contracts/";

struct PackageBytes final {
  std::string bytes;
  std::string object_hash;
};

std::string read_file(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("unable to open fixture: " + path);
  }
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string trim_ascii(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1U);
}

PackageBytes load_package(const std::string &stem) {
  return {read_file(fixture_root + stem + ".jcs"),
          trim_ascii(read_file(fixture_root + stem + ".sha256"))};
}

PackageBytes canonical_package(Json value) {
  auto bytes = canonicalize_json_bytes(value.dump());
  return {bytes, object_hash(bytes)};
}

Json &claim_by_slot(Json &package, const std::string_view slot_name) {
  const auto slot = std::find_if(
      package["parameter_slots"].begin(), package["parameter_slots"].end(),
      [&](const Json &candidate) {
        return candidate.at("name").get<std::string>() ==
               std::string(slot_name);
      });
  require(slot != package["parameter_slots"].end(),
          "test fixture slot must exist");
  const auto claim_id = slot->at("selected_claim_id").get<std::string>();
  const auto claim = std::find_if(
      package["claims"].begin(), package["claims"].end(),
      [&](const Json &candidate) { return candidate.at("claim_id") == claim_id; });
  require(claim != package["claims"].end(), "test fixture claim must exist");
  return *claim;
}

void refresh_claim_fingerprint(Json &package, const std::string_view slot_name) {
  auto &claim = claim_by_slot(package, slot_name);
  auto evidence_ids = claim.at("evidence_ids").get<std::vector<std::string>>();
  std::sort(evidence_ids.begin(), evidence_ids.end());
  Json semantic = {
      {"revision_id", claim.at("revision_id")},
      {"slot_id", claim.at("slot_id")},
      {"value_state", claim.at("value_state")},
      {"value", claim.at("value")},
      {"provenance", claim.at("provenance")},
      {"evidence_ids", evidence_ids},
      {"validity_conditions", claim.at("validity_conditions")},
  };
  if (claim.at("value_state") == "known") {
    semantic["unit"] = claim.at("unit");
    semantic["original_value"] = claim.at("original_value");
    semantic["original_unit"] = claim.at("original_unit");
  }
  const auto fingerprint = object_hash(canonicalize_json_bytes(semantic.dump()));
  claim["claim_fingerprint"] = fingerprint;
  const auto claim_id = claim.at("claim_id");
  const auto review = std::find_if(
      package["claim_reviews"].begin(), package["claim_reviews"].end(),
      [&](const Json &candidate) { return candidate.at("claim_id") == claim_id; });
  require(review != package["claim_reviews"].end(),
          "test fixture review must exist");
  (*review)["reviewed_claim_fingerprint"] = fingerprint;
}

void sort_graph(Json &package) {
  auto &slots = package["parameter_slots"];
  std::sort(slots.begin(), slots.end(), [](const Json &left, const Json &right) {
    return std::pair(left.at("name").get<std::string>(),
                     left.at("slot_id").get<std::string>()) <
           std::pair(right.at("name").get<std::string>(),
                     right.at("slot_id").get<std::string>());
  });
  auto &claims = package["claims"];
  std::sort(claims.begin(), claims.end(), [](const Json &left, const Json &right) {
    return left.at("claim_id").get<std::string>() <
           right.at("claim_id").get<std::string>();
  });
  auto &reviews = package["claim_reviews"];
  std::sort(reviews.begin(), reviews.end(),
            [](const Json &left, const Json &right) {
              return left.at("review_event_id").get<std::string>() <
                     right.at("review_event_id").get<std::string>();
            });
}

void remove_slot_graph(Json &package, const std::string_view slot_name) {
  auto &slots = package["parameter_slots"];
  const auto slot = std::find_if(
      slots.begin(), slots.end(), [&](const Json &candidate) {
        return candidate.at("name").get<std::string>() ==
               std::string(slot_name);
      });
  require(slot != slots.end(), "remove fixture slot must exist");
  const auto claim_id = slot->at("selected_claim_id").get<std::string>();
  slots.erase(slot);

  auto &claims = package["claims"];
  const auto claim = std::find_if(
      claims.begin(), claims.end(), [&](const Json &candidate) {
        return candidate.at("claim_id") == claim_id;
      });
  require(claim != claims.end(), "remove fixture claim must exist");
  claims.erase(claim);

  auto &reviews = package["claim_reviews"];
  const auto review = std::find_if(
      reviews.begin(), reviews.end(), [&](const Json &candidate) {
        return candidate.at("claim_id") == claim_id;
      });
  require(review != reviews.end(), "remove fixture review must exist");
  const auto review_id = review->at("review_event_id").get<std::string>();
  reviews.erase(review);

  for (auto &gate : package["gates"]) {
    auto &references = gate["satisfying_reference_ids"];
    references.erase(
        std::remove(references.begin(), references.end(), Json(claim_id)),
        references.end());
    references.erase(
        std::remove(references.begin(), references.end(), Json(review_id)),
        references.end());
  }
}

void add_optional_slot_graph(Json &package, const bool required) {
  const auto original_slot = std::find_if(
      package["parameter_slots"].begin(), package["parameter_slots"].end(),
      [](const Json &candidate) {
        return candidate.at("name") == "gearbox_lifetime";
      });
  require(original_slot != package["parameter_slots"].end(),
          "optional source slot must exist");
  auto slot = *original_slot;
  slot["name"] = "optional_service_note";
  slot["quantity"] = "service_note";
  slot["dimension"] = "dimensionless";
  slot["required_for_execution"] = required;
  slot["slot_id"] = "13000000-0000-4000-8000-000000000099";
  slot["selected_claim_id"] = "14000000-0000-4000-8000-000000000099";
  package["parameter_slots"].push_back(slot);

  auto claim = claim_by_slot(package, "gearbox_lifetime");
  claim["claim_id"] = "14000000-0000-4000-8000-000000000099";
  claim["slot_id"] = "13000000-0000-4000-8000-000000000099";
  package["claims"].push_back(claim);

  const auto original_review = std::find_if(
      package["claim_reviews"].begin(), package["claim_reviews"].end(),
      [&](const Json &candidate) {
        return candidate.at("claim_id") ==
               claim_by_slot(package, "gearbox_lifetime").at("claim_id");
      });
  require(original_review != package["claim_reviews"].end(),
          "optional source review must exist");
  auto review = *original_review;
  review["claim_id"] = "14000000-0000-4000-8000-000000000099";
  review["review_event_id"] = "16000000-0000-4000-8000-000000000099";
  package["claim_reviews"].push_back(review);
  refresh_claim_fingerprint(package, "optional_service_note");
  sort_graph(package);
}

template <typename Mutation>
PackageBytes mutate(const PackageBytes &source, Mutation mutation) {
  auto value = Json::parse(source.bytes);
  mutation(value);
  return canonical_package(std::move(value));
}

void expect_consumer_failure(const PackageBytes &package,
                             const std::string &stage,
                             const std::string &code) {
  require_failure(consume_motor_component(package.bytes, package.object_hash),
                  stage, code);
}

const ClaimBinding &binding_named(const std::vector<ClaimBinding> &bindings,
                                  const std::string_view name) {
  const auto binding =
      std::find_if(bindings.begin(), bindings.end(), [&](const auto &candidate) {
        return candidate.slot_name == name;
      });
  require(binding != bindings.end(), "expected provenance binding is absent");
  return *binding;
}

template <std::size_t Size>
const ClaimBinding &binding_named(const std::array<ClaimBinding, Size> &bindings,
                                  const std::string_view name) {
  const auto binding =
      std::find_if(bindings.begin(), bindings.end(), [&](const auto &candidate) {
        return candidate.slot_name == name;
      });
  require(binding != bindings.end(), "expected provenance binding is absent");
  return *binding;
}

void test_motor_a_b_and_blocked_inspection() {
  const auto motor_a = load_package("execution-component-v2.motor-a");
  const auto motor_b = load_package("execution-component-v2.motor-b");
  const auto blocked = load_package("execution-component-v2.pm-36-gm");

  const auto &a = require_success(
      consume_motor_component(motor_a.bytes, motor_a.object_hash),
      "consume Motor A");
  const auto &b = require_success(
      consume_motor_component(motor_b.bytes, motor_b.object_hash),
      "consume Motor B");
  require(a.package.package_hash == motor_a.object_hash &&
              b.package.package_hash == motor_b.object_hash,
          "typed inputs retain exact package identity");
  require(a.continuous_torque_nm == 0.208 &&
              b.continuous_torque_nm == 0.320,
          "A/B continuous torque values are distinct");
  require(a.gear_ratio == b.gear_ratio &&
              a.gearbox_efficiency_nominal ==
                  b.gearbox_efficiency_nominal &&
              a.stall_torque_nm == b.stall_torque_nm &&
              a.no_load_speed_rad_s == b.no_load_speed_rad_s &&
              a.no_load_current_a == b.no_load_current_a &&
              a.torque_constant_nm_a == b.torque_constant_nm_a &&
              a.driver_current_limit_a == b.driver_current_limit_a &&
              a.winding_resistance_ohm == b.winding_resistance_ohm &&
              a.thermal_resistance_k_w == b.thermal_resistance_k_w &&
              a.thermal_capacitance_j_k == b.thermal_capacitance_j_k &&
              a.maximum_temperature_c == b.maximum_temperature_c &&
              a.gearbox_efficiency_minimum ==
                  b.gearbox_efficiency_minimum &&
              a.gearbox_efficiency_maximum ==
                  b.gearbox_efficiency_maximum &&
              a.torque_speed_curve == b.torque_speed_curve,
          "A/B normalized engineering inputs differ only in continuous torque");
  require(a.calculation_inputs.size() == 12U &&
              a.validation_inputs.size() == 2U &&
              a.available_but_unused.size() == 3U,
          "input provenance is classified by consumer use");
  const auto &lifetime =
      binding_named(a.available_but_unused, "gearbox_lifetime");
  require(!lifetime.value_known && lifetime.unknown_reason.has_value(),
          "optional unknown lifetime remains visible and nonblocking");

  const auto &inspection = require_success(
      inspect_execution_component(blocked.bytes, blocked.object_hash),
      "inspect blocked Program 01A package");
  require(inspection.execution_readiness == "blocked" &&
              inspection.capability_id == "component_input.pm_36_gm",
          "blocked package remains inspectable");
  require_failure(consume_motor_component(blocked.bytes, blocked.object_hash),
                  "package_consumer", "package_not_ready");
}

void test_integrity_and_top_level_contract_mutations() {
  const auto source = load_package("execution-component-v2.motor-a");
  auto wrong_hash = source.object_hash;
  wrong_hash.back() = wrong_hash.back() == '0' ? '1' : '0';
  require_failure(consume_motor_component(source.bytes, wrong_hash),
                  "package_integrity", "object_hash_mismatch");

  const PackageBytes noncanonical{
      read_file(fixture_root + "execution-component-v2.motor-a.json"),
      source.object_hash};
  expect_consumer_failure(noncanonical, "package_integrity",
                          "noncanonical_bytes");

  expect_consumer_failure(
      mutate(source, [](Json &value) { value["$schema"] = "unsupported"; }),
      "package_integrity", "unsupported_schema");
  expect_consumer_failure(
      mutate(source,
             [](Json &value) { value["schema_version"] = "3.0.0"; }),
      "package_integrity", "unsupported_schema");
  expect_consumer_failure(
      mutate(source, [](Json &value) { value["package_kind"] = "report"; }),
      "package_integrity", "unsupported_package_kind");

  const auto unsupported_capability = mutate(source, [](Json &value) {
    value["capability_id"] = "component_input.other";
    for (auto &gate : value["gates"]) {
      gate["capability_id"] = "component_input.other";
    }
  });
  expect_consumer_failure(unsupported_capability, "package_consumer",
                          "unsupported_capability");

  for (const auto field : {"authority_role", "engineering_decision_authority",
                           "package_role"}) {
    expect_consumer_failure(
        mutate(source, [&](Json &value) { value["authority"][field] = "other"; }),
        "package_integrity", "invalid_authority");
  }
  expect_consumer_failure(
      mutate(source,
             [](Json &value) { value["execution_readiness"] = "unknown"; }),
      "package_integrity", "invalid_execution_readiness");
  expect_consumer_failure(
      mutate(source, [](Json &value) {
        value["component"].erase("component_id");
      }),
      "package_integrity", "missing_field");
}

void test_gate_and_consumer_artifact_mutations() {
  const auto source = load_package("execution-component-v2.motor-a");
  const auto execution_gate = [](Json &value) -> Json & {
    const auto gate = std::find_if(
        value["gates"].begin(), value["gates"].end(), [](const Json &candidate) {
          return candidate.at("required_review_type") == "package_consumer";
        });
    require(gate != value["gates"].end(), "consumer gate must exist");
    return *gate;
  };
  const auto consumer_artifact = [](Json &value) -> Json & {
    const auto artifact = std::find_if(
        value["artifacts"].begin(), value["artifacts"].end(),
        [](const Json &candidate) {
          return candidate.at("filename") ==
                 "package-consumer.motor-arm-builtin-v1.jcs";
        });
    require(artifact != value["artifacts"].end(),
            "consumer artifact must exist");
    return *artifact;
  };

  expect_consumer_failure(
      mutate(source, [&](Json &value) {
        execution_gate(value)["phase"] = "deployment";
      }),
      "package_integrity", "invalid_gate_phase");
  expect_consumer_failure(
      mutate(source, [&](Json &value) {
        execution_gate(value)["state"] = "pending";
      }),
      "package_integrity", "unresolved_gate");

  const auto known_wrong_reference = mutate(source, [&](Json &value) {
    execution_gate(value)["satisfying_reference_ids"] =
        Json::array({value["artifacts"][0]["artifact_hash"]});
  });
  expect_consumer_failure(known_wrong_reference, "package_consumer",
                          "consumer_gate_reference_mismatch");

  expect_consumer_failure(
      mutate(source, [&](Json &value) {
        consumer_artifact(value)["artifact_role"] = "source_evidence";
      }),
      "package_consumer", "consumer_artifact_role_mismatch");
  expect_consumer_failure(
      mutate(source, [&](Json &value) {
        consumer_artifact(value)["media_type"] = "application/json";
      }),
      "package_consumer", "consumer_artifact_media_type_mismatch");
  expect_consumer_failure(
      mutate(source, [&](Json &value) {
        consumer_artifact(value)["byte_length"] = 3630;
      }),
      "package_consumer", "consumer_artifact_length_mismatch");

  const auto wrong_consumer_hash = mutate(source, [&](Json &value) {
    const auto replacement =
        "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    consumer_artifact(value)["artifact_hash"] = replacement;
    execution_gate(value)["satisfying_reference_ids"] =
        Json::array({replacement});
  });
  expect_consumer_failure(wrong_consumer_hash, "package_consumer",
                          "consumer_artifact_hash_mismatch");
}

void test_graph_and_review_mutations() {
  const auto source = load_package("execution-component-v2.motor-a");
  expect_consumer_failure(
      mutate(source, [](Json &value) {
        value["parameter_slots"][1]["name"] =
            value["parameter_slots"][0]["name"];
      }),
      "package_integrity", "duplicate_slot_name");

  expect_consumer_failure(
      mutate(source,
             [](Json &value) { remove_slot_graph(value, "gear_ratio"); }),
      "package_consumer", "missing_required_slot");

  expect_consumer_failure(
      mutate(source,
             [](Json &value) { add_optional_slot_graph(value, true); }),
      "package_consumer", "unexpected_required_slot");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        value["parameter_slots"][1]["selected_claim_id"] =
            value["parameter_slots"][0]["selected_claim_id"];
      }),
      "package_integrity", "duplicate_selected_claim");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        claim_by_slot(value, "gear_ratio")["revision_id"] =
            "11000000-0000-4000-8000-000000000099";
      }),
      "package_integrity", "cross_revision_claim");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        value["claim_reviews"][0]["reviewed_claim_fingerprint"] =
            "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
      }),
      "package_integrity", "stale_review");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        value["claim_reviews"][0]["decision"] = "rejected";
      }),
      "package_integrity", "review_not_accepted");
}

void test_typed_slot_mutations_and_optional_extension() {
  const auto source = load_package("execution-component-v2.motor-a");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        auto &claim = claim_by_slot(value, "gear_ratio");
        claim["value_state"] = "unknown";
        claim["value"] = {{"kind", "unknown"}, {"reason", "not known"}};
        claim.erase("unit");
        claim.erase("original_value");
        claim.erase("original_unit");
        refresh_claim_fingerprint(value, "gear_ratio");
      }),
      "package_consumer", "required_value_unknown");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        auto &claim = claim_by_slot(value, "continuous_torque_nm");
        claim["value"] =
            {{"kind", "range"}, {"minimum", 0.1}, {"maximum", 0.3}};
        refresh_claim_fingerprint(value, "continuous_torque_nm");
      }),
      "package_consumer", "invalid_value_shape");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        auto &claim = claim_by_slot(value, "continuous_torque_nm");
        claim["unit"] = "lb*ft";
        claim["original_unit"] = "lb*ft";
        refresh_claim_fingerprint(value, "continuous_torque_nm");
      }),
      "package_consumer", "invalid_unit");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        claim_by_slot(value, "gearbox_efficiency_range")["value"] =
            {{"kind", "range"}, {"minimum", 0.8}, {"maximum", 0.82}};
        refresh_claim_fingerprint(value, "gearbox_efficiency_range");
      }),
      "package_consumer", "invalid_efficiency_range");

  expect_consumer_failure(
      mutate(source, [](Json &value) {
        claim_by_slot(value, "torque_speed_curve")["value"]["points"][0]
                                                        ["y"] = 1.8;
        refresh_claim_fingerprint(value, "torque_speed_curve");
      }),
      "package_consumer", "invalid_torque_speed_curve");

  auto extended = Json::parse(source.bytes);
  add_optional_slot_graph(extended, false);
  const auto extended_package = canonical_package(std::move(extended));
  const auto &input = require_success(
      consume_motor_component(extended_package.bytes,
                              extended_package.object_hash),
      "consume valid additional optional slot");
  require(input.available_but_unused.size() == 4U,
          "additional optional package slots remain visible");
  require(!binding_named(input.available_but_unused, "optional_service_note")
               .value_known,
          "additional optional unknown does not influence calculation input");
}

// Phase 5 checkpoint 4: proves the existing, unmodified motor-arm consumer
// genuinely runs on a component that was manually typed by a user (not a
// pinned fixture) and published through the manual-draft intake path, using
// the exact same package-consumer contract Motor A/B already validate
// against. This fixture is a static, one-time export of a real HTTP
// create/review/publish/export round trip -- see
// scripts note in docs/phase-05-component-intake.md checkpoint 4.
void test_manual_motor_consumption() {
  const auto manual_motor =
      load_package("execution-component-v2.manual-motor-c");
  const auto &input = require_success(
      consume_motor_component(manual_motor.bytes, manual_motor.object_hash),
      "consume a manually entered, non-fixture motor component");
  require(input.package.package_hash == manual_motor.object_hash,
          "manually entered input retains exact package identity");
  require(input.package.capability_id == "component_input.dc_gearmotor_v1",
          "manually entered component declares the shared DC gearmotor capability");
  require(input.package.execution_readiness == "ready",
          "a fully specified manual entry is execution-ready, not just gate-satisfied");
  require(input.continuous_torque_nm == 0.31 && input.stall_torque_nm == 2.85 &&
              input.gear_ratio == 64.0 &&
              input.driver_current_limit_a == 6.0,
          "manually typed engineering values reach the authoritative calculation input");
  require(input.calculation_inputs.size() == 12U &&
              input.validation_inputs.size() == 2U,
          "manual entry satisfies the same 12 calculation + 2 validation slot contract");
}

void test_nonfinite_and_limit_paths() {
  const auto source = load_package("execution-component-v2.motor-a");
  auto nonfinite = source.bytes;
  const std::string needle = "\"value\":0.208";
  const auto position = nonfinite.find(needle);
  require(position != std::string::npos, "raw numeric mutation target exists");
  nonfinite.replace(position, needle.size(), "\"value\":1e400");
  expect_consumer_failure({nonfinite, sha256_bytes(nonfinite)},
                          "package_integrity", "number_overflow");

  const std::string oversized((8U * 1024U * 1024U) + 1U, 'x');
  expect_consumer_failure({oversized, sha256_bytes(oversized)},
                          "package_integrity", "max_raw_bytes_exceeded");
}

} // namespace

int main() {
  try {
    test_motor_a_b_and_blocked_inspection();
    test_integrity_and_top_level_contract_mutations();
    test_gate_and_consumer_artifact_mutations();
    test_graph_and_review_mutations();
    test_typed_slot_mutations_and_optional_extension();
    test_manual_motor_consumption();
    test_nonfinite_and_limit_paths();
    std::cout << "All typed package-consumer tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
