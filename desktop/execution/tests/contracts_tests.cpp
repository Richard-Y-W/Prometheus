#include "test_support.hpp"

#include <prometheus/execution/contracts.hpp>
#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using nlohmann::json;
using prometheus::execution::AnalysisRequestDraft;
using prometheus::execution::CanonicalObject;
using prometheus::execution::ScenarioDraftDegrees;
using prometheus::execution::ScenarioPreview;
using prometheus::execution::build_analysis_request;
using prometheus::execution::confirm_motor_arm_scenario;
using prometheus::execution::parse_analysis_request;
using prometheus::execution::parse_motor_arm_scenario;
using prometheus::execution::preview_motor_arm_scenario;
using prometheus::execution::test::require;
using prometheus::execution::test::require_failure;
using prometheus::execution::test::require_near;
using prometheus::execution::test::require_success;
using prometheus::integrity::canonicalize_json_bytes;

constexpr std::string_view scenario_schema =
    "urn:prometheus:schema:motor-arm-scenario:1.0.0";
constexpr std::string_view scenario_media_type =
    "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0";
constexpr std::string_view request_schema =
    "urn:prometheus:schema:analysis-request:1.0.0";
constexpr std::string_view request_media_type =
    "application/vnd.prometheus.analysis-request+json;version=1.0.0";

const std::vector<std::string> obligations{
    "motor_arm.move_torque_speed",
    "motor_arm.hold_continuous_torque",
    "motor_arm.driver_current_limit",
    "motor_arm.thermal_peak",
};

std::string hash_with(const char digit) {
  return "sha256:" + std::string(64U, digit);
}

ScenarioDraftDegrees valid_scenario_draft() {
  return ScenarioDraftDegrees{
      8.0,
      0.2,
      90.0,
      1.2,
      4.0,
      10.0,
      35.0,
  };
}

AnalysisRequestDraft valid_request_draft() {
  AnalysisRequestDraft draft;
  draft.package_hash = hash_with('a');
  draft.scenario_hash = hash_with('b');
  draft.assembly_artifact_hash = hash_with('c');
  draft.bound_cad_entity_id = "motor";
  draft.backend_id = "motor_arm_builtin_v1";
  draft.backend_contract_version = "1.0.0";
  draft.package_consumer_contract_hash = hash_with('d');
  draft.obligation_ids = obligations;
  return draft;
}

CanonicalObject valid_scenario_object() {
  auto preview = require_success(preview_motor_arm_scenario(valid_scenario_draft()),
                                 "build valid scenario preview");
  preview.confirmed_by_user = true;
  return require_success(
      confirm_motor_arm_scenario(
          preview,
          "Evaluate the bound motor for the reviewed motor-arm operating cycle."),
      "confirm valid scenario");
}

template <typename Mutation>
std::string mutate_canonical(const std::string &bytes, Mutation mutation) {
  auto value = json::parse(bytes);
  mutation(value);
  return canonicalize_json_bytes(value.dump());
}

void test_scenario_preview_confirmation_and_determinism() {
  const auto first_preview_result =
      preview_motor_arm_scenario(valid_scenario_draft());
  const auto &first_preview =
      require_success(first_preview_result, "preview fixed scenario");
  require_near(first_preview.rotation_rad, 1.5707963267948966, 0.0,
               "90 degrees converts once to the approved radians value");
  require(first_preview.payload_mass_kg == 8.0,
          "typed preview exposes payload mass in kilograms");
  require(first_preview.arm_radius_m == 0.2,
          "typed preview exposes arm radius in metres");
  require(first_preview.motion_profile == "symmetric_triangular_velocity",
          "typed preview exposes the fixed motion profile");
  require(!first_preview.confirmed_by_user,
          "a generated preview is not implicitly confirmed");

  require_failure(
      confirm_motor_arm_scenario(first_preview, "reviewed intent"),
      "scenario_confirmation", "scenario_not_confirmed");

  auto confirmed_preview = first_preview;
  confirmed_preview.confirmed_by_user = true;
  require_failure(confirm_motor_arm_scenario(confirmed_preview, " \t\n"),
                  "scenario_confirmation", "intent_empty");
  require_failure(confirm_motor_arm_scenario(confirmed_preview, "\xC2\xA0"),
                  "scenario_confirmation", "intent_empty");
  std::string invalid_intent(1U, static_cast<char>(0xff));
  require_failure(confirm_motor_arm_scenario(confirmed_preview, invalid_intent),
                  "scenario_confirmation", "invalid_utf8");

  const auto first = require_success(
      confirm_motor_arm_scenario(confirmed_preview, " reviewed intent "),
      "serialize confirmed scenario");
  const auto second = require_success(
      confirm_motor_arm_scenario(confirmed_preview, " reviewed intent "),
      "serialize confirmed scenario twice");
  require(first.bytes == second.bytes,
          "identical scenario calls produce identical canonical bytes");
  require(first.object_hash == second.object_hash,
          "identical scenario calls produce identical hashes");
  require(first.schema_id == scenario_schema &&
              first.schema_version == "1.0.0" &&
              first.media_type == scenario_media_type,
          "scenario object carries exact contract metadata");

  const auto parsed = require_success(parse_motor_arm_scenario(first.bytes),
                                      "parse built scenario");
  require(parsed.confirmed_by_user, "parsed scenario remains confirmed");
  require(parsed.intent == "reviewed intent",
          "scenario intent is trimmed before canonical serialization");
  require_near(parsed.rotation_rad, 1.5707963267948966, 0.0,
               "parsed scenario preserves exact radians value");
}

