#include "test_support.hpp"

#include <prometheus/execution/finding_compiler.hpp>
#include <prometheus/execution/numeric_profile.hpp>
#include <prometheus/simulation/motor_arm_builtin_v1.hpp>

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace execution = prometheus::execution;
namespace simulation = prometheus::simulation;
using execution::test::require;
using execution::test::require_failure;
using execution::test::require_near;
using execution::test::require_success;

std::string hash_with(const char value) {
  return "sha256:" + std::string(64U, value);
}

execution::ClaimBinding claim_binding(const std::string &slot_name,
                                      const std::size_t index,
                                      const bool known = true) {
  const auto suffix = std::to_string(index);
  return execution::ClaimBinding{
      slot_name,
      "slot-" + suffix,
      "claim-" + suffix,
      hash_with(static_cast<char>('a' + static_cast<int>(index % 6U))),
      known,
      known ? std::nullopt
            : std::optional<std::string>("optional value is unknown"),
  };
}

execution::MotorComponentInput consumed_motor(const double continuous_torque) {
  const std::array<std::string, 12> names{
      "gear_ratio",
      "gearbox_efficiency_nominal",
      "continuous_torque_nm",
      "stall_torque_nm",
      "no_load_speed_rad_s",
      "no_load_current_a",
      "torque_constant_nm_a",
      "driver_current_limit_a",
      "winding_resistance_ohm",
      "thermal_resistance_k_w",
      "thermal_capacitance_j_k",
      "maximum_temperature_c",
  };
  std::array<execution::ClaimBinding, 12> calculations;
  for (std::size_t index = 0U; index < calculations.size(); ++index) {
    calculations[index] = claim_binding(names[index], index + 1U);
  }
  std::array<execution::ClaimBinding, 2> validation{
      claim_binding("gearbox_efficiency_range", 20U),
      claim_binding("torque_speed_curve", 21U),
  };
  std::vector<execution::ClaimBinding> unused{
      claim_binding("nominal_voltage_v", 30U),
      claim_binding("supply_current_limit_a", 31U),
      claim_binding("gearbox_lifetime", 32U, false),
  };
  return execution::MotorComponentInput{
      execution::PackageInspection{
          hash_with(continuous_torque == 0.208 ? '1' : '2'),
          "revision-id",
          "component-id",
          "Prometheus Fixture Works",
          continuous_torque == 0.208 ? "DC-GM-A" : "DC-GM-B",
          "fixture-1",
          "dc_gearmotor",
          "component_execution_input",
          "component_input.dc_gearmotor_v1",
          "ready",
          {"Synthetic conformance input only."},
      },
      100.0,
      0.70,
      continuous_torque,
      1.92,
      418.879,
      0.18,
      0.0749,
      4.0,
      1.4,
      3.2,
      110.0,
      125.0,
      0.55,
      0.82,
      {{{0.0, 1.92}, {418.879, 0.0}}},
      std::move(calculations),
      std::move(validation),
      std::move(unused),
  };
}

execution::MotorArmScenario reviewed_scenario() {
  return execution::MotorArmScenario{
      8.0,
      0.2,
      std::acos(-1.0) / 2.0,
      1.2,
      4.0,
      10.0,
      35.0,
      "symmetric_triangular_velocity",
      true,
      "Evaluate the bound motor for the reviewed motor-arm operating cycle.",
  };
}

simulation::MotorComponentInput
backend_motor(const execution::MotorComponentInput &value,
              const double efficiency) {
  return simulation::MotorComponentInput{
      value.gear_ratio,
      efficiency,
      value.continuous_torque_nm,
      value.stall_torque_nm,
      value.no_load_speed_rad_s,
      value.no_load_current_a,
      value.torque_constant_nm_a,
      value.driver_current_limit_a,
      value.winding_resistance_ohm,
      value.thermal_resistance_k_w,
      value.thermal_capacitance_j_k,
      value.maximum_temperature_c,
  };
}

simulation::MotorArmScenario
backend_scenario(const execution::MotorArmScenario &value) {
  return simulation::MotorArmScenario{
      value.payload_mass_kg,  value.arm_radius_m,      value.rotation_rad,
      value.move_duration_s, value.hold_duration_s,   value.cycle_duration_s,
      value.ambient_temperature_c,
  };
}

const simulation::MotorArmBackendOutput &
backend_output(const simulation::MotorArmBackendResult &value) {
  require(std::holds_alternative<simulation::MotorArmBackendOutput>(value),
          "finding fixture backend execution must complete");
  return std::get<simulation::MotorArmBackendOutput>(value);
}

simulation::MotorArmBackendOutput
backend_output(simulation::MotorArmBackendResult &&value) {
  require(std::holds_alternative<simulation::MotorArmBackendOutput>(value),
          "finding fixture backend execution must complete");
  return std::get<simulation::MotorArmBackendOutput>(std::move(value));
}

