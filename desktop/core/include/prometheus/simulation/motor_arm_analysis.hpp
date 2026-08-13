#pragma once
#include <prometheus/simulation/motor_arm_builtin_v1.hpp>

#include <stdexcept>

namespace prometheus::simulation {

struct [[deprecated("use run_motor_arm_builtin_v1 through prometheus_execution")]]
MotorArmInput final {
  double payload_kg, arm_m, rotation_rad, move_s, hold_s, cycle_s, ambient_c;
  double gear_ratio, efficiency, continuous_torque_nm, stall_torque_nm;
  double no_load_speed_rad_s, no_load_current_a, torque_constant_nm_a;
  double driver_current_limit_a, winding_resistance_ohm;
  double thermal_resistance_k_w, thermal_capacitance_j_k, maximum_temperature_c;
};

struct [[deprecated("use MotorArmCalculations and compiled findings")]]
MotorArmResult final {
  double holding_load_nm, acceleration_load_nm, required_hold_motor_nm, required_move_motor_nm;
  double peak_motor_speed_rad_s, available_move_torque_nm, hold_margin, move_margin;
  double estimated_current_a, current_margin, steady_cycle_temperature_c, thermal_margin_c;
};

[[deprecated("use run_motor_arm_builtin_v1 through prometheus_execution")]]
inline MotorArmResult analyze_motor_arm(const MotorArmInput &input) {
  const MotorComponentInput component{
      input.gear_ratio,
      input.efficiency,
      input.continuous_torque_nm,
      input.stall_torque_nm,
      input.no_load_speed_rad_s,
      input.no_load_current_a,
      input.torque_constant_nm_a,
      input.driver_current_limit_a,
      input.winding_resistance_ohm,
      input.thermal_resistance_k_w,
      input.thermal_capacitance_j_k,
      input.maximum_temperature_c,
  };
  const MotorArmScenario scenario{
      input.payload_kg, input.arm_m, input.rotation_rad, input.move_s,
      input.hold_s,    input.cycle_s, input.ambient_c,
  };
  const auto result = run_motor_arm_builtin_v1(component, scenario);
  if (const auto *failure = std::get_if<MotorArmBackendFailure>(&result)) {
    if (failure->code == MotorArmBackendError::unsupported_rounding_mode) {
      throw std::runtime_error("unsupported motor-arm numeric profile");
    }
    throw std::invalid_argument("invalid motor-arm input");
  }
  const auto &value = std::get<MotorArmBackendOutput>(result).calculations;
  return {
      value.holding_load_torque_nm,
      value.acceleration_load_torque_nm,
      value.required_hold_motor_torque_nm,
      value.required_move_motor_torque_nm,
      value.peak_motor_speed_rad_s,
      value.available_move_torque_nm,
      available_over_required_margin(input.continuous_torque_nm,
                                     value.required_hold_motor_torque_nm),
      available_over_required_margin(value.available_move_torque_nm,
                                     value.required_move_motor_torque_nm),
      value.estimated_move_current_a,
      available_over_required_margin(input.driver_current_limit_a,
                                     value.estimated_move_current_a),
      value.estimated_peak_temperature_c,
      remaining_limit_absolute(input.maximum_temperature_c,
                               value.estimated_peak_temperature_c),
  };
}

} // namespace prometheus::simulation
