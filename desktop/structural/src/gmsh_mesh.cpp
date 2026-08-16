#include "prometheus/structural/gmsh_mesh.hpp"

#include "prometheus/structural/mesh_validation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

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

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == std::string::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r");
  return value.substr(first, last - first + 1U);
}

std::string element_set_name(const std::string &line) {
  const auto keyword = upper(line);
  constexpr std::string_view option = "ELSET=";
  const auto position = keyword.find(option);
  if (position == std::string::npos)
    return {};
  const auto value_start = position + option.size();
  const auto value_end = line.find(',', value_start);
  return trim(line.substr(value_start, value_end - value_start));
}

} // namespace

ParsedGmshAbaqusSource parse_gmsh_abaqus_source(
    const std::string_view rawMesh, const double coordinateScaleToM) {
  if (!std::isfinite(coordinateScaleToM) || coordinateScaleToM <= 0.0)
    throw std::invalid_argument(
        "Mesh coordinate scale must be finite and positive");

  enum class Section { ignored, nodes, tetrahedra, triangles, element_set };
  Section section = Section::ignored;
  ParsedGmshAbaqusSource result;
  std::set<int> nodeIds;
  std::set<int> allElementIds;
  std::set<int> tetrahedronIds;
  std::map<int, SourceSurfaceTriangle> trianglesById;
  std::vector<std::pair<std::string, std::vector<int>>> triangleBlocks;
  std::map<std::string, std::size_t> triangleBlockIndices;
  std::vector<std::pair<std::string, std::vector<int>>> elementSets;
  std::map<std::string, std::size_t> elementSetIndices;
  std::vector<std::set<int>> elementSetMemberIds;
  std::optional<std::size_t> activeTriangleBlock;
  std::optional<std::size_t> activeElementSet;

  std::istringstream input{std::string(rawMesh)};
  std::string line;
  while (std::getline(input, line)) {
    line = trim(std::move(line));
    if (line.empty())
      continue;
    if (line.front() == '*') {
      activeTriangleBlock.reset();
      activeElementSet.reset();
      const auto keyword = upper(line);
      if (keyword == "*NODE" || keyword.starts_with("*NODE,")) {
        section = Section::nodes;
      } else if (keyword.starts_with("*ELEMENT") &&
                 keyword.find("TYPE=C3D4") != std::string::npos) {
        section = Section::tetrahedra;
      } else if (keyword.starts_with("*ELEMENT") &&
                 keyword.find("TYPE=CPS3") != std::string::npos) {
        const auto groupName = element_set_name(line);
        if (groupName.empty())
          throw std::runtime_error(
              "Gmsh/Abaqus CPS3 section requires an ELSET name");
        auto [group, inserted] =
            triangleBlockIndices.emplace(groupName, triangleBlocks.size());
        if (inserted)
          triangleBlocks.emplace_back(groupName, std::vector<int>{});
        activeTriangleBlock = group->second;
        section = Section::triangles;
      } else if (keyword.starts_with("*ELSET")) {
        if (keyword.find("GENERATE") != std::string::npos)
          throw std::runtime_error(
              "Gmsh/Abaqus generated ELSET ranges are unsupported");
        const auto groupName = element_set_name(line);
        if (groupName.empty())
          throw std::runtime_error("Gmsh/Abaqus ELSET requires a name");
        auto [group, inserted] =
            elementSetIndices.emplace(groupName, elementSets.size());
        if (!inserted)
          throw std::runtime_error(
              "Gmsh/Abaqus ELSET names must be unique");
        elementSets.emplace_back(groupName, std::vector<int>{});
        elementSetMemberIds.emplace_back();
        activeElementSet = group->second;
        section = Section::element_set;
      } else {
        section = Section::ignored;
      }
      continue;
    }

    if (section == Section::nodes) {
      auto row = fields(line);
      Node node;
      if (!(row >> node.id >> node.position_m[0] >> node.position_m[1] >>
            node.position_m[2]))
        throw std::runtime_error("Malformed Gmsh/Abaqus node row");
      if (node.id <= 0 || !nodeIds.insert(node.id).second)
        throw std::runtime_error(
            "Gmsh/Abaqus node IDs are not unique and positive");
      for (auto &coordinate : node.position_m)
        coordinate *= coordinateScaleToM;
      result.mesh.nodes.push_back(node);
    } else if (section == Section::tetrahedra) {
      auto row = fields(line);
      Tetrahedron element;
      if (!(row >> element.id >> element.node_ids[0] >> element.node_ids[1] >>
            element.node_ids[2] >> element.node_ids[3]))
        throw std::runtime_error("Malformed Gmsh/Abaqus C3D4 row");
      if (element.id <= 0 || !allElementIds.insert(element.id).second)
        throw std::runtime_error(
            "Gmsh/Abaqus element IDs are not unique and positive");
      tetrahedronIds.insert(element.id);
      result.mesh.elements.push_back(element);
    } else if (section == Section::triangles) {
      auto row = fields(line);
      SourceSurfaceTriangle triangle;
      if (!(row >> triangle.source_element_id >> triangle.node_ids[0] >>
            triangle.node_ids[1] >> triangle.node_ids[2]))
        throw std::runtime_error("Malformed Gmsh/Abaqus CPS3 row");
      if (triangle.source_element_id <= 0 ||
          !allElementIds.insert(triangle.source_element_id).second ||
          !trianglesById
               .emplace(triangle.source_element_id, triangle)
               .second)
        throw std::runtime_error(
            "Gmsh/Abaqus element IDs are not unique and positive");
      triangleBlocks.at(*activeTriangleBlock)
          .second.push_back(triangle.source_element_id);
    } else if (section == Section::element_set) {
      auto row = fields(line);
      int elementId{};
      bool parsed = false;
      auto &members = elementSets.at(*activeElementSet).second;
      auto &memberIds = elementSetMemberIds.at(*activeElementSet);
      while (row >> elementId) {
        if (elementId <= 0 || !memberIds.insert(elementId).second)
          throw std::runtime_error(
              "Gmsh/Abaqus ELSET members must be unique and positive");
        members.push_back(elementId);
        parsed = true;
      }
      if (!parsed || !row.eof())
        throw std::runtime_error("Malformed Gmsh/Abaqus ELSET row");
    }
  }

  if (result.mesh.nodes.empty() || result.mesh.elements.empty())
    throw std::runtime_error(
        "Gmsh/Abaqus mesh has no nodes or C3D4 volume elements");
  for (const auto &element : result.mesh.elements)
    for (const int nodeId : element.node_ids)
      if (!nodeIds.contains(nodeId))
        throw std::runtime_error(
            "Gmsh/Abaqus C3D4 references a missing node");

  bool hasPhysicalSurfaceGroups = false;
  for (const auto &[name, members] : elementSets) {
    bool hasTriangles = false;
    bool hasTetrahedra = false;
    for (const int elementId : members) {
      hasTriangles = hasTriangles || trianglesById.contains(elementId);
      hasTetrahedra = hasTetrahedra || tetrahedronIds.contains(elementId);
      if (!trianglesById.contains(elementId) &&
          !tetrahedronIds.contains(elementId))
        throw std::runtime_error(
            "Gmsh/Abaqus ELSET references an unknown element");
    }
    if (hasTriangles && hasTetrahedra)
      throw std::runtime_error(
          "Gmsh/Abaqus ELSET mixes surface and volume elements");
    if (!hasTriangles)
      continue;
    hasPhysicalSurfaceGroups = true;
    SourceSurfaceCandidate group{.name = name};
    for (const int elementId : members)
      group.triangles.push_back(trianglesById.at(elementId));
    result.source_surface_candidates.push_back(std::move(group));
  }
  if (!hasPhysicalSurfaceGroups) {
    for (const auto &[name, members] : triangleBlocks) {
      SourceSurfaceCandidate group{.name = name};
      for (const int elementId : members)
        group.triangles.push_back(trianglesById.at(elementId));
      result.source_surface_candidates.push_back(std::move(group));
    }
  }
  return result;
}

VolumeMesh parse_gmsh_abaqus_mesh(const std::string_view rawMesh,
                                  const double coordinateScaleToM) {
  return parse_gmsh_abaqus_source(rawMesh, coordinateScaleToM).mesh;
}

std::vector<BoundaryFace> extract_boundary_faces(const VolumeMesh &mesh) {
  try {
    return validate_and_measure_mesh(mesh, {}).boundary_faces;
  } catch (const std::invalid_argument &) {
    throw;
  } catch (const std::exception &error) {
    throw std::invalid_argument(error.what());
  }
}

} // namespace prometheus::structural
