#include "solver_process.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <system_error>

namespace prometheus::structural::detail {
namespace {

std::wstring wide(const std::string &value) {
  if (value.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        value.data(), static_cast<int>(value.size()),
                                        nullptr, 0);
  if (count <= 0) throw std::system_error(GetLastError(), std::system_category());
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), count) != count)
    throw std::system_error(GetLastError(), std::system_category());
  return result;
}

std::wstring quote(const std::wstring &value) {
  if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
  std::wstring result{L'\"'};
  std::size_t slashes = 0;
  for (const wchar_t c : value) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'\"') result.append(slashes * 2 + 1, L'\\');
    else result.append(slashes, L'\\');
    slashes = 0;
    result.push_back(c);
  }
  result.append(slashes * 2, L'\\');
  result.push_back(L'\"');
  return result;
}

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
  static std::atomic<unsigned long long> counter{};
  const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" +
                      std::to_wstring(counter++);
  const auto stdoutPath = workingDirectory / (L".prometheus-stdout-" + suffix);
  const auto stderrPath = workingDirectory / (L".prometheus-stderr-" + suffix);
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE stdoutHandle = CreateFileW(stdoutPath.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  HANDLE stderrHandle = CreateFileW(stderrPath.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (stdoutHandle == INVALID_HANDLE_VALUE || stderrHandle == INVALID_HANDLE_VALUE) {
    if (stdoutHandle != INVALID_HANDLE_VALUE) CloseHandle(stdoutHandle);
    if (stderrHandle != INVALID_HANDLE_VALUE) CloseHandle(stderrHandle);
    result.detail = "capture_file_creation_failed";
    return result;
  }
  std::wstring command = quote(executable.wstring());
  try {
    for (const auto &argument : arguments) command += L" " + quote(wide(argument));
  } catch (const std::exception &error) {
    CloseHandle(stdoutHandle); CloseHandle(stderrHandle);
    std::filesystem::remove(stdoutPath); std::filesystem::remove(stderrPath);
    result.detail = error.what();
    return result;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = stdoutHandle;
  startup.hStdError = stderrHandle;
  PROCESS_INFORMATION process{};
  result.launched = CreateProcessW(executable.c_str(), command.data(), nullptr,
      nullptr, TRUE, CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(),
      &startup, &process) != 0;
  CloseHandle(stdoutHandle); CloseHandle(stderrHandle);
  if (!result.launched) {
    result.detail = "process_launch_failed:" + std::to_string(GetLastError());
  } else {
    CloseHandle(process.hThread);
    const auto waitMs = static_cast<DWORD>(std::min<long long>(
        timeout.count(), static_cast<long long>(INFINITE - 1)));
    const auto wait = WaitForSingleObject(process.hProcess, waitMs);
    if (wait == WAIT_TIMEOUT) {
      result.timed_out = true;
      TerminateProcess(process.hProcess, 124U);
      WaitForSingleObject(process.hProcess, 5000U);
    }
    DWORD code = 127U;
    if (GetExitCodeProcess(process.hProcess, &code)) result.exit_code = static_cast<int>(code);
    CloseHandle(process.hProcess);
  }
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
