#include "prometheus/structural/structural_observables.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace prometheus::structural {
namespace {

using Json = nlohmann::json;

[[noreturn]] void reject(std::string_view code, std::string_view message) {
  throw std::invalid_argument(std::string(code) + ": " +
                              std::string(message));
}

bool safe_id(const std::string_view value) {
  return !value.empty() && value.size() <= 128U &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return (character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9') ||
                  character == '.' || character == '_' || character == '-';
         });
}

std::string quantity_string(const StructuralObservableQuantity quantity) {
  switch (quantity) {
  case StructuralObservableQuantity::displacement_magnitude_m:
    return "displacement_magnitude_m";
  case StructuralObservableQuantity::von_mises_stress_pa:
    return "von_mises_stress_pa";
  }
  reject("refinement_observable_invalid",
         "Observable quantity is unsupported");
}

std::string reduction_string(const StructuralObservableReduction reduction) {
  switch (reduction) {
  case StructuralObservableReduction::maximum:
    return "maximum";
  }
  reject("refinement_observable_invalid",
         "Observable reduction is unsupported");
}

Json region_json(const StructuralObservableSpec &spec) {
  switch (spec.region.kind) {
  case StructuralObservableRegionKind::all_nodes:
    if (spec.quantity !=
        StructuralObservableQuantity::displacement_magnitude_m)
      reject("refinement_observable_invalid",
             "all_nodes is valid only for displacement magnitude");
    return {{"kind", "all_nodes"}};
  case StructuralObservableRegionKind::all_elements:
    if (spec.quantity != StructuralObservableQuantity::von_mises_stress_pa)
      reject("refinement_observable_invalid",
             "all_elements is valid only for von Mises stress");
    return {{"kind", "all_elements"}};
  case StructuralObservableRegionKind::element_centroid_box_m: {
    if (spec.quantity != StructuralObservableQuantity::von_mises_stress_pa)
      reject("refinement_observable_invalid",
             "element centroid boxes are valid only for von Mises stress");
    const auto &box = spec.region.element_centroid_box_m;
    for (std::size_t axis = 0; axis < 3U; ++axis)
      if (!std::isfinite(box.minimum_m[axis]) ||
          !std::isfinite(box.maximum_m[axis]) ||
          box.minimum_m[axis] > box.maximum_m[axis])
        reject("refinement_region_invalid",
               "Element centroid box bounds must be finite and ordered");
    return {{"kind", "element_centroid_box_m"},
            {"minimum_m", box.minimum_m},
            {"maximum_m", box.maximum_m}};
  }
  }
  reject("refinement_observable_invalid",
         "Observable region is unsupported");
}

void add_issue(StructuralObservableCompilation &compilation,
               std::string code, std::string observableId,
               std::string message) {
  const auto duplicate = std::ranges::any_of(
      compilation.issues, [&](const auto &issue) {
        return issue.code == code && issue.observable_id == observableId;
      });
  if (!duplicate)
    compilation.issues.push_back(
        {std::move(code), std::move(observableId), std::move(message)});
}

std::array<double, 3> centroid(
    const Tetrahedron &element,
    const std::map<int, const Node *> &nodes) {
  std::array<double, 3> result{};
  for (const int nodeId : element.node_ids) {
    const auto found = nodes.find(nodeId);
    if (found == nodes.end())
      throw std::invalid_argument("Compiled setup contains an unknown node");
    for (std::size_t axis = 0; axis < 3U; ++axis)
      result[axis] += found->second->position_m[axis] / 4.0;
  }
  return result;
}

bool inside(const std::array<double, 3> &position,
            const StructuralElementCentroidBox &box) {
  for (std::size_t axis = 0; axis < 3U; ++axis)
    if (position[axis] < box.minimum_m[axis] ||
        position[axis] > box.maximum_m[axis])
      return false;
  return true;
}

} // namespace

std::vector<StructuralObservableDefinition>
compile_structural_observable_definitions(
    std::vector<StructuralObservableSpec> specs) {
  if (specs.empty())
    reject("refinement_observable_invalid",
           "At least one observable is required");
  std::set<std::string> identities;
  std::vector<StructuralObservableDefinition> definitions;
  definitions.reserve(specs.size());
  for (auto &spec : specs) {
    if (!safe_id(spec.id))
      reject("refinement_observable_invalid",
             "Observable ID must be lowercase and bounded");
    if (!identities.insert(spec.id).second)
      reject("refinement_observable_duplicate",
             "Observable IDs must be unique");
    if (!std::isfinite(spec.maximum_change_fraction) ||
        spec.maximum_change_fraction <= 0.0 ||
        spec.maximum_change_fraction > 1.0)
      reject("refinement_observable_invalid",
             "Observable change threshold must be in (0, 1]");
    const Json document{
        {"$schema",
         "urn:prometheus:schema:structural-observable-definition:1.0.0"},
        {"schema_version", "1.0.0"},
        {"id", spec.id},
        {"quantity", quantity_string(spec.quantity)},
        {"reduction", reduction_string(spec.reduction)},
        {"region", region_json(spec)},
        {"maximum_change_fraction", spec.maximum_change_fraction}};
    const auto canonical = integrity::canonicalize_json_bytes(document.dump());
    definitions.push_back(
        {std::move(spec), integrity::sha256_bytes(canonical)});
  }
  return definitions;
}

