#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/mesh_validation.hpp"
#include "prometheus/structural/structural_request.hpp"
#include "prometheus/structural/surface_setup.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace ps = prometheus::structural;

[[noreturn]] void fail(const char *message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition) fail(message);
}

void requireThrowsContaining(const std::function<void()> &operation,
                             const std::string &expected,
                             const char *message) {
  try {
    operation();
  } catch (const std::exception &error) {
    require(std::string(error.what()).find(expected) != std::string::npos,
            message);
    return;
  }
  fail(message);
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
      .material_designation = "A2024",
      .material_temper = "T351",
      .material_product_form = "plate",
      .material_applicability = "assumed",
      .material_evidence_sha256 =
          "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
      .mesh_sha256 =
          "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
      .restraint_surface_groups = {"SurfaceFixed"},
      .load_surface_groups = {"SurfaceLoad"},
      .reviewed_force_magnitude_n = 100.0,
      .reviewed_force_direction = {0.0, 0.0, -1.0},
      .selected_load_area_m2 = 0.01,
      .mesh_target_size_m = 0.002,
      .minimum_mean_ratio_threshold = 0.05,
      .observed_minimum_mean_ratio = 0.75,
      .displacement_limit_basis = "reviewed test displacement requirement",
      .von_mises_limit_basis = "reviewed test stress requirement",
      .mesh_reviewed = true,
  };
}

bool hasIssue(const std::vector<ps::ValidationIssue> &issues,
              const std::string &code) {
  return std::ranges::any_of(issues, [&](const auto &value) {
    return value.code == code;
  });
}

