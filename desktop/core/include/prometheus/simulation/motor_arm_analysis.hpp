#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace prometheus::simulation {
struct MotorArmInput final {
  double payload_kg, arm_m, rotation_rad, move_s, hold_s, cycle_s, ambient_c;
  double gear_ratio, efficiency, continuous_torque_nm, stall_torque_nm;
  double no_load_speed_rad_s, no_load_current_a, torque_constant_nm_a;
  double driver_current_limit_a, winding_resistance_ohm;
  double thermal_resistance_k_w, thermal_capacitance_j_k, maximum_temperature_c;
};
struct MotorArmResult final {
  double holding_load_nm, acceleration_load_nm, required_hold_motor_nm, required_move_motor_nm;
  double peak_motor_speed_rad_s, available_move_torque_nm, hold_margin, move_margin;
  double estimated_current_a, current_margin, steady_cycle_temperature_c, thermal_margin_c;
};
inline MotorArmResult analyze_motor_arm(const MotorArmInput& i) {
  if(i.payload_kg<=0||i.arm_m<=0||i.rotation_rad<=0||i.move_s<=0||i.cycle_s<=0||i.gear_ratio<=0||i.efficiency<=0||i.efficiency>1||i.torque_constant_nm_a<=0||i.thermal_resistance_k_w<=0||i.thermal_capacitance_j_k<=0)throw std::invalid_argument("invalid motor-arm input");
  constexpr double g=9.80665;
  const double hold_load=i.payload_kg*g*i.arm_m;
  const double alpha=4.0*i.rotation_rad/(i.move_s*i.move_s);
  const double acceleration_load=i.payload_kg*i.arm_m*i.arm_m*alpha;
  const double required_hold=hold_load/(i.gear_ratio*i.efficiency);
  const double required_move=(hold_load+acceleration_load)/(i.gear_ratio*i.efficiency);
  const double peak_speed=2.0*i.rotation_rad/i.move_s*i.gear_ratio;
  const double available_move=std::max(0.0,i.stall_torque_nm*(1.0-peak_speed/i.no_load_speed_rad_s));
  const double current=i.no_load_current_a+required_move/i.torque_constant_nm_a;
  const double active=std::clamp(i.move_s+i.hold_s,0.0,i.cycle_s),rest=i.cycle_s-active;
  const double copper_loss=current*current*i.winding_resistance_ohm;
  const double tau=i.thermal_resistance_k_w*i.thermal_capacitance_j_k;
  const double active_decay=std::exp(-active/tau),rest_decay=std::exp(-rest/tau);
  const double asymptotic_rise=copper_loss*i.thermal_resistance_k_w;
  const double start_rise=rest_decay*(1.0-active_decay)*asymptotic_rise/(1.0-active_decay*rest_decay);
  const double peak_rise=active_decay*start_rise+(1.0-active_decay)*asymptotic_rise;
  const double steady_temperature=i.ambient_c+peak_rise;
  return {hold_load,acceleration_load,required_hold,required_move,peak_speed,available_move,
    (i.continuous_torque_nm-required_hold)/required_hold,(available_move-required_move)/required_move,
    current,(i.driver_current_limit_a-current)/current,steady_temperature,i.maximum_temperature_c-steady_temperature};
}
}
