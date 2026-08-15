#include "prometheus/structural/gmsh_mesh.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

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
      if (keyword == "*NODE" || keyword.starts_with("*NODE,"))
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

std::vector<BoundaryFace> extract_boundary_faces(const VolumeMesh &mesh) {
  if (mesh.nodes.empty() || mesh.elements.empty())
    throw std::invalid_argument("Boundary extraction requires a volume mesh");

  std::unordered_map<int, std::array<double, 3>> positions;
  for (const auto &node : mesh.nodes) {
    if (node.id <= 0 || !positions.emplace(node.id, node.position_m).second)
      throw std::invalid_argument("Volume mesh node IDs must be unique and positive");
    for (const double coordinate : node.position_m)
      if (!std::isfinite(coordinate))
        throw std::invalid_argument("Volume mesh node coordinates must be finite");
  }

  struct Candidate final {
    int element_id{};
    std::array<int, 3> oriented{};
    int opposite_node_id{};
    int occurrences{};
  };
  std::map<std::array<int, 3>, Candidate> candidates;
  std::set<int> elementIds;
  constexpr std::array<std::array<int, 4>, 4> faceIndices{{
      {{1, 2, 3, 0}}, {{0, 3, 2, 1}}, {{0, 1, 3, 2}}, {{0, 2, 1, 3}}}};
  for (const auto &element : mesh.elements) {
    if (element.id <= 0 || !elementIds.insert(element.id).second)
      throw std::invalid_argument("Volume element IDs must be unique and positive");
    std::set<int> distinct(element.node_ids.begin(), element.node_ids.end());
    if (distinct.size() != 4)
      throw std::invalid_argument("Tetrahedron must reference four distinct nodes");
    for (const int nodeId : element.node_ids)
      if (!positions.contains(nodeId))
        throw std::invalid_argument("Tetrahedron references a missing node");
    for (const auto &indices : faceIndices) {
      std::array<int, 3> face{element.node_ids[indices[0]],
                              element.node_ids[indices[1]],
                              element.node_ids[indices[2]]};
      auto key = face;
      std::ranges::sort(key);
      auto [entry, inserted] = candidates.try_emplace(
          key, Candidate{element.id, face, element.node_ids[indices[3]], 1});
      if (!inserted && ++entry->second.occurrences > 2)
        throw std::invalid_argument("Volume mesh contains a non-manifold face");
    }
  }

  std::vector<BoundaryFace> result;
  for (const auto &[key, candidate] : candidates) {
    (void)key;
    if (candidate.occurrences == 2) continue;
    auto oriented = candidate.oriented;
    const auto &a = positions.at(oriented[0]);
    const auto &b = positions.at(oriented[1]);
    const auto &c = positions.at(oriented[2]);
    const auto &opposite = positions.at(candidate.opposite_node_id);
    const std::array<double, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const std::array<double, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    std::array<double, 3> normal{
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0]};
    const double magnitude = std::sqrt(normal[0] * normal[0] +
                                       normal[1] * normal[1] +
                                       normal[2] * normal[2]);
    if (!std::isfinite(magnitude) || magnitude == 0.0)
      throw std::invalid_argument("Volume mesh contains a zero-area face");
    const std::array<double, 3> towardOpposite{
        opposite[0] - a[0], opposite[1] - a[1], opposite[2] - a[2]};
    const double orientation = normal[0] * towardOpposite[0] +
                               normal[1] * towardOpposite[1] +
                               normal[2] * towardOpposite[2];
    if (orientation > 0.0) {
      std::swap(oriented[1], oriented[2]);
      for (double &component : normal) component = -component;
    } else if (orientation == 0.0) {
      throw std::invalid_argument("Volume mesh contains a zero-volume tetrahedron");
    }
    for (double &component : normal) component /= magnitude;
    result.push_back({candidate.element_id,
                      oriented,
                      {(a[0] + b[0] + c[0]) / 3.0,
                       (a[1] + b[1] + c[1]) / 3.0,
                       (a[2] + b[2] + c[2]) / 3.0},
                      normal,
                      magnitude / 2.0});
  }
  return result;
}

} // namespace prometheus::structural
