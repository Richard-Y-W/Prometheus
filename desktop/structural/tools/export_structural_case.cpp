#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/structural_case.hpp"
#include "prometheus/structural/structural_request.hpp"
#include "prometheus/structural/surface_setup.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace integrity = prometheus::integrity;
namespace ps = prometheus::structural;
using Json = nlohmann::json;

namespace {

constexpr std::uintmax_t maximumCaseBytes = 16U * 1024U * 1024U;
constexpr std::uintmax_t maximumMeshBytes = 512U * 1024U * 1024U;
constexpr std::string_view executionSchema =
    "urn:prometheus:structural-execution-package:0.1.0";
constexpr std::string_view expectationsSchema =
    "urn:prometheus:structural-validation-expectations:0.1.0";
constexpr std::string_view caseName = "reviewed-structural-case.json";
constexpr std::string_view meshName = "source-mesh.inp";
constexpr std::string_view jobName = "prometheus_structural_case";
constexpr std::string_view manifestName =
    "prometheus-structural-execution.json";
constexpr std::string_view genericResultProfile = "structural_findings_v1";
constexpr std::string_view tensionBarResultProfile =
    "analytic_tension_bar_v1";

std::string read_file(const fs::path &path, const std::uintmax_t maximumBytes) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > maximumBytes)
    throw std::runtime_error("cannot read bounded file: " + path.string());
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot open file: " + path.string());
  std::string bytes(static_cast<std::size_t>(size), '\0');
  if (size != 0U)
    stream.read(bytes.data(), static_cast<std::streamsize>(size));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(size))
    throw std::runtime_error("file changed or could not be read exactly: " +
                             path.string());
  return bytes;
}

void write_file(const fs::path &path, const std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream)
    throw std::runtime_error("cannot open output file: " + path.string());
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream)
    throw std::runtime_error("cannot write output file: " + path.string());
}

void require_keys(const Json &value,
                  const std::initializer_list<std::string_view> expected,
                  const std::string_view field) {
  if (!value.is_object())
    throw std::runtime_error(std::string(field) + " must be an object");
  std::set<std::string> expectedKeys;
  for (const auto key : expected)
    expectedKeys.emplace(key);
  std::set<std::string> actualKeys;
  for (const auto &[key, unused] : value.items()) {
    (void)unused;
    actualKeys.insert(key);
  }
  if (actualKeys != expectedKeys)
    throw std::runtime_error(std::string(field) +
                             " has missing or unknown members");
}

const Json &required_object(const Json &value, const std::string &key,
                            const std::string_view field) {
  const auto found = value.find(key);
  if (found == value.end() || !found->is_object())
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be an object");
  return *found;
}

std::string required_string(const Json &value, const std::string &key,
                            const std::string_view field) {
  const auto found = value.find(key);
  if (found == value.end() || !found->is_string())
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be a string");
  const auto text = found->get<std::string>();
  if (text.empty() || text.size() > 4096U ||
      std::ranges::any_of(text, [](const unsigned char character) {
        return character < 0x20U || character == 0x7fU;
      }))
    throw std::runtime_error(std::string(field) + "." + key +
                             " is not bounded safe text");
  return text;
}

double required_number(const Json &value, const std::string &key,
                       const std::string_view field) {
  const auto found = value.find(key);
  if (found == value.end() || !found->is_number())
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be a number");
  const auto number = found->get<double>();
  if (!std::isfinite(number))
    throw std::runtime_error(std::string(field) + "." + key +
                             " must be finite");
  return number;
}

std::array<double, 3> required_vector3(const Json &value,
                                       const std::string &key,
                                       const std::string_view field) {
  const auto found = value.find(key);
  if (found == value.end() || !found->is_array() || found->size() != 3U)
    throw std::runtime_error(std::string(field) + "." + key +
                             " must contain three numbers");
  std::array<double, 3> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    if (!(*found)[index].is_number())
      throw std::runtime_error(std::string(field) + "." + key +
                               " must contain three numbers");
    result[index] = (*found)[index].get<double>();
    if (!std::isfinite(result[index]))
      throw std::runtime_error(std::string(field) + "." + key +
                               " must contain finite numbers");
  }
  return result;
}

