#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace prometheus::cad {

struct BoundingBox final {
  double min_x{}, min_y{}, min_z{}, max_x{}, max_y{}, max_z{};
};

struct DisplayMesh final {
  std::vector<float> positions;
  std::vector<std::uint32_t> indices;
};

struct AssemblyNode final {
  std::string persistent_id;
  std::string name;
  std::array<double, 16> transform{};
  BoundingBox bounds;
  double volume_m3{};
  double surface_area_m2{};
  int face_count{}, edge_count{};
  DisplayMesh mesh;
  std::vector<AssemblyNode> children;
};

struct StaticInterference final {
  std::string first_id, second_id;
  double volume_m3{};
};

struct SweepInterference final {
  std::string moving_id, other_id;
  double first_angle_deg{}, maximum_volume_m3{};
  int samples_tested{};
};

struct PartPlacement final {
  std::string persistent_id;
  double translation_x_m{}, translation_y_m{}, translation_z_m{};
  double rotation_x_deg{}, rotation_y_deg{}, rotation_z_deg{};
};

struct StepImportResult final {
  std::string source_name;
  std::vector<AssemblyNode> roots;
  std::vector<StaticInterference> interferences;
  std::vector<std::string> warnings;
};

} // namespace prometheus::cad
