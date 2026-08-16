#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/smoke_case.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace ps = prometheus::structural;

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: prometheus_export_structural_smoke OUTPUT_DIRECTORY\n";
    return 2;
  }
  const std::filesystem::path output(argv[1]);
  std::filesystem::create_directories(output);
  const auto deck = ps::generate_calculix_deck(ps::structural_smoke_request());
  const auto path = output / (std::string(ps::calculix_smoke_job_name) + ".inp");
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(deck.data(), static_cast<std::streamsize>(deck.size()));
  if (!stream) {
    std::cerr << "could not write structural smoke deck\n";
    return 3;
  }
  std::cout << path.string() << '\n';
  return 0;
}
