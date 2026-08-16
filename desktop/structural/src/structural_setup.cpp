#include "prometheus/structural/structural_setup.hpp"

#include "calculix_deck_internal.hpp"
#include "prometheus/structural/structural_request.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace prometheus::structural {
namespace {

using Json = nlohmann::json;
constexpr std::string_view setupSchema =
    "urn:prometheus:schema:reviewed-structural-setup:2.0.0";
constexpr std::string_view compiledSetupSchema =
    "urn:prometheus:schema:compiled-structural-setup:1.0.0";

bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool safe_text(const std::string_view value,
               const std::size_t maximumBytes = 4096U) {
  return !value.empty() && value.size() <= maximumBytes &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return character >= 0x20U && character != 0x7fU;
         });
}

void issue(std::vector<ValidationIssue> &issues, std::string code,
           std::string message) {
  issues.push_back({std::move(code), std::move(message)});
}

std::array<int, 3> canonical(std::array<int, 3> nodes) {
  std::ranges::sort(nodes);
  return nodes;
}

bool valid_selection(const BoundarySelection &selection,
                     const std::vector<BoundaryFace> &boundary) {
  if (!safe_text(selection.label, 512U) ||
      selection.face_node_ids.empty() || selection.node_ids.empty() ||
      !std::isfinite(selection.area_m2) || selection.area_m2 <= 0.0)
    return false;
  std::map<std::array<int, 3>, double> available;
  for (const auto &face : boundary)
    if (!available.emplace(canonical(face.node_ids), face.area_m2).second)
      return false;
  std::set<std::array<int, 3>> selectedFaces;
  std::set<int> selectedNodes;
  double selectedArea = 0.0;
  for (const auto &face : selection.face_node_ids) {
    const auto key = canonical(face);
    const auto found = available.find(key);
    if (found == available.end() || !selectedFaces.insert(key).second)
      return false;
    selectedArea += found->second;
    selectedNodes.insert(key.begin(), key.end());
  }
  const std::vector<int> exactNodes(selectedNodes.begin(), selectedNodes.end());
  const double areaTolerance =
      std::max(1e-15, selection.area_m2 * 1e-12);
  return exactNodes == selection.node_ids &&
         std::abs(selectedArea - selection.area_m2) <= areaTolerance;
}

std::array<double, 3> normalized(const std::array<double, 3> &value) {
  const double magnitude = std::hypot(value[0], value[1], value[2]);
  return {value[0] / magnitude, value[1] / magnitude,
          value[2] / magnitude};
}

StructuralRequest compile_validated_request(const StructuralSetup &setup) {
  const double forceMagnitude =
      std::hypot(setup.load.total_force_n[0], setup.load.total_force_n[1],
                 setup.load.total_force_n[2]);
  return {
      .analysis_id = setup.analysis_id,
      .component_name = setup.component_name,
      .geometry_sha256 = setup.geometry_sha256,
      .nodes = setup.mesh.nodes,
      .elements = setup.mesh.elements,
      .youngs_modulus_pa = setup.material.youngs_modulus_pa,
      .poisson_ratio = setup.material.poisson_ratio,
      .fully_fixed_node_ids = setup.restraint.selection.node_ids,
      .nodal_forces = distribute_surface_total_force(
          setup.load.selection, setup.load.total_force_n,
          setup.boundary_faces),
      .displacement_limit_m = setup.requirement.displacement_limit_m,
      .von_mises_limit_pa = setup.requirement.von_mises_limit_pa,
      .material_reviewed = setup.material.reviewed,
      .loads_reviewed = setup.load.reviewed,
      .restraints_reviewed = setup.restraint.reviewed,
      .requirements_reviewed = setup.requirement.reviewed,
      .scenario_confirmed = setup.scenario_confirmed,
      .material_designation = setup.material.designation,
      .material_temper = setup.material.temper,
      .material_product_form = setup.material.product_form,
      .material_applicability = setup.material.applicability,
      .material_evidence_sha256 = setup.material.source_sha256,
      .mesh_sha256 = setup.mesh_controls.mesh_sha256,
      .mesh_coordinate_scale_to_m =
          setup.mesh_controls.coordinate_scale_to_m,
      .reviewed_force_magnitude_n = forceMagnitude,
      .reviewed_force_direction = normalized(setup.load.total_force_n),
      .selected_load_area_m2 = setup.load.selection.area_m2,
      .mesh_target_size_m = setup.mesh_controls.target_size_m,
      .minimum_mean_ratio_threshold =
          setup.mesh_controls.minimum_mean_ratio_threshold,
      .observed_minimum_mean_ratio =
          setup.mesh_controls.observed_minimum_mean_ratio,
      .displacement_limit_basis =
          setup.requirement.displacement_limit_basis,
      .von_mises_limit_basis = setup.requirement.von_mises_limit_basis,
      .mesh_reviewed = setup.mesh_controls.reviewed};
}

