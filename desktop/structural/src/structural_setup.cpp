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
constexpr std::string_view setupSchemaV2 =
    "urn:prometheus:schema:reviewed-structural-setup:2.0.0";
constexpr std::string_view setupSchemaV21 =
    "urn:prometheus:schema:reviewed-structural-setup:2.1.0";
constexpr std::string_view setupSchemaV22 =
    "urn:prometheus:schema:reviewed-structural-setup:2.2.0";
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
  return exactNodes == selection.node_ids &&
         detail::calculix_deck_round_trip_number_equivalent(
             selection.area_m2, selectedArea);
}

std::array<double, 3> normalized(const std::array<double, 3> &value) {
  const double magnitude = std::hypot(value[0], value[1], value[2]);
  return {value[0] / magnitude, value[1] / magnitude,
          value[2] / magnitude};
}

enum class CapabilityMatch { none, linear_static, modal_frequency, ambiguous };

// The real matcher Phase 7 needed a second capability to prove itself
// against: a reviewed setup's requirement quantities resolve to exactly
// one validated capability, both at once (ambiguous -- this architecture
// compiles one request per review, see StructuralSetup), or none (the
// requirement_unsupported_only case).
CapabilityMatch recommend_capability(
    const std::vector<ReviewedRequirement> &requirements) {
  bool sawStatic = false;
  bool sawFrequency = false;
  for (const auto &requirement : requirements) {
    if (requirement.quantity == RequirementQuantity::displacement ||
        requirement.quantity == RequirementQuantity::von_mises_stress)
      sawStatic = true;
    else if (requirement.quantity == RequirementQuantity::natural_frequency)
      sawFrequency = true;
  }
  if (sawStatic && sawFrequency) return CapabilityMatch::ambiguous;
  if (sawFrequency) return CapabilityMatch::modal_frequency;
  if (sawStatic) return CapabilityMatch::linear_static;
  return CapabilityMatch::none;
}

