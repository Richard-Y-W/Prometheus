#include "prometheus/structural/calculix_result.hpp"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace prometheus::structural {

CalculixMetrics parse_calculix_dat(const std::string_view rawDat) {
  enum class Section { none, displacement, stress };
  Section section = Section::none;
  CalculixMetrics result;
  std::istringstream input{std::string(rawDat)};
  input.imbue(std::locale::classic());
  std::string line;
  while (std::getline(input, line)) {
    if (line.find("displacements (vx,vy,vz)") != std::string::npos) {
      section = Section::displacement;
      continue;
    }
    if (line.find("stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz)") !=
        std::string::npos) {
      section = Section::stress;
      continue;
    }
    std::istringstream row(line);
    row.imbue(std::locale::classic());
    if (section == Section::displacement) {
      int node{};
      double x{}, y{}, z{};
      if (row >> node >> x >> y >> z) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
          throw std::runtime_error("CalculiX displacement output is non-finite");
        const auto magnitude = std::hypot(x, y, z);
        result.maximum_displacement_m =
            std::max(result.maximum_displacement_m, magnitude);
        result.displacements.push_back({node, x, y, z, magnitude});
        ++result.displacement_rows;
      }
    } else if (section == Section::stress) {
      int element{}, integrationPoint{};
      double xx{}, yy{}, zz{}, xy{}, xz{}, yz{};
      if (row >> element >> integrationPoint >> xx >> yy >> zz >> xy >> xz >> yz) {
        const double vonMises = std::sqrt(
            0.5 * ((xx - yy) * (xx - yy) + (yy - zz) * (yy - zz) +
                   (zz - xx) * (zz - xx)) +
            3.0 * (xy * xy + xz * xz + yz * yz));
        if (!std::isfinite(vonMises))
          throw std::runtime_error("CalculiX stress output is non-finite");
        result.maximum_von_mises_pa =
            std::max(result.maximum_von_mises_pa, vonMises);
        result.stresses.push_back(
            {element, integrationPoint, xx, yy, zz, xy, xz, yz, vonMises});
        ++result.stress_rows;
      }
    }
  }
  if (result.displacement_rows == 0 || result.stress_rows == 0)
    throw std::runtime_error(
        "CalculiX output is missing required displacement or stress rows");
  return result;
}

} // namespace prometheus::structural