Json parse_json(const std::string &bytes) {
  return Json::parse(integrity::canonicalize_json_bytes(bytes));
}

bool same_nodes(std::vector<ps::Node> first, std::vector<ps::Node> second) {
  std::ranges::sort(first, {}, &ps::Node::id);
  std::ranges::sort(second, {}, &ps::Node::id);
  if (first.size() != second.size())
    return false;
  for (std::size_t index = 0; index < first.size(); ++index)
    if (first[index].id != second[index].id ||
        first[index].position_m != second[index].position_m)
      return false;
  return true;
}

bool same_elements(std::vector<ps::Tetrahedron> first,
                   std::vector<ps::Tetrahedron> second) {
  std::ranges::sort(first, {}, &ps::Tetrahedron::id);
  std::ranges::sort(second, {}, &ps::Tetrahedron::id);
  if (first.size() != second.size())
    return false;
  for (std::size_t index = 0; index < first.size(); ++index)
    if (first[index].id != second[index].id ||
        first[index].node_ids != second[index].node_ids)
      return false;
  return true;
}

bool near(const double first, const double second) {
  return std::abs(first - second) <=
         std::max({1.0, std::abs(first), std::abs(second)}) * 1.0e-12;
}

bool near_relative(const double first, const double second) {
  return std::abs(first - second) <=
         std::max(std::abs(first), std::abs(second)) * 1.0e-10;
}

void verify_case_against_mesh(const ps::CanonicalStructuralCase &structuralCase,
                              const ps::VolumeMesh &mesh,
                              const std::string &meshHash) {
  const auto &request = structuralCase.request;
  if (request.mesh_sha256 != meshHash)
    throw std::runtime_error("reviewed case does not identify the exact mesh");
  if (!same_nodes(request.nodes, mesh.nodes) ||
      !same_elements(request.elements, mesh.elements))
    throw std::runtime_error(
        "reviewed case nodes or elements differ from the exact mesh");
  const auto setup = ps::compile_surface_setup(
      mesh, request.restraint_surface_groups, request.load_surface_groups,
      request.reviewed_force_magnitude_n, request.reviewed_force_direction);
  auto expectedForces = request.nodal_forces;
  auto compiledForces = setup.nodal_forces;
  auto expectedFixed = request.fully_fixed_node_ids;
  auto compiledFixed = setup.fully_fixed_node_ids;
  std::ranges::sort(expectedFixed);
  std::ranges::sort(compiledFixed);
  std::ranges::sort(expectedForces, {}, &ps::NodalForce::node_id);
  std::ranges::sort(compiledForces, {}, &ps::NodalForce::node_id);
  if (compiledFixed != expectedFixed ||
      expectedForces.size() != compiledForces.size() ||
      !near(setup.selected_load_area_m2, request.selected_load_area_m2) ||
      !near(mesh.diagnostics.minimum_mean_ratio,
            request.observed_minimum_mean_ratio))
    throw std::runtime_error(
        "reviewed selections do not reproduce the structural request");
  for (std::size_t index = 0; index < expectedForces.size(); ++index) {
    if (expectedForces[index].node_id != compiledForces[index].node_id)
      throw std::runtime_error("reviewed nodal-force identities differ");
    for (std::size_t axis = 0; axis < 3U; ++axis)
      if (!near(expectedForces[index].force_n[axis],
                compiledForces[index].force_n[axis]))
        throw std::runtime_error("reviewed nodal-force values differ");
  }
  if (!ps::validate_request(request).empty())
    throw std::runtime_error("reviewed structural request is invalid");
}

