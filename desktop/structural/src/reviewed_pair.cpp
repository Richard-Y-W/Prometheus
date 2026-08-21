#include "prometheus/structural/reviewed_pair.hpp"

#include "prometheus/structural/prepared_mesh.hpp"
#include "prometheus/structural/structural_observables.hpp"
#include "prometheus/structural/surface_groups.hpp"
#include "prometheus/structural/surface_selection.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prometheus::structural {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr std::string_view pairSchema =
    "urn:prometheus:schema:reviewed-structural-pair:1.0.0";
constexpr std::string_view pairVersion = "1.0.0";
constexpr std::string_view materialSchema =
    "urn:prometheus:material-candidate-evidence:0.1.0";
constexpr std::size_t maximumManifestBytes = 256U * 1024U;
constexpr std::size_t maximumMaterialBytes = 2U * 1024U * 1024U;
constexpr std::size_t maximumMeshBytes = 64U * 1024U * 1024U;

[[noreturn]] void reject(const std::string_view code,
                         const std::string_view message) {
  throw std::invalid_argument(std::string(code) + ": " +
                              std::string(message));
}

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

bool safe_job_name(const std::string_view value) {
  return !value.empty() && value.size() <= 128U &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return std::isalnum(character) || character == '_' ||
                  character == '-';
         });
}

