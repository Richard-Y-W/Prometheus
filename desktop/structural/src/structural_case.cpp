#include "prometheus/structural/structural_case.hpp"

#include "prometheus/structural/structural_request.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prometheus::structural {
namespace {

using Json = nlohmann::json;

constexpr std::size_t maximumNodes = 1'000'000U;
constexpr std::size_t maximumElements = 2'000'000U;
constexpr std::size_t maximumGroups = 65'536U;
constexpr std::size_t maximumTextBytes = 4'096U;

[[noreturn]] void reject(const std::string_view code,
                         const std::string_view message) {
  throw std::invalid_argument(std::string(code) + ": " +
                              std::string(message));
}

prometheus::integrity::Limits case_limits() {
  return {
      .raw_bytes = 128U * 1024U * 1024U,
      .depth = 32U,
      .nodes = 32'000'000U,
      .object_members = 64U,
      .array_elements = 24'000'000U,
      .string_bytes = 8U * 1024U * 1024U,
  };
}

bool contains(const std::initializer_list<std::string_view> values,
              const std::string_view candidate) {
  return std::ranges::find(values, candidate) != values.end();
}

void require_exact_keys(const Json &value,
                        const std::initializer_list<std::string_view> keys,
                        const std::string_view field) {
  if (!value.is_object())
    reject("invalid_type", std::string(field) + " must be an object");
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
    if (!contains(keys, iterator.key()))
      reject("unknown_field", std::string(field) + "." + iterator.key());
  for (const auto key : keys)
    if (!value.contains(std::string(key)))
      reject("missing_field", std::string(field) + "." + std::string(key));
}

const Json &member(const Json &object, const std::string_view key) {
  return object.at(std::string(key));
}

std::string required_string(const Json &object, const std::string_view key,
                            const std::string_view field) {
  const auto &value = member(object, key);
  if (!value.is_string())
    reject("invalid_type", std::string(field) + " must be a string");
  auto result = value.get<std::string>();
  if (result.size() > maximumTextBytes)
    reject("text_too_long", std::string(field) + " exceeds 4096 bytes");
  return result;
}

bool required_bool(const Json &object, const std::string_view key,
                   const std::string_view field) {
  const auto &value = member(object, key);
  if (!value.is_boolean())
    reject("invalid_type", std::string(field) + " must be a boolean");
  return value.get<bool>();
}

int required_int(const Json &value, const std::string_view field) {
  if (!value.is_number_integer())
    reject("invalid_type", std::string(field) + " must be an integer");
  const auto wide = value.get<std::int64_t>();
  if (wide < std::numeric_limits<int>::min() ||
      wide > std::numeric_limits<int>::max())
    reject("integer_out_of_range", std::string(field) + " is out of range");
  return static_cast<int>(wide);
}

double required_number(const Json &value, const std::string_view field) {
  if (!value.is_number())
    reject("invalid_type", std::string(field) + " must be a number");
  const auto result = value.get<double>();
  if (!std::isfinite(result))
    reject("nonfinite_number", std::string(field) + " must be finite");
  return result;
}

std::optional<double> optional_number(const Json &value,
                                      const std::string_view field) {
  if (value.is_null())
    return std::nullopt;
  return required_number(value, field);
}

template <std::size_t Size>
std::array<double, Size> number_array(const Json &value,
                                      const std::string_view field) {
  if (!value.is_array() || value.size() != Size)
    reject("invalid_array_size", std::string(field) + " has the wrong size");
  std::array<double, Size> result{};
  for (std::size_t index = 0; index < Size; ++index)
    result[index] =
        required_number(value[index], std::string(field) + "[]");
  return result;
}

template <std::size_t Size>
std::array<int, Size> integer_array(const Json &value,
                                    const std::string_view field) {
  if (!value.is_array() || value.size() != Size)
    reject("invalid_array_size", std::string(field) + " has the wrong size");
  std::array<int, Size> result{};
  for (std::size_t index = 0; index < Size; ++index)
    result[index] = required_int(value[index], std::string(field) + "[]");
  return result;
}

std::vector<std::string> string_array(const Json &value,
                                      const std::string_view field) {
  if (!value.is_array())
    reject("invalid_type", std::string(field) + " must be an array");
  if (value.size() > maximumGroups)
    reject("array_too_large", std::string(field) + " exceeds its limit");
  std::vector<std::string> result;
  result.reserve(value.size());
  for (const auto &item : value) {
    if (!item.is_string())
      reject("invalid_type", std::string(field) + "[] must be a string");
    auto text = item.get<std::string>();
    if (text.size() > maximumTextBytes)
      reject("text_too_long", std::string(field) + "[] exceeds 4096 bytes");
    result.push_back(std::move(text));
  }
  return result;
}

template <typename Value>
void sort_unique_or_reject(std::vector<Value> &values,
                           const std::string_view code) {
  std::ranges::sort(values);
  if (std::adjacent_find(values.begin(), values.end()) != values.end())
    reject(code, "structural case contains a duplicate ordered value");
}

StructuralRequest normalized_request(StructuralRequest request) {
  if (request.nodes.size() > maximumNodes ||
      request.nodal_forces.size() > maximumNodes ||
      request.fully_fixed_node_ids.size() > maximumNodes)
    reject("array_too_large",
           "node, force, or restraint count exceeds the case limit");
  if (request.elements.size() > maximumElements)
    reject("array_too_large", "element count exceeds the case limit");
  if (request.restraint_surface_groups.size() > maximumGroups ||
      request.load_surface_groups.size() > maximumGroups)
    reject("array_too_large", "surface group count exceeds the case limit");

  const auto require_bounded_text = [](const std::string &value,
                                       const std::string_view field) {
    if (value.size() > maximumTextBytes)
      reject("text_too_long", std::string(field) + " exceeds 4096 bytes");
  };
  require_bounded_text(request.schema, "request.$schema");
  require_bounded_text(request.analysis_id, "request.analysis_id");
  require_bounded_text(request.component_name, "request.component_name");
  require_bounded_text(request.geometry_sha256, "request.geometry_sha256");
  require_bounded_text(request.material_designation,
                       "request.material.designation");
  require_bounded_text(request.material_temper, "request.material.temper");
  require_bounded_text(request.material_product_form,
                       "request.material.product_form");
  require_bounded_text(request.material_applicability,
                       "request.material.applicability");
  require_bounded_text(request.material_evidence_sha256,
                       "request.material.evidence_sha256");
  require_bounded_text(request.mesh_sha256, "request.mesh_sha256");
  require_bounded_text(request.displacement_limit_basis,
                       "request.requirements.displacement_limit_basis");
  require_bounded_text(request.von_mises_limit_basis,
                       "request.requirements.von_mises_limit_basis");
  for (const auto &group : request.restraint_surface_groups)
    require_bounded_text(group, "request.restraints.surface_groups[]");
  for (const auto &group : request.load_surface_groups)
    require_bounded_text(group, "request.load.surface_groups[]");

  std::ranges::sort(request.nodes, {}, &Node::id);
  std::ranges::sort(request.elements, {}, &Tetrahedron::id);
  std::ranges::sort(request.nodal_forces, {}, &NodalForce::node_id);
  sort_unique_or_reject(request.fully_fixed_node_ids,
                        "duplicate_restraint_node");
  sort_unique_or_reject(request.restraint_surface_groups,
                        "duplicate_restraint_surface_group");
  sort_unique_or_reject(request.load_surface_groups,
                        "duplicate_load_surface_group");

  const auto issues = validate_request(request);
  if (!issues.empty())
    reject(issues.front().code, issues.front().message);
  return request;
}

Json request_json(const StructuralRequest &request) {
  Json nodes = Json::array();
  for (const auto &node : request.nodes)
    nodes.push_back({{"id", node.id}, {"position_m", node.position_m}});

  Json elements = Json::array();
  for (const auto &element : request.elements)
    elements.push_back(
        {{"id", element.id}, {"node_ids", element.node_ids}});

  Json forces = Json::array();
  for (const auto &force : request.nodal_forces)
    forces.push_back(
        {{"force_n", force.force_n}, {"node_id", force.node_id}});

  return {
      {"$schema", request.schema},
      {"analysis_id", request.analysis_id},
      {"component_name", request.component_name},
      {"geometry_sha256", request.geometry_sha256},
      {"load",
       {{"nodal_forces", std::move(forces)},
        {"reviewed", request.loads_reviewed},
        {"reviewed_direction", request.reviewed_force_direction},
        {"reviewed_magnitude_n", request.reviewed_force_magnitude_n},
        {"selected_area_m2", request.selected_load_area_m2},
        {"surface_groups", request.load_surface_groups}}},
      {"material",
       {{"applicability", request.material_applicability},
        {"designation", request.material_designation},
        {"evidence_sha256", request.material_evidence_sha256},
        {"poisson_ratio", request.poisson_ratio},
        {"product_form", request.material_product_form},
        {"reviewed", request.material_reviewed},
        {"temper", request.material_temper},
        {"youngs_modulus_pa", request.youngs_modulus_pa}}},
      {"mesh",
       {{"coordinate_scale_to_m", request.mesh_coordinate_scale_to_m},
        {"elements", std::move(elements)},
        {"minimum_mean_ratio_threshold",
         request.minimum_mean_ratio_threshold},
        {"nodes", std::move(nodes)},
        {"observed_minimum_mean_ratio",
         request.observed_minimum_mean_ratio},
        {"reviewed", request.mesh_reviewed},
        {"target_size_m", request.mesh_target_size_m}}},
      {"mesh_sha256", request.mesh_sha256},
      {"requirements",
       {{"displacement_limit_basis", request.displacement_limit_basis},
        {"displacement_limit_m",
         request.displacement_limit_m.has_value()
             ? Json(*request.displacement_limit_m)
             : Json(nullptr)},
        {"reviewed", request.requirements_reviewed},
        {"von_mises_limit_basis", request.von_mises_limit_basis},
        {"von_mises_limit_pa",
         request.von_mises_limit_pa.has_value()
             ? Json(*request.von_mises_limit_pa)
             : Json(nullptr)}}},
      {"restraints",
       {{"fully_fixed_node_ids", request.fully_fixed_node_ids},
        {"reviewed", request.restraints_reviewed},
        {"surface_groups", request.restraint_surface_groups}}},
      {"scenario_confirmed", request.scenario_confirmed},
  };
}

StructuralRequest decode_request(const Json &value) {
  require_exact_keys(value,
                     {"$schema", "analysis_id", "component_name",
                      "geometry_sha256", "load", "material", "mesh",
                      "mesh_sha256", "requirements", "restraints",
                      "scenario_confirmed"},
                     "request");
  StructuralRequest request;
  request.schema = required_string(value, "$schema", "request.$schema");
  request.analysis_id =
      required_string(value, "analysis_id", "request.analysis_id");
  request.component_name =
      required_string(value, "component_name", "request.component_name");
  request.geometry_sha256 = required_string(
      value, "geometry_sha256", "request.geometry_sha256");
  request.mesh_sha256 =
      required_string(value, "mesh_sha256", "request.mesh_sha256");
  request.scenario_confirmed = required_bool(
      value, "scenario_confirmed", "request.scenario_confirmed");

  const auto &material = member(value, "material");
  require_exact_keys(material,
                     {"applicability", "designation", "evidence_sha256",
                      "poisson_ratio", "product_form", "reviewed", "temper",
                      "youngs_modulus_pa"},
                     "request.material");
  request.material_applicability = required_string(
      material, "applicability", "request.material.applicability");
  request.material_designation = required_string(
      material, "designation", "request.material.designation");
  request.material_evidence_sha256 = required_string(
      material, "evidence_sha256", "request.material.evidence_sha256");
  request.poisson_ratio = required_number(
      member(material, "poisson_ratio"), "request.material.poisson_ratio");
  request.material_product_form = required_string(
      material, "product_form", "request.material.product_form");
  request.material_reviewed = required_bool(
      material, "reviewed", "request.material.reviewed");
  request.material_temper =
      required_string(material, "temper", "request.material.temper");
  request.youngs_modulus_pa = required_number(
      member(material, "youngs_modulus_pa"),
      "request.material.youngs_modulus_pa");

  const auto &mesh = member(value, "mesh");
  require_exact_keys(mesh,
                     {"coordinate_scale_to_m", "elements",
                      "minimum_mean_ratio_threshold", "nodes",
                      "observed_minimum_mean_ratio", "reviewed",
                      "target_size_m"},
                     "request.mesh");
  request.mesh_coordinate_scale_to_m = required_number(
      member(mesh, "coordinate_scale_to_m"),
      "request.mesh.coordinate_scale_to_m");
  const auto &nodes = member(mesh, "nodes");
  if (!nodes.is_array())
    reject("invalid_type", "request.mesh.nodes must be an array");
  if (nodes.size() > maximumNodes)
    reject("array_too_large", "request.mesh.nodes exceeds its limit");
  request.nodes.reserve(nodes.size());
  for (const auto &node : nodes) {
    require_exact_keys(node, {"id", "position_m"}, "request.mesh.nodes[]");
    request.nodes.push_back(
        {required_int(member(node, "id"), "request.mesh.nodes[].id"),
         number_array<3>(member(node, "position_m"),
                         "request.mesh.nodes[].position_m")});
  }
  const auto &elements = member(mesh, "elements");
  if (!elements.is_array())
    reject("invalid_type", "request.mesh.elements must be an array");
  if (elements.size() > maximumElements)
    reject("array_too_large", "request.mesh.elements exceeds its limit");
  request.elements.reserve(elements.size());
  for (const auto &element : elements) {
    require_exact_keys(element, {"id", "node_ids"},
                       "request.mesh.elements[]");
    request.elements.push_back(
        {required_int(member(element, "id"),
                      "request.mesh.elements[].id"),
         integer_array<4>(member(element, "node_ids"),
                          "request.mesh.elements[].node_ids")});
  }
  request.minimum_mean_ratio_threshold = required_number(
      member(mesh, "minimum_mean_ratio_threshold"),
      "request.mesh.minimum_mean_ratio_threshold");
  request.observed_minimum_mean_ratio = required_number(
      member(mesh, "observed_minimum_mean_ratio"),
      "request.mesh.observed_minimum_mean_ratio");
  request.mesh_reviewed =
      required_bool(mesh, "reviewed", "request.mesh.reviewed");
  request.mesh_target_size_m = required_number(
      member(mesh, "target_size_m"), "request.mesh.target_size_m");

  const auto &load = member(value, "load");
  require_exact_keys(load,
                     {"nodal_forces", "reviewed", "reviewed_direction",
                      "reviewed_magnitude_n", "selected_area_m2",
                      "surface_groups"},
                     "request.load");
  const auto &forces = member(load, "nodal_forces");
  if (!forces.is_array())
    reject("invalid_type", "request.load.nodal_forces must be an array");
  if (forces.size() > maximumNodes)
    reject("array_too_large", "request.load.nodal_forces exceeds its limit");
  request.nodal_forces.reserve(forces.size());
  for (const auto &force : forces) {
    require_exact_keys(force, {"force_n", "node_id"},
                       "request.load.nodal_forces[]");
    request.nodal_forces.push_back(
        {required_int(member(force, "node_id"),
                      "request.load.nodal_forces[].node_id"),
         number_array<3>(member(force, "force_n"),
                         "request.load.nodal_forces[].force_n")});
  }
  request.loads_reviewed =
      required_bool(load, "reviewed", "request.load.reviewed");
  request.reviewed_force_direction = number_array<3>(
      member(load, "reviewed_direction"), "request.load.reviewed_direction");
  request.reviewed_force_magnitude_n = required_number(
      member(load, "reviewed_magnitude_n"),
      "request.load.reviewed_magnitude_n");
  request.selected_load_area_m2 = required_number(
      member(load, "selected_area_m2"), "request.load.selected_area_m2");
  request.load_surface_groups = string_array(
      member(load, "surface_groups"), "request.load.surface_groups");

  const auto &restraints = member(value, "restraints");
  require_exact_keys(restraints,
                     {"fully_fixed_node_ids", "reviewed", "surface_groups"},
                     "request.restraints");
  const auto &fixed = member(restraints, "fully_fixed_node_ids");
  if (!fixed.is_array())
    reject("invalid_type",
           "request.restraints.fully_fixed_node_ids must be an array");
  if (fixed.size() > maximumNodes)
    reject("array_too_large",
           "request.restraints.fully_fixed_node_ids exceeds its limit");
  request.fully_fixed_node_ids.reserve(fixed.size());
  for (const auto &id : fixed)
    request.fully_fixed_node_ids.push_back(required_int(
        id, "request.restraints.fully_fixed_node_ids[]"));
  request.restraints_reviewed = required_bool(
      restraints, "reviewed", "request.restraints.reviewed");
  request.restraint_surface_groups = string_array(
      member(restraints, "surface_groups"),
      "request.restraints.surface_groups");

  const auto &requirements = member(value, "requirements");
  require_exact_keys(requirements,
                     {"displacement_limit_basis", "displacement_limit_m",
                      "reviewed", "von_mises_limit_basis",
                      "von_mises_limit_pa"},
                     "request.requirements");
  request.displacement_limit_basis = required_string(
      requirements, "displacement_limit_basis",
      "request.requirements.displacement_limit_basis");
  request.displacement_limit_m = optional_number(
      member(requirements, "displacement_limit_m"),
      "request.requirements.displacement_limit_m");
  request.requirements_reviewed = required_bool(
      requirements, "reviewed", "request.requirements.reviewed");
  request.von_mises_limit_basis = required_string(
      requirements, "von_mises_limit_basis",
      "request.requirements.von_mises_limit_basis");
  request.von_mises_limit_pa = optional_number(
      member(requirements, "von_mises_limit_pa"),
      "request.requirements.von_mises_limit_pa");
  return request;
}

} // namespace

CanonicalStructuralCase
build_structural_case(const StructuralRequest &sourceRequest) {
  auto request = normalized_request(sourceRequest);
  const Json value{{"$schema", structural_case_schema},
                   {"request", request_json(request)}};
  auto bytes = prometheus::integrity::canonicalize_json_bytes(
      value.dump(), case_limits());
  auto objectHash = prometheus::integrity::sha256_bytes(bytes);
  return {std::move(request), std::move(bytes), std::move(objectHash)};
}

CanonicalStructuralCase
parse_structural_case(const std::string_view canonicalBytes) {
  const auto canonical = prometheus::integrity::verify_canonical_bytes(
      canonicalBytes, case_limits());
  const auto value = Json::parse(canonical);
  require_exact_keys(value, {"$schema", "request"}, "structural_case");
  const auto schema =
      required_string(value, "$schema", "structural_case.$schema");
  if (schema != structural_case_schema)
    reject("unsupported_schema", "structural case schema is unsupported");
  auto rebuilt = build_structural_case(decode_request(member(value, "request")));
  if (rebuilt.bytes != canonical)
    reject("invalid_contract_order",
           "structural case arrays are not in deterministic order");
  return rebuilt;
}

} // namespace prometheus::structural
