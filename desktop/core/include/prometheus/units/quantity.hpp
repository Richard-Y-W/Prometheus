#pragma once
#include <compare>

namespace prometheus::units {
template<class Dimension> class Quantity final {
public:
  explicit constexpr Quantity(double si_value) : si_value_(si_value) {}
  [[nodiscard]] constexpr double si_value() const { return si_value_; }
  constexpr auto operator<=>(const Quantity&) const = default;
  constexpr Quantity operator+(Quantity other) const { return Quantity(si_value_ + other.si_value_); }
  constexpr Quantity operator-(Quantity other) const { return Quantity(si_value_ - other.si_value_); }
private: double si_value_;
};
struct MassDimension {}; struct LengthDimension {}; struct TorqueDimension {}; struct CurrentDimension {}; struct VoltageDimension {};
using Mass = Quantity<MassDimension>; using Length = Quantity<LengthDimension>; using Torque = Quantity<TorqueDimension>; using Current = Quantity<CurrentDimension>; using Voltage = Quantity<VoltageDimension>;
constexpr Mass kilograms(double value) { return Mass(value); }
constexpr Length metres(double value) { return Length(value); }
constexpr Torque newton_metres(double value) { return Torque(value); }
}