bool lowercase_hex(const std::string_view value,
                   const std::size_t expectedBytes) {
  return value.size() == expectedBytes &&
         std::ranges::all_of(value, [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool exact_keys(const Json &value,
                const std::initializer_list<std::string_view> expected) {
  if (!value.is_object() || value.size() != expected.size()) return false;
  return std::ranges::all_of(expected, [&](const auto key) {
    return value.contains(std::string(key));
  });
}

void require_keys(const Json &value,
                  const std::initializer_list<std::string_view> expected,
                  const std::string_view context) {
  if (!exact_keys(value, expected))
    reject("reviewed_pair_contract_invalid",
           std::string(context) + " has missing or unknown members");
}

std::string string_field(const Json &value, const std::string_view key,
                         const std::string_view context,
                         const std::size_t maximumBytes = 4096U) {
  const auto name = std::string(key);
  if (!value.contains(name) || !value.at(name).is_string())
    reject("reviewed_pair_contract_invalid",
           std::string(context) + "." + name + " must be a string");
  auto result = value.at(name).get<std::string>();
  if (!safe_text(result, maximumBytes))
    reject("reviewed_pair_contract_invalid",
           std::string(context) + "." + name + " is unsafe or empty");
  return result;
}

bool bool_field(const Json &value, const std::string_view key,
                const std::string_view context) {
  const auto name = std::string(key);
  if (!value.contains(name) || !value.at(name).is_boolean())
    reject("reviewed_pair_contract_invalid",
           std::string(context) + "." + name + " must be boolean");
  return value.at(name).get<bool>();
}

double number_field(const Json &value, const std::string_view key,
                    const std::string_view context) {
  const auto name = std::string(key);
  if (!value.contains(name) || !value.at(name).is_number())
    reject("reviewed_pair_contract_invalid",
           std::string(context) + "." + name + " must be numeric");
  const auto result = value.at(name).get<double>();
  if (!std::isfinite(result))
    reject("reviewed_pair_contract_invalid",
           std::string(context) + "." + name + " must be finite");
  return result;
}

std::size_t size_field(const Json &value, const std::string_view key,
                       const std::string_view context,
                       const std::size_t maximum = 1000000U) {
  const auto name = std::string(key);
  if (!value.contains(name) || !value.at(name).is_number_unsigned())
    reject("reviewed_pair_contract_invalid",
           std::string(context) + "." + name +
               " must be an unsigned integer");
  const auto result = value.at(name).get<std::size_t>();
  if (result == 0U || result > maximum)
    reject("reviewed_pair_contract_invalid",
           std::string(context) + "." + name + " is outside its bound");
  return result;
}

std::array<double, 3> vector3(const Json &value,
                              const std::string_view context) {
  if (!value.is_array() || value.size() != 3U)
    reject("reviewed_pair_contract_invalid",
           std::string(context) + " must contain three numbers");
  std::array<double, 3> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    if (!value.at(index).is_number())
      reject("reviewed_pair_contract_invalid",
             std::string(context) + " must contain only numbers");
    result[index] = value.at(index).get<double>();
    if (!std::isfinite(result[index]))
      reject("reviewed_pair_contract_invalid",
             std::string(context) + " must contain finite numbers");
  }
  return result;
}

std::vector<int> patch_ids(const Json &value,
                           const std::string_view context) {
  if (!value.is_array() || value.empty() || value.size() > 32U)
    reject("reviewed_pair_contract_invalid",
           std::string(context) + " must contain 1 to 32 patch IDs");
  std::set<int> unique;
  std::vector<int> result;
  for (const auto &item : value) {
    if (!item.is_number_integer())
      reject("reviewed_pair_contract_invalid",
             std::string(context) + " must contain integer patch IDs");
    const auto id = item.get<int>();
    if (id <= 0 || !unique.insert(id).second)
      reject("reviewed_pair_contract_invalid",
             std::string(context) +
                 " must contain unique positive patch IDs");
    result.push_back(id);
  }
  return result;
}

std::string read_bounded_regular_file(const fs::path &path,
                                      const std::size_t maximumBytes,
                                      const std::string_view role) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (error || fs::is_symlink(status) || !fs::is_regular_file(status))
    reject("reviewed_pair_artifact_invalid",
           std::string(role) + " must be a non-symlink regular file");
  const auto length = fs::file_size(path, error);
  if (error || length == 0U || length > maximumBytes)
    reject("reviewed_pair_artifact_invalid",
           std::string(role) + " size is outside the allowed bound");
  std::ifstream input(path, std::ios::binary);
  if (!input)
    reject("reviewed_pair_artifact_invalid",
           std::string(role) + " could not be opened");
  std::string bytes{std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>()};
  if (input.bad() || bytes.size() != length)
    reject("reviewed_pair_artifact_invalid",
           std::string(role) + " could not be read exactly");
  return bytes;
}

fs::path local_artifact(const fs::path &directory,
                        const std::string &filename,
                        const std::string_view role) {
  const fs::path relative(filename);
  if (relative.empty() || relative.is_absolute() ||
      relative.has_parent_path() || relative.filename() != relative ||
      filename == "." || filename == "..")
    reject("reviewed_pair_artifact_path_invalid",
           std::string(role) + " must use a bare local filename");
  const auto path = directory / relative;
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (error || fs::is_symlink(status) || !fs::is_regular_file(status))
    reject("reviewed_pair_artifact_invalid",
           std::string(role) + " is missing or is not a regular file");
  return path;
}

std::string canonical_json(const std::string_view bytes,
                           const integrity::Limits limits,
                           const std::string_view role) {
  try {
    return integrity::canonicalize_json_bytes(bytes, limits);
  } catch (const integrity::CanonicalJsonError &error) {
    reject("reviewed_pair_json_invalid",
           std::string(role) + " rejected: " + error.code());
  } catch (const std::exception &error) {
    reject("reviewed_pair_json_invalid",
           std::string(role) + " rejected: " + error.what());
  }
}

Json parse_canonical(const std::string &canonical,
                     const std::string_view role) {
  try {
    return Json::parse(canonical);
  } catch (const std::exception &error) {
    reject("reviewed_pair_json_invalid",
           std::string(role) + " rejected: " + error.what());
  }
}

struct PatchExpectation final {
  std::vector<int> ids;
  double area_m2{};
  std::array<double, 3> centroid_m{};
  std::array<double, 3> normal{};
};

struct MeshSpec final {
  fs::path path;
  std::string sha256;
  double coordinate_scale_to_m{};
  double minimum_size_m{};
  double maximum_size_m{};
  double target_size_m{};
  double minimum_mean_ratio_threshold{};
  double expected_minimum_mean_ratio{};
  std::size_t expected_node_count{};
  std::size_t expected_element_count{};
  std::size_t expected_boundary_face_count{};
  std::string mesher_identity;
};

struct SampleSpec final {
  MeshSpec mesh;
  PatchExpectation load;
  PatchExpectation restraint;
};

PatchExpectation decode_patch(const Json &value,
                              const std::string_view context) {
  require_keys(value,
               {"patch_ids", "expected_area_m2", "expected_centroid_m",
                "expected_normal"},
               context);
  PatchExpectation result{
      .ids = patch_ids(value.at("patch_ids"),
                       std::string(context) + ".patch_ids"),
      .area_m2 = number_field(value, "expected_area_m2", context),
      .centroid_m = vector3(value.at("expected_centroid_m"),
                            std::string(context) + ".expected_centroid_m"),
      .normal = vector3(value.at("expected_normal"),
                        std::string(context) + ".expected_normal")};
  const auto normalMagnitude =
      std::hypot(result.normal[0], result.normal[1], result.normal[2]);
  if (result.area_m2 <= 0.0 ||
      std::abs(normalMagnitude - 1.0) > 1.0e-8)
    reject("reviewed_pair_contract_invalid",
           std::string(context) + " area or normal is invalid");
  return result;
}

SampleSpec decode_sample(const Json &value, const fs::path &directory,
                         const std::string_view role) {
  const auto context = std::string("samples.") + std::string(role);
  require_keys(value, {"mesh", "load_selection", "restraint_selection"},
               context);
  const auto &mesh = value.at("mesh");
  require_keys(mesh,
               {"path", "sha256", "coordinate_scale_to_m",
                "minimum_size_m", "maximum_size_m", "target_size_m",
                "minimum_mean_ratio_threshold",
                "expected_minimum_mean_ratio", "expected_node_count",
                "expected_element_count", "expected_boundary_face_count",
                "mesher_identity", "reviewed"},
               context + ".mesh");
  const auto meshFilename = string_field(mesh, "path", context + ".mesh", 255U);
  const auto sha256 = string_field(mesh, "sha256", context + ".mesh", 71U);
  if (!strict_sha256(sha256) ||
      !bool_field(mesh, "reviewed", context + ".mesh"))
    reject("reviewed_pair_contract_invalid",
           context + ".mesh identity and review are required");
  MeshSpec decoded{
      .path = local_artifact(directory, meshFilename,
                             context + ".mesh"),
      .sha256 = sha256,
      .coordinate_scale_to_m =
          number_field(mesh, "coordinate_scale_to_m", context + ".mesh"),
      .minimum_size_m =
          number_field(mesh, "minimum_size_m", context + ".mesh"),
      .maximum_size_m =
          number_field(mesh, "maximum_size_m", context + ".mesh"),
      .target_size_m =
          number_field(mesh, "target_size_m", context + ".mesh"),
      .minimum_mean_ratio_threshold = number_field(
          mesh, "minimum_mean_ratio_threshold", context + ".mesh"),
      .expected_minimum_mean_ratio = number_field(
          mesh, "expected_minimum_mean_ratio", context + ".mesh"),
      .expected_node_count =
          size_field(mesh, "expected_node_count", context + ".mesh"),
      .expected_element_count =
          size_field(mesh, "expected_element_count", context + ".mesh"),
      .expected_boundary_face_count = size_field(
          mesh, "expected_boundary_face_count", context + ".mesh", 4000000U),
      .mesher_identity =
          string_field(mesh, "mesher_identity", context + ".mesh", 512U)};
  if (decoded.coordinate_scale_to_m <= 0.0 ||
      decoded.minimum_size_m <= 0.0 ||
      decoded.maximum_size_m < decoded.minimum_size_m ||
      decoded.target_size_m < decoded.minimum_size_m ||
      decoded.target_size_m > decoded.maximum_size_m ||
      decoded.minimum_mean_ratio_threshold <= 0.0 ||
      decoded.minimum_mean_ratio_threshold > 1.0 ||
      decoded.expected_minimum_mean_ratio <= 0.0 ||
      decoded.expected_minimum_mean_ratio > 1.0)
    reject("reviewed_pair_contract_invalid",
           context + ".mesh controls are invalid");
  return {.mesh = std::move(decoded),
          .load = decode_patch(value.at("load_selection"),
                               context + ".load_selection"),
          .restraint = decode_patch(value.at("restraint_selection"),
                                    context + ".restraint_selection")};
}

bool close_value(const double actual, const double expected,
                 const double absoluteTolerance,
                 const double relativeTolerance) {
  return std::abs(actual - expected) <=
         std::max(absoluteTolerance,
                  relativeTolerance *
                      std::max(std::abs(actual), std::abs(expected)));
}

struct PatchMeasurement final {
  double area_m2{};
  std::array<double, 3> centroid_m{};
  std::array<double, 3> normal{};
};

PatchMeasurement measure_patches(const std::vector<SurfacePatch> &patches,
                                 const std::vector<int> &ids) {
  std::map<int, const SurfacePatch *> byId;
  for (const auto &patch : patches) byId.emplace(patch.id, &patch);
  PatchMeasurement result;
  std::array<double, 3> weightedNormal{};
  for (const int id : ids) {
    const auto found = byId.find(id);
    if (found == byId.end())
      reject("reviewed_pair_patch_invalid",
             "a reviewed surface patch no longer exists");
    const auto &patch = *found->second;
    result.area_m2 += patch.area_m2;
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      result.centroid_m[axis] +=
          patch.area_weighted_centroid_m[axis] * patch.area_m2;
      weightedNormal[axis] +=
          patch.representative_unit_normal[axis] * patch.area_m2;
    }
  }
  if (result.area_m2 <= 0.0)
    reject("reviewed_pair_patch_invalid",
           "reviewed patches have no positive area");
  for (double &coordinate : result.centroid_m)
    coordinate /= result.area_m2;
  const auto normalMagnitude =
      std::hypot(weightedNormal[0], weightedNormal[1], weightedNormal[2]);
  if (!std::isfinite(normalMagnitude) || normalMagnitude <= 1.0e-15)
    reject("reviewed_pair_patch_invalid",
           "reviewed patches have no representative normal");
  for (std::size_t axis = 0; axis < 3U; ++axis)
    result.normal[axis] = weightedNormal[axis] / normalMagnitude;
  return result;
}

