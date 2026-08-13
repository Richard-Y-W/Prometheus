#pragma once

#include <prometheus/run_store/run_store.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace prometheus::replay {

enum class ReplayStatus {
  exact_match,
  verification_failed,
  execution_unavailable,
  execution_failed,
  mismatch,
};

struct ReplayDiagnostic final {
  std::string stage;
  std::string code;
  std::string message;
};

struct ReplayReport final {
  ReplayStatus status;
  std::string manifest_hash;
  std::optional<std::string> recorded_result_hash;
  std::optional<std::string> replayed_result_hash;
  std::optional<ReplayDiagnostic> diagnostic;
};

[[nodiscard]] ReplayReport
replay_exact(const std::filesystem::path &project_path,
             std::string_view manifest_hash,
             run_store::TransactionOptions options = {}) noexcept;

[[nodiscard]] std::string_view status_name(ReplayStatus status) noexcept;
[[nodiscard]] int recommended_exit_code(ReplayStatus status) noexcept;

} // namespace prometheus::replay
