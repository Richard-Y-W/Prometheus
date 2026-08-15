#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/structural_request.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace ps = prometheus::structural;

[[noreturn]] void fail(const char *message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition) fail(message);
}

ps::StructuralRequest validRequest() {
  return {
      .analysis_id = "yubi-bracket-linear-static-1",
      .component_name = "YUBI BRACKET_GRIPPER",
      .geometry_sha256 = "sha256:4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a",
      .nodes = {{4, {0.0, 0.0, 1.0}}, {2, {1.0, 0.0, 0.0}},
                {1, {0.0, 0.0, 0.0}}, {3, {0.0, 1.0, 0.0}}},
      .elements = {{1, {1, 2, 3, 4}}},
      .youngs_modulus_pa = 7.0e10,
      .poisson_ratio = 0.33,
      .fully_fixed_node_ids = {3, 1, 2},
      .nodal_forces = {{4, {0.0, 0.0, -100.0}}},
      .displacement_limit_m = 0.001,
      .von_mises_limit_pa = 1.0e8,
      .material_reviewed = true,
      .loads_reviewed = true,
      .restraints_reviewed = true,
      .requirements_reviewed = true,
      .scenario_confirmed = true,
  };
}

bool hasIssue(const std::vector<ps::ValidationIssue> &issues,
              const std::string &code) {
  return std::ranges::any_of(issues, [&](const auto &value) {
    return value.code == code;
  });
}

int main() {
  const auto request = validRequest();
  require(ps::validate_request(request).empty(), "reviewed tetra request validates");
  const auto deck = ps::generate_calculix_deck(request);
  require(deck == ps::generate_calculix_deck(request),
          "CalculiX deck bytes are deterministic");
  require(deck.find("1, 0.0000000000e+00, 0.0000000000e+00, 0.0000000000e+00") != std::string::npos,
          "nodes are ordered deterministically in SI units");
  require(deck.find("*ELEMENT, TYPE=C3D4") != std::string::npos &&
              deck.find("*STATIC, SOLVER=SPOOLES") != std::string::npos &&
              deck.find("4, 3, -1.0000000000e+02") != std::string::npos,
          "deck pins the solver and contains bounded tetra and reviewed force");

  auto blocked = request;
  blocked.material_reviewed = false;
  blocked.scenario_confirmed = false;
  blocked.fully_fixed_node_ids = {1, 2};
  blocked.displacement_limit_m.reset();
  blocked.von_mises_limit_pa.reset();
  const auto issues = ps::validate_request(blocked);
  require(hasIssue(issues, "material_unreviewed") &&
              hasIssue(issues, "scenario_unconfirmed") &&
              hasIssue(issues, "inadequate_restraints") &&
              hasIssue(issues, "missing_requirement"),
          "missing review, restraints, and requirements stay blocked");
  try {
    (void)ps::generate_calculix_deck(blocked);
    fail("invalid request generated a solver deck");
  } catch (const std::invalid_argument &) {
  }

  auto missingNode = request;
  missingNode.nodal_forces.front().node_id = 99;
  require(hasIssue(ps::validate_request(missingNode), "load_node_missing"),
          "load on missing mesh node stays blocked");

  auto zeroVolume = request;
  zeroVolume.nodes.front().position_m = {1.0, 1.0, 0.0};
  require(hasIssue(ps::validate_request(zeroVolume), "zero_volume_element"),
          "zero-volume tetrahedron stays blocked");

  auto collinear = request;
  collinear.nodes[3].position_m = {2.0, 0.0, 0.0};
  require(hasIssue(ps::validate_request(collinear), "inadequate_restraints"),
          "collinear fixed nodes stay blocked");

  constexpr auto rawDat = R"(
 displacements (vx,vy,vz) for set NALL and time  0.1000000E+01

         1  0.000000E+00  0.000000E+00  0.000000E+00
         4  0.000000E+00  0.000000E+00 -2.228571E-08

 stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set COMPONENT and time  0.1000000E+01

         1   1 -2.571429E+03 -2.571429E+03 -6.000000E+03  0.000000E+00  0.000000E+00  0.000000E+00
)";
  const auto metrics = ps::parse_calculix_dat(rawDat);
  require(metrics.displacement_rows == 2 && metrics.stress_rows == 1 &&
              std::abs(metrics.maximum_displacement_m - 2.228571e-8) < 1e-15 &&
              std::abs(metrics.maximum_von_mises_pa - 3428.571) < 1e-6,
          "raw CalculiX displacement and stress rows compile to SI maxima");
  try {
    (void)ps::parse_calculix_dat("solver stopped before result output\n");
    fail("missing solver output became metrics");
  } catch (const std::runtime_error &) {
  }

  constexpr auto rawMesh = R"(*Heading
*NODE
4, 0, 0, 10
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
*ELEMENT, type=CPS3, ELSET=Surface1
8, 1, 2, 3
*ELEMENT, type=C3D4, ELSET=Volume1
9, 1, 2, 3, 4
)";
  const auto mesh = ps::parse_gmsh_abaqus_mesh(rawMesh, 0.001);
  require(mesh.nodes.size() == 4 && mesh.elements.size() == 1 &&
              mesh.nodes.front().position_m[2] == 0.01,
          "Gmsh mesh parser retains only C3D4 and converts mm to m explicitly");
  const auto boundary = ps::extract_boundary_faces(mesh);
  require(boundary.size() == 4,
          "one tetrahedron exposes four deterministic boundary faces");
  double boundaryArea = 0.0;
  for (const auto &face : boundary) {
    boundaryArea += face.area_m2;
    const auto oppositeId = *std::ranges::find_if(
        mesh.elements.front().node_ids, [&](const int nodeId) {
          return std::ranges::find(face.node_ids, nodeId) == face.node_ids.end();
        });
    const auto oppositeNode = std::ranges::find_if(
        mesh.nodes, [&](const auto &node) { return node.id == oppositeId; });
    require(oppositeNode != mesh.nodes.end(), "opposite boundary node exists");
    const auto &opposite = oppositeNode->position_m;
    const auto towardInterior = std::array<double, 3>{
        opposite[0] - face.centroid_m[0], opposite[1] - face.centroid_m[1],
        opposite[2] - face.centroid_m[2]};
    require(face.outward_unit_normal[0] * towardInterior[0] +
                    face.outward_unit_normal[1] * towardInterior[1] +
                    face.outward_unit_normal[2] * towardInterior[2] <
                0.0,
            "boundary normals point away from the tetrahedron interior");
  }
  require(std::abs(boundaryArea - (0.00015 + std::sqrt(3.0) * 0.00005)) < 1e-12,
          "boundary face areas are reported in square metres");

  auto paired = mesh;
  paired.nodes.push_back({5, {0.0, 0.0, -0.01}});
  paired.elements.push_back({10, {1, 3, 2, 5}});
  require(ps::extract_boundary_faces(paired).size() == 6,
          "a shared tetrahedron face is excluded from the exterior boundary");
  auto nonManifold = paired;
  nonManifold.nodes.push_back({6, {0.0, 0.0, 0.02}});
  nonManifold.elements.push_back({11, {1, 2, 3, 6}});
  try {
    (void)ps::extract_boundary_faces(nonManifold);
    fail("non-manifold volume face was accepted");
  } catch (const std::invalid_argument &) {
  }
  try {
    (void)ps::parse_gmsh_abaqus_mesh(rawMesh, 0.0);
    fail("invalid mesh scale was accepted");
  } catch (const std::invalid_argument &) {
  }
  return 0;
}
