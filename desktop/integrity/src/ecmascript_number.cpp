#include "ecmascript_number.hpp"

#include <ryu/ryu.h>

#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace prometheus::integrity::detail {
namespace {

int parse_exponent(const std::string_view encoded) {
  int exponent = 0;
  const auto *begin = encoded.data();
  const auto *end = encoded.data() + encoded.size();
  const auto result = std::from_chars(begin, end, exponent);
  if (result.ec != std::errc{} || result.ptr != end) {
    throw std::runtime_error("Ryu returned an invalid decimal exponent");
  }
  return exponent;
}

} // namespace

std::string format_ecmascript_number(const double value) {
  if (!std::isfinite(value)) {
    throw std::runtime_error("cannot format a non-finite number");
  }
  if (value == 0.0) {
    return "0";
  }

  std::array<char, 32> buffer{};
  const auto length = d2s_buffered_n(value, buffer.data());
  const std::string_view ryu(buffer.data(), static_cast<std::size_t>(length));

  const bool negative = ryu.front() == '-';
  const auto mantissa_start = negative ? 1U : 0U;
  const auto exponent_marker = ryu.find('E', mantissa_start);
  if (exponent_marker == std::string_view::npos) {
    throw std::runtime_error("Ryu returned a decimal without an exponent");
  }

  std::string digits;
  digits.reserve(exponent_marker - mantissa_start);
  for (auto index = mantissa_start; index < exponent_marker; ++index) {
    if (ryu[index] != '.') {
      digits.push_back(ryu[index]);
    }
  }
  if (digits.empty()) {
    throw std::runtime_error("Ryu returned an empty decimal mantissa");
  }

  const auto scientific_exponent =
      parse_exponent(ryu.substr(exponent_marker + 1U));
  const auto decimal_point = scientific_exponent + 1;

  std::string result;
  if (negative) {
    result.push_back('-');
  }

  if (scientific_exponent >= -6 && scientific_exponent < 21) {
    if (decimal_point <= 0) {
      result += "0.";
      result.append(static_cast<std::size_t>(-decimal_point), '0');
      result += digits;
    } else if (static_cast<std::size_t>(decimal_point) >= digits.size()) {
      result += digits;
      result.append(static_cast<std::size_t>(decimal_point) - digits.size(),
                    '0');
    } else {
      result.append(digits, 0U, static_cast<std::size_t>(decimal_point));
      result.push_back('.');
      result.append(digits, static_cast<std::size_t>(decimal_point));
    }
    return result;
  }

  result.push_back(digits.front());
  if (digits.size() > 1U) {
    result.push_back('.');
    result.append(digits, 1U);
  }
  result.push_back('e');
  if (scientific_exponent >= 0) {
    result.push_back('+');
  }
  result += std::to_string(scientific_exponent);
  return result;
}

} // namespace prometheus::integrity::detail