Json selection_json(const BoundarySelection &selection) {
  auto exactFaces = selection.face_node_ids;
  for (auto &face : exactFaces)
    face = canonical(face);
  std::ranges::sort(exactFaces);
  auto nodeIds = selection.node_ids;
  std::ranges::sort(nodeIds);
  Json faces = Json::array();
  for (const auto &face : exactFaces)
    faces.push_back({face[0], face[1], face[2]});
  return {{"label", selection.label},
          {"face_node_ids", std::move(faces)},
          {"node_ids", std::move(nodeIds)},
          {"area_m2", selection.area_m2}};
}

std::string serialize_validated_setup(const StructuralSetup &setup) {
  const auto optionalNumber = [](const std::optional<double> value) {
    return value ? Json(*value) : Json(nullptr);
  };
  std::vector<int> nodeIds;
  nodeIds.reserve(setup.mesh.nodes.size());
  for (const auto &node : setup.mesh.nodes)
    nodeIds.push_back(node.id);
  std::ranges::sort(nodeIds);
  std::vector<int> elementIds;
  elementIds.reserve(setup.mesh.elements.size());
  for (const auto &element : setup.mesh.elements)
    elementIds.push_back(element.id);
  std::ranges::sort(elementIds);
  const Json document{
      {"$schema", setupSchema},
      {"schema_version", "2.0.0"},
      {"analysis_id", setup.analysis_id},
      {"component_name", setup.component_name},
      {"geometry_sha256", setup.geometry_sha256},
      {"mesh",
       {{"source_sha256", setup.mesh_controls.mesh_sha256},
        {"coordinate_scale_to_m",
         setup.mesh_controls.coordinate_scale_to_m},
        {"node_count", setup.mesh.nodes.size()},
        {"element_count", setup.mesh.elements.size()},
        {"boundary_face_count", setup.boundary_faces.size()},
        {"node_ids", std::move(nodeIds)},
        {"element_ids", std::move(elementIds)},
        {"minimum_size_m", setup.mesh_controls.minimum_size_m},
        {"maximum_size_m", setup.mesh_controls.maximum_size_m},
        {"target_size_m", setup.mesh_controls.target_size_m},
        {"minimum_mean_ratio_threshold",
         setup.mesh_controls.minimum_mean_ratio_threshold},
        {"observed_minimum_mean_ratio",
         setup.mesh_controls.observed_minimum_mean_ratio},
        {"mesher_identity", setup.mesh_controls.mesher_identity},
        {"reviewed", setup.mesh_controls.reviewed}}},
      {"material",
       {{"designation", setup.material.designation},
        {"temper", setup.material.temper},
        {"product_form", setup.material.product_form},
        {"source_sha256", setup.material.source_sha256},
        {"applicability", setup.material.applicability},
        {"youngs_modulus_pa", setup.material.youngs_modulus_pa},
        {"poisson_ratio", setup.material.poisson_ratio},
        {"reviewed", setup.material.reviewed}}},
      {"load",
       {{"selection", selection_json(setup.load.selection)},
        {"total_force_n", setup.load.total_force_n},
        {"reviewed", setup.load.reviewed}}},
      {"restraint",
       {{"selection", selection_json(setup.restraint.selection)},
        {"reviewed", setup.restraint.reviewed}}},
      {"requirement",
       {{"displacement_limit_m",
         optionalNumber(setup.requirement.displacement_limit_m)},
        {"von_mises_limit_pa",
         optionalNumber(setup.requirement.von_mises_limit_pa)},
        {"source_or_exploratory_rationale",
         setup.requirement.source_or_exploratory_rationale},
        {"displacement_limit_basis",
         setup.requirement.displacement_limit_basis},
        {"von_mises_limit_basis",
         setup.requirement.von_mises_limit_basis},
        {"reviewed", setup.requirement.reviewed}}},
      {"scenario",
       {{"description", setup.scenario_description},
        {"confirmed", setup.scenario_confirmed}}},
      {"selection_patch_angle_degrees",
       setup.selection_patch_angle_degrees}};
  return integrity::canonicalize_json_bytes(
      document.dump(),
      integrity::Limits{8U * 1024U * 1024U, 64U, 500000U, 10000U,
                        100000U, 4U * 1024U * 1024U});
}

