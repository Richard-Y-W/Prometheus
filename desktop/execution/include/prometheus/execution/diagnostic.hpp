#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace prometheus::execution {

struct Diagnostic final {
  std::string stage;
  std::string code;
  std::string message;
  std::optional<std::string> object_hash;
  std::optional<std::string> field;
};

template <typename T> class Result final {
public:
  [[nodiscard]] static Result success(T value) {
    return Result(std::move(value));
  }

  [[nodiscard]] static Result failure(Diagnostic diagnostic) {
    return Result(std::move(diagnostic));
  }

  [[nodiscard]] bool has_value() const noexcept {
    return std::holds_alternative<T>(payload_);
  }

  [[nodiscard]] bool has_diagnostic() const noexcept {
    return std::holds_alternative<Diagnostic>(payload_);
  }

  [[nodiscard]] const T &value() const { return std::get<T>(payload_); }
  [[nodiscard]] T &value() { return std::get<T>(payload_); }

  [[nodiscard]] const Diagnostic &diagnostic() const {
    return std::get<Diagnostic>(payload_);
  }

private:
  explicit Result(T value) : payload_(std::move(value)) {}
  explicit Result(Diagnostic diagnostic) : payload_(std::move(diagnostic)) {}

  std::variant<T, Diagnostic> payload_;
};

enum class ExecutionDisposition { rejected_input, unsupported, failed, cancelled };

struct ExecutionFailure final {
  ExecutionDisposition disposition;
  std::vector<Diagnostic> diagnostics;
};

} // namespace prometheus::execution
