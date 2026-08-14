#include <cmath>
#include <iostream>
#include "prometheus/simulation/motor_arm_builtin_v1.hpp"
#include "prometheus/units/quantity.hpp"
int main() {
  using namespace prometheus;
  constexpr auto mass = units::kilograms(8.0);
  constexpr auto length = units::metres(0.2);
  static_assert(mass.si_value() == 8.0 && length.si_value() == 0.2);
  const simulation::MotorComponentInput motor{
      100, 0.70, 0.208, 1.92, 418.879, 0.18,
      0.0749, 4, 1.4, 3.2, 110, 125};
  const simulation::MotorArmScenario scenario{
      8, 0.2, std::acos(-1.0) / 2.0, 1.2, 4, 10, 35};
  const auto result = simulation::run_motor_arm_builtin_v1(motor, scenario);
  if (!std::holds_alternative<simulation::MotorArmBackendOutput>(result)) {
    std::cerr << "motor-arm backend rejected the fixed input\n";
    return 1;
  }
  return 0;
}
