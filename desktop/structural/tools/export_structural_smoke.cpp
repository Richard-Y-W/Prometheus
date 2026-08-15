#include "prometheus/structural/calculix_deck.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace ps = prometheus::structural;

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: prometheus_export_structural_smoke OUTPUT_DIRECTORY\n";
    return 2;
  }
  const std::filesystem::path output(argv[1]);
  std::filesystem::create_directories(output);
  const ps::StructuralRequest request{
      .analysis_id = "analytic-tetra-smoke-v1",
      .component_name = "analytic tetrahedron (not YUBI evidence)",
      .geometry_sha256 = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      .nodes = {{1, {0.0, 0.0, 0.0}}, {2, {1.0, 0.0, 0.0}},
                {3, {0.0, 1.0, 0.0}}, {4, {0.0, 0.0, 1.0}}},
      .elements = {{1, {1, 2, 3, 4}}},
      .youngs_modulus_pa = 2.0e11,
      .poisson_ratio = 0.3,
      .fully_fixed_node_ids = {1, 2, 3},
      .nodal_forces = {{4, {0.0, 0.0, -1000.0}}},
      .displacement_limit_m = 1.0e-6,
      .von_mises_limit_pa = 1.0e7,
      .material_reviewed = true,
      .loads_reviewed = true,
      .restraints_reviewed = true,
      .requirements_reviewed = true,
      .scenario_confirmed = true,
  };
  const auto deck = ps::generate_calculix_deck(request);
  const auto path = output / "prometheus_tetra_smoke.inp";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(deck.data(), static_cast<std::streamsize>(deck.size()));
  if (!stream) {
    std::cerr << "could not write structural smoke deck\n";
    return 3;
  }
  std::cout << path.string() << '\n';
  return 0;
}
