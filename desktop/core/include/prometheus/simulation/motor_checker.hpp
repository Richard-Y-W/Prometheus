#pragma once
#include <optional>
#include <stdexcept>
#include "prometheus/units/quantity.hpp"

namespace prometheus::simulation {
struct MotorCheckInput final {
  units::Torque load_torque;
  double gear_ratio;
  double gearbox_efficiency;
  units::Torque available_motor_torque;
};
struct MotorCheckResult final { units::Torque required_motor_torque; double signed_margin_fraction; };
inline MotorCheckResult check_motor_torque(const MotorCheckInput& input) {
  if (input.gear_ratio <= 0.0 || input.gearbox_efficiency <= 0.0 || input.gearbox_efficiency > 1.0) throw std::invalid_argument("invalid drivetrain parameters");
  const auto required = units::newton_metres(input.load_torque.si_value() / (input.gear_ratio * input.gearbox_efficiency));
  if (required.si_value() <= 0.0) throw std::invalid_argument("required torque must be positive");
  return {required, (input.available_motor_torque.si_value() - required.si_value()) / required.si_value()};
}
}
