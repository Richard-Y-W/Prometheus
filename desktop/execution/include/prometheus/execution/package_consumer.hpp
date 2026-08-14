#pragma once

#include <prometheus/execution/diagnostic.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prometheus::execution {

struct ClaimBinding final {
  std::string slot_name;
  std::string slot_id;
  std::string claim_id;
  std::string claim_fingerprint;
  bool value_known;
  std::optional<std::string> unknown_reason;
};

struct PackageInspection final {
  std::string package_hash;
  std::string revision_id;
  std::string component_id;
  std::string manufacturer;
  std::string part_number;
  std::string revision;
  std::string component_class;
  std::string package_kind;
  std::string capability_id;
  std::string execution_readiness;
  std::vector<std::string> limitations;
  std::optional<std::string> blocked_reason;
};

struct TorqueSpeedPoint final {
  double angular_velocity_rad_s;
  double torque_nm;
  bool operator==(const TorqueSpeedPoint &) const = default;
};

struct MotorComponentInput final {
  PackageInspection package;
  double gear_ratio;
  double gearbox_efficiency_nominal;
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
  double gearbox_efficiency_minimum;
  double gearbox_efficiency_maximum;
  std::array<TorqueSpeedPoint, 2> torque_speed_curve;
  std::array<ClaimBinding, 12> calculation_inputs;
  std::array<ClaimBinding, 2> validation_inputs;
  std::vector<ClaimBinding> available_but_unused;
};

[[nodiscard]] Result<PackageInspection>
inspect_execution_component(std::string_view stored_bytes,
                            std::string_view expected_object_hash);
[[nodiscard]] Result<MotorComponentInput>
consume_motor_component(std::string_view stored_bytes,
                        std::string_view expected_object_hash);

// Identity of the one reviewed consumer contract compiled into this backend.
// Orchestrators use this value to bind requests without duplicating it.
[[nodiscard]] std::string_view
supported_motor_consumer_contract_hash() noexcept;

} // namespace prometheus::execution
