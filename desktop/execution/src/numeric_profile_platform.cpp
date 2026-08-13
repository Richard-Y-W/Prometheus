#include "numeric_profile_internal.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <version>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__) && defined(__GLIBC__)
#include <gnu/libc-version.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#endif

namespace prometheus::execution::detail {
namespace {

std::optional<ToolIdentity> standard_library_identity() {
#if defined(_LIBCPP_VERSION)
  return ToolIdentity{"libc++", std::to_string(_LIBCPP_VERSION)};
#elif defined(__GLIBCXX__)
  return ToolIdentity{"libstdc++", std::to_string(__GLIBCXX__)};
#elif defined(_MSVC_STL_VERSION) && defined(_MSVC_STL_UPDATE)
  return ToolIdentity{"msvc-stl", std::to_string(_MSVC_STL_VERSION) + "." +
                                      std::to_string(_MSVC_STL_UPDATE)};
#else
  return std::nullopt;
#endif
}

#if defined(__APPLE__)
std::string apple_library_version(const std::int32_t encoded) {
  const auto value = static_cast<std::uint32_t>(encoded);
  return std::to_string(value >> 16U) + "." +
         std::to_string((value >> 8U) & 0xffU) + "." +
         std::to_string(value & 0xffU);
}
#elif defined(_WIN32)
std::string windows_architecture() {
#if defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
  return "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
  return "x86";
#else
  return {};
#endif
}

std::optional<std::string> windows_release() {
  using RtlGetVersionFunction = LONG(WINAPI *)(OSVERSIONINFOW *);
  const auto module = GetModuleHandleW(L"ntdll.dll");
  if (module == nullptr) {
    return std::nullopt;
  }
  const auto function = reinterpret_cast<RtlGetVersionFunction>(
      GetProcAddress(module, "RtlGetVersion"));
  if (function == nullptr) {
    return std::nullopt;
  }
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  if (function(&version) != 0) {
    return std::nullopt;
  }
  return std::to_string(version.dwMajorVersion) + "." +
         std::to_string(version.dwMinorVersion) + "." +
         std::to_string(version.dwBuildNumber);
}

std::optional<std::string> ucrt_file_version() {
  const auto ucrt = GetModuleHandleW(L"ucrtbase.dll");
  if (ucrt == nullptr) {
    return std::nullopt;
  }
  std::wstring path(32768U, L'\0');
  const auto length =
      GetModuleFileNameW(ucrt, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0U || static_cast<std::size_t>(length) >= path.size()) {
    return std::nullopt;
  }
  path.resize(length);

  const auto version_module = LoadLibraryW(L"version.dll");
  if (version_module == nullptr) {
    return std::nullopt;
  }
  using GetSizeFunction = DWORD(WINAPI *)(LPCWSTR, LPDWORD);
  using GetInfoFunction = BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, LPVOID);
  using QueryFunction = BOOL(WINAPI *)(LPCVOID, LPCWSTR, LPVOID *, PUINT);
  const auto get_size = reinterpret_cast<GetSizeFunction>(
      GetProcAddress(version_module, "GetFileVersionInfoSizeW"));
  const auto get_info = reinterpret_cast<GetInfoFunction>(
      GetProcAddress(version_module, "GetFileVersionInfoW"));
  const auto query = reinterpret_cast<QueryFunction>(
      GetProcAddress(version_module, "VerQueryValueW"));
  if (get_size == nullptr || get_info == nullptr || query == nullptr) {
    FreeLibrary(version_module);
    return std::nullopt;
  }
  DWORD ignored = 0U;
  const auto size = get_size(path.c_str(), &ignored);
  if (size == 0U) {
    FreeLibrary(version_module);
    return std::nullopt;
  }
  std::vector<unsigned char> bytes(size);
  if (!get_info(path.c_str(), 0U, size, bytes.data())) {
    FreeLibrary(version_module);
    return std::nullopt;
  }
  VS_FIXEDFILEINFO *fixed = nullptr;
  UINT fixed_size = 0U;
  void *raw_fixed = nullptr;
  const auto queried = query(bytes.data(), L"\\", &raw_fixed, &fixed_size);
  FreeLibrary(version_module);
  if (!queried || raw_fixed == nullptr || fixed_size < sizeof(VS_FIXEDFILEINFO)) {
    return std::nullopt;
  }
  fixed = static_cast<VS_FIXEDFILEINFO *>(raw_fixed);
  return std::to_string(HIWORD(fixed->dwFileVersionMS)) + "." +
         std::to_string(LOWORD(fixed->dwFileVersionMS)) + "." +
         std::to_string(HIWORD(fixed->dwFileVersionLS)) + "." +
         std::to_string(LOWORD(fixed->dwFileVersionLS));
}
#endif

} // namespace

std::optional<PlatformRuntimeIdentity> platform_runtime_identity() {
  const auto standard_library = standard_library_identity();
  if (!standard_library.has_value()) {
    return std::nullopt;
  }

#if defined(__APPLE__)
  utsname system{};
  if (uname(&system) != 0) {
    return std::nullopt;
  }
  const auto libsystem_version = NSVersionOfRunTimeLibrary("System");
  if (libsystem_version < 0) {
    return std::nullopt;
  }
  return PlatformRuntimeIdentity{
      PlatformIdentity{"macos", system.release, system.machine},
      *standard_library,
      ToolIdentity{"apple-libSystem",
                   apple_library_version(libsystem_version)},
  };
#elif defined(__linux__) && defined(__GLIBC__)
  utsname system{};
  if (uname(&system) != 0) {
    return std::nullopt;
  }
  const auto *glibc_version = gnu_get_libc_version();
  if (glibc_version == nullptr || *glibc_version == '\0') {
    return std::nullopt;
  }
  return PlatformRuntimeIdentity{
      PlatformIdentity{"linux", system.release, system.machine},
      *standard_library,
      ToolIdentity{"glibc-libm", glibc_version},
  };
#elif defined(_WIN32)
  const auto release = windows_release();
  const auto architecture = windows_architecture();
  const auto ucrt_version = ucrt_file_version();
  if (!release.has_value() || architecture.empty() ||
      !ucrt_version.has_value()) {
    return std::nullopt;
  }
  return PlatformRuntimeIdentity{
      PlatformIdentity{"windows", *release, architecture},
      *standard_library,
      ToolIdentity{"ucrtbase", *ucrt_version},
  };
#else
  return std::nullopt;
#endif
}

} // namespace prometheus::execution::detail