void throw_first(const std::vector<ValidationIssue> &issues) {
  if (!issues.empty())
    throw std::invalid_argument(issues.front().code + ": " +
                                issues.front().message);
}

} // namespace

std::vector<ValidationIssue> validate_setup(const StructuralSetup &setup) {
  std::vector<ValidationIssue> issues;
  if (setup.analysis_id.empty())
    issue(issues, "missing_analysis_id", "Analysis identity is required.");
  else if (!safe_text(setup.analysis_id, 512U))
    issue(issues, "unsafe_heading_text",
          "Analysis identity contains unsafe CalculiX heading text.");
  if (setup.component_name.empty())
    issue(issues, "missing_component", "A component is required.");
  else if (!safe_text(setup.component_name, 512U))
    issue(issues, "unsafe_heading_text",
          "Component name contains unsafe CalculiX heading text.");
  if (!strict_sha256(setup.geometry_sha256))
    issue(issues, "invalid_geometry_identity",
          "Exact lowercase geometry SHA-256 is required.");
  if (setup.mesh.nodes.empty() || setup.mesh.elements.empty() ||
      setup.boundary_faces.empty())
    issue(issues, "missing_mesh",
          "A volume mesh and exterior boundary are required.");
  if (!std::isfinite(setup.selection_patch_angle_degrees) ||
      setup.selection_patch_angle_degrees <= 0.0 ||
      setup.selection_patch_angle_degrees > 180.0)
    issue(issues, "selection_patch_angle_invalid",
          "Surface selection patch angle must be in (0, 180] degrees.");

  if (!setup.material.reviewed)
    issue(issues, "material_unreviewed",
          "Material applicability and properties require review.");
  if (!safe_text(setup.material.designation, 512U) ||
      !safe_text(setup.material.temper, 512U) ||
      !safe_text(setup.material.product_form, 512U) ||
      !strict_sha256(setup.material.source_sha256))
    issue(issues, "material_provenance_missing",
          "Material identity, product form, and exact evidence are required.");
  if (setup.material.applicability != "known" &&
      setup.material.applicability != "assumed")
    issue(issues, "material_applicability_unresolved",
          "Material applicability must be explicitly known or assumed.");
  if (!std::isfinite(setup.material.youngs_modulus_pa) ||
      setup.material.youngs_modulus_pa <= 0.0 ||
      !std::isfinite(setup.material.poisson_ratio) ||
      setup.material.poisson_ratio <= -1.0 ||
      setup.material.poisson_ratio >= 0.5)
    issue(issues, "material_properties_invalid",
          "Reviewed elastic properties are invalid.");

  if (!setup.load.reviewed)
    issue(issues, "load_unreviewed",
          "Surface load selection and vector require review.");
  if (!valid_selection(setup.load.selection, setup.boundary_faces))
    issue(issues, "load_selection_invalid",
          "Load faces must resolve to the exact mesh boundary.");
  double loadMagnitudeSquared = 0.0;
  for (const double component : setup.load.total_force_n) {
    if (!std::isfinite(component))
      loadMagnitudeSquared = -1.0;
    else if (loadMagnitudeSquared >= 0.0)
      loadMagnitudeSquared += component * component;
  }
  if (!std::isfinite(loadMagnitudeSquared) || loadMagnitudeSquared <= 0.0)
    issue(issues, "load_vector_invalid",
          "Reviewed total surface force must be finite and nonzero.");

  if (!setup.restraint.reviewed)
    issue(issues, "restraint_unreviewed",
          "Fixed surface selection requires review.");
  if (!valid_selection(setup.restraint.selection, setup.boundary_faces))
    issue(issues, "restraint_selection_invalid",
          "Restraint faces must resolve to the exact mesh boundary.");
  std::set<std::array<int, 3>> restraintFaces;
  for (const auto &face : setup.restraint.selection.face_node_ids)
    restraintFaces.insert(canonical(face));
  if (std::ranges::any_of(
          setup.load.selection.face_node_ids, [&](const auto &face) {
            return restraintFaces.contains(canonical(face));
          }))
    issue(issues, "load_restraint_overlap",
          "The bounded model does not permit the same face to be loaded and fully fixed.");

  if (!setup.requirement.reviewed)
    issue(issues, "requirement_unreviewed",
          "Structural limits require review.");
  const auto positive = [](const auto &value) {
    return value && std::isfinite(*value) && *value > 0.0;
  };
  if (!positive(setup.requirement.displacement_limit_m) &&
      !positive(setup.requirement.von_mises_limit_pa))
    issue(issues, "requirement_missing",
          "A positive displacement or stress limit is required.");
  if (!safe_text(setup.requirement.source_or_exploratory_rationale))
    issue(issues, "requirement_provenance_missing",
          "A requirement source or explicit exploratory rationale is required.");
  if (setup.requirement.displacement_limit_m &&
      !safe_text(setup.requirement.displacement_limit_basis))
    issue(issues, "missing_displacement_limit_basis",
          "A displacement limit requires a reviewed basis.");
  if (setup.requirement.von_mises_limit_pa &&
      !safe_text(setup.requirement.von_mises_limit_basis))
    issue(issues, "missing_von_mises_limit_basis",
          "A von Mises limit requires a reviewed basis.");

  if (!setup.mesh_controls.reviewed)
    issue(issues, "mesh_controls_unreviewed",
          "Mesh controls require review.");
  if (!std::isfinite(setup.mesh_controls.minimum_size_m) ||
      !std::isfinite(setup.mesh_controls.maximum_size_m) ||
      setup.mesh_controls.minimum_size_m <= 0.0 ||
      setup.mesh_controls.maximum_size_m <
          setup.mesh_controls.minimum_size_m ||
      !safe_text(setup.mesh_controls.mesher_identity, 512U) ||
      !strict_sha256(setup.mesh_controls.mesh_sha256) ||
      !std::isfinite(setup.mesh_controls.coordinate_scale_to_m) ||
      setup.mesh_controls.coordinate_scale_to_m <= 0.0 ||
      !std::isfinite(setup.mesh_controls.target_size_m) ||
      setup.mesh_controls.target_size_m <= 0.0 ||
      !std::isfinite(setup.mesh_controls.minimum_mean_ratio_threshold) ||
      setup.mesh_controls.minimum_mean_ratio_threshold <= 0.0 ||
      setup.mesh_controls.minimum_mean_ratio_threshold > 1.0 ||
      !std::isfinite(setup.mesh_controls.observed_minimum_mean_ratio) ||
      setup.mesh_controls.observed_minimum_mean_ratio <= 0.0 ||
      setup.mesh_controls.observed_minimum_mean_ratio > 1.0)
    issue(issues, "mesh_controls_invalid",
          "Reviewed mesh identity, scale, sizing, and quality are required.");
  else if (setup.mesh_controls.observed_minimum_mean_ratio + 1.0e-15 <
           setup.mesh_controls.minimum_mean_ratio_threshold)
    issue(issues, "mesh_quality_below_limit",
          "Observed mesh quality is below the reviewed threshold.");

  if (!setup.scenario_confirmed ||
      !safe_text(setup.scenario_description))
    issue(issues, "scenario_unconfirmed",
          "The described structural scenario requires confirmation.");
  return issues;
}