void require_patch_matches(const std::vector<SurfacePatch> &patches,
                           const PatchExpectation &expected,
                           const std::string_view role) {
  const auto actual = measure_patches(patches, expected.ids);
  if (!close_value(actual.area_m2, expected.area_m2, 1.0e-15, 1.0e-8))
    reject("reviewed_pair_patch_drift",
           std::string(role) + " selected area changed");
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    if (!close_value(actual.centroid_m[axis], expected.centroid_m[axis],
                     1.0e-10, 1.0e-8))
      reject("reviewed_pair_patch_drift",
             std::string(role) + " selected centroid changed");
    if (!close_value(actual.normal[axis], expected.normal[axis],
                     1.0e-8, 1.0e-8))
      reject("reviewed_pair_patch_drift",
             std::string(role) + " selected normal changed");
  }
}

ReviewedMaterial decode_material(const Json &value,
                                 const fs::path &directory) {
  require_keys(value,
               {"evidence", "candidate_id", "designation", "temper",
                "product_form", "applicability", "youngs_modulus_pa",
                "poisson_ratio", "reviewed"},
               "material");
  const auto &evidence = value.at("evidence");
  require_keys(evidence, {"path", "sha256"}, "material.evidence");
  const auto evidenceFilename =
      string_field(evidence, "path", "material.evidence", 255U);
  const auto evidenceSha =
      string_field(evidence, "sha256", "material.evidence", 71U);
  if (!strict_sha256(evidenceSha))
    reject("reviewed_pair_contract_invalid",
           "material.evidence.sha256 must be lowercase SHA-256");
  const auto evidencePath = local_artifact(
      directory, evidenceFilename, "material evidence");
  const auto evidenceBytes = read_bounded_regular_file(
      evidencePath, maximumMaterialBytes, "material evidence");
  if (integrity::sha256_bytes(evidenceBytes) != evidenceSha)
    reject("reviewed_pair_artifact_hash_mismatch",
           "material evidence bytes changed");
  const auto evidenceCanonical = canonical_json(
      evidenceBytes,
      integrity::Limits{maximumMaterialBytes, 64U, 100000U, 10000U,
                        10000U, 1024U * 1024U},
      "material evidence");
  const auto materialEvidence =
      parse_canonical(evidenceCanonical, "material evidence");
  require_keys(materialEvidence, {"$schema", "candidates", "sources"},
               "material evidence");
  if (string_field(materialEvidence, "$schema", "material evidence") !=
          materialSchema ||
      !materialEvidence.at("candidates").is_array() ||
      materialEvidence.at("candidates").empty() ||
      materialEvidence.at("candidates").size() > 1024U ||
      !materialEvidence.at("sources").is_array())
    reject("reviewed_pair_material_evidence_invalid",
           "material evidence schema or candidate collection is invalid");

  const auto candidateId =
      string_field(value, "candidate_id", "material", 512U);
  const auto designation =
      string_field(value, "designation", "material", 512U);
  const auto temper = string_field(value, "temper", "material", 512U);
  const auto productForm =
      string_field(value, "product_form", "material", 512U);
  const auto applicability =
      string_field(value, "applicability", "material", 64U);
  const auto modulus = number_field(value, "youngs_modulus_pa", "material");
  const auto poisson = number_field(value, "poisson_ratio", "material");
  if (!bool_field(value, "reviewed", "material") ||
      (applicability != "known" && applicability != "assumed") ||
      modulus <= 0.0 || poisson <= -1.0 || poisson >= 0.5)
    reject("reviewed_pair_contract_invalid",
           "material review or properties are invalid");

  std::set<std::string> candidateIds;
  const Json *selected = nullptr;
  for (const auto &candidate : materialEvidence.at("candidates")) {
    if (!candidate.is_object() || !candidate.contains("candidate_id") ||
        !candidate.at("candidate_id").is_string())
      reject("reviewed_pair_material_evidence_invalid",
             "every material candidate needs a string candidate_id");
    const auto id = candidate.at("candidate_id").get<std::string>();
    if (!candidateIds.insert(id).second)
      reject("reviewed_pair_material_evidence_invalid",
             "material candidate IDs must be unique");
    if (id == candidateId) selected = &candidate;
  }
  if (selected == nullptr)
    reject("reviewed_pair_material_candidate_mismatch",
           "selected material candidate is absent");
  try {
    const bool matches =
        selected->at("designation").is_string() &&
        selected->at("designation").get<std::string>() == designation &&
        selected->at("temper").is_string() &&
        selected->at("temper").get<std::string>() == temper &&
        selected->at("product_form").is_string() &&
        selected->at("product_form").get<std::string>() == productForm &&
        selected->at("youngs_modulus_pa").is_number() &&
        close_value(selected->at("youngs_modulus_pa").get<double>(), modulus,
                    1.0e-6, 1.0e-15) &&
        selected->at("poisson_ratio").is_number() &&
        close_value(selected->at("poisson_ratio").get<double>(), poisson,
                    1.0e-15, 1.0e-15);
    if (!matches)
      reject("reviewed_pair_material_candidate_mismatch",
             "approved material values differ from the selected candidate");
  } catch (const nlohmann::json::exception &) {
    reject("reviewed_pair_material_candidate_mismatch",
           "selected material candidate lacks an approved field");
  }
  return {.designation = designation,
          .source_sha256 = evidenceSha,
          .applicability = applicability,
          .youngs_modulus_pa = modulus,
          .poisson_ratio = poisson,
          .reviewed = true,
          .temper = temper,
          .product_form = productForm};
}

