#include "prometheus/structural/structural_archive.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: prometheus_replay_structural_run ARCHIVE_MANIFEST\n";
    return 2;
  }
  const auto verified = prometheus::structural::verify_structural_archive(
      std::filesystem::absolute(argv[1]));
  if (!verified.valid) {
    std::cerr << verified.code << ": " << verified.detail << '\n';
    return 3;
  }
  std::cout << "status=verified max_displacement_m="
            << verified.metrics->maximum_displacement_m
            << " max_von_mises_pa=" << verified.metrics->maximum_von_mises_pa
            << " obligations=" << verified.evaluated_obligations << '/'
            << verified.declared_obligations << '\n';
  return 0;
}