ps::CanonicalStructuralCase build_tension_bar_case(
    const fs::path &expectationsPath, const std::string &expectationsBytes,
    const ps::VolumeMesh &mesh, const std::string &meshHash,
    const std::string &meshCase, const std::string &requirementCase) {
  const auto root = parse_json(expectationsBytes);
  require_keys(root,
               {"$schema", "acceptance", "analysis_id", "analytic",
                "component_name", "force", "geometry", "material", "mesh_cases",
                "requirements", "surface_groups"},
               "expectations");
  if (required_string(root, "$schema", "expectations") != expectationsSchema)
    throw std::runtime_error("unsupported structural expectations schema");

  const auto &geometry = required_object(root, "geometry", "expectations");
  require_keys(geometry,
               {"cross_section_area_m2", "height_m", "length_m", "path",
                "sha256", "width_m"},
               "expectations.geometry");
  const auto length =
      required_number(geometry, "length_m", "expectations.geometry");
  const auto width =
      required_number(geometry, "width_m", "expectations.geometry");
  const auto height =
      required_number(geometry, "height_m", "expectations.geometry");
  const auto area = required_number(geometry, "cross_section_area_m2",
                                    "expectations.geometry");
  if (length <= 0.0 || width <= 0.0 || height <= 0.0 || area <= 0.0 ||
      !near_relative(area, width * height))
    throw std::runtime_error(
        "benchmark geometry dimensions and area are inconsistent");
  const auto geometryPathText =
      required_string(geometry, "path", "expectations.geometry");
  const fs::path relativeGeometry(geometryPathText);
  if (relativeGeometry.is_absolute() || relativeGeometry.has_parent_path())
    throw std::runtime_error("benchmark geometry path must be one file name");
  const auto geometryBytes = read_file(
      expectationsPath.parent_path() / relativeGeometry, maximumCaseBytes);
  const auto geometryHash =
      required_string(geometry, "sha256", "expectations.geometry");
  if (integrity::sha256_bytes(geometryBytes) != geometryHash)
    throw std::runtime_error("benchmark geometry hash does not match");

  const auto &meshCases = required_object(root, "mesh_cases", "expectations");
  require_keys(meshCases, {"coarse", "fine", "medium"},
               "expectations.mesh_cases");
  for (const auto label : {"coarse", "medium", "fine"}) {
    const auto &meshDefinition =
        required_object(meshCases, label, "expectations.mesh_cases");
    require_keys(meshDefinition, {"target_size_m"},
                 "expectations mesh definition");
    if (required_number(meshDefinition, "target_size_m",
                        "expectations mesh definition") <= 0.0)
      throw std::runtime_error("benchmark mesh target must be positive");
  }
  const auto meshTargetFor = [&](const std::string &label) {
    return required_number(
        required_object(meshCases, label, "expectations.mesh_cases"),
        "target_size_m", "expectations mesh definition");
  };
  if (!(meshTargetFor("coarse") > meshTargetFor("medium") &&
        meshTargetFor("medium") > meshTargetFor("fine")))
    throw std::runtime_error(
        "benchmark mesh targets must strictly refine coarse to medium to fine");
  const auto &selectedMesh =
      required_object(meshCases, meshCase, "expectations.mesh_cases");
  require_keys(selectedMesh, {"target_size_m"}, "selected mesh case");
  const auto meshTarget =
      required_number(selectedMesh, "target_size_m", "selected mesh case");

  const auto &requirements =
      required_object(root, "requirements", "expectations");
  require_keys(requirements, {"known_fail", "known_pass"},
               "expectations.requirements");
  for (const auto label : {"known_pass", "known_fail"}) {
    const auto &definition =
        required_object(requirements, label, "expectations.requirements");
    require_keys(
        definition,
        {"displacement_limit_basis", "displacement_limit_m", "expected_status"},
        "expectations requirement definition");
    if (required_number(definition, "displacement_limit_m",
                        "expectations requirement definition") <= 0.0)
      throw std::runtime_error("benchmark displacement limit must be positive");
  }
  const auto &selectedRequirement = required_object(
      requirements, requirementCase, "expectations.requirements");
  require_keys(
      selectedRequirement,
      {"displacement_limit_basis", "displacement_limit_m", "expected_status"},
      "selected requirement case");
  const auto expectedStatus = required_string(
      selectedRequirement, "expected_status", "selected requirement case");
  if (expectedStatus != "pass" && expectedStatus != "fail")
    throw std::runtime_error("benchmark expected status is unsupported");

  const auto &material = required_object(root, "material", "expectations");
  require_keys(material,
               {"applicability", "designation", "poisson_ratio", "product_form",
                "temper", "youngs_modulus_pa"},
               "expectations.material");
  const auto &force = required_object(root, "force", "expectations");
  require_keys(force, {"direction", "magnitude_n"}, "expectations.force");
  const auto &surfaceGroups =
      required_object(root, "surface_groups", "expectations");
  require_keys(surfaceGroups, {"load", "restraint"},
               "expectations.surface_groups");
  const auto restraint = required_string(surfaceGroups, "restraint",
                                         "expectations.surface_groups");
  const auto load =
      required_string(surfaceGroups, "load", "expectations.surface_groups");
  const auto magnitude =
      required_number(force, "magnitude_n", "expectations.force");
  const auto direction =
      required_vector3(force, "direction", "expectations.force");
  const auto directionMagnitude =
      std::hypot(direction[0], direction[1], direction[2]);
  if (!std::isfinite(directionMagnitude) || directionMagnitude <= 0.0)
    throw std::runtime_error("benchmark force direction must be nonzero");
  const std::array<double, 3> normalizedDirection{
      direction[0] / directionMagnitude, direction[1] / directionMagnitude,
      direction[2] / directionMagnitude};
  if (!near(normalizedDirection[0], 1.0) ||
      !near(normalizedDirection[1], 0.0) ||
      !near(normalizedDirection[2], 0.0))
    throw std::runtime_error(
        "tension-bar benchmark force must use the positive x axis");
  const auto setup = ps::compile_surface_setup(mesh, {restraint}, {load},
                                               magnitude, direction);
  std::array<double, 3> minimum{
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::max()};
  std::array<double, 3> maximum{
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::lowest()};
  for (const auto &node : mesh.nodes)
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      minimum[axis] = std::min(minimum[axis], node.position_m[axis]);
      maximum[axis] = std::max(maximum[axis], node.position_m[axis]);
    }
  if (!near(minimum[0], 0.0) || !near(minimum[1], 0.0) ||
      !near(minimum[2], 0.0) || !near(maximum[0], length) ||
      !near(maximum[1], width) || !near(maximum[2], height))
    throw std::runtime_error(
        "mesh bounds differ from the predeclared tension-bar geometry");
  const auto restraintSurface =
      std::ranges::find(mesh.surface_groups, restraint,
                        &ps::SurfaceGroup::name);
  const auto loadSurface =
      std::ranges::find(mesh.surface_groups, load, &ps::SurfaceGroup::name);
  if (restraintSurface == mesh.surface_groups.end() ||
      loadSurface == mesh.surface_groups.end() ||
      !near_relative(restraintSurface->area_m2, area) ||
      !near_relative(loadSurface->area_m2, area) ||
      !restraintSurface->representative_normal_defined ||
      !loadSurface->representative_normal_defined ||
      restraintSurface->representative_normal[0] > -1.0 + 1.0e-10 ||
      loadSurface->representative_normal[0] < 1.0 - 1.0e-10)
    throw std::runtime_error(
        "tension-bar end groups do not match the predeclared cross section");

  const auto &acceptance = required_object(root, "acceptance", "expectations");
  require_keys(
      acceptance,
      {"central_band_x_over_length",
       "fine_loaded_face_displacement_relative_error_maximum",
       "fine_volume_weighted_central_axial_stress_relative_error_maximum",
       "medium_to_fine_loaded_face_displacement_change_denominator",
       "medium_to_fine_loaded_face_displacement_change_maximum",
       "minimum_mean_ratio_threshold"},
      "expectations.acceptance");
  const auto &centralBand = acceptance.at("central_band_x_over_length");
  if (!centralBand.is_array() || centralBand.size() != 2U ||
      !centralBand[0].is_number() || !centralBand[1].is_number() ||
      centralBand[0].get<double>() != 0.4 ||
      centralBand[1].get<double>() != 0.6 ||
      required_string(
          acceptance,
          "medium_to_fine_loaded_face_displacement_change_denominator",
          "expectations.acceptance") != "absolute fine value")
    throw std::runtime_error(
        "benchmark central band or refinement denominator is unsupported");
  for (const auto key :
       {"fine_loaded_face_displacement_relative_error_maximum",
        "fine_volume_weighted_central_axial_stress_relative_error_maximum",
        "medium_to_fine_loaded_face_displacement_change_maximum",
        "minimum_mean_ratio_threshold"})
    if (required_number(acceptance, key, "expectations.acceptance") <= 0.0)
      throw std::runtime_error("benchmark acceptance limit must be positive");

  const auto &analytic = required_object(root, "analytic", "expectations");
  require_keys(analytic,
               {"loaded_face_axial_displacement_m",
                "volume_weighted_central_axial_stress_pa"},
               "expectations.analytic");
  const auto elasticModulus =
      required_number(material, "youngs_modulus_pa", "expectations.material");
  const auto analyticDisplacement = required_number(
      analytic, "loaded_face_axial_displacement_m", "expectations.analytic");
  const auto analyticStress =
      required_number(analytic, "volume_weighted_central_axial_stress_pa",
                      "expectations.analytic");
  if (elasticModulus <= 0.0 || magnitude <= 0.0 ||
      !near_relative(analyticDisplacement,
                     magnitude * length / (area * elasticModulus)) ||
      !near_relative(analyticStress, magnitude / area))
    throw std::runtime_error(
        "predeclared analytic values do not match F L/(A E) and F/A");
  const auto &knownPass = requirements.at("known_pass");
  const auto &knownFail = requirements.at("known_fail");
  if (required_string(knownPass, "expected_status",
                      "expectations.requirements.known_pass") != "pass" ||
      required_string(knownFail, "expected_status",
                      "expectations.requirements.known_fail") != "fail" ||
      required_number(knownPass, "displacement_limit_m",
                      "expectations.requirements.known_pass") <=
          analyticDisplacement ||
      required_number(knownFail, "displacement_limit_m",
                      "expectations.requirements.known_fail") >
          analyticDisplacement)
    throw std::runtime_error(
        "predeclared pass/fail limits do not bracket the analytic result");
  ps::StructuralRequest request{
      .analysis_id = required_string(root, "analysis_id", "expectations"),
      .component_name = required_string(root, "component_name", "expectations"),
      .geometry_sha256 = geometryHash,
      .nodes = mesh.nodes,
      .elements = mesh.elements,
      .youngs_modulus_pa = elasticModulus,
      .poisson_ratio =
          required_number(material, "poisson_ratio", "expectations.material"),
      .fully_fixed_node_ids = setup.fully_fixed_node_ids,
      .nodal_forces = setup.nodal_forces,
      .displacement_limit_m =
          required_number(selectedRequirement, "displacement_limit_m",
                          "selected requirement case"),
      .material_reviewed = true,
      .loads_reviewed = true,
      .restraints_reviewed = true,
      .requirements_reviewed = true,
      .scenario_confirmed = true,
      .material_designation =
          required_string(material, "designation", "expectations.material"),
      .material_temper =
          required_string(material, "temper", "expectations.material"),
      .material_product_form =
          required_string(material, "product_form", "expectations.material"),
      .material_applicability =
          required_string(material, "applicability", "expectations.material"),
      .material_evidence_sha256 = integrity::sha256_bytes(expectationsBytes),
      .mesh_sha256 = meshHash,
      .restraint_surface_groups = {restraint},
      .load_surface_groups = {load},
      .reviewed_force_magnitude_n = magnitude,
      .reviewed_force_direction = normalizedDirection,
      .selected_load_area_m2 = setup.selected_load_area_m2,
      .mesh_target_size_m = meshTarget,
      .minimum_mean_ratio_threshold =
          required_number(acceptance, "minimum_mean_ratio_threshold",
                          "expectations.acceptance"),
      .observed_minimum_mean_ratio = mesh.diagnostics.minimum_mean_ratio,
      .displacement_limit_basis =
          required_string(selectedRequirement, "displacement_limit_basis",
                          "selected requirement case"),
      .mesh_reviewed = true,
      .mesh_coordinate_scale_to_m = 1.0,
  };
  return ps::build_structural_case(request);
}

