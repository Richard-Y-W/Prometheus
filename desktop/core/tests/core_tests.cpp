#include <cmath>
#include <iostream>
#include "prometheus/simulation/motor_checker.hpp"
#include "prometheus/simulation/motor_arm_analysis.hpp"
int main() {
  using namespace prometheus;
  const auto result = simulation::check_motor_torque({units::newton_metres(15.69064), 100.0, 0.7, units::newton_metres(0.208)});
  if (std::abs(result.required_motor_torque.si_value() - 0.224152) > 1e-6) { std::cerr << "torque conversion failed\n"; return 1; }
  if (result.signed_margin_fraction >= 0.0) { std::cerr << "negative margin not detected\n"; return 1; }
  constexpr auto mass = units::kilograms(8.0);
  constexpr auto length = units::metres(0.2);
  static_assert(mass.si_value() == 8.0 && length.si_value() == 0.2);
  const simulation::MotorArmInput arm{8,0.2,std::acos(-1.0)/2,1.2,4,10,35,100,0.70,0.208,1.92,418.879,0.18,0.0749,4,1.4,3.2,110,125};
  const auto analysis=simulation::analyze_motor_arm(arm);
  if(analysis.move_margin<=0||analysis.hold_margin>=0||analysis.current_margin<=0||analysis.steady_cycle_temperature_c<=35){std::cerr<<"motor-arm classification failed\n";return 1;}
  return 0;
}