StructuralRequest compile_structural_request(const StructuralSetup &setup) {
  throw_first(validate_setup(setup));
  auto request = compile_validated_request(setup);
  throw_first(validate_request(request));
  return request;
}

std::string serialize_structural_setup_evidence(
    const StructuralSetup &setup) {
  throw_first(validate_setup(setup));
  return serialize_validated_setup(setup);
}

CompiledStructuralSetup compile_structural_setup(
    const StructuralSetup &setup) {
  throw_first(validate_setup(setup));
  auto request = compile_validated_request(setup);
  throw_first(validate_request(request));
  auto evidence = serialize_validated_setup(setup);
  auto deck = detail::generate_validated_calculix_deck(request);
  const auto identityDocument = integrity::canonicalize_json_bytes(
      Json{{"$schema", compiledSetupSchema},
           {"schema_version", "1.0.0"},
           {"compiler_version", "structural-setup-compiler-v1"},
           {"setup_evidence_sha256", integrity::sha256_bytes(evidence)},
           {"calculix_deck_sha256", integrity::sha256_bytes(deck)}}
          .dump());
  return {.request = std::move(request),
          .canonical_setup_evidence = std::move(evidence),
          .calculix_deck = std::move(deck),
          .identity = integrity::sha256_bytes(identityDocument),
          .reviewed_setup = setup};
}

} // namespace prometheus::structural