ps::VolumeMesh twoTetraSurfaceMesh() {
  ps::VolumeMesh mesh;
  mesh.nodes = {{1, {0.0, 0.0, 0.0}}, {2, {1.0, 0.0, 0.0}},
                {3, {0.0, 1.0, 0.0}}, {4, {0.0, 0.0, 1.0}},
                {5, {0.0, 0.0, -1.0}}};
  mesh.elements = {{1, {1, 2, 3, 4}}, {2, {1, 3, 2, 5}}};
  const auto validated = ps::validate_and_measure_mesh(
      mesh.nodes, mesh.elements,
      {{.name = "Fixed", .triangles = {{10, {1, 2, 4}}}},
       {.name = "Load",
        .triangles = {{11, {1, 3, 4}}, {12, {3, 2, 5}}}},
       {.name = "Other",
        .triangles = {{13, {2, 3, 4}},
                      {14, {1, 3, 5}},
                      {15, {1, 2, 5}}}}});
  mesh.surface_groups = validated.surface_groups;
  mesh.diagnostics = validated.diagnostics;
  return mesh;
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

  auto zeroForce = request;
  zeroForce.nodal_forces = {{4, {0.0, 0.0, 0.0}}};
  zeroForce.reviewed_force_magnitude_n = 0.0;
  require(hasIssue(ps::validate_request(zeroForce), "zero_resultant_load"),
          "an all-zero load stays blocked");

  auto duplicateForce = request;
  duplicateForce.nodal_forces.push_back({4, {1.0, 0.0, 0.0}});
  require(hasIssue(ps::validate_request(duplicateForce), "duplicate_load_node"),
          "duplicate nodal forces stay blocked");

  auto malformedHash = request;
  malformedHash.geometry_sha256 =
      "sha256:4A6FBA05B237B725BE2CA4E5BA7F7617674B4BCAE4164FF32E88D9E75275017A";
  require(hasIssue(ps::validate_request(malformedHash),
                   "invalid_geometry_identity"),
          "uppercase SHA-256 stays blocked");

  auto injectedHeading = request;
  injectedHeading.component_name = "bracket\n*INCLUDE, INPUT=other.inp";
  require(hasIssue(ps::validate_request(injectedHeading),
                   "unsafe_heading_text"),
          "CalculiX keyword injection stays blocked");

  auto unknownMaterial = request;
  unknownMaterial.material_applicability = "unresolved";
  require(hasIssue(ps::validate_request(unknownMaterial),
                   "material_applicability_unresolved"),
          "an unresolved material cannot become reviewed");

  auto weakMesh = request;
  weakMesh.observed_minimum_mean_ratio = 0.09;
  weakMesh.minimum_mean_ratio_threshold = 0.10;
  require(hasIssue(ps::validate_request(weakMesh),
                   "mesh_quality_below_limit"),
          "a mesh below its predeclared quality floor stays blocked");

  auto mismatchedResultant = request;
  mismatchedResultant.reviewed_force_magnitude_n = 101.0;
  require(hasIssue(ps::validate_request(mismatchedResultant),
                   "compiled_load_mismatch"),
          "compiled nodal forces must reproduce the reviewed force");

  auto nonunitDirection = request;
  nonunitDirection.reviewed_force_direction = {0.0, 0.0, -2.0};
  require(hasIssue(ps::validate_request(nonunitDirection),
                   "invalid_reviewed_force_direction"),
          "reviewed force direction must already be normalized");

  auto unsafeBasis = request;
  unsafeBasis.displacement_limit_basis.clear();
  require(hasIssue(ps::validate_request(unsafeBasis),
                   "missing_displacement_limit_basis"),
          "a displacement limit without a basis stays blocked");

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
*ELEMENT, type=CPS3, ELSET=FixedFaces
5, 1, 3, 2
6, 1, 2, 4
7, 1, 4, 3
*ELEMENT, type=CPS3, ELSET=LoadedFace
8, 2, 3, 4
*ELEMENT, type=C3D4, ELSET=Volume1
9, 1, 2, 3, 4
)";
  const auto mesh = ps::parse_gmsh_abaqus_mesh(rawMesh, 0.001);
  require(mesh.nodes.size() == 4 && mesh.elements.size() == 1 &&
              mesh.nodes.front().position_m[2] == 0.01,
          "Gmsh mesh parser retains C3D4 and converts mm to m explicitly");
  require(mesh.surface_groups.size() == 2,
          "Gmsh surface ELSETs remain selectable");
  const auto loaded = std::ranges::find(mesh.surface_groups,
                                        std::string("LoadedFace"),
                                        &ps::SurfaceGroup::name);
  require(loaded != mesh.surface_groups.end() && loaded->area_m2 > 0.0 &&
              loaded->node_ids == std::vector<int>({2, 3, 4}) &&
              loaded->representative_normal_defined,
          "surface groups expose measured SI geometry");
  require(mesh.diagnostics.connected_components == 1 &&
              mesh.diagnostics.minimum_mean_ratio > 0.0 &&
              mesh.diagnostics.maximum_mean_ratio <= 1.0,
          "mesh diagnostics expose connectivity and tetra quality");
  try {
    (void)ps::parse_gmsh_abaqus_mesh(rawMesh, 0.0);
    fail("invalid mesh scale was accepted");
  } catch (const std::invalid_argument &) {
  }

  const std::vector<ps::Node> directNodes{
      {1, {0.0, 0.0, 0.0}}, {2, {1.0, 0.0, 0.0}},
      {3, {0.0, 1.0, 0.0}}, {4, {0.0, 0.0, 1.0}}};
  const std::vector<ps::SurfaceGroup> completeBoundary{{
      .name = "boundary",
      .triangles = {{1, {1, 3, 2}}, {2, {1, 2, 4}},
                    {3, {1, 4, 3}}, {4, {2, 3, 4}}},
  }};
  requireThrowsContaining(
      [&] {
        (void)ps::validate_and_measure_mesh(
            directNodes, {{1, {1, 3, 2, 4}}}, completeBoundary);
      },
      "inverted tetrahedron", "inverted tetrahedra are rejected");

  std::vector<ps::Node> disconnectedNodes = directNodes;
  disconnectedNodes.insert(disconnectedNodes.end(),
                           {{5, {10.0, 0.0, 0.0}},
                            {6, {11.0, 0.0, 0.0}},
                            {7, {10.0, 1.0, 0.0}},
                            {8, {10.0, 0.0, 1.0}}});
  auto disconnectedBoundary = completeBoundary;
  disconnectedBoundary.front().triangles.insert(
      disconnectedBoundary.front().triangles.end(),
      {{5, {5, 7, 6}}, {6, {5, 6, 8}}, {7, {5, 8, 7}},
       {8, {6, 7, 8}}});
  requireThrowsContaining(
      [&] {
        (void)ps::validate_and_measure_mesh(
            disconnectedNodes, {{1, {1, 2, 3, 4}}, {2, {5, 6, 7, 8}}},
            disconnectedBoundary);
      },
      "face-connected volume component",
      "face-disconnected tetrahedral regions are rejected");

  auto missingBoundary = completeBoundary;
  missingBoundary.front().triangles.pop_back();
  requireThrowsContaining(
      [&] {
        (void)ps::validate_and_measure_mesh(
            directNodes, {{1, {1, 2, 3, 4}}}, missingBoundary);
      },
      "not completely represented",
      "missing boundary triangles are rejected");

  auto duplicateBoundary = completeBoundary;
  duplicateBoundary.front().triangles.push_back({5, {2, 4, 3}});
  requireThrowsContaining(
      [&] {
        (void)ps::validate_and_measure_mesh(
            directNodes, {{1, {1, 2, 3, 4}}}, duplicateBoundary);
      },
      "more than once",
      "duplicate boundary triangles cannot appear in surface groups");

  auto nonBoundary = completeBoundary;
  nonBoundary.front().triangles.front().node_ids = {1, 2, 2};
  requireThrowsContaining(
      [&] {
        (void)ps::validate_and_measure_mesh(
            directNodes, {{1, {1, 2, 3, 4}}}, nonBoundary);
      },
      "three distinct nodes", "invalid surface triangles are rejected");

  auto joinedNodes = directNodes;
  joinedNodes.push_back({5, {0.0, 0.0, -1.0}});
  const std::vector<ps::SurfaceGroup> interiorFace{{
      .name = "not-a-boundary",
      .triangles = {{1, {1, 2, 3}}},
  }};
  requireThrowsContaining(
      [&] {
        (void)ps::validate_and_measure_mesh(
            joinedNodes, {{1, {1, 2, 3, 4}}, {2, {1, 3, 2, 5}}},
            interiorFace);
      },
      "not a volume boundary face",
      "interior tetrahedral faces cannot become selectable surfaces");

  const auto setupMesh = twoTetraSurfaceMesh();
  const auto setup = ps::compile_surface_setup(
      setupMesh, {"Fixed"}, {"Load"}, 120.0, {0.0, 0.0, -2.0});
  require(setup.fully_fixed_node_ids == std::vector<int>({1, 2, 4}),
          "restraint groups compile unique sorted nodes");
  require(setup.loaded_node_ids == std::vector<int>({1, 2, 3, 4, 5}) &&
              setup.nodal_forces.size() == setup.loaded_node_ids.size(),
          "shared load nodes compile once in sorted order");
  require(std::abs(setup.resultant_force_n[0]) < 1.0e-10 &&
              std::abs(setup.resultant_force_n[1]) < 1.0e-10 &&
              std::abs(setup.resultant_force_n[2] + 120.0) < 1.0e-10,
          "area-weighted nodal loads preserve the reviewed resultant");

  requireThrowsContaining(
      [&] {
        (void)ps::compile_surface_setup(setupMesh, {"Missing"}, {"Load"},
                                        120.0, {0.0, 0.0, -1.0});
      },
      "Unknown restraint surface group",
      "unknown restraint groups are rejected");
  requireThrowsContaining(
      [&] {
        (void)ps::compile_surface_setup(setupMesh, {"Fixed"}, {}, 120.0,
                                        {0.0, 0.0, -1.0});
      },
      "At least one load surface group",
      "empty load selections are rejected");
  requireThrowsContaining(
      [&] {
        (void)ps::compile_surface_setup(setupMesh, {"Fixed"}, {"Load"}, 0.0,
                                        {0.0, 0.0, -1.0});
      },
      "finite and positive", "zero force magnitude is rejected");
  requireThrowsContaining(
      [&] {
        (void)ps::compile_surface_setup(
            setupMesh, {"Fixed"}, {"Load"}, 120.0,
            {0.0, std::numeric_limits<double>::quiet_NaN(), -1.0});
      },
      "finite nonzero vector", "nonfinite force direction is rejected");
  requireThrowsContaining(
      [&] {
        (void)ps::compile_surface_setup(setupMesh, {"Fixed"},
                                        {"Load", "Load"}, 120.0,
                                        {0.0, 0.0, -1.0});
      },
      "selected more than once",
      "duplicate surface selections cannot double the applied load");
  requireThrowsContaining(
      [&] {
        (void)ps::compile_surface_setup(setupMesh, {"Load"}, {"Load"},
                                        120.0, {0.0, 0.0, -1.0});
      },
      "both restrained and loaded",
      "one surface group cannot carry conflicting boundary roles");
  return 0;
}
