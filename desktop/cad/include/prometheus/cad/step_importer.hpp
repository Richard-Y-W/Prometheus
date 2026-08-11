#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace prometheus::cad {
struct BoundingBox final { double min_x{},min_y{},min_z{},max_x{},max_y{},max_z{}; };
struct DisplayMesh final { std::vector<float> positions; std::vector<std::uint32_t> indices; };
struct AssemblyNode final {
  std::string persistent_id;
  std::string name;
  std::array<double,16> transform{};
  BoundingBox bounds;
  double volume_m3{};
  double surface_area_m2{};
  int face_count{},edge_count{};
  DisplayMesh mesh;
  std::vector<AssemblyNode> children;
};
struct StaticInterference final { std::string first_id,second_id;double volume_m3{}; };
struct SweepInterference final { std::string moving_id,other_id;double first_angle_deg{},maximum_volume_m3{};int samples_tested{}; };
struct PartPlacement final { std::string persistent_id;double translation_x_m{},translation_y_m{},translation_z_m{},rotation_x_deg{},rotation_y_deg{},rotation_z_deg{}; };
struct StepImportResult final { std::string source_name; std::vector<AssemblyNode> roots; std::vector<StaticInterference> interferences;std::vector<std::string> warnings; };
class StepImporter final {
public:
  [[nodiscard]] StepImportResult import_file(const std::filesystem::path& path, double linear_deflection_m=0.0005, bool compute_interferences=true) const;
  [[nodiscard]] std::vector<StaticInterference> static_interferences(const std::filesystem::path& path,const std::vector<PartPlacement>& placements)const;
  [[nodiscard]] std::vector<SweepInterference> sweep_revolute(const std::filesystem::path& path,const std::string& moving_id,const std::string& excluded_id,const std::array<double,3>& pivot_m,const std::array<double,3>& axis,double minimum_deg,double maximum_deg,int samples=19,const std::vector<PartPlacement>& placements={})const;
};
}
