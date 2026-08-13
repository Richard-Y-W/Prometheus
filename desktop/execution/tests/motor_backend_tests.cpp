#include <prometheus/simulation/motor_arm_builtin_v1.hpp>

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

namespace simulation = prometheus::simulation;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(const double actual, const double expected,
                  const double tolerance, const std::string &message) {
  require(std::isfinite(actual), message + ": actual value is not finite");
  require(std::abs(actual - expected) <= tolerance, message);
}

simulation::MotorComponentInput valid_motor(const double continuous_torque =
                                                0.208,
                                            const double efficiency = 0.70) {
  return simulation::MotorComponentInput{
      100.0, efficiency, continuous_torque, 1.92, 418.879, 0.18, 0.0749,
      4.0,   1.4,        3.2,               110.0, 125.0,
  };
}

simulation::MotorArmScenario valid_scenario() {
  return simulation::MotorArmScenario{
      8.0, 0.2, std::acos(-1.0) / 2.0, 1.2, 4.0, 10.0, 35.0,
  };
}

simulation::MotorArmBackendOutput
require_output(simulation::MotorArmBackendResult &&result,
               const std::string &context) {
  require(std::holds_alternative<simulation::MotorArmBackendOutput>(result),
          context + ": expected backend output");
  return std::get<simulation::MotorArmBackendOutput>(std::move(result));
}

void require_error(const simulation::MotorArmBackendResult &result,
                   const simulation::MotorArmBackendError expected,
                   const std::string &context) {
  require(std::holds_alternative<simulation::MotorArmBackendFailure>(result),
          context + ": expected backend failure");
  require(std::get<simulation::MotorArmBackendFailure>(result).code == expected,
          context + ": unexpected backend error code");
}

void test_fixed_acceptance_calculations() {
  const auto &output = require_output(
      simulation::run_motor_arm_builtin_v1(valid_motor(), valid_scenario()),
      "fixed acceptance scenario");
  const auto &value = output.calculations;
  require_near(value.holding_load_torque_nm, 15.69064, 1e-10,
               "holding load torque");
  require_near(value.acceleration_load_torque_nm, 1.396263401595464, 1e-12,
               "acceleration load torque");
  require_near(value.required_hold_motor_torque_nm, 0.224152, 1e-10,
               "required hold motor torque");
  require_near(value.required_move_motor_torque_nm, 0.24409862002279234,
               1e-14, "required move motor torque");
  require_near(value.peak_motor_speed_rad_s, 261.79938779914943, 1e-12,
               "peak motor speed");
  require_near(value.available_move_torque_nm, 0.7199999413330177, 1e-14,
               "available move torque");
  require_near(value.estimated_move_current_a, 3.4389935917595778, 1e-14,
               "estimated move current");
  require_near(value.estimated_peak_temperature_c, 62.73923846782907,
               1e-12, "estimated peak temperature");

  const std::array calculations{
      value.holding_load_torque_nm,
      value.acceleration_load_torque_nm,
      value.required_hold_motor_torque_nm,
      value.required_move_motor_torque_nm,
      value.peak_motor_speed_rad_s,
      value.available_move_torque_nm,
      value.estimated_move_current_a,
      value.estimated_peak_temperature_c,
  };
  require(std::all_of(calculations.begin(), calculations.end(),
                      [](const double item) { return std::isfinite(item); }),
          "every backend intermediate exposed to findings is finite");
  require(output.applicability_ids == simulation::motor_arm_applicability_ids,
          "backend returns the fixed applicability contract order");
}