void export_package(const ps::CanonicalStructuralCase &structuralCase,
                    const std::string &meshBytes, const fs::path &output,
                    const std::string_view resultProfile) {
  const auto meshHash = integrity::sha256_bytes(meshBytes);
  const auto mesh = ps::parse_gmsh_abaqus_mesh(
      meshBytes, structuralCase.request.mesh_coordinate_scale_to_m);
  verify_case_against_mesh(structuralCase, mesh, meshHash);
  const auto deck = ps::generate_calculix_deck(structuralCase.request);

  std::error_code error;
  fs::create_directories(output, error);
  if (error || !fs::is_directory(output) || fs::is_symlink(output))
    throw std::runtime_error("output must be a regular directory");
  write_file(output / caseName, structuralCase.bytes);
  write_file(output / meshName, meshBytes);
  write_file(output / (std::string(jobName) + ".inp"), deck);

  const Json manifest{
      {"$schema", executionSchema},
      {"analysis_id", structuralCase.request.analysis_id},
      {"case", {{"path", caseName}, {"sha256", structuralCase.object_hash}}},
      {"component_name", structuralCase.request.component_name},
      {"deck",
       {{"job_name", jobName},
        {"path", std::string(jobName) + ".inp"},
        {"sha256", integrity::sha256_bytes(deck)}}},
      {"expected_result_coverage",
       {{"displacement_rows", structuralCase.request.nodes.size()},
        {"stress_rows", structuralCase.request.elements.size()}}},
      {"mesh",
       {{"coordinate_scale_to_m",
         structuralCase.request.mesh_coordinate_scale_to_m},
        {"path", meshName},
        {"sha256", meshHash}}},
      {"result_profile", resultProfile},
  };
  const auto manifestBytes =
      integrity::canonicalize_json_bytes(manifest.dump());
  write_file(output / manifestName, manifestBytes);
  std::cout << (output / manifestName).string() << '\n';
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 4) {
      const auto caseBytes = read_file(argv[1], maximumCaseBytes);
      const auto structuralCase = ps::parse_structural_case(caseBytes);
      const auto meshBytes = read_file(argv[2], maximumMeshBytes);
      export_package(structuralCase, meshBytes, argv[3], genericResultProfile);
      return 0;
    }
    if (argc == 7 && std::string_view(argv[1]) == "--tension-bar") {
      const fs::path expectationsPath(argv[2]);
      const auto expectationsBytes =
          read_file(expectationsPath, maximumCaseBytes);
      const auto meshBytes = read_file(argv[3], maximumMeshBytes);
      const auto meshHash = integrity::sha256_bytes(meshBytes);
      const auto mesh = ps::parse_gmsh_abaqus_mesh(meshBytes, 1.0);
      const auto structuralCase =
          build_tension_bar_case(expectationsPath, expectationsBytes, mesh,
                                 meshHash, argv[5], argv[6]);
      export_package(structuralCase, meshBytes, argv[4],
                     tensionBarResultProfile);
      return 0;
    }
    std::cerr << "usage: prometheus_export_structural_case CASE_JSON MESH_INP "
                 "OUTPUT_DIRECTORY\n"
              << "   or: prometheus_export_structural_case --tension-bar "
                 "EXPECTATIONS_JSON MESH_INP OUTPUT_DIRECTORY MESH_CASE "
                 "REQUIREMENT_CASE\n";
    return 2;
  } catch (const std::exception &error) {
    std::cerr << "structural_case_export_blocked: " << error.what() << '\n';
    return 9;
  }
}
