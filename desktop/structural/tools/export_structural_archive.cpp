#include "prometheus/structural/structural_archive.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: prometheus_export_structural_archive MANIFEST DESTINATION\n";
    return 2;
  }
  try {
    const auto exported = prometheus::structural::export_structural_archive(
        std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
    std::cout << "status=exported manifest=" << exported.manifest_path.string()
              << " sha256=" << exported.manifest_sha256 << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "archive_export_failed: " << error.what() << '\n';
    return 3;
  }
}
