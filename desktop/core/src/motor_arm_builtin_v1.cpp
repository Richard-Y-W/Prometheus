#include <prometheus/simulation/motor_arm_builtin_v1.hpp>

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <stdexcept>

namespace prometheus::simulation {
namespace {

constexpr double standard_gravity_m_s2 = 9.80665;

bool all_finite(const MotorComponentInput &component,
                const MotorArmScenario &scenario) {
  const std::array values{
      component.gear_ratio,
      component.gearbox_efficiency,
      component.continuous_torque_nm,
      component.stall_torque_nm,
      component.no_load_speed_rad_s,
      component.no_load_current_a,
      component.torque_constant_nm_a,
      component.driver_current_limit_a,
      component.winding_resistance_ohm,
      component.thermal_resistance_k_w,
      component.thermal_capacitance_j_k,
      component.maximum_temperature_c,
      scenario.payload_mass_kg,
      scenario.arm_radius_m,
      scenario.rotation_rad,
      scenario.move_duration_s,
      scenario.hold_duration_s,
      scenario.cycle_duration_s,
      scenario.ambient_temperature_c,
  };
  return std::all_of(values.begin(), values.end(),
                     [](const double value) { return std::isfinite(value); });
}

bool valid_domains(const MotorComponentInput &component,
                   const MotorArmScenario &scenario) {
  const auto occupied_duration =
      scenario.move_duration_s + scenario.hold_duration_s;
  return std::isfinite(occupied_duration) && component.gear_ratio > 0.0 &&
         component.gearbox_efficiency > 0.0 &&
         component.gearbox_efficiency <= 1.0 &&
         component.continuous_torque_nm > 0.0 &&
         component.stall_torque_nm > 0.0 &&
         component.no_load_speed_rad_s > 0.0 &&
         component.no_load_current_a >= 0.0 &&
         component.torque_constant_nm_a > 0.0 &&
         component.driver_current_limit_a > 0.0 &&
         component.winding_resistance_ohm > 0.0 &&
         component.thermal_resistance_k_w > 0.0 &&
         component.thermal_capacitance_j_k > 0.0 &&
         scenario.payload_mass_kg > 0.0 && scenario.arm_radius_m > 0.0 &&
         scenario.rotation_rad > 0.0 && scenario.move_duration_s > 0.0 &&
         scenario.hold_duration_s >= 0.0 && scenario.cycle_duration_s > 0.0 &&
         scenario.cycle_duration_s >= occupied_duration;
}

bool calculations_are_finite(const MotorArmCalculations &value) {
  const std::array values{
      value.holding_load_torque_nm,
      value.acceleration_load_torque_nm,
      value.required_hold_motor_torque_nm,
      value.required_move_motor_torque_nm,
      value.peak_motor_speed_rad_s,
      value.available_move_torque_nm,
      value.estimated_move_current_a,
      value.estimated_peak_temperature_c,
  };
  return std::all_of(values.begin(), values.end(),
                     [](const double item) { return std::isfinite(item); });
}

void require_margin_inputs(const double first, const double second,
                           const double denominator) {
  if (!std::isfinite(first) || !std::isfinite(second) ||
      !std::isfinite(denominator) || !(denominator > 0.0)) {
    throw std::invalid_argument("invalid signed-margin input");
  }
}

} // namespace

MotorArmBackendResult
run_motor_arm_builtin_v1(const MotorComponentInput &component,
                         const MotorArmScenario &scenario) noexcept {
  if (!all_finite(component, scenario)) {
    return MotorArmBackendFailure{MotorArmBackendError::non_finite_input};
  }
  if (!valid_domains(component, scenario)) {
    return MotorArmBackendFailure{MotorArmBackendError::invalid_domain};
  }
  if (std::fegetround() != FE_TONEAREST) {
    return MotorArmBackendFailure{
        MotorArmBackendError::unsupported_rounding_mode};
  }

  const auto holding_load = scenario.payload_mass_kg * standard_gravity_m_s2 *
                            scenario.arm_radius_m;
  const auto angular_acceleration =
      4.0 * scenario.rotation_rad /
      (scenario.move_duration_s * scenario.move_duration_s);
  const auto acceleration_load = scenario.payload_mass_kg *
                                 scenario.arm_radius_m * scenario.arm_radius_m *
                                 angular_acceleration;
  const auto drivetrain_scale =
      component.gear_ratio * component.gearbox_efficiency;
  const auto required_hold = holding_load / drivetrain_scale;
  const auto required_move =
      (holding_load + acceleration_load) / drivetrain_scale;
  const auto peak_speed = 2.0 * scenario.rotation_rad /
                          scenario.move_duration_s * component.gear_ratio;
  const auto available_move =
      std::max(0.0, component.stall_torque_nm *
                        (1.0 - peak_speed / component.no_load_speed_rad_s));
  const auto estimated_current =
      component.no_load_current_a +
      required_move / component.torque_constant_nm_a;

  const auto active_duration =
      scenario.move_duration_s + scenario.hold_duration_s;
  const auto rest_duration = scenario.cycle_duration_s - active_duration;
  const auto copper_loss = estimated_current * estimated_current *
                           component.winding_resistance_ohm;
  const auto thermal_time_constant = component.thermal_resistance_k_w *
                                     component.thermal_capacitance_j_k;
  const auto active_decay =
      std::exp(-active_duration / thermal_time_constant);
  const auto rest_decay = std::exp(-rest_duration / thermal_time_constant);
  const auto asymptotic_rise =
      copper_loss * component.thermal_resistance_k_w;
  const auto periodic_denominator = 1.0 - active_decay * rest_decay;
  const auto start_rise = rest_decay * (1.0 - active_decay) * asymptotic_rise /
                          periodic_denominator;
  const auto peak_rise = active_decay * start_rise +
                         (1.0 - active_decay) * asymptotic_rise;
  const auto peak_temperature = scenario.ambient_temperature_c + peak_rise;

  const MotorArmCalculations calculations{
      holding_load,
      acceleration_load,
      required_hold,
      required_move,
      peak_speed,
      available_move,
      estimated_current,
      peak_temperature,
  };
  if (!calculations_are_finite(calculations)) {
    return MotorArmBackendFailure{MotorArmBackendError::non_finite_output};
  }
  return MotorArmBackendOutput{calculations, motor_arm_applicability_ids};
}

double available_over_required_margin(const double available,
                                      const double required) {
  require_margin_inputs(available, required, required);
  return (available - required) / required;
}

double remaining_limit_fraction(const double limit, const double used) {
  require_margin_inputs(limit, used, limit);
  return (limit - used) / limit;
}

double remaining_limit_absolute(const double limit, const double used) {
  require_margin_inputs(limit, used, limit);
  return limit - used;
}

} // namespace prometheus::simulation
