#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/surface_groups.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: prometheus_structural_mesh_probe GMSH_ABAQUS_MESH "
                 "[MAX_NORMAL_ANGLE_DEGREES]\n";
    return 2;
  }
  double patchAngleDegrees = 5.0;
  if (argc == 3) {
    try {
      std::size_t consumed = 0;
      patchAngleDegrees = std::stod(argv[2], &consumed);
      if (consumed != std::string(argv[2]).size()) throw std::invalid_argument("suffix");
    } catch (const std::exception &) {
      std::cerr << "surface patch angle must be a number\n";
      return 2;
    }
  }
  std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
  const std::string bytes{std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>()};
  if (!input && !input.eof()) {
    std::cerr << "could not read mesh\n";
    return 3;
  }
  try {
    const auto mesh = prometheus::structural::parse_gmsh_abaqus_mesh(bytes, 0.001);
    const auto boundary = prometheus::structural::extract_boundary_faces(mesh);
    const auto patches = prometheus::structural::group_boundary_faces(
        boundary, patchAngleDegrees);
    std::array<double, 3> minimum{std::numeric_limits<double>::max(),
                                  std::numeric_limits<double>::max(),
                                  std::numeric_limits<double>::max()};
    std::array<double, 3> maximum{std::numeric_limits<double>::lowest(),
                                  std::numeric_limits<double>::lowest(),
                                  std::numeric_limits<double>::lowest()};
    for (const auto &node : mesh.nodes)
      for (int axis = 0; axis < 3; ++axis) {
        minimum[axis] = std::min(minimum[axis], node.position_m[axis]);
        maximum[axis] = std::max(maximum[axis], node.position_m[axis]);
      }
    double boundaryArea = 0.0;
    for (const auto &face : boundary) boundaryArea += face.area_m2;
    std::cout << "nodes=" << mesh.nodes.size()
              << " c3d4=" << mesh.elements.size()
              << " exterior_faces=" << boundary.size()
              << " surface_patch_angle_deg=" << patchAngleDegrees
              << " surface_patches=" << patches.size()
              << " exterior_area_m2=" << boundaryArea
              << " bounds_m=[" << minimum[0] << ',' << minimum[1] << ','
              << minimum[2] << "]-[" << maximum[0] << ',' << maximum[1]
              << ',' << maximum[2] << "]\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 4;
  }
}
