#include "solver_process.hpp"

#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace prometheus::structural::detail {
namespace {
std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace

ProcessResult run_process(const std::filesystem::path &executable,
                          const std::vector<std::string> &arguments,
                          const std::filesystem::path &workingDirectory,
                          const std::chrono::milliseconds timeout) {
  ProcessResult result;
  const auto started = std::chrono::steady_clock::now();
  const auto suffix = std::to_string(::getpid());
  const auto stdoutPath = workingDirectory / (".prometheus-stdout-" + suffix);
  const auto stderrPath = workingDirectory / (".prometheus-stderr-" + suffix);
  const pid_t child = ::fork();
  if (child < 0) { result.detail = "process_launch_failed"; return result; }
  if (child == 0) {
    const int out = ::open(stdoutPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    const int err = ::open(stderrPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (out < 0 || err < 0 || ::dup2(out, STDOUT_FILENO) < 0 ||
        ::dup2(err, STDERR_FILENO) < 0 || ::chdir(workingDirectory.c_str()) != 0)
      ::_exit(126);
    ::close(out); ::close(err);
    std::vector<std::string> owned{executable.string()};
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    for (auto &argument : owned) argv.push_back(argument.data());
    argv.push_back(nullptr);
    ::execv(executable.c_str(), argv.data());
    ::_exit(127);
  }
  result.launched = true;
  int status = 0;
  const auto deadline = started + timeout;
  while (::waitpid(child, &status, WNOHANG) == 0) {
    if (std::chrono::steady_clock::now() >= deadline) {
      result.timed_out = true;
      ::kill(child, SIGKILL);
      ::waitpid(child, &status, 0);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
  result.standard_output = read(stdoutPath);
  result.standard_error = read(stderrPath);
  std::error_code ignored;
  std::filesystem::remove(stdoutPath, ignored);
  std::filesystem::remove(stderrPath, ignored);
  result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  return result;
}

} // namespace prometheus::structural::detail
