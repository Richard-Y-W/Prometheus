#include "prometheus/structural/gmsh_mesh.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace prometheus::structural {
namespace {

std::string upper(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return value;
}

std::istringstream fields(std::string line) {
  std::ranges::replace(line, ',', ' ');
  std::istringstream result(std::move(line));
  result.imbue(std::locale::classic());
  return result;
}

} // namespace

VolumeMesh parse_gmsh_abaqus_mesh(const std::string_view rawMesh,
                                  const double coordinateScaleToM) {
  if (!std::isfinite(coordinateScaleToM) || coordinateScaleToM <= 0.0)
    throw std::invalid_argument("Mesh coordinate scale must be finite and positive");
  enum class Section { ignored, nodes, tetrahedra };
  Section section = Section::ignored;
  VolumeMesh result;
  std::set<int> nodeIds;
  std::set<int> elementIds;
  std::istringstream input{std::string(rawMesh)};
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    if (line.front() == '*') {
      const auto keyword = upper(line);
      if (keyword.starts_with("*NODE"))
        section = Section::nodes;
      else if (keyword.starts_with("*ELEMENT") &&
               keyword.find("TYPE=C3D4") != std::string::npos)
        section = Section::tetrahedra;
      else
        section = Section::ignored;
      continue;
    }
    if (section == Section::nodes) {
      auto row = fields(line);
      Node node;
      if (!(row >> node.id >> node.position_m[0] >> node.position_m[1] >>
            node.position_m[2]))
        throw std::runtime_error("Malformed Gmsh/Abaqus node row");
      if (node.id <= 0 || !nodeIds.insert(node.id).second)
        throw std::runtime_error("Gmsh/Abaqus node IDs are not unique and positive");
      for (auto &coordinate : node.position_m) coordinate *= coordinateScaleToM;
      result.nodes.push_back(node);
    } else if (section == Section::tetrahedra) {
      auto row = fields(line);
      Tetrahedron element;
      if (!(row >> element.id >> element.node_ids[0] >> element.node_ids[1] >>
            element.node_ids[2] >> element.node_ids[3]))
        throw std::runtime_error("Malformed Gmsh/Abaqus C3D4 row");
      if (element.id <= 0 || !elementIds.insert(element.id).second)
        throw std::runtime_error("Gmsh/Abaqus C3D4 IDs are not unique and positive");
      result.elements.push_back(element);
    }
  }
  if (result.nodes.empty() || result.elements.empty())
    throw std::runtime_error("Gmsh/Abaqus mesh has no nodes or C3D4 volume elements");
  for (const auto &element : result.elements)
    for (const int nodeId : element.node_ids)
      if (!nodeIds.contains(nodeId))
        throw std::runtime_error("Gmsh/Abaqus C3D4 references a missing node");
  return result;
}

} // namespace prometheus::structural