std::vector<ReviewedRequirement> decode_requirements(const Json &value) {
  if (!value.is_array() || value.empty() || value.size() > 16U)
    reject("reviewed_pair_contract_invalid",
           "requirements must contain 1 to 16 entries");
  std::vector<ReviewedRequirement> result;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const auto &requirement = value.at(index);
    const auto context = "requirements[" + std::to_string(index) + "]";
    require_keys(requirement,
                 {"quantity", "comparator", "limit_value", "unit",
                  "applicability", "criticality",
                  "source_or_exploratory_rationale", "limit_basis",
                  "reviewed"},
                 context);
    const auto quantity = string_field(requirement, "quantity", context, 64U);
    const auto comparator =
        string_field(requirement, "comparator", context, 64U);
    const auto criticality =
        string_field(requirement, "criticality", context, 64U);
    if ((quantity != "displacement" &&
         quantity != "von_mises_stress") ||
        comparator != "less_or_equal" ||
        (criticality != "informational" && criticality != "advisory" &&
         criticality != "critical") ||
        !bool_field(requirement, "reviewed", context))
      reject("reviewed_pair_contract_invalid",
             context + " uses an unsupported or unreviewed value");
    const auto limit = number_field(requirement, "limit_value", context);
    if (limit <= 0.0)
      reject("reviewed_pair_contract_invalid",
             context + ".limit_value must be positive");
    result.push_back(
        {.quantity = quantity == "displacement"
                         ? RequirementQuantity::displacement
                         : RequirementQuantity::von_mises_stress,
         .other_quantity_description = {},
         .comparator = RequirementComparator::less_or_equal,
         .limit_value = limit,
         .unit = string_field(requirement, "unit", context, 64U),
         .applicability =
             string_field(requirement, "applicability", context),
         .criticality = criticality == "informational"
                            ? RequirementCriticality::informational
                        : criticality == "advisory"
                            ? RequirementCriticality::advisory
                            : RequirementCriticality::critical,
         .source_or_exploratory_rationale = string_field(
             requirement, "source_or_exploratory_rationale", context),
         .reviewed = true,
         .limit_basis =
             string_field(requirement, "limit_basis", context)});
  }
  return result;
}

