#pragma once

#include "prometheus/cad/types.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace prometheus::cad {

class StepImporter final {
public:
  [[nodiscard]] StepImportResult import_file(const std::filesystem::path& path, double linear_deflection_m=0.0005, bool compute_interferences=true) const;
  [[nodiscard]] std::vector<StaticInterference> static_interferences(const std::filesystem::path& path,const std::vector<PartPlacement>& placements)const;
  [[nodiscard]] std::vector<SweepInterference> sweep_revolute(const std::filesystem::path& path,const std::string& moving_id,const std::string& excluded_id,const std::array<double,3>& pivot_m,const std::array<double,3>& axis,double minimum_deg,double maximum_deg,int samples=19,const std::vector<PartPlacement>& placements={})const;
};

} // namespace prometheus::cad
