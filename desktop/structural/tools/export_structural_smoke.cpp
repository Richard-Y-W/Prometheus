#include "prometheus/structural/structural_benchmarks.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace ps = prometheus::structural;

namespace {

void write_file(const std::filesystem::path &path,
                const std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream)
    throw std::runtime_error("could not write compiled smoke artifact");
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: prometheus_export_structural_smoke OUTPUT_DIRECTORY\n";
    return 2;
  }
  const std::filesystem::path output(argv[1]);
  std::filesystem::create_directories(output);
  try {
    const auto compiled = ps::axial_tension_bar_benchmark().setup;
    const auto setupPath = output / "reviewed-structural-setup.json";
    const auto deckPath = output / "prometheus_axial_smoke.inp";
    const auto identityPath = output / "compiled-structural-setup.sha256";
    write_file(setupPath, compiled.canonical_setup_evidence);
    write_file(deckPath, compiled.calculix_deck);
    write_file(identityPath, compiled.identity + "\n");
    std::cout << "status=exported compiled_setup_identity="
              << compiled.identity << '\n'
              << "setup=" << setupPath.string() << '\n'
              << "deck=" << deckPath.string() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}