struct SharedSpec final {
  std::string analysis_id;
  std::string component_name;
  std::string geometry_sha256;
  ReviewedMaterial material;
  std::string load_label;
  std::array<double, 3> force_n{};
  std::string restraint_label;
  std::vector<ReviewedRequirement> requirements;
  std::string scenario;
  double patch_angle_degrees{};
  double maximum_change_fraction{};
  bool load_correspondence{};
  bool restraint_correspondence{};
};

CompiledStructuralSetup prepare_sample(const SampleSpec &spec,
                                       const SharedSpec &shared,
                                       const std::string_view role) {
  const auto meshBytes = read_bounded_regular_file(
      spec.mesh.path, maximumMeshBytes,
      std::string(role) + " mesh");
  if (integrity::sha256_bytes(meshBytes) != spec.mesh.sha256)
    reject("reviewed_pair_artifact_hash_mismatch",
           std::string(role) + " mesh bytes changed");
  PreparedMesh prepared;
  try {
    prepared = prepare_gmsh_abaqus_mesh(
        meshBytes, spec.mesh.coordinate_scale_to_m);
  } catch (const std::exception &error) {
    reject("reviewed_pair_mesh_invalid",
           std::string(role) + " mesh rejected: " + error.what());
  }
  if (prepared.identity.source_sha256 != spec.mesh.sha256 ||
      prepared.mesh.nodes.size() != spec.mesh.expected_node_count ||
      prepared.mesh.elements.size() != spec.mesh.expected_element_count ||
      prepared.boundary_faces.size() !=
          spec.mesh.expected_boundary_face_count)
    reject("reviewed_pair_mesh_expectation_mismatch",
           std::string(role) + " mesh counts or identity changed");
  if (!close_value(prepared.diagnostics.minimum_mean_ratio,
                   spec.mesh.expected_minimum_mean_ratio,
                   1.0e-6, 1.0e-6))
    reject("reviewed_pair_mesh_expectation_mismatch",
           std::string(role) + " mesh quality measurement changed");

  std::vector<SurfacePatch> patches;
  try {
    patches = group_boundary_faces(prepared.boundary_faces,
                                   shared.patch_angle_degrees);
  } catch (const std::exception &error) {
    reject("reviewed_pair_patch_invalid",
           std::string(role) + " patch grouping failed: " + error.what());
  }
  require_patch_matches(patches, spec.load,
                        std::string(role) + " load");
  require_patch_matches(patches, spec.restraint,
                        std::string(role) + " restraint");
  BoundarySelection load;
  BoundarySelection restraint;
  try {
    load = resolve_boundary_selection(shared.load_label, patches,
                                      spec.load.ids);
    restraint = resolve_boundary_selection(shared.restraint_label, patches,
                                            spec.restraint.ids);
  } catch (const std::exception &error) {
    reject("reviewed_pair_patch_invalid",
           std::string(role) + " selection failed: " + error.what());
  }

  StructuralSetup setup{
      .analysis_id = shared.analysis_id,
      .component_name = shared.component_name,
      .geometry_sha256 = shared.geometry_sha256,
      .mesh = std::move(prepared.mesh),
      .boundary_faces = std::move(prepared.boundary_faces),
      .material = shared.material,
      .load = {.selection = std::move(load),
               .total_force_n = shared.force_n,
               .reviewed = true},
      .restraint = {.selection = std::move(restraint), .reviewed = true},
      .requirements = shared.requirements,
      .mesh_controls =
          {.minimum_size_m = spec.mesh.minimum_size_m,
           .maximum_size_m = spec.mesh.maximum_size_m,
           .mesher_identity = spec.mesh.mesher_identity,
           .reviewed = true,
           .mesh_sha256 = spec.mesh.sha256,
           .coordinate_scale_to_m = spec.mesh.coordinate_scale_to_m,
           .target_size_m = spec.mesh.target_size_m,
           .minimum_mean_ratio_threshold =
               spec.mesh.minimum_mean_ratio_threshold,
           .observed_minimum_mean_ratio =
               prepared.diagnostics.minimum_mean_ratio},
      .scenario_description = shared.scenario,
      .scenario_confirmed = true,
      .selection_patch_angle_degrees = shared.patch_angle_degrees};
  try {
    return compile_structural_setup(setup);
  } catch (const std::exception &error) {
    reject("reviewed_pair_setup_invalid",
           std::string(role) + " setup rejected: " + error.what());
  }
}

} // namespace

