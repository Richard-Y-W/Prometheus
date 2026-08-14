#include <prometheus/replay/replay.hpp>

#include <prometheus/run_store/project_v2.hpp>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

using Json = nlohmann::json;
namespace replay = prometheus::replay;
namespace run_store = prometheus::run_store;

void print_argument_failure(const std::string &message) {
  std::cout << Json{{"code", "invalid_arguments"},
                    {"stage", "arguments"},
                    {"status", "invalid_arguments"}}
                   .dump()
            << '\n';
  std::cerr << message << '\n';
}

Json report_json(const replay::ReplayReport &report) {
  if (report.status == replay::ReplayStatus::exact_match) {
    return Json{{"manifest_hash", report.manifest_hash},
                {"recorded_result_hash", *report.recorded_result_hash},
                {"replayed_result_hash", *report.replayed_result_hash},
                {"status", replay::status_name(report.status)}};
  }
  return Json{{"code", report.diagnostic->code},
              {"manifest_hash", report.manifest_hash},
              {"stage", report.diagnostic->stage},
              {"status", replay::status_name(report.status)}};
}

} // namespace

int main(const int argc, char **argv) {
#if defined(_WIN32)
  if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
    std::cerr << "unable to configure machine-readable stdout\n";
    return 74;
  }
#endif
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "Usage: prometheus_replay --project <project.prometheus> "
                 "--run <sha256:...>\n";
    return 0;
  }
  if (argc != 5 || std::string(argv[1]) != "--project" ||
      std::string(argv[3]) != "--run" || std::string(argv[2]).empty() ||
      !run_store::is_valid_object_hash(argv[4])) {
    print_argument_failure(
        "expected exactly --project <path> --run <lowercase SHA-256 hash>");
    return 2;
  }

  const auto report = replay::replay_exact(std::filesystem::path(argv[2]),
                                           std::string(argv[4]));
  std::cout << report_json(report).dump() << '\n';
  if (report.diagnostic.has_value()) {
    std::cerr << report.diagnostic->stage << '/' << report.diagnostic->code
              << ": " << report.diagnostic->message << '\n';
  }
  return replay::recommended_exit_code(report.status);
}