execution::AnalysisRequest request_for(const execution::MotorComponentInput &motor) {
  std::array<std::string, 4> obligations;
  std::transform(execution::motor_arm_obligation_ids.begin(),
                 execution::motor_arm_obligation_ids.end(), obligations.begin(),
                 [](const std::string_view value) { return std::string(value); });
  return execution::AnalysisRequest{
      motor.package.package_hash,
      hash_with('b'),
      hash_with('c'),
      "motor",
      "motor_arm_builtin_v1",
      "1.0.0",
      hash_with('d'),
      std::move(obligations),
  };
}

execution::Result<execution::CompiledMotorArmFindings>
compile(const execution::MotorComponentInput &motor,
        const execution::MotorArmScenario &scenario,
        const std::string &request_hash = hash_with('a')) {
  const auto scenario_values = backend_scenario(scenario);
  const auto nominal = simulation::run_motor_arm_builtin_v1(
      backend_motor(motor, motor.gearbox_efficiency_nominal), scenario_values);
  const auto minimum = simulation::run_motor_arm_builtin_v1(
      backend_motor(motor, motor.gearbox_efficiency_minimum), scenario_values);
  const auto maximum = simulation::run_motor_arm_builtin_v1(
      backend_motor(motor, motor.gearbox_efficiency_maximum), scenario_values);
  return execution::compile_motor_arm_findings(
      motor, scenario, request_for(motor), request_hash, hash_with('b'),
      backend_output(nominal), backend_output(minimum), backend_output(maximum));
}

const execution::ObligationFinding &
finding(const execution::CompiledMotorArmFindings &result,
        const std::string_view obligation_id) {
  const auto item = std::find_if(
      result.obligation_outcomes.begin(), result.obligation_outcomes.end(),
      [&](const execution::ObligationFinding &candidate) {
        return candidate.obligation_id == obligation_id;
      });
  require(item != result.obligation_outcomes.end(),
          "expected obligation finding is absent");
  return *item;
}

void test_motor_a_scoped_findings_and_identity() {
  const auto motor = consumed_motor(0.208);
  const auto result = require_success(compile(motor, reviewed_scenario()),
                                      "compile Motor A findings");
  require(result.obligation_outcomes.size() == 4U,
          "exactly four requested findings are emitted");
  require(result.calculations.size() == 8U,
          "all backend calculations are contract ordered");
  require(result.obligation_outcomes[0].finding_id ==
              "sha256:6eefd5975640bfc03305dc6e866187b2fc2732f036fefd2af4c0beb0fecfb95c",
          "movement finding ID uses the independent canonical vector");
  require(result.obligation_outcomes[1].finding_id ==
              "sha256:7fe0bc5d660541ec68460f51c198f83fe26eeba94234b43ab0979ea11476888b",
          "holding finding ID uses the independent canonical vector");
  require(result.obligation_outcomes[2].finding_id ==
              "sha256:4548f4a2629b7dd583f6fb5535e7c397ced03d925f98a1c398d57a64cefafd5a",
          "current finding ID uses the independent canonical vector");
  require(result.obligation_outcomes[3].finding_id ==
              "sha256:15c9accaccd2d4ab64a2c42ae86ecc9cc92376e9bac7f7dbee1dc244afa54c0b",
          "thermal finding ID uses the independent canonical vector");

  const auto &move = finding(result, "motor_arm.move_torque_speed");
  const auto &hold = finding(result, "motor_arm.hold_continuous_torque");
  const auto &current = finding(result, "motor_arm.driver_current_limit");
  const auto &thermal = finding(result, "motor_arm.thermal_peak");
  require(move.outcome == execution::EngineeringOutcome::pass &&
              hold.outcome == execution::EngineeringOutcome::fail &&
              current.outcome == execution::EngineeringOutcome::pass &&
              thermal.outcome == execution::EngineeringOutcome::pass,
          "Motor A has three scoped passes and one hold failure");
  require(hold.package_hash == motor.package.package_hash &&
              hold.request_hash == hash_with('a') &&
              hold.scenario_hash == hash_with('b'),
          "finding provenance freezes package/request/scenario hashes");
  require(!hold.consumed_claim_ids.empty() && !hold.assumptions.empty() &&
              !hold.limitations.empty(),
          "findings carry claim, assumption, and limitation provenance");
  require(thermal.severity == execution::FindingSeverity::caution,
          "simplified thermal pass retains caution severity");
  require_near(hold.signed_margin, -0.0720582461900853, 1e-12,
               "Motor A hold margin");
  require(result.sensitivity.crosses_zero,
          "Motor A efficiency range crosses the holding boundary");
  require(result.coverage.counts.pass == 3U &&
              result.coverage.counts.fail == 1U &&
              result.coverage.counts.indeterminate == 0U &&
              result.coverage.counts.not_evaluated == 0U,
          "Motor A coverage counts match four requested obligations");
  require(result.missing_information.size() == 1U &&
              result.missing_information[0].question_id ==
                  "assembly.center_of_gravity",
          "center of gravity is recorded as uncovered information");
  require(std::none_of(
              result.obligation_outcomes.begin(),
              result.obligation_outcomes.end(), [](const auto &item) {
                return item.obligation_id == "assembly.center_of_gravity";
              }),
          "center of gravity is not misrepresented as a fifth finding");
}