void test_scenario_draft_domains() {
  auto draft = valid_scenario_draft();
  draft.payload_mass_kg = 0.0;
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "value_not_positive");

  draft = valid_scenario_draft();
  draft.arm_radius_m = -0.2;
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "value_not_positive");

  draft = valid_scenario_draft();
  draft.rotation_degrees = 0.0;
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "value_not_positive");

  draft = valid_scenario_draft();
  draft.move_duration_s = 0.0;
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "value_not_positive");

  draft = valid_scenario_draft();
  draft.hold_duration_s = -0.01;
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "value_negative");

  draft = valid_scenario_draft();
  draft.cycle_duration_s = 0.0;
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "value_not_positive");

  draft = valid_scenario_draft();
  draft.ambient_temperature_c =
      std::numeric_limits<double>::infinity();
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "non_finite_number");

  draft = valid_scenario_draft();
  draft.rotation_degrees = std::numeric_limits<double>::quiet_NaN();
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "non_finite_number");

  draft = valid_scenario_draft();
  draft.cycle_duration_s = 5.19;
  require_failure(preview_motor_arm_scenario(draft), "scenario_preview",
                  "cycle_duration_too_short");
}

void test_strict_scenario_parser_edges() {
  const auto object = valid_scenario_object();

  const auto wrong_unit = mutate_canonical(object.bytes, [](json &value) {
    value["payload_mass"]["unit"] = "lb";
  });
  require_failure(parse_motor_arm_scenario(wrong_unit), "scenario_contract",
                  "invalid_unit");

  const auto wrong_profile = mutate_canonical(object.bytes, [](json &value) {
    value["motion_profile"] = "trapezoidal_velocity";
  });
  require_failure(parse_motor_arm_scenario(wrong_profile), "scenario_contract",
                  "unsupported_motion_profile");

  const auto unknown_field = mutate_canonical(object.bytes, [](json &value) {
    value["unexpected"] = true;
  });
  require_failure(parse_motor_arm_scenario(unknown_field), "scenario_contract",
                  "unknown_field");

  const auto oversized_unknown =
      mutate_canonical(object.bytes, [](json &value) {
        value[std::string(5000U, 'x')] = true;
      });
  const auto &bounded =
      require_failure(parse_motor_arm_scenario(oversized_unknown),
                      "scenario_contract", "unknown_field");
  require(bounded.field.has_value() && bounded.field->size() <= 512U,
          "diagnostic field context must be bounded");
  require(bounded.message.size() <= 4096U,
          "diagnostic messages must be bounded");

  const auto wrong_version = mutate_canonical(object.bytes, [](json &value) {
    value["schema_version"] = "2.0.0";
  });
  require_failure(parse_motor_arm_scenario(wrong_version), "scenario_contract",
                  "unsupported_schema_version");

  require_failure(
      parse_motor_arm_scenario(
          "{\"$schema\":\"a\",\"$schema\":\"b\"}"),
      "scenario_contract", "duplicate_key");

  std::string invalid_utf8{"{\"value\":\""};
  invalid_utf8.push_back(static_cast<char>(0xff));
  invalid_utf8 += "\"}";
  require_failure(parse_motor_arm_scenario(invalid_utf8), "scenario_contract",
                  "invalid_utf8");
  require_failure(parse_motor_arm_scenario("{\"value\":-0}"),
                  "scenario_contract", "negative_zero");
  require_failure(
      parse_motor_arm_scenario("{\"value\":9007199254740992}"),
      "scenario_contract", "unsafe_integer");

  const std::string over_limit((8U * 1024U * 1024U) + 1U, 'x');
  require_failure(parse_motor_arm_scenario(over_limit), "scenario_contract",
                  "max_raw_bytes_exceeded");
}

