#pragma once

#include <prometheus/execution/diagnostic.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace prometheus::execution {

inline constexpr std::string_view motor_arm_scenario_schema_id =
    "urn:prometheus:schema:motor-arm-scenario:1.0.0";
inline constexpr std::string_view motor_arm_scenario_media_type =
    "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0";
inline constexpr std::string_view analysis_request_schema_id =
    "urn:prometheus:schema:analysis-request:1.0.0";
inline constexpr std::string_view analysis_request_media_type =
    "application/vnd.prometheus.analysis-request+json;version=1.0.0";
inline constexpr std::string_view motor_arm_backend_id =
    "motor_arm_builtin_v1";
inline constexpr std::string_view motor_arm_backend_contract_version = "1.0.0";

inline constexpr std::array<std::string_view, 4> motor_arm_obligation_ids{
    "motor_arm.move_torque_speed",
    "motor_arm.hold_continuous_torque",
    "motor_arm.driver_current_limit",
    "motor_arm.thermal_peak",
};

struct CanonicalObject final {
  std::string bytes;
  std::string object_hash;
  std::string media_type;
  std::string schema_id;
  std::string schema_version;
};

struct ScenarioDraftDegrees final {
  double payload_mass_kg;
  double arm_radius_m;
  double rotation_degrees;
  double move_duration_s;
  double hold_duration_s;
  double cycle_duration_s;
  double ambient_temperature_c;
};

struct ScenarioPreview final {
  double payload_mass_kg;
  double arm_radius_m;
  double rotation_rad;
  double move_duration_s;
  double hold_duration_s;
  double cycle_duration_s;
  double ambient_temperature_c;
  std::string motion_profile;
  bool confirmed_by_user{false};
};

struct MotorArmScenario final {
  double payload_mass_kg;
  double arm_radius_m;
  double rotation_rad;
  double move_duration_s;
  double hold_duration_s;
  double cycle_duration_s;
  double ambient_temperature_c;
  std::string motion_profile;
  bool confirmed_by_user;
  std::string intent;
};

struct AnalysisRequestDraft final {
  std::string package_hash;
  std::string scenario_hash;
  std::string assembly_artifact_hash;
  std::string bound_cad_entity_id;
  std::string backend_id;
  std::string backend_contract_version;
  std::string package_consumer_contract_hash;
  std::vector<std::string> obligation_ids;
};

struct AnalysisRequest final {
  std::string package_hash;
  std::string scenario_hash;
  std::string assembly_artifact_hash;
  std::string bound_cad_entity_id;
  std::string backend_id;
  std::string backend_contract_version;
  std::string package_consumer_contract_hash;
  std::array<std::string, 4> obligation_ids;
};

[[nodiscard]] Result<ScenarioPreview>
preview_motor_arm_scenario(const ScenarioDraftDegrees &draft);
[[nodiscard]] Result<CanonicalObject>
confirm_motor_arm_scenario(const ScenarioPreview &preview,
                           std::string_view intent);
[[nodiscard]] Result<MotorArmScenario>
parse_motor_arm_scenario(std::string_view stored_bytes);
[[nodiscard]] Result<CanonicalObject>
build_analysis_request(const AnalysisRequestDraft &draft);
[[nodiscard]] Result<AnalysisRequest>
parse_analysis_request(std::string_view stored_bytes);

} // namespace prometheus::execution
