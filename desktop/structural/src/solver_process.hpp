#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace prometheus::structural::detail {

struct ProcessResult final {
  bool launched{};
  bool timed_out{};
  int exit_code{-1};
  std::chrono::milliseconds elapsed{};
  std::string standard_output;
  std::string standard_error;
  std::string detail;
};

[[nodiscard]] ProcessResult run_process(
    const std::filesystem::path &executable,
    const std::vector<std::string> &arguments,
    const std::filesystem::path &working_directory,
    std::chrono::milliseconds timeout);

} // namespace prometheus::structural::detail