void test_request_contract_and_determinism() {
  const auto draft = valid_request_draft();
  const auto first =
      require_success(build_analysis_request(draft), "build valid request");
  const auto second =
      require_success(build_analysis_request(draft), "build request twice");
  require(first.bytes == second.bytes && first.object_hash == second.object_hash,
          "request bytes and identity are deterministic");
  require(first.schema_id == request_schema &&
              first.schema_version == "1.0.0" &&
              first.media_type == request_media_type,
          "request object carries exact contract metadata");

  const auto parsed = require_success(parse_analysis_request(first.bytes),
                                      "parse built request");
  require(parsed.package_hash == hash_with('a'),
          "request binds the exact package hash");
  require(parsed.scenario_hash == hash_with('b'),
          "request binds the exact scenario hash");
  require(parsed.package_consumer_contract_hash == hash_with('d'),
          "request binds the exact consumer hash");
  require(parsed.backend_id == "motor_arm_builtin_v1" &&
              parsed.backend_contract_version == "1.0.0",
          "request freezes authoritative backend identity");
  require(std::vector<std::string>(parsed.obligation_ids.begin(),
                                   parsed.obligation_ids.end()) == obligations,
          "request freezes exact obligation order");

  auto invalid = draft;
  invalid.package_hash = "sha256:" + std::string(64U, 'A');
  require_failure(build_analysis_request(invalid), "request_contract",
                  "invalid_hash");

  invalid = draft;
  invalid.package_consumer_contract_hash = "sha256:1234";
  require_failure(build_analysis_request(invalid), "request_contract",
                  "invalid_hash");

  invalid = draft;
  invalid.bound_cad_entity_id = " \t ";
  require_failure(build_analysis_request(invalid), "request_contract",
                  "bound_entity_empty");

  invalid = draft;
  invalid.bound_cad_entity_id = "\xE3\x80\x80";
  require_failure(build_analysis_request(invalid), "request_contract",
                  "bound_entity_empty");

  invalid = draft;
  std::swap(invalid.obligation_ids[0], invalid.obligation_ids[1]);
  require_failure(build_analysis_request(invalid), "request_contract",
                  "obligation_order_mismatch");

  invalid = draft;
  invalid.backend_id = "alternate_backend";
  require_failure(build_analysis_request(invalid), "request_contract",
                  "unsupported_backend");
}

void test_strict_request_parser_edges() {
  const auto request =
      require_success(build_analysis_request(valid_request_draft()),
                      "build parser fixture request");

  const auto wrong_version = mutate_canonical(request.bytes, [](json &value) {
    value["backend_contract_version"] = "2.0.0";
  });
  require_failure(parse_analysis_request(wrong_version), "request_contract",
                  "unsupported_backend_version");

  const auto wrong_backend = mutate_canonical(request.bytes, [](json &value) {
    value["backend_id"] = "alternate_backend";
  });
  require_failure(parse_analysis_request(wrong_backend), "request_contract",
                  "unsupported_backend");

  const auto wrong_obligations =
      mutate_canonical(request.bytes, [](json &value) {
        std::swap(value["obligation_ids"][0], value["obligation_ids"][1]);
      });
  require_failure(parse_analysis_request(wrong_obligations), "request_contract",
                  "obligation_order_mismatch");

  const auto unknown_field = mutate_canonical(request.bytes, [](json &value) {
    value["unexpected"] = true;
  });
  require_failure(parse_analysis_request(unknown_field), "request_contract",
                  "unknown_field");
}

void test_qt_free_execution_boundary() {
#ifndef PROMETHEUS_EXECUTION_LINK_LIBRARIES
#error "execution link libraries must be visible to the boundary test"
#endif
  const std::string link_libraries = PROMETHEUS_EXECUTION_LINK_LIBRARIES;
  require(link_libraries == "prometheus_integrity|prometheus_core",
          "execution must link only the integrity and core libraries");
  require(link_libraries.find("Qt") == std::string::npos,
          "execution must not link a Qt target");
}

} // namespace

int main() {
  try {
    test_scenario_preview_confirmation_and_determinism();
    test_scenario_draft_domains();
    test_strict_scenario_parser_edges();
    test_request_contract_and_determinism();
    test_strict_request_parser_edges();
    test_qt_free_execution_boundary();
    std::cout << "All strict execution-contract tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
