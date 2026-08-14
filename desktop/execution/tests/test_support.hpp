#pragma once

#include <prometheus/execution/diagnostic.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace prometheus::execution::test {

inline void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline void require_near(const double actual, const double expected,
                         const double tolerance, const std::string &message) {
  require(std::isfinite(actual), message + ": actual value is not finite");
  require(std::abs(actual - expected) <= tolerance, message);
}

template <typename T>
const T &require_success(const Result<T> &result, const std::string &message) {
  require(result.has_value(), message + ": expected a value");
  require(!result.has_diagnostic(),
          message + ": success must not contain a diagnostic");
  return result.value();
}

template <typename T>
T require_success(Result<T> &&result, const std::string &message) {
  require(result.has_value(), message + ": expected a value");
  require(!result.has_diagnostic(),
          message + ": success must not contain a diagnostic");
  return std::move(result.value());
}

template <typename T>
const Diagnostic &require_failure(const Result<T> &result,
                                  const std::string &expected_stage,
                                  const std::string &expected_code) {
  require(!result.has_value(), "failure must not contain a partial value");
  require(result.has_diagnostic(), "failure must contain one diagnostic");
  const auto &diagnostic = result.diagnostic();
  require(diagnostic.stage == expected_stage,
          "unexpected diagnostic stage: " + diagnostic.stage);
  require(diagnostic.code == expected_code,
          "unexpected diagnostic code: " + diagnostic.code);
  require(!diagnostic.message.empty(), "diagnostic message must be non-empty");
  return diagnostic;
}

template <typename T>
Diagnostic require_failure(Result<T> &&result,
                           const std::string &expected_stage,
                           const std::string &expected_code) {
  require(!result.has_value(), "failure must not contain a partial value");
  require(result.has_diagnostic(), "failure must contain one diagnostic");
  auto diagnostic = result.diagnostic();
  require(diagnostic.stage == expected_stage,
          "unexpected diagnostic stage: " + diagnostic.stage);
  require(diagnostic.code == expected_code,
          "unexpected diagnostic code: " + diagnostic.code);
  require(!diagnostic.message.empty(), "diagnostic message must be non-empty");
  return diagnostic;
}

} // namespace prometheus::execution::test