void test_motor_a_b_and_efficiency_sensitivity() {
  const auto &motor_a = require_output(
      simulation::run_motor_arm_builtin_v1(valid_motor(0.208), valid_scenario()),
      "Motor A");
  const auto &motor_b = require_output(
      simulation::run_motor_arm_builtin_v1(valid_motor(0.320), valid_scenario()),
      "Motor B");
  require(motor_a.calculations == motor_b.calculations,
          "continuous rating does not alter backend calculations");

  const auto required_hold =
      motor_a.calculations.required_hold_motor_torque_nm;
  require_near(simulation::available_over_required_margin(0.208, required_hold),
               -0.0720582461900853, 1e-12, "Motor A hold margin");
  require_near(simulation::available_over_required_margin(0.320, required_hold),
               0.4276026981690996, 1e-12, "Motor B hold margin");

  const auto &minimum = require_output(
      simulation::run_motor_arm_builtin_v1(valid_motor(0.320, 0.55),
                                           valid_scenario()),
      "minimum efficiency");
  const auto &maximum = require_output(
      simulation::run_motor_arm_builtin_v1(valid_motor(0.320, 0.82),
                                           valid_scenario()),
      "maximum efficiency");
  require_near(simulation::available_over_required_margin(
                   0.320,
                   minimum.calculations.required_hold_motor_torque_nm),
               0.12168783427572118, 1e-12,
               "Motor B minimum-efficiency margin");
  require(minimum.calculations.required_hold_motor_torque_nm >
              maximum.calculations.required_hold_motor_torque_nm,
          "higher efficiency lowers required motor torque");
}

void test_domains_and_nonfinite_rejection() {
  auto motor = valid_motor();
  motor.gear_ratio = 0.0;
  require_error(simulation::run_motor_arm_builtin_v1(motor, valid_scenario()),
                simulation::MotorArmBackendError::invalid_domain,
                "zero gear ratio");

  motor = valid_motor();
  motor.gearbox_efficiency = 1.01;
  require_error(simulation::run_motor_arm_builtin_v1(motor, valid_scenario()),
                simulation::MotorArmBackendError::invalid_domain,
                "efficiency above one");

  motor = valid_motor();
  motor.no_load_speed_rad_s = 0.0;
  require_error(simulation::run_motor_arm_builtin_v1(motor, valid_scenario()),
                simulation::MotorArmBackendError::invalid_domain,
                "zero no-load speed");

  motor = valid_motor();
  motor.thermal_capacitance_j_k = -1.0;
  require_error(simulation::run_motor_arm_builtin_v1(motor, valid_scenario()),
                simulation::MotorArmBackendError::invalid_domain,
                "negative thermal capacitance");

  auto scenario = valid_scenario();
  scenario.hold_duration_s = -0.1;
  require_error(simulation::run_motor_arm_builtin_v1(valid_motor(), scenario),
                simulation::MotorArmBackendError::invalid_domain,
                "negative hold duration");

  scenario = valid_scenario();
  scenario.cycle_duration_s = 5.19;
  require_error(simulation::run_motor_arm_builtin_v1(valid_motor(), scenario),
                simulation::MotorArmBackendError::invalid_domain,
                "incomplete cycle");

  motor = valid_motor();
  motor.stall_torque_nm = std::numeric_limits<double>::infinity();
  require_error(simulation::run_motor_arm_builtin_v1(motor, valid_scenario()),
                simulation::MotorArmBackendError::non_finite_input,
                "non-finite component input");

  scenario = valid_scenario();
  scenario.rotation_rad = std::numeric_limits<double>::quiet_NaN();
  require_error(simulation::run_motor_arm_builtin_v1(valid_motor(), scenario),
                simulation::MotorArmBackendError::non_finite_input,
                "non-finite scenario input");
}

void test_rounding_mode_and_margin_boundaries() {
  require_near(simulation::available_over_required_margin(2.0, 2.0), 0.0,
               0.0, "equal available/required margin");
  require_near(simulation::remaining_limit_fraction(125.0, 125.0), 0.0, 0.0,
               "equal thermal limit margin");

  const auto previous_rounding = std::fegetround();
  require(previous_rounding != -1, "read current floating-point rounding mode");
  require(std::fesetround(FE_DOWNWARD) == 0,
          "set unsupported rounding mode for test");
  const auto result =
      simulation::run_motor_arm_builtin_v1(valid_motor(), valid_scenario());
  require(std::fesetround(previous_rounding) == 0,
          "restore floating-point rounding mode");
  require_error(result,
                simulation::MotorArmBackendError::unsupported_rounding_mode,
                "non-nearest rounding mode");
}

} // namespace

int main() {
  try {
    test_fixed_acceptance_calculations();
    test_motor_a_b_and_efficiency_sensitivity();
    test_domains_and_nonfinite_rejection();
    test_rounding_mode_and_margin_boundaries();
    std::cout << "All authoritative motor backend tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
