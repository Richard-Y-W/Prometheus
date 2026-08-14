#pragma once

#include <array>
#include <string_view>
#include <variant>

namespace prometheus::simulation {

inline constexpr std::string_view motor_arm_backend_id =
    "motor_arm_builtin_v1";
inline constexpr std::string_view motor_arm_backend_contract_version = "1.0.0";

inline constexpr std::array<std::string_view, 6> motor_arm_applicability_ids{
    "point_payload_at_reviewed_radius",
    "horizontal_gravity_loading",
    "symmetric_triangular_velocity",
    "linear_torque_speed_model",
    "algebraic_current_estimate",
    "one_node_periodic_rc_thermal_model",
};

struct MotorComponentInput final {
  double gear_ratio;
  double gearbox_efficiency;
  double continuous_torque_nm;
  double stall_torque_nm;
  double no_load_speed_rad_s;
  double no_load_current_a;
  double torque_constant_nm_a;
  double driver_current_limit_a;
  double winding_resistance_ohm;
  double thermal_resistance_k_w;
  double thermal_capacitance_j_k;
  double maximum_temperature_c;
};

struct MotorArmScenario final {
  double payload_mass_kg;
  double arm_radius_m;
  double rotation_rad;
  double move_duration_s;
  double hold_duration_s;
  double cycle_duration_s;
  double ambient_temperature_c;
};

struct MotorArmCalculations final {
  double holding_load_torque_nm;
  double acceleration_load_torque_nm;
  double required_hold_motor_torque_nm;
  double required_move_motor_torque_nm;
  double peak_motor_speed_rad_s;
  double available_move_torque_nm;
  double estimated_move_current_a;
  double estimated_peak_temperature_c;
  bool operator==(const MotorArmCalculations &) const = default;
};

struct MotorArmBackendOutput final {
  MotorArmCalculations calculations;
  std::array<std::string_view, 6> applicability_ids;
};

enum class MotorArmBackendError {
  invalid_domain,
  non_finite_input,
  unsupported_rounding_mode,
  non_finite_output,
};

struct MotorArmBackendFailure final {
  MotorArmBackendError code;
};

using MotorArmBackendResult =
    std::variant<MotorArmBackendOutput, MotorArmBackendFailure>;

[[nodiscard]] MotorArmBackendResult
run_motor_arm_builtin_v1(const MotorComponentInput &component,
                         const MotorArmScenario &scenario) noexcept;

[[nodiscard]] double available_over_required_margin(double available,
                                                     double required);
[[nodiscard]] double remaining_limit_fraction(double limit, double used);
[[nodiscard]] double remaining_limit_absolute(double limit, double used);

} // namespace prometheus::simulation
