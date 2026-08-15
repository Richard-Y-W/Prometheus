#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/structural_request.hpp"
#include "prometheus/structural/structural_findings.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/structural/structural_setup.hpp"
#include "prometheus/structural/surface_groups.hpp"
#include "prometheus/structural/surface_selection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace ps = prometheus::structural;
namespace fs = std::filesystem;

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
  const auto axialBenchmark = ps::axial_tension_bar_benchmark();
  require(ps::validate_request(axialBenchmark.request).empty() &&
              std::abs(axialBenchmark.expected_maximum_displacement_m - 5.0e-7) < 1e-18 &&
              std::abs(axialBenchmark.expected_maximum_von_mises_pa - 1.0e5) < 1e-9,
          "axial benchmark derives closed-form displacement and stress references");
  const auto exactComparison = ps::compare_benchmark(
      axialBenchmark, {5.0e-7, 1.0e5, 8, 6});
  require(exactComparison.passed(), "exact analytic benchmark metrics pass tolerance");
  const auto badComparison = ps::compare_benchmark(
      axialBenchmark, {6.0e-7, 1.2e5, 8, 6});
  require(!badComparison.passed(), "benchmark comparison rejects material error");
  const auto cantilever = ps::cantilever_benchmark(20, 3, 3);
  require(ps::validate_request(cantilever.request).empty() &&
              cantilever.request.nodes.size() == 336 &&
              cantilever.request.elements.size() == 1080 &&
              std::abs(cantilever.expected_maximum_displacement_m - 0.0002) < 1e-15 &&
              std::abs(cantilever.expected_maximum_von_mises_pa - 6.0e6) < 1e-8,
          "cantilever benchmark derives beam references and bounded solid mesh");
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
              metrics.displacements.size() == 2 &&
              metrics.displacements.back().node_id == 4 &&
              metrics.stresses.size() == 1 && metrics.stresses.front().element_id == 1 &&
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
  require(ps::group_boundary_faces(boundary, 1.0).size() == 4,
          "sharp tetrahedron faces remain separate selectable patches");

  const std::vector<ps::BoundaryFace> flatFaces{
      {1, {1, 2, 3}, {2.0 / 3.0, 1.0 / 3.0, 0.0}, {0.0, 0.0, 1.0}, 0.5},
      {2, {2, 4, 3}, {1.0 / 3.0, 2.0 / 3.0, 0.0}, {0.0, 0.0, 1.0}, 0.5}};
  const auto flatPatches = ps::group_boundary_faces(flatFaces, 0.0);
  require(flatPatches.size() == 1 && flatPatches.front().id == 1 &&
              flatPatches.front().face_node_ids.size() == 2 &&
              flatPatches.front().node_ids == std::vector<int>({1, 2, 3, 4}) &&
              std::abs(flatPatches.front().area_m2 - 1.0) < 1e-15 &&
              std::abs(flatPatches.front().area_weighted_centroid_m[0] - 0.5) < 1e-15,
          "adjacent coplanar triangles form one deterministic SI surface patch");
  const auto selected = ps::resolve_boundary_selection(
      "reviewed load face", flatPatches, {flatPatches.front().id});
  require(selected.face_node_ids.size() == 2 && selected.node_ids.size() == 4 &&
              std::abs(selected.area_m2 - 1.0) < 1e-15,
          "visual patches resolve into durable exact boundary topology");
  const auto distributed = ps::distribute_surface_total_force(
      selected, {0.0, 0.0, -120.0}, flatFaces);
  std::array<double, 3> distributedSum{};
  for (const auto &nodal : distributed)
    for (int axis = 0; axis < 3; ++axis)
      distributedSum[axis] += nodal.force_n[axis];
  require(distributed.size() == 4 && std::abs(distributedSum[2] + 120.0) < 1e-12 &&
              std::abs(distributed[0].force_n[2] + 20.0) < 1e-12 &&
              std::abs(distributed[1].force_n[2] + 40.0) < 1e-12,
          "surface force distribution preserves the reviewed total vector");
  try {
    (void)ps::resolve_boundary_selection("reviewed load face", flatPatches,
                                         {1, 1});
    fail("duplicate visual patch selection was accepted");
  } catch (const std::invalid_argument &) {
  }
  try {
    (void)ps::group_boundary_faces(flatFaces, -1.0);
    fail("invalid surface grouping angle was accepted");
  } catch (const std::invalid_argument &) {
  }

  const auto tetraPatches = ps::group_boundary_faces(boundary, 1.0);
  ps::StructuralSetup setup{
      .analysis_id = "reviewed-tetra-setup",
      .component_name = "benchmark tetrahedron",
      .geometry_sha256 = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      .mesh = mesh,
      .boundary_faces = boundary,
      .material = {"benchmark isotropic material",
                   "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                   "applies only to this analytic benchmark", 7.0e10, 0.33, true},
      .load = {ps::resolve_boundary_selection("load", tetraPatches, {1}),
               {0.0, 0.0, -100.0}, true},
      .restraint = {ps::resolve_boundary_selection("fixed", tetraPatches, {2}), true},
      .requirement = {0.001, 1.0e8, "explicit exploratory benchmark limits", true},
      .mesh_controls = {0.001, 0.003, "test mesher 1.0", true},
      .scenario_description = "one bounded linear-static benchmark scenario",
      .scenario_confirmed = true};
  require(ps::validate_setup(setup).empty(),
          "reviewed setup with provenance and exact topology validates");
  const auto compiledSetup = ps::compile_structural_request(setup);
  require(compiledSetup.fully_fixed_node_ids.size() == 3 &&
              compiledSetup.nodal_forces.size() == 3,
          "reviewed surface setup compiles into the narrow solver request");
  auto unreviewedSetup = setup;
  unreviewedSetup.material.reviewed = false;
  unreviewedSetup.requirement.source_or_exploratory_rationale.clear();
  unreviewedSetup.scenario_confirmed = false;
  const auto setupIssues = ps::validate_setup(unreviewedSetup);
  require(hasIssue(setupIssues, "material_unreviewed") &&
              hasIssue(setupIssues, "requirement_provenance_missing") &&
              hasIssue(setupIssues, "scenario_unconfirmed"),
          "unreviewed setup and missing rationale remain explicit blockers");
  auto staleSelection = setup;
  staleSelection.load.selection.area_m2 *= 2.0;
  require(hasIssue(ps::validate_setup(staleSelection), "load_selection_invalid"),
          "stale exact boundary selection is rejected before solver compilation");
  try {
    (void)ps::compile_structural_request(unreviewedSetup);
    fail("unreviewed structural setup compiled into a solver request");
  } catch (const std::invalid_argument &) {
  }

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

  const auto processRoot = fs::temp_directory_path() /
      ("prometheus-structural-process-" + std::to_string(std::rand()));
  fs::create_directories(processRoot);
  const auto fixture = fs::path(PROMETHEUS_SOLVER_FIXTURE_PATH);
  const auto runFixture = [&](const std::string &job,
                              const std::chrono::milliseconds timeout) {
    std::ofstream(processRoot / (job + ".inp")) << "fixture input\n";
    return ps::run_calculix({fixture, processRoot, job, timeout});
  };
  const auto completed = runFixture("success", std::chrono::seconds(5));
  require(completed.status == ps::SolverRunStatus::completed &&
              completed.exit_code == 0 && completed.metrics.has_value() &&
              std::abs(completed.metrics->maximum_displacement_m - 2.0e-5) < 1e-15 &&
              completed.standard_output.find("fixture stdout") != std::string::npos &&
              completed.standard_error.find("fixture stderr") != std::string::npos,
          "isolated solver process captures streams and parses required raw outputs");
  const auto staleOutputs = runFixture("success", std::chrono::seconds(5));
  require(staleOutputs.status == ps::SolverRunStatus::output_conflict &&
              !staleOutputs.metrics,
          "pre-existing raw solver outputs cannot be reused as a new run");
  const auto withinLimits = ps::compile_structural_findings(request, completed);
  require(withinLimits.declared_obligations == 2 &&
              withinLimits.evaluated_obligations == 2 &&
              std::ranges::all_of(withinLimits.findings, [](const auto &finding) {
                return finding.disposition ==
                    ps::StructuralFindingDisposition::no_violation_detected_within_scope;
              }) &&
              withinLimits.limitation.find("project-wide") != std::string::npos,
          "completed metrics compile into scoped no-violation findings");
  auto strictRequest = request;
  strictRequest.displacement_limit_m = 1.0e-6;
  strictRequest.von_mises_limit_pa = 1.0e5;
  const auto violated = ps::compile_structural_findings(strictRequest, completed);
  require(std::ranges::all_of(violated.findings, [](const auto &finding) {
            return finding.disposition == ps::StructuralFindingDisposition::violated &&
                   finding.margin_to_limit < 0.0;
          }),
          "the same completed metrics produce known-fail violations at tighter limits");
  const auto nonzero = runFixture("nonzero", std::chrono::seconds(5));
  require(nonzero.status == ps::SolverRunStatus::nonzero_exit &&
              nonzero.exit_code == 7 && !nonzero.metrics,
          "nonzero solver exit cannot become completed metrics");
  const auto missing = runFixture("missing", std::chrono::seconds(5));
  require(missing.status == ps::SolverRunStatus::output_missing && !missing.metrics,
          "successful process without required raw files fails closed");
  const auto timedOut = runFixture("timeout", std::chrono::milliseconds(30));
  require(timedOut.status == ps::SolverRunStatus::timed_out && !timedOut.metrics,
          "solver timeout is terminated and classified without metrics");
  const auto indeterminate = ps::compile_structural_findings(request, timedOut);
  require(indeterminate.declared_obligations == 2 &&
              indeterminate.evaluated_obligations == 0 &&
              indeterminate.findings.empty(),
          "failed execution cannot satisfy or violate engineering obligations");
  fs::remove_all(processRoot);
  return 0;
}