StructuralRequest compile_validated_request(const StructuralSetup &setup) {
  const bool modal =
      recommend_capability(setup.requirements) == CapabilityMatch::modal_frequency;
  const double forceMagnitude =
      std::hypot(setup.load.total_force_n[0], setup.load.total_force_n[1],
                 setup.load.total_force_n[2]);
  std::optional<double> displacementLimit;
  std::optional<double> vonMisesLimit;
  std::optional<double> frequencyLimit;
  std::string displacementBasis;
  std::string vonMisesBasis;
  std::string frequencyBasis;
  for (const auto &requirement : setup.requirements) {
    if (requirement.quantity == RequirementQuantity::displacement) {
      displacementLimit = requirement.limit_value;
      displacementBasis = requirement.limit_basis;
    } else if (requirement.quantity == RequirementQuantity::von_mises_stress) {
      vonMisesLimit = requirement.limit_value;
      vonMisesBasis = requirement.limit_basis;
    } else if (requirement.quantity == RequirementQuantity::natural_frequency) {
      frequencyLimit = requirement.limit_value;
      frequencyBasis = requirement.limit_basis;
    }
  }
  return {
      .analysis_id = setup.analysis_id,
      .component_name = setup.component_name,
      .geometry_sha256 = setup.geometry_sha256,
      .nodes = setup.mesh.nodes,
      .elements = setup.mesh.elements,
      .youngs_modulus_pa = setup.material.youngs_modulus_pa,
      .poisson_ratio = setup.material.poisson_ratio,
      .fully_fixed_node_ids = setup.restraint.selection.node_ids,
      .nodal_forces = modal ? std::vector<NodalForce>{}
                             : distribute_surface_total_force(
                                   setup.load.selection,
                                   setup.load.total_force_n,
                                   setup.boundary_faces),
      .displacement_limit_m = modal ? std::nullopt : displacementLimit,
      .von_mises_limit_pa = modal ? std::nullopt : vonMisesLimit,
      .material_reviewed = setup.material.reviewed,
      .loads_reviewed = setup.load.reviewed,
      .restraints_reviewed = setup.restraint.reviewed,
      .requirements_reviewed = std::ranges::all_of(
          setup.requirements, &ReviewedRequirement::reviewed),
      .scenario_confirmed = setup.scenario_confirmed,
      .material_designation = setup.material.designation,
      .material_temper = setup.material.temper,
      .material_product_form = setup.material.product_form,
      .material_applicability = setup.material.applicability,
      .material_evidence_sha256 = setup.material.source_sha256,
      .mesh_sha256 = setup.mesh_controls.mesh_sha256,
      .mesh_coordinate_scale_to_m =
          setup.mesh_controls.coordinate_scale_to_m,
      .reviewed_force_magnitude_n = modal ? 0.0 : forceMagnitude,
      .reviewed_force_direction =
          modal ? std::array<double, 3>{} : normalized(setup.load.total_force_n),
      .selected_load_area_m2 = modal ? 0.0 : setup.load.selection.area_m2,
      .mesh_target_size_m = setup.mesh_controls.target_size_m,
      .minimum_mean_ratio_threshold =
          setup.mesh_controls.minimum_mean_ratio_threshold,
      .observed_minimum_mean_ratio =
          setup.mesh_controls.observed_minimum_mean_ratio,
      .displacement_limit_basis = std::move(displacementBasis),
      .von_mises_limit_basis = std::move(vonMisesBasis),
      .mesh_reviewed = setup.mesh_controls.reviewed,
      .capability = modal ? StructuralCapability::modal_frequency
                           : StructuralCapability::linear_static,
      .density_kg_m3 = modal ? setup.material.density_kg_m3 : std::nullopt,
      .minimum_natural_frequency_hz = modal ? frequencyLimit : std::nullopt,
      .minimum_natural_frequency_basis =
          modal ? std::move(frequencyBasis) : std::string{}};
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
  const bool legacyV2 = setup.evidence_schema_version == "2.0.0";
  const bool legacyV21 = setup.evidence_schema_version == "2.1.0";
  const std::string_view schemaId =
      legacyV2 ? setupSchemaV2 : legacyV21 ? setupSchemaV21 : setupSchemaV22;
  const std::string_view schemaVersion =
      legacyV2 ? "2.0.0" : legacyV21 ? "2.1.0" : "2.2.0";
  Json materialJson{
      {"designation", setup.material.designation},
      {"temper", setup.material.temper},
      {"product_form", setup.material.product_form},
      {"source_sha256", setup.material.source_sha256},
      {"applicability", setup.material.applicability},
      {"youngs_modulus_pa", setup.material.youngs_modulus_pa},
      {"poisson_ratio", setup.material.poisson_ratio},
      {"reviewed", setup.material.reviewed}};
  // Density is a 2.2-and-later field only: 2.0.0/2.1.0 must keep
  // reproducing their exact original canonical bytes for legacy archive
  // replay, so a new key cannot be added to their material object.
  if (!legacyV2 && !legacyV21)
    materialJson["density_kg_m3"] = optionalNumber(setup.material.density_kg_m3);
  Json document{
      {"$schema", schemaId},
      {"schema_version", schemaVersion},
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
      {"material", std::move(materialJson)},
      {"load",
       {{"selection", selection_json(setup.load.selection)},
        {"total_force_n", setup.load.total_force_n},
        {"reviewed", setup.load.reviewed}}},
      {"restraint",
       {{"selection", selection_json(setup.restraint.selection)},
        {"reviewed", setup.restraint.reviewed}}},
      {"scenario",
       {{"description", setup.scenario_description},
        {"confirmed", setup.scenario_confirmed}}},
      {"selection_patch_angle_degrees",
       setup.selection_patch_angle_degrees}};
  const ReviewedRequirement *displacement = nullptr;
  const ReviewedRequirement *vonMises = nullptr;
  for (const auto &requirement : setup.requirements) {
    if (requirement.quantity == RequirementQuantity::displacement)
      displacement = &requirement;
    else if (requirement.quantity == RequirementQuantity::von_mises_stress)
      vonMises = &requirement;
  }
  if (legacyV2) {
    const auto *shared = displacement != nullptr ? displacement : vonMises;
    document["requirement"] = {
        {"displacement_limit_m",
         optionalNumber(displacement == nullptr
                            ? std::optional<double>{}
                            : std::optional<double>{displacement->limit_value})},
        {"von_mises_limit_pa",
         optionalNumber(vonMises == nullptr
                            ? std::optional<double>{}
                            : std::optional<double>{vonMises->limit_value})},
        {"source_or_exploratory_rationale",
         shared == nullptr ? std::string{} :
                             shared->source_or_exploratory_rationale},
        {"displacement_limit_basis",
         displacement == nullptr ? std::string{} : displacement->limit_basis},
        {"von_mises_limit_basis",
         vonMises == nullptr ? std::string{} : vonMises->limit_basis},
        {"reviewed", shared != nullptr && std::ranges::all_of(
             setup.requirements, &ReviewedRequirement::reviewed)}};
  } else {
    Json requirements = Json::array();
    for (const auto &requirement : setup.requirements) {
      requirements.push_back(
          {{"quantity", to_string(requirement.quantity)},
           {"other_quantity_description",
            requirement.other_quantity_description},
           {"comparator", to_string(requirement.comparator)},
           {"limit_value", requirement.limit_value},
           {"unit", requirement.unit},
           {"applicability", requirement.applicability},
           {"criticality", to_string(requirement.criticality)},
           {"source_or_exploratory_rationale",
            requirement.source_or_exploratory_rationale},
           {"reviewed", requirement.reviewed},
           {"limit_basis", requirement.limit_basis}});
    }
    document["requirements"] = std::move(requirements);
  }
  return integrity::canonicalize_json_bytes(
      document.dump(),
      // Keep canonicalization bounded while accommodating the structural
      // mesher's independently enforced ceiling of 480,000 tetrahedra.
      integrity::Limits{8U * 1024U * 1024U, 64U, 1000000U, 10000U,
                        500000U, 4U * 1024U * 1024U});
}

