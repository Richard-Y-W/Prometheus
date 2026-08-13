#include <prometheus/execution/execute.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

std::string read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("unable to open probe input");
  }
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("unable to open probe output");
  }
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    throw std::runtime_error("unable to write probe output");
  }
}

} // namespace

int main(const int argc, char **argv) {
  try {
    if (argc != 9) {
      std::cerr << "usage: probe PACKAGE PACKAGE_HASH SCENARIO SCENARIO_HASH "
                   "REQUEST REQUEST_HASH RESULT_OUT MANIFEST_OUT\n";
      return 64;
    }
    const prometheus::execution::ExecutionInput input{
        read_file(argv[1]), argv[2], read_file(argv[3]), argv[4],
        read_file(argv[5]), argv[6],
    };
    const auto outcome = prometheus::execution::execute(input);
    const auto *completed =
        std::get_if<prometheus::execution::CompletedExecution>(&outcome);
    if (completed == nullptr) {
      const auto &failure =
          std::get<prometheus::execution::ExecutionFailure>(outcome);
      std::cerr << "execution failed with disposition "
                << static_cast<int>(failure.disposition) << '\n';
      return 2;
    }
    write_file(argv[7], completed->result.bytes);
    write_file(argv[8], completed->manifest.bytes);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