std::vector<StructuralObservableSpec>
global_structural_observable_specs(const double maximumChangeFraction) {
  return {
      {.id = "global.maximum_displacement",
       .quantity = StructuralObservableQuantity::displacement_magnitude_m,
       .reduction = StructuralObservableReduction::maximum,
       .region = {.kind = StructuralObservableRegionKind::all_nodes},
       .maximum_change_fraction = maximumChangeFraction},
      {.id = "global.maximum_von_mises_stress",
       .quantity = StructuralObservableQuantity::von_mises_stress_pa,
       .reduction = StructuralObservableReduction::maximum,
       .region = {.kind = StructuralObservableRegionKind::all_elements},
       .maximum_change_fraction = maximumChangeFraction}};
}

StructuralObservableCompilation evaluate_structural_observables(
    const std::vector<StructuralObservableDefinition> &definitions,
    const CompiledStructuralSetup &setup,
    const CompiledCalculixResult &result) {
  StructuralObservableCompilation compilation;
  if (definitions.empty()) {
    add_issue(compilation, "refinement_observable_invalid", {},
              "At least one compiled observable is required");
    return compilation;
  }
  if (!result.complete() ||
      result.compiled_setup_identity != setup.identity) {
    add_issue(compilation, "refinement_observable_row_missing", {},
              "A complete result bound to the exact setup is required");
    return compilation;
  }

  std::map<int, const Node *> nodes;
  std::map<int, const Tetrahedron *> elements;
  for (const auto &node : setup.request.nodes)
    nodes.emplace(node.id, &node);
  for (const auto &element : setup.request.elements)
    elements.emplace(element.id, &element);

  std::map<int, const NodalDisplacement *> displacements;
  for (const auto &row : result.normalized.displacements) {
    if (!nodes.contains(row.node_id)) {
      add_issue(compilation, "refinement_observable_entity_unknown", {},
                "A displacement row references an unknown node");
      continue;
    }
    if (!std::isfinite(row.magnitude_m) || row.magnitude_m < 0.0) {
      add_issue(compilation, "refinement_observable_nonfinite", {},
                "Displacement magnitude must be finite and nonnegative");
      continue;
    }
    if (!displacements.emplace(row.node_id, &row).second)
      add_issue(compilation, "refinement_observable_row_duplicate", {},
                "Each node may have only one final displacement row");
  }

  std::map<int, const ElementStress *> stresses;
  for (const auto &row : result.normalized.stresses) {
    if (!elements.contains(row.element_id)) {
      add_issue(compilation, "refinement_observable_entity_unknown", {},
                "A stress row references an unknown element");
      continue;
    }
    if (row.integration_point != 1) {
      add_issue(compilation, "refinement_observable_row_missing", {},
                "C3D4 stress evidence requires integration point 1");
      continue;
    }
    if (!std::isfinite(row.von_mises_pa) || row.von_mises_pa < 0.0) {
      add_issue(compilation, "refinement_observable_nonfinite", {},
                "Von Mises stress must be finite and nonnegative");
      continue;
    }
    if (!stresses.emplace(row.element_id, &row).second)
      add_issue(compilation, "refinement_observable_row_duplicate", {},
                "Each C3D4 element may have only one final stress row");
  }
  if (!compilation.issues.empty()) return compilation;

  for (const auto &definition : definitions) {
    double maximum = -std::numeric_limits<double>::infinity();
    std::size_t selectedRows{};
    const auto missing = [&] {
      add_issue(compilation, "refinement_observable_row_missing",
                definition.spec.id,
                "Every selected mesh entity requires one normalized row");
    };
    switch (definition.spec.region.kind) {
    case StructuralObservableRegionKind::all_nodes:
      for (const auto &[nodeId, node] : nodes) {
        (void)node;
        const auto row = displacements.find(nodeId);
        if (row == displacements.end()) {
          missing();
          continue;
        }
        maximum = std::max(maximum, row->second->magnitude_m);
        ++selectedRows;
      }
      break;
    case StructuralObservableRegionKind::all_elements:
      for (const auto &[elementId, element] : elements) {
        (void)element;
        const auto row = stresses.find(elementId);
        if (row == stresses.end()) {
          missing();
          continue;
        }
        maximum = std::max(maximum, row->second->von_mises_pa);
        ++selectedRows;
      }
      break;
    case StructuralObservableRegionKind::element_centroid_box_m:
      for (const auto &[elementId, element] : elements) {
        if (!inside(centroid(*element,
                             nodes),
                    definition.spec.region.element_centroid_box_m))
          continue;
        const auto row = stresses.find(elementId);
        if (row == stresses.end()) {
          missing();
          continue;
        }
        maximum = std::max(maximum, row->second->von_mises_pa);
        ++selectedRows;
      }
      break;
    }
    if (selectedRows == 0U)
      add_issue(compilation, "refinement_region_empty", definition.spec.id,
                "Observable region selects no result rows");
    else if (std::isfinite(maximum))
      compilation.values.push_back({definition, maximum, selectedRows});
    else
      add_issue(compilation, "refinement_observable_nonfinite",
                definition.spec.id,
                "Observable maximum must be finite");
  }
  if (!compilation.issues.empty()) compilation.values.clear();
  return compilation;
}

} // namespace prometheus::structural