void test_motor_b_and_ab_normalized_behavior() {
  const auto a = require_success(compile(consumed_motor(0.208), reviewed_scenario()),
                                 "compile Motor A");
  const auto b = require_success(compile(consumed_motor(0.320), reviewed_scenario()),
                                 "compile Motor B");
  require(a.calculations == b.calculations,
          "A/B backend calculation records remain equal");
  require(finding(a, "motor_arm.move_torque_speed").outcome ==
              finding(b, "motor_arm.move_torque_speed").outcome &&
              finding(a, "motor_arm.driver_current_limit").outcome ==
                  finding(b, "motor_arm.driver_current_limit").outcome &&
              finding(a, "motor_arm.thermal_peak").outcome ==
                  finding(b, "motor_arm.thermal_peak").outcome,
          "A/B non-holding outcomes remain equal");
  const auto &hold_b = finding(b, "motor_arm.hold_continuous_torque");
  require(hold_b.outcome == execution::EngineeringOutcome::pass,
          "Motor B holding outcome passes");
  require_near(hold_b.signed_margin, 0.4276026981690996, 1e-12,
               "Motor B hold margin");
  require_near(b.sensitivity.minimum_efficiency_hold_margin,
               0.12168783427572118, 1e-12,
               "Motor B minimum-efficiency hold margin");
  require(!b.sensitivity.crosses_zero,
          "Motor B remains positive throughout the efficiency range");
  require(b.coverage.counts.pass == 4U && b.coverage.counts.fail == 0U,
          "Motor B has four scoped passes");
}

void test_inclusive_equality_and_nonfinite_rejection() {
  auto motor = consumed_motor(0.208);
  const auto scenario = reviewed_scenario();
  const auto scenario_values = backend_scenario(scenario);
  const auto initial = backend_output(simulation::run_motor_arm_builtin_v1(
      backend_motor(motor, motor.gearbox_efficiency_nominal), scenario_values));
  motor.continuous_torque_nm =
      initial.calculations.required_hold_motor_torque_nm;
  motor.driver_current_limit_a = initial.calculations.estimated_move_current_a;
  motor.maximum_temperature_c =
      initial.calculations.estimated_peak_temperature_c;
  motor.stall_torque_nm =
      initial.calculations.required_move_motor_torque_nm /
      (1.0 - initial.calculations.peak_motor_speed_rad_s /
                 motor.no_load_speed_rad_s);

  const auto equality =
      require_success(compile(motor, scenario), "compile equality boundaries");
  for (const auto &outcome : equality.obligation_outcomes) {
    require(outcome.outcome == execution::EngineeringOutcome::pass,
            "exact equality is an inclusive pass");
    require_near(outcome.signed_margin, 0.0, 1e-14,
                 "equality produces zero signed margin");
  }

  const auto backend = simulation::run_motor_arm_builtin_v1(
      backend_motor(consumed_motor(0.208), 0.70), scenario_values);
  auto nonfinite = backend_output(backend);
  nonfinite.calculations.available_move_torque_nm =
      std::numeric_limits<double>::infinity();
  const auto rejected = execution::compile_motor_arm_findings(
      consumed_motor(0.208), scenario, request_for(consumed_motor(0.208)),
      hash_with('a'), hash_with('b'), nonfinite, backend_output(backend),
      backend_output(backend));
  require_failure(std::move(rejected), "finding_compiler",
                  "non_finite_calculation");
}

void test_numeric_profile_is_concrete_and_rounding_guarded() {
  const auto profile = require_success(execution::collect_numeric_profile(),
                                       "collect numeric profile");
  require(!profile.operating_system.name.empty() &&
              !profile.operating_system.release.empty() &&
              !profile.operating_system.architecture.empty() &&
              !profile.compiler.id.empty() && !profile.compiler.version.empty() &&
              !profile.standard_library.id.empty() &&
              !profile.standard_library.version.empty() &&
              !profile.math_runtime.id.empty() &&
              !profile.math_runtime.version.empty(),
          "numeric profile never invents unknown identity values");
  require(profile.backend_build_fingerprint.starts_with("sha256:") &&
              profile.backend_build_fingerprint.size() == 71U,
          "numeric profile includes compiled backend fingerprint");
  require(profile.floating_point.contraction == "disabled" &&
              !profile.floating_point.fast_math &&
              profile.floating_point.rounding_mode == "to_nearest" &&
              profile.numeric_serialization_version == "1.0.0",
          "numeric profile freezes the approved floating-point policy");

  const auto previous_rounding = std::fegetround();
  require(std::fesetround(FE_UPWARD) == 0,
          "set unsupported profile rounding mode");
  const auto unavailable = execution::collect_numeric_profile();
  require(std::fesetround(previous_rounding) == 0,
          "restore numeric-profile rounding mode");
  require_failure(unavailable, "numeric_profile",
                  "unsupported_numeric_profile");
}

} // namespace

int main() {
  try {
    test_motor_a_scoped_findings_and_identity();
    test_motor_b_and_ab_normalized_behavior();
    test_inclusive_equality_and_nonfinite_rejection();
    test_numeric_profile_is_concrete_and_rounding_guarded();
    std::cout << "All motor finding compiler tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