PreparedReviewedStructuralPair preflight_reviewed_structural_pair(
    const fs::path &manifestPath) {
  if (manifestPath.empty())
    reject("reviewed_pair_manifest_invalid", "manifest path is required");
  std::error_code error;
  const auto absolute = fs::absolute(manifestPath, error);
  if (error)
    reject("reviewed_pair_manifest_invalid",
           "manifest path could not be resolved");
  const auto manifestBytes = read_bounded_regular_file(
      absolute, maximumManifestBytes, "reviewed-pair manifest");
  const auto canonical = canonical_json(
      manifestBytes,
      integrity::Limits{maximumManifestBytes, 64U, 100000U, 10000U,
                        10000U, 128U * 1024U},
      "reviewed-pair manifest");
  const auto root = parse_canonical(canonical, "reviewed-pair manifest");
  require_keys(root,
               {"$schema", "schema_version", "review", "analysis",
                "geometry", "material", "load", "restraint",
                "requirements", "scenario", "refinement", "jobs",
                "samples"},
               "reviewed-pair manifest");
  if (string_field(root, "$schema", "reviewed-pair manifest") != pairSchema ||
      string_field(root, "schema_version", "reviewed-pair manifest") !=
          pairVersion)
    reject("reviewed_pair_schema_unsupported",
           "reviewed-pair schema must be 1.0.0");

  const auto &review = root.at("review");
  require_keys(review,
               {"status", "reviewed_on", "reviewer_role",
                "claim_boundary"},
               "review");
  const auto reviewedOn =
      string_field(review, "reviewed_on", "review", 10U);
  if (string_field(review, "status", "review", 32U) != "approved" ||
      reviewedOn.size() != 10U || reviewedOn[4] != '-' ||
      reviewedOn[7] != '-' ||
      !std::ranges::all_of(reviewedOn, [index = std::size_t{0}](
                                           const char character) mutable {
        const bool separator = index == 4U || index == 7U;
        ++index;
        return separator ? character == '-'
                         : character >= '0' && character <= '9';
      }))
    reject("reviewed_pair_review_invalid",
           "an approved review with YYYY-MM-DD date is required");
  static_cast<void>(string_field(review, "reviewer_role", "review", 512U));
  static_cast<void>(string_field(review, "claim_boundary", "review"));

  const auto &analysis = root.at("analysis");
  require_keys(analysis, {"analysis_id", "component_name"}, "analysis");
  const auto analysisId =
      string_field(analysis, "analysis_id", "analysis", 512U);
  const auto componentName =
      string_field(analysis, "component_name", "analysis", 512U);

  const auto &geometry = root.at("geometry");
  require_keys(geometry,
               {"upstream", "source_commit", "source_path", "sha256"},
               "geometry");
  static_cast<void>(string_field(geometry, "upstream", "geometry"));
  static_cast<void>(string_field(geometry, "source_path", "geometry"));
  const auto sourceCommit =
      string_field(geometry, "source_commit", "geometry", 40U);
  const auto geometrySha =
      string_field(geometry, "sha256", "geometry", 71U);
  if (!lowercase_hex(sourceCommit, 40U) || !strict_sha256(geometrySha))
    reject("reviewed_pair_contract_invalid",
           "geometry source commit or SHA-256 is invalid");

  const auto directory = absolute.parent_path();
  auto material = decode_material(root.at("material"), directory);

  const auto &load = root.at("load");
  require_keys(load, {"label", "total_force_n", "reviewed"}, "load");
  const auto loadLabel = string_field(load, "label", "load", 512U);
  const auto force = vector3(load.at("total_force_n"), "load.total_force_n");
  if (!bool_field(load, "reviewed", "load") ||
      std::hypot(force[0], force[1], force[2]) <= 0.0)
    reject("reviewed_pair_contract_invalid",
           "a reviewed nonzero total force is required");

  const auto &restraint = root.at("restraint");
  require_keys(restraint, {"label", "reviewed"}, "restraint");
  const auto restraintLabel =
      string_field(restraint, "label", "restraint", 512U);
  if (!bool_field(restraint, "reviewed", "restraint"))
    reject("reviewed_pair_contract_invalid",
           "a reviewed restraint label is required");

  auto requirements = decode_requirements(root.at("requirements"));
  const auto &scenario = root.at("scenario");
  require_keys(scenario, {"description", "confirmed"}, "scenario");
  const auto scenarioDescription =
      string_field(scenario, "description", "scenario");
  if (!bool_field(scenario, "confirmed", "scenario"))
    reject("reviewed_pair_review_invalid",
           "the structural scenario must be confirmed");

  const auto &refinement = root.at("refinement");
  require_keys(refinement,
               {"selection_patch_angle_degrees",
                "maximum_change_fraction",
                "load_region_correspondence_reviewed",
                "restraint_region_correspondence_reviewed"},
               "refinement");
  const auto patchAngle = number_field(
      refinement, "selection_patch_angle_degrees", "refinement");
  const auto maximumChange = number_field(
      refinement, "maximum_change_fraction", "refinement");
  const auto loadCorrespondence = bool_field(
      refinement, "load_region_correspondence_reviewed", "refinement");
  const auto restraintCorrespondence = bool_field(
      refinement, "restraint_region_correspondence_reviewed", "refinement");
  if (patchAngle <= 0.0 || patchAngle > 180.0 || maximumChange <= 0.0 ||
      maximumChange > 1.0 || !loadCorrespondence ||
      !restraintCorrespondence)
    reject("reviewed_pair_review_invalid",
           "refinement threshold and boundary correspondence require review");

  const auto &jobs = root.at("jobs");
  require_keys(jobs, {"coarse", "fine"}, "jobs");
  const auto coarseJob = string_field(jobs, "coarse", "jobs", 128U);
  const auto fineJob = string_field(jobs, "fine", "jobs", 128U);
  if (!safe_job_name(coarseJob) || !safe_job_name(fineJob) ||
      coarseJob == fineJob)
    reject("reviewed_pair_job_invalid",
           "coarse and fine need distinct safe job names");

  const auto &samples = root.at("samples");
  require_keys(samples, {"coarse", "fine"}, "samples");
  const auto coarseSpec =
      decode_sample(samples.at("coarse"), directory, "coarse");
  const auto fineSpec =
      decode_sample(samples.at("fine"), directory, "fine");

  SharedSpec shared{
      .analysis_id = analysisId,
      .component_name = componentName,
      .geometry_sha256 = geometrySha,
      .material = std::move(material),
      .load_label = loadLabel,
      .force_n = force,
      .restraint_label = restraintLabel,
      .requirements = std::move(requirements),
      .scenario = scenarioDescription,
      .patch_angle_degrees = patchAngle,
      .maximum_change_fraction = maximumChange,
      .load_correspondence = loadCorrespondence,
      .restraint_correspondence = restraintCorrespondence};

  auto coarseSetup = prepare_sample(coarseSpec, shared, "coarse");
  auto fineSetup = prepare_sample(fineSpec, shared, "fine");
  if (coarseSpec.mesh.sha256 == fineSpec.mesh.sha256)
    reject("reviewed_pair_meshes_not_distinct",
           "coarse and fine mesh identities must differ");
  if (fineSetup.request.elements.size() <=
          coarseSetup.request.elements.size() ||
      fineSpec.mesh.target_size_m >= coarseSpec.mesh.target_size_m ||
      fineSpec.mesh.coordinate_scale_to_m !=
          coarseSpec.mesh.coordinate_scale_to_m)
    reject("reviewed_pair_refinement_order_invalid",
           "fine mesh ordering or coordinate scale is invalid");

  StructuralRefinementCriterion criterion =
      compile_structural_refinement_criterion(
          global_structural_observable_specs(maximumChange));
  auto correspondence = review_structural_boundary_correspondence(
      coarseSetup, fineSetup, loadCorrespondence,
      restraintCorrespondence);
  return {.manifest_path = absolute,
          .manifest_identity = integrity::sha256_bytes(canonical),
          .coarse_job_name = coarseJob,
          .fine_job_name = fineJob,
          .criterion = std::move(criterion),
          .coarse_setup = std::move(coarseSetup),
          .fine_setup = std::move(fineSetup),
          .boundary_correspondence = std::move(correspondence)};
}

} // namespace prometheus::structural