void throw_first(const std::vector<ValidationIssue> &issues) {
  if (!issues.empty())
    throw std::invalid_argument(issues.front().code + ": " +
                                issues.front().message);
}

} // namespace

std::string_view to_string(const RequirementQuantity value) {
  switch (value) {
  case RequirementQuantity::displacement: return "displacement";
  case RequirementQuantity::von_mises_stress: return "von_mises_stress";
  case RequirementQuantity::natural_frequency: return "natural_frequency";
  case RequirementQuantity::other: return "other";
  }
  return "other";
}

std::string_view to_string(const RequirementComparator value) {
  switch (value) {
  case RequirementComparator::less_or_equal: return "less_or_equal";
  case RequirementComparator::greater_or_equal: return "greater_or_equal";
  }
  return "less_or_equal";
}

std::string_view to_string(const StructuralCapability value) {
  switch (value) {
  case StructuralCapability::linear_static: return "linear_static";
  case StructuralCapability::modal_frequency: return "modal_frequency";
  }
  return "linear_static";
}

std::string_view to_string(const RequirementCriticality value) {
  switch (value) {
  case RequirementCriticality::informational: return "informational";
  case RequirementCriticality::advisory: return "advisory";
  case RequirementCriticality::critical: return "critical";
  }
  return "advisory";
}

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

  // Which validated capability this setup's reviewed requirements resolve
  // to determines whether a load review is required at all (a modal deck
  // has no *CLOAD -- forcing a fake load past validation to satisfy a
  // capability that does not use it would be a false schema) and whether a
  // reviewed density is required (essential to a modal solve, irrelevant to
  // linear-static).
  const auto capability = recommend_capability(setup.requirements);

  if (capability == CapabilityMatch::modal_frequency) {
    if (!setup.material.density_kg_m3.has_value())
      issue(issues, "material_density_missing",
            "A reviewed material density is required for a natural-frequency analysis.");
    else if (!std::isfinite(*setup.material.density_kg_m3) ||
             *setup.material.density_kg_m3 <= 0.0)
      issue(issues, "material_density_invalid", "Reviewed material density is invalid.");
    // Density is only representable in the 2.2 evidence contract (see
    // serialize_validated_setup) -- a modal setup pinned to an older
    // version would silently lose its reviewed density from the persisted
    // evidence document.
    if (setup.evidence_schema_version != "2.2.0")
      issue(issues, "setup_schema_version_invalid",
            "A natural-frequency setup requires the 2.2.0 evidence contract.");
  }

  if (capability != CapabilityMatch::modal_frequency) {
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
  }

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

  if (setup.evidence_schema_version != "2.0.0" &&
      setup.evidence_schema_version != "2.1.0" &&
      setup.evidence_schema_version != "2.2.0")
    issue(issues, "setup_schema_version_invalid",
          "Reviewed setup evidence version must be 2.0.0, 2.1.0, or 2.2.0.");
  if (setup.requirements.empty())
    issue(issues, "requirement_missing",
          "At least one reviewed requirement is required.");
  if (capability == CapabilityMatch::ambiguous)
    issue(issues, "requirement_capability_ambiguous",
          "This setup reviews requirements for both the linear-static and "
          "natural-frequency capabilities; only one solver call runs per "
          "review, so review and run each capability's requirements "
          "separately.");
  bool sawDisplacement = false;
  bool sawVonMises = false;
  bool sawFrequency = false;
  bool anySupported = false;
  std::set<std::string> otherDescriptions;
  for (const auto &requirement : setup.requirements) {
    if (!requirement.reviewed)
      issue(issues, "requirement_unreviewed",
            "All structural requirements require review.");
    if (!safe_text(requirement.source_or_exploratory_rationale))
      issue(issues, "requirement_provenance_missing",
            "A requirement source or explicit exploratory rationale is required.");
    if (!safe_text(requirement.applicability))
      issue(issues, "requirement_applicability_missing",
            "Every structural requirement needs a bounded applicability statement.");
    if (!std::isfinite(requirement.limit_value) ||
        requirement.limit_value <= 0.0)
      issue(issues, "requirement_limit_invalid",
            "Every structural requirement needs a finite positive limit.");
    if (!safe_text(requirement.unit, 64U))
      issue(issues, "requirement_unit_missing",
            "Every structural requirement needs a unit.");
    if (!safe_text(requirement.limit_basis))
      issue(issues, "requirement_limit_basis_missing",
            "Every structural requirement needs a reviewed limit basis.");
    switch (requirement.quantity) {
    case RequirementQuantity::displacement:
      anySupported = true;
      if (std::exchange(sawDisplacement, true))
        issue(issues, "requirement_duplicate_quantity",
              "Only one displacement requirement is supported.");
      if (requirement.unit != "m")
        issue(issues, "requirement_unit_invalid",
              "The displacement requirement must use metres (m).");
      if (requirement.comparator != RequirementComparator::less_or_equal)
        issue(issues, "requirement_comparator_mismatch",
              "A displacement requirement must use 'less_or_equal'.");
      break;
    case RequirementQuantity::von_mises_stress:
      anySupported = true;
      if (std::exchange(sawVonMises, true))
        issue(issues, "requirement_duplicate_quantity",
              "Only one von Mises stress requirement is supported.");
      if (requirement.unit != "Pa")
        issue(issues, "requirement_unit_invalid",
              "The von Mises stress requirement must use pascals (Pa).");
      if (requirement.comparator != RequirementComparator::less_or_equal)
        issue(issues, "requirement_comparator_mismatch",
              "A von Mises stress requirement must use 'less_or_equal'.");
      break;
    case RequirementQuantity::natural_frequency:
      anySupported = true;
      if (std::exchange(sawFrequency, true))
        issue(issues, "requirement_duplicate_quantity",
              "Only one natural-frequency requirement is supported.");
      if (requirement.unit != "Hz")
        issue(issues, "requirement_unit_invalid",
              "The natural-frequency requirement must use hertz (Hz).");
      if (requirement.comparator != RequirementComparator::greater_or_equal)
        issue(issues, "requirement_comparator_mismatch",
              "A natural-frequency requirement must use 'greater_or_equal'.");
      break;
    case RequirementQuantity::other:
      if (!safe_text(requirement.other_quantity_description))
        issue(issues, "requirement_description_missing",
              "An uncovered requirement needs an explicit description.");
      else if (!otherDescriptions.insert(
                    requirement.other_quantity_description).second)
        issue(issues, "requirement_duplicate_quantity",
              "Duplicate uncovered requirements are ambiguous.");
      break;
    }
  }
  if (!setup.requirements.empty() && !anySupported)
    issue(issues, "requirement_unsupported_only",
          "At least one reviewed displacement, von Mises, or natural-frequency "
          "limit is required for this capability.");

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
