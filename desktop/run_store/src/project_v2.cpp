#include <prometheus/run_store/project_v2.hpp>
#include <prometheus/run_store/project_evidence_archive.hpp>

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace prometheus::run_store {
namespace {

using Json = nlohmann::json;

constexpr std::string_view package_media_type =
    "application/vnd.prometheus.execution-component+json;version=2.0.0";
constexpr std::string_view package_schema_id =
    "urn:prometheus:schema:execution-component:2.0.0";
constexpr std::string_view scenario_media_type =
    "application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0";
constexpr std::string_view scenario_schema_id =
    "urn:prometheus:schema:motor-arm-scenario:1.0.0";
constexpr std::string_view request_media_type =
    "application/vnd.prometheus.analysis-request+json;version=1.0.0";
constexpr std::string_view request_schema_id =
    "urn:prometheus:schema:analysis-request:1.0.0";
constexpr std::string_view result_media_type =
    "application/vnd.prometheus.analysis-result+json;version=1.0.0";
constexpr std::string_view result_schema_id =
    "urn:prometheus:schema:analysis-result:1.0.0";
constexpr std::string_view manifest_media_type =
    "application/vnd.prometheus.run-manifest+json;version=1.0.0";
constexpr std::string_view manifest_schema_id =
    "urn:prometheus:schema:run-manifest:1.0.0";

constexpr std::size_t maximum_identity_bytes = 512U;
constexpr std::size_t maximum_text_bytes = 4096U;
constexpr std::size_t maximum_geometry_findings = 10000U;
constexpr std::size_t maximum_cad_records = 10000U;

class ProjectError final : public std::runtime_error {
public:
  ProjectError(std::string code, std::string message,
               std::optional<std::string> field = std::nullopt)
      : std::runtime_error(std::move(message)), code_(std::move(code)),
        field_(std::move(field)) {}

  [[nodiscard]] const std::string &code() const noexcept { return code_; }
  [[nodiscard]] const std::optional<std::string> &field() const noexcept {
    return field_;
  }

private:
  std::string code_;
  std::optional<std::string> field_;
};

[[noreturn]] void reject(std::string code, std::string message,
                         std::optional<std::string> field = std::nullopt) {
  throw ProjectError(std::move(code), std::move(message), std::move(field));
}

std::string bounded(std::string value, const std::size_t maximum_bytes) {
  if (value.size() <= maximum_bytes) {
    return value;
  }
  auto boundary = maximum_bytes;
  while (boundary > 0U && boundary < value.size() &&
         (static_cast<unsigned char>(value[boundary]) & 0xC0U) == 0x80U) {
    --boundary;
  }
  value.resize(boundary);
  return value;
}

Diagnostic diagnostic(std::string code, std::string message,
                      std::optional<std::string> field = std::nullopt) {
  if (field.has_value()) {
    *field = bounded(std::move(*field), maximum_identity_bytes);
  }
  return Diagnostic{"project_v2", bounded(std::move(code), 128U),
                    bounded(std::move(message), maximum_text_bytes),
                    std::move(field), std::nullopt};
}

integrity::Limits project_limits() {
  return integrity::Limits{maximum_project_bytes, 64U, 500000U, 10000U,
                           10000U, 1024U * 1024U};
}

bool contains(const std::initializer_list<std::string_view> allowed,
              const std::string_view candidate) {
  return std::find(allowed.begin(), allowed.end(), candidate) != allowed.end();
}

void require_exact_members(
    const Json &value, const std::initializer_list<std::string_view> members,
    const std::string_view field) {
  if (!value.is_object()) {
    reject("invalid_type", "project member must be an object",
           std::string(field));
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!contains(members, iterator.key())) {
      reject("unknown_field", "project object has an unknown field",
             std::string(field) + "." + iterator.key());
    }
  }
  for (const auto member : members) {
    if (!value.contains(std::string(member))) {
      reject("missing_field", "project object is missing a required field",
             std::string(field) + "." + std::string(member));
    }
  }
}

const std::string &require_string(const Json &object,
                                  const std::string_view member,
                                  const std::string_view field,
                                  const std::size_t maximum_bytes =
                                      maximum_text_bytes,
                                  const bool allow_empty = false) {
  const auto &value = object.at(std::string(member));
  if (!value.is_string()) {
    reject("invalid_type", "project member must be a string",
           std::string(field));
  }
  const auto &text = value.get_ref<const std::string &>();
  if ((!allow_empty && text.empty()) || text.size() > maximum_bytes) {
    reject("invalid_string", "project string violates its byte bounds",
           std::string(field));
  }
  return text;
}

std::uint64_t require_unsigned(const Json &object,
                               const std::string_view member,
                               const std::string_view field) {
  const auto &value = object.at(std::string(member));
  if ((!value.is_number_integer() && !value.is_number_unsigned()) ||
      (value.is_number_integer() && value.get<std::int64_t>() < 0)) {
    reject("invalid_type", "project member must be a nonnegative integer",
           std::string(field));
  }
  return value.get<std::uint64_t>();
}

double require_finite_number(const Json &object, const std::string_view member,
                             const std::string_view field) {
  const auto &value = object.at(std::string(member));
  if (!value.is_number()) {
    reject("invalid_type", "project member must be a number",
           std::string(field));
  }
  const auto result = value.get<double>();
  if (!std::isfinite(result) || (result == 0.0 && std::signbit(result))) {
    reject("invalid_number", "project number must be finite and nonnegative zero",
           std::string(field));
  }
  return result;
}

bool require_bool(const Json &object, const std::string_view member,
                  const std::string_view field) {
  const auto &value = object.at(std::string(member));
  if (!value.is_boolean()) {
    reject("invalid_type", "project member must be a Boolean",
           std::string(field));
  }
  return value.get<bool>();
}

const Json &require_array(const Json &object, const std::string_view member,
                          const std::string_view field,
                          const std::size_t maximum) {
  const auto &value = object.at(std::string(member));
  if (!value.is_array()) {
    reject("invalid_type", "project member must be an array",
           std::string(field));
  }
  if (value.size() > maximum) {
    reject(std::string(member) == "events" ? "event_limit_exceeded"
                                            : "collection_limit_exceeded",
           "project array exceeds its semantic limit", std::string(field));
  }
  return value;
}

bool ascii_identifier(const std::string_view value) {
  if (value.empty() || value.size() > 128U || value.front() < 'a' ||
      value.front() > 'z') {
    return false;
  }
  return std::all_of(value.begin() + 1, value.end(), [](const char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '_';
  });
}

enum class ReferenceKind {
  package, scenario, request, result, manifest, committed_manifest
};

bool reference_matches(const StoredObjectReference &reference,
                       const ReferenceKind kind) {
  switch (kind) {
  case ReferenceKind::package:
    return reference.media_type == package_media_type &&
           reference.schema_id == package_schema_id &&
           reference.schema_version == "2.0.0";
  case ReferenceKind::scenario:
    return reference.media_type == scenario_media_type &&
           reference.schema_id == scenario_schema_id &&
           reference.schema_version == "1.0.0";
  case ReferenceKind::request:
    return reference.media_type == request_media_type &&
           reference.schema_id == request_schema_id &&
           reference.schema_version == "1.0.0";
  case ReferenceKind::result:
    return reference.media_type == result_media_type &&
           reference.schema_id == result_schema_id &&
           reference.schema_version == "1.0.0";
  case ReferenceKind::manifest:
    return reference.media_type == manifest_media_type &&
           reference.schema_id == manifest_schema_id &&
           reference.schema_version == "1.0.0";
  case ReferenceKind::committed_manifest:
    return reference_matches(reference, ReferenceKind::manifest) ||
           (reference.media_type == project_evidence_archive_media_type &&
            reference.schema_id == project_evidence_archive_schema_id &&
            reference.schema_version == "1.0.0") ||
           (reference.media_type == execution_project_snapshot_media_type &&
            reference.schema_id == execution_project_snapshot_schema_id &&
           reference.schema_version == "1.0.0") ||
           (reference.media_type == project_inventory_media_type &&
            reference.schema_id == project_inventory_schema_id &&
            reference.schema_version == "1.0.0") ||
           (reference.media_type == structural_manifest_media_type &&
            ((reference.schema_id == structural_manifest_schema_id_v1 &&
              reference.schema_version == "1.0.0") ||
             (reference.schema_id == structural_manifest_schema_id_v2 &&
              reference.schema_version == "2.0.0") ||
             (reference.schema_id == structural_manifest_schema_id_v3 &&
              reference.schema_version == "3.0.0") ||
             (reference.schema_id == structural_manifest_schema_id_v4 &&
              reference.schema_version == "4.0.0") ||
             (reference.schema_id == structural_manifest_schema_id_modal &&
              reference.schema_version == "1.0.0"))) ||
           (reference.media_type == structural_project_run_media_type &&
            ((reference.schema_id == structural_project_run_schema_id_v1 &&
              reference.schema_version == "1.0.0") ||
             (reference.schema_id == structural_project_run_schema_id_v2 &&
              reference.schema_version == "2.0.0")));
  }
  return false;
}

StoredObjectReference parse_reference(const Json &value,
                                      const std::string_view field,
                                      const ReferenceKind expected) {
  require_exact_members(value,
                        {"object_hash", "byte_length", "media_type",
                         "schema_id", "schema_version"},
                        field);
  StoredObjectReference reference{
      require_string(value, "object_hash", std::string(field) + ".object_hash",
                     71U),
      require_unsigned(value, "byte_length",
                       std::string(field) + ".byte_length"),
      require_string(value, "media_type", std::string(field) + ".media_type",
                     maximum_identity_bytes),
      require_string(value, "schema_id", std::string(field) + ".schema_id",
                     maximum_identity_bytes),
      require_string(value, "schema_version",
                     std::string(field) + ".schema_version", 32U)};
  if (!is_valid_object_hash(reference.object_hash)) {
    reject("invalid_hash", "stored-object hash is not lowercase SHA-256",
           std::string(field) + ".object_hash");
  }
  if (reference.byte_length == 0U ||
      reference.byte_length > maximum_object_bytes) {
    reject("invalid_object_length",
           "stored-object byte length is outside the supported range",
           std::string(field) + ".byte_length");
  }
  if (!reference_matches(reference, expected)) {
    reject("invalid_object_contract",
           "stored-object reference has the wrong media/schema identity",
           std::string(field));
  }
  return reference;
}

ComponentBinding parse_component_binding(const Json &value,
                                         const std::size_t index) {
  const auto field = "component_bindings[" + std::to_string(index) + "]";
  require_exact_members(value, {"cad_entity_id", "revision_id", "label"},
                        field);
  return {require_string(value, "cad_entity_id", field + ".cad_entity_id",
                         maximum_identity_bytes),
          require_string(value, "revision_id", field + ".revision_id",
                         maximum_identity_bytes),
          require_string(value, "label", field + ".label",
                         maximum_text_bytes)};
}

PlacementOverride parse_placement(const Json &value, const std::size_t index) {
  const auto field = "placement_overrides[" + std::to_string(index) + "]";
  require_exact_members(
      value,
      {"cad_entity_id", "translation_x_m", "translation_y_m",
       "translation_z_m", "rotation_x_deg", "rotation_y_deg",
       "rotation_z_deg", "rotation_convention"},
      field);
  return {require_string(value, "cad_entity_id", field + ".cad_entity_id",
                         maximum_identity_bytes),
          require_finite_number(value, "translation_x_m",
                                field + ".translation_x_m"),
          require_finite_number(value, "translation_y_m",
                                field + ".translation_y_m"),
          require_finite_number(value, "translation_z_m",
                                field + ".translation_z_m"),
          require_finite_number(value, "rotation_x_deg",
                                field + ".rotation_x_deg"),
          require_finite_number(value, "rotation_y_deg",
                                field + ".rotation_y_deg"),
          require_finite_number(value, "rotation_z_deg",
                                field + ".rotation_z_deg"),
          require_string(value, "rotation_convention",
                         field + ".rotation_convention",
                         maximum_identity_bytes)};
}

Connection parse_connection(const Json &value, const std::size_t index) {
  const auto field = "connections[" + std::to_string(index) + "]";
  require_exact_members(
      value,
      {"id", "source_part", "source_name", "source_anchor", "target_part",
       "target_name", "target_anchor", "connection_type",
       "confirmed_by_user", "anchor_origin", "semantic_status"},
      field);
  const auto type = require_string(value, "connection_type",
                                   field + ".connection_type", 32U);
  if (!contains({"fixed", "revolute", "sliding", "contact"}, type)) {
    reject("invalid_connection_type", "connection type is unsupported",
           field + ".connection_type");
  }
  return {
      require_string(value, "id", field + ".id", maximum_text_bytes),
      require_string(value, "source_part", field + ".source_part",
                     maximum_identity_bytes),
      require_string(value, "source_name", field + ".source_name",
                     maximum_text_bytes),
      require_string(value, "source_anchor", field + ".source_anchor",
                     maximum_identity_bytes),
      require_string(value, "target_part", field + ".target_part",
                     maximum_identity_bytes),
      require_string(value, "target_name", field + ".target_name",
                     maximum_text_bytes),
      require_string(value, "target_anchor", field + ".target_anchor",
                     maximum_identity_bytes),
      type,
      require_bool(value, "confirmed_by_user", field + ".confirmed_by_user"),
      require_string(value, "anchor_origin", field + ".anchor_origin",
                     maximum_identity_bytes),
      require_string(value, "semantic_status", field + ".semantic_status",
                     maximum_identity_bytes)};
}

InterferenceClassification parse_classification(const Json &value,
                                                 const std::size_t index) {
  const auto field =
      "interference_classifications[" + std::to_string(index) + "]";
  require_exact_members(value, {"first_id", "second_id", "classification"},
                        field);
  const auto classification = require_string(
      value, "classification", field + ".classification", 64U);
  if (!contains({"unclassified", "intended_engagement", "prohibited"},
                classification)) {
    reject("invalid_interference_classification",
           "interference classification is unsupported",
           field + ".classification");
  }
  return {require_string(value, "first_id", field + ".first_id",
                         maximum_identity_bytes),
          require_string(value, "second_id", field + ".second_id",
                         maximum_identity_bytes),
          classification};
}

RevoluteJoint parse_joint(const Json &value) {
  require_exact_members(
      value,
      {"type", "source_index", "target_index", "axis", "minimum_deg",
       "maximum_deg", "pivot_x", "pivot_y", "pivot_z",
       "confirmed_by_user"},
      "engineering.joint");
  const auto type = require_string(value, "type", "engineering.joint.type", 32U);
  const auto axis = require_string(value, "axis", "engineering.joint.axis", 8U);
  if (type != "revolute") {
    reject("invalid_joint_type", "only revolute joints are supported",
           "engineering.joint.type");
  }
  if (!contains({"X", "Y", "Z"}, axis)) {
    reject("invalid_joint_axis", "joint axis is unsupported",
           "engineering.joint.axis");
  }
  const auto source = require_unsigned(value, "source_index",
                                       "engineering.joint.source_index");
  const auto target = require_unsigned(value, "target_index",
                                       "engineering.joint.target_index");
  if (source == target) {
    reject("invalid_joint_entities", "joint endpoints must differ",
           "engineering.joint");
  }
  const auto minimum = require_finite_number(value, "minimum_deg",
                                             "engineering.joint.minimum_deg");
  const auto maximum = require_finite_number(value, "maximum_deg",
                                             "engineering.joint.maximum_deg");
  if (minimum > maximum) {
    reject("invalid_joint_range", "joint minimum exceeds maximum",
           "engineering.joint");
  }
  return {type,
          source,
          target,
          axis,
          minimum,
          maximum,
          require_finite_number(value, "pivot_x", "engineering.joint.pivot_x"),
          require_finite_number(value, "pivot_y", "engineering.joint.pivot_y"),
          require_finite_number(value, "pivot_z", "engineering.joint.pivot_z"),
          require_bool(value, "confirmed_by_user",
                       "engineering.joint.confirmed_by_user")};
}

std::optional<std::string> nullable_string(const Json &object,
                                           const std::string_view member,
                                           const std::string_view field) {
  const auto &value = object.at(std::string(member));
  if (value.is_null()) {
    return std::nullopt;
  }
  return require_string(object, member, field, maximum_identity_bytes);
}

GeometryFinding parse_geometry_finding(const Json &value,
                                       const std::size_t index) {
  const auto field =
      "engineering.geometry_findings[" + std::to_string(index) + "]";
  require_exact_members(
      value,
      {"finding_kind", "status", "severity", "title", "mechanism",
       "calculated", "unit", "available", "margin_fraction", "evidence",
       "assumption", "estimated_range", "first_id", "second_id"},
      field);
  const auto kind =
      require_string(value, "finding_kind", field + ".finding_kind", 64U);
  if (!contains({"static_interference", "sampled_joint_sweep"}, kind)) {
    reject("invalid_geometry_finding_kind",
           "mutable engineering state accepts only geometry finding kinds",
           field + ".finding_kind");
  }
  const auto status = require_string(value, "status", field + ".status", 32U);
  if (!contains({"information", "caution", "fail"}, status)) {
    reject("invalid_geometry_status", "geometry finding status is unsupported",
           field + ".status");
  }
  const auto severity =
      require_string(value, "severity", field + ".severity", 32U);
  if (!contains({"information", "caution", "warning", "critical"},
                severity)) {
    reject("invalid_geometry_severity",
           "geometry finding severity is unsupported", field + ".severity");
  }
  const auto unit = require_string(value, "unit", field + ".unit", 32U);
  if (!contains({"m³", "samples"}, unit)) {
    reject("invalid_geometry_unit", "geometry finding unit is unsupported",
           field + ".unit");
  }
  return {
      kind,
      status,
      severity,
      require_string(value, "title", field + ".title", maximum_text_bytes),
      require_string(value, "mechanism", field + ".mechanism",
                     maximum_text_bytes),
      require_finite_number(value, "calculated", field + ".calculated"),
      unit,
      require_finite_number(value, "available", field + ".available"),
      require_finite_number(value, "margin_fraction", field + ".margin_fraction"),
      require_string(value, "evidence", field + ".evidence",
                     maximum_text_bytes),
      require_string(value, "assumption", field + ".assumption",
                     maximum_text_bytes, true),
      require_string(value, "estimated_range", field + ".estimated_range",
                     maximum_text_bytes, true),
      nullable_string(value, "first_id", field + ".first_id"),
      nullable_string(value, "second_id", field + ".second_id")};
}

EngineeringState parse_engineering(const Json &value) {
  require_exact_members(value,
                        {"joint", "geometry_findings", "geometry_status"},
                        "engineering");
  std::optional<RevoluteJoint> joint;
  if (!value.at("joint").is_null()) {
    joint = parse_joint(value.at("joint"));
  }
  std::vector<GeometryFinding> findings;
  const auto &items = require_array(value, "geometry_findings",
                                    "engineering.geometry_findings",
                                    maximum_geometry_findings);
  findings.reserve(items.size());
  for (std::size_t index = 0U; index < items.size(); ++index) {
    findings.push_back(parse_geometry_finding(items[index], index));
  }
  const auto status = require_string(value, "geometry_status",
                                     "engineering.geometry_status", 32U);
  if (!contains({"not_evaluated", "completed", "failed"}, status)) {
    reject("invalid_geometry_status", "geometry execution status is unsupported",
           "engineering.geometry_status");
  }
  return {std::move(joint), std::move(findings), status};
}

std::optional<std::string> parse_legacy(const Json &value) {
  if (value.is_null()) {
    return std::nullopt;
  }
  if (!value.is_object() || value.size() > 4U) {
    reject("invalid_legacy_state",
           "legacy v1 engineering preservation must be a bounded object",
           "legacy_v1_engineering_state");
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!contains({"joint", "scenario", "findings", "run_status"},
                  iterator.key())) {
      reject("unknown_field", "legacy v1 engineering object has an unknown field",
             "legacy_v1_engineering_state." + iterator.key());
    }
  }
  if (value.contains("joint") && !value.at("joint").is_object()) {
    reject("invalid_legacy_state", "legacy joint must be an object",
           "legacy_v1_engineering_state.joint");
  }
  if (value.contains("scenario") && !value.at("scenario").is_object()) {
    reject("invalid_legacy_state", "legacy scenario must be an object",
           "legacy_v1_engineering_state.scenario");
  }
  if (value.contains("findings") && !value.at("findings").is_array()) {
    reject("invalid_legacy_state", "legacy findings must be an array",
           "legacy_v1_engineering_state.findings");
  }
  if (value.contains("findings") && value.at("findings").size() > 256U) {
    reject("invalid_legacy_state", "legacy findings exceed the preservation limit",
           "legacy_v1_engineering_state.findings");
  }
  if (value.contains("run_status") && !value.at("run_status").is_string()) {
    reject("invalid_legacy_state", "legacy run status must be a string",
           "legacy_v1_engineering_state.run_status");
  }
  return integrity::canonicalize_json_bytes(value.dump(), project_limits());
}

PackageBinding parse_package_binding(const Json &value,
                                     const std::size_t index) {
  const auto field =
      "execution.package_bindings[" + std::to_string(index) + "]";
  require_exact_members(value,
                        {"binding_revision", "supersedes_binding_revision",
                         "cad_entity_id", "package"},
                        field);
  std::optional<std::uint64_t> supersedes;
  if (!value.at("supersedes_binding_revision").is_null()) {
    supersedes = require_unsigned(value, "supersedes_binding_revision",
                                  field + ".supersedes_binding_revision");
  }
  return {require_unsigned(value, "binding_revision",
                           field + ".binding_revision"),
          supersedes,
          require_string(value, "cad_entity_id", field + ".cad_entity_id",
                         maximum_identity_bytes),
          parse_reference(value.at("package"), field + ".package",
                          ReferenceKind::package)};
}

JointBinding parse_joint_binding(const Json &value, const std::size_t index) {
  const auto field = "execution.joint_bindings[" + std::to_string(index) + "]";
  require_exact_members(
      value,
      {"binding_revision", "supersedes_binding_revision",
       "source_cad_entity_id", "target_cad_entity_id", "type", "axis",
       "minimum_deg", "maximum_deg", "pivot_x", "pivot_y", "pivot_z"},
      field);
  std::optional<std::uint64_t> supersedes;
  if (!value.at("supersedes_binding_revision").is_null()) {
    supersedes = require_unsigned(value, "supersedes_binding_revision",
                                  field + ".supersedes_binding_revision");
  }
  const auto &source =
      require_string(value, "source_cad_entity_id",
                     field + ".source_cad_entity_id", maximum_identity_bytes);
  const auto &target =
      require_string(value, "target_cad_entity_id",
                     field + ".target_cad_entity_id", maximum_identity_bytes);
  if (source == target) {
    reject("invalid_joint_binding_entities",
           "joint binding endpoints must differ", field);
  }
  const auto &type = require_string(value, "type", field + ".type", 32U);
  if (type != "revolute") {
    reject("invalid_joint_binding_type", "only revolute joints are supported",
           field + ".type");
  }
  const auto &axis = require_string(value, "axis", field + ".axis", 8U);
  if (!contains({"X", "Y", "Z"}, axis)) {
    reject("invalid_joint_binding_axis", "joint binding axis is unsupported",
           field + ".axis");
  }
  const auto minimum =
      require_finite_number(value, "minimum_deg", field + ".minimum_deg");
  const auto maximum =
      require_finite_number(value, "maximum_deg", field + ".maximum_deg");
  if (minimum > maximum) {
    reject("invalid_joint_binding_range",
           "joint binding minimum exceeds maximum", field);
  }
  return {require_unsigned(value, "binding_revision",
                           field + ".binding_revision"),
          supersedes,
          source,
          target,
          type,
          axis,
          minimum,
          maximum,
          require_finite_number(value, "pivot_x", field + ".pivot_x"),
          require_finite_number(value, "pivot_y", field + ".pivot_y"),
          require_finite_number(value, "pivot_z", field + ".pivot_z")};
}

// Two CAD entities joined by a confirmed joint form a single, symmetric
// physical relationship -- a rebind with source and target swapped is still
// the same joint, not a second one. The supersession chain is therefore keyed
// by the unordered pair of entity ids, unlike a package binding's single
// cad_entity_id key.
std::string joint_pair_key(const std::string &first,
                           const std::string &second) {
  return first <= second ? first + '\x01' + second
                         : second + '\x01' + first;
}

void validate_joint_binding_graph(const std::vector<JointBinding> &bindings) {
  struct Revision final {
    std::string pair_key;
    bool superseded{false};
  };
  std::unordered_map<std::uint64_t, Revision> revisions;
  std::unordered_set<std::string> active_pairs;
  revisions.reserve(bindings.size());
  active_pairs.reserve(bindings.size());
  for (std::size_t index = 0U; index < bindings.size(); ++index) {
    const auto &binding = bindings[index];
    const auto expected = static_cast<std::uint64_t>(index) + 1U;
    if (binding.binding_revision != expected) {
      reject("joint_binding_revision_order_invalid",
             "joint binding revisions must be unique, contiguous, and "
             "ordered",
             "execution.joint_bindings[" + std::to_string(index) +
                 "].binding_revision");
    }
    const auto pair_key = joint_pair_key(binding.source_cad_entity_id,
                                         binding.target_cad_entity_id);
    if (binding.supersedes_binding_revision.has_value()) {
      const auto prior = revisions.find(*binding.supersedes_binding_revision);
      if (prior == revisions.end()) {
        reject("joint_binding_supersession_invalid",
               "joint binding supersession must identify an earlier "
               "revision",
               "execution.joint_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.pair_key != pair_key) {
        reject("joint_binding_supersession_cross_pair",
               "a joint binding cannot supersede a different CAD entity "
               "pair",
               "execution.joint_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.superseded) {
        reject("joint_binding_supersession_invalid",
               "a joint binding revision cannot be superseded twice",
               "execution.joint_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      prior->second.superseded = true;
      active_pairs.erase(pair_key);
    } else if (active_pairs.contains(pair_key)) {
      reject("multiple_active_joint_bindings",
             "a CAD entity pair may have only one unsuperseded joint "
             "binding",
             "execution.joint_bindings[" + std::to_string(index) + "]");
    }
    active_pairs.insert(pair_key);
    revisions.emplace(binding.binding_revision, Revision{pair_key, false});
  }
}

RequirementBinding parse_requirement_binding(const Json &value,
                                             const std::size_t index) {
  const auto field =
      "execution.requirement_bindings[" + std::to_string(index) + "]";
  require_exact_members(
      value,
      {"binding_revision", "supersedes_binding_revision", "geometry_sha256",
       "analysis_id", "quantity", "other_quantity_description", "comparator",
       "limit_value", "unit", "applicability", "criticality",
       "source_or_exploratory_rationale"},
      field);
  std::optional<std::uint64_t> supersedes;
  if (!value.at("supersedes_binding_revision").is_null()) {
    supersedes = require_unsigned(value, "supersedes_binding_revision",
                                  field + ".supersedes_binding_revision");
  }
  const auto &geometry =
      require_string(value, "geometry_sha256", field + ".geometry_sha256",
                     maximum_identity_bytes);
  const auto &analysis_id = require_string(
      value, "analysis_id", field + ".analysis_id", maximum_identity_bytes);
  const auto &quantity =
      require_string(value, "quantity", field + ".quantity", 32U);
  if (!contains({"displacement", "von_mises_stress", "natural_frequency",
                "other"}, quantity)) {
    reject("invalid_requirement_binding_quantity",
           "requirement binding quantity is unsupported", field + ".quantity");
  }
  const auto &description =
      require_string(value, "other_quantity_description",
                     field + ".other_quantity_description", maximum_text_bytes,
                     true);
  if ((quantity == "other") == description.empty()) {
    reject("invalid_requirement_binding_description",
           "an 'other' requirement binding needs a description and a "
           "supported quantity must not have one",
           field + ".other_quantity_description");
  }
  const auto &comparator =
      require_string(value, "comparator", field + ".comparator", 32U);
  if (!contains({"less_or_equal", "greater_or_equal"}, comparator)) {
    reject("invalid_requirement_binding_comparator",
           "requirement binding comparator is unsupported",
           field + ".comparator");
  }
  const auto limit =
      require_finite_number(value, "limit_value", field + ".limit_value");
  if (quantity != "other" && limit <= 0.0) {
    reject("invalid_requirement_binding_limit",
           "a supported requirement binding quantity needs a positive limit",
           field + ".limit_value");
  }
  const auto &unit =
      require_string(value, "unit", field + ".unit", 32U);
  const auto &applicability = require_string(
      value, "applicability", field + ".applicability", maximum_text_bytes, true);
  const auto &criticality =
      require_string(value, "criticality", field + ".criticality", 32U);
  if (!contains({"informational", "advisory", "critical"}, criticality)) {
    reject("invalid_requirement_binding_criticality",
           "requirement binding criticality is unsupported",
           field + ".criticality");
  }
  const auto &rationale =
      require_string(value, "source_or_exploratory_rationale",
                     field + ".source_or_exploratory_rationale",
                     maximum_text_bytes);
  return {require_unsigned(value, "binding_revision",
                           field + ".binding_revision"),
          supersedes,
          geometry,
          analysis_id,
          quantity,
          description,
          comparator,
          limit,
          unit,
          applicability,
          criticality,
          rationale};
}

// A requirement is identified by which geometry and which quantity it
// reviews -- not a symmetric two-entity relationship like a joint, so no
// unordered-pair key is needed. An "other" (uncovered) requirement has no
// supported quantity to disambiguate it, so its own description joins the
// key, letting two distinct uncovered requirements on the same geometry
// open independent chains instead of colliding.
std::string requirement_key(const RequirementBinding &binding) {
  return binding.geometry_sha256 + '\x01' + binding.quantity + '\x01' +
        binding.other_quantity_description;
}

void validate_requirement_binding_graph(
    const std::vector<RequirementBinding> &bindings) {
  struct Revision final {
    std::string key;
    bool superseded{false};
  };
  std::unordered_map<std::uint64_t, Revision> revisions;
  std::unordered_set<std::string> active_keys;
  revisions.reserve(bindings.size());
  active_keys.reserve(bindings.size());
  for (std::size_t index = 0U; index < bindings.size(); ++index) {
    const auto &binding = bindings[index];
    const auto expected = static_cast<std::uint64_t>(index) + 1U;
    if (binding.binding_revision != expected) {
      reject("requirement_binding_revision_order_invalid",
             "requirement binding revisions must be unique, contiguous, and "
             "ordered",
             "execution.requirement_bindings[" + std::to_string(index) +
                 "].binding_revision");
    }
    const auto key = requirement_key(binding);
    if (binding.supersedes_binding_revision.has_value()) {
      const auto prior = revisions.find(*binding.supersedes_binding_revision);
      if (prior == revisions.end()) {
        reject("requirement_binding_supersession_invalid",
               "requirement binding supersession must identify an earlier "
               "revision",
               "execution.requirement_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.key != key) {
        reject("requirement_binding_supersession_cross_key",
               "a requirement binding cannot supersede a different "
               "geometry/quantity key",
               "execution.requirement_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.superseded) {
        reject("requirement_binding_supersession_invalid",
               "a requirement binding revision cannot be superseded twice",
               "execution.requirement_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      prior->second.superseded = true;
      active_keys.erase(key);
    } else if (active_keys.contains(key)) {
      reject("multiple_active_requirement_bindings",
             "a geometry/quantity key may have only one unsuperseded "
             "requirement binding",
             "execution.requirement_bindings[" + std::to_string(index) + "]");
    }
    active_keys.insert(key);
    revisions.emplace(binding.binding_revision, Revision{key, false});
  }
}

MaterialBinding parse_material_binding(const Json &value,
                                       const std::size_t index) {
  const auto field =
      "execution.material_bindings[" + std::to_string(index) + "]";
  require_exact_members(
      value,
      {"binding_revision", "supersedes_binding_revision", "geometry_sha256",
       "analysis_id", "designation", "source_sha256", "applicability",
       "youngs_modulus_pa", "poisson_ratio", "density_kg_m3"},
      field);
  std::optional<std::uint64_t> supersedes;
  if (!value.at("supersedes_binding_revision").is_null()) {
    supersedes = require_unsigned(value, "supersedes_binding_revision",
                                  field + ".supersedes_binding_revision");
  }
  const auto &geometry =
      require_string(value, "geometry_sha256", field + ".geometry_sha256",
                     maximum_identity_bytes);
  const auto &analysis_id = require_string(
      value, "analysis_id", field + ".analysis_id", maximum_identity_bytes);
  const auto &designation = require_string(value, "designation",
                                           field + ".designation");
  const auto &source_sha256 = require_string(
      value, "source_sha256", field + ".source_sha256", maximum_identity_bytes);
  const auto &applicability =
      require_string(value, "applicability", field + ".applicability");
  const auto modulus = require_finite_number(value, "youngs_modulus_pa",
                                             field + ".youngs_modulus_pa");
  if (modulus <= 0.0) {
    reject("invalid_material_binding_modulus",
           "a reviewed material needs a positive Young's modulus",
           field + ".youngs_modulus_pa");
  }
  const auto ratio =
      require_finite_number(value, "poisson_ratio", field + ".poisson_ratio");
  if (ratio <= -1.0 || ratio >= 0.5) {
    reject("invalid_material_binding_poisson_ratio",
           "a reviewed material needs a Poisson ratio between -1 and 0.5",
           field + ".poisson_ratio");
  }
  std::optional<double> density;
  if (!value.at("density_kg_m3").is_null()) {
    density = require_finite_number(value, "density_kg_m3",
                                    field + ".density_kg_m3");
    if (*density <= 0.0) {
      reject("invalid_material_binding_density",
             "a reviewed material density must be positive",
             field + ".density_kg_m3");
    }
  }
  return {require_unsigned(value, "binding_revision",
                           field + ".binding_revision"),
          supersedes,
          geometry,
          analysis_id,
          designation,
          source_sha256,
          applicability,
          modulus,
          ratio,
          density};
}

void validate_material_binding_graph(
    const std::vector<MaterialBinding> &bindings) {
  struct Revision final {
    std::string geometry;
    bool superseded{false};
  };
  std::unordered_map<std::uint64_t, Revision> revisions;
  std::unordered_set<std::string> active_geometries;
  revisions.reserve(bindings.size());
  active_geometries.reserve(bindings.size());
  for (std::size_t index = 0U; index < bindings.size(); ++index) {
    const auto &binding = bindings[index];
    const auto expected = static_cast<std::uint64_t>(index) + 1U;
    if (binding.binding_revision != expected) {
      reject("material_binding_revision_order_invalid",
             "material binding revisions must be unique, contiguous, and "
             "ordered",
             "execution.material_bindings[" + std::to_string(index) +
                 "].binding_revision");
    }
    if (binding.supersedes_binding_revision.has_value()) {
      const auto prior = revisions.find(*binding.supersedes_binding_revision);
      if (prior == revisions.end()) {
        reject("material_binding_supersession_invalid",
               "material binding supersession must identify an earlier "
               "revision",
               "execution.material_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.geometry != binding.geometry_sha256) {
        reject("material_binding_supersession_cross_geometry",
               "a material binding cannot supersede a different geometry",
               "execution.material_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.superseded) {
        reject("material_binding_supersession_invalid",
               "a material binding revision cannot be superseded twice",
               "execution.material_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      prior->second.superseded = true;
      active_geometries.erase(binding.geometry_sha256);
    } else if (active_geometries.contains(binding.geometry_sha256)) {
      reject("multiple_active_material_bindings",
             "a geometry may have only one unsuperseded material binding",
             "execution.material_bindings[" + std::to_string(index) + "]");
    }
    active_geometries.insert(binding.geometry_sha256);
    revisions.emplace(binding.binding_revision,
                      Revision{binding.geometry_sha256, false});
  }
}

// Shared by parse_load_binding and parse_restraint_binding: the exact
// durable boundary topology a visual patch selection resolved to
// (prometheus::structural::BoundarySelection), not a transient patch id.
std::vector<std::array<int, 3>> parse_selection_faces(
    const Json &value, const std::string &field) {
  const auto &faces = require_array(value, "face_node_ids",
                                    field + ".face_node_ids",
                                    maximum_selection_faces);
  if (faces.empty()) {
    reject("invalid_selection_faces",
           "a surface selection needs at least one face",
           field + ".face_node_ids");
  }
  std::vector<std::array<int, 3>> result;
  result.reserve(faces.size());
  for (std::size_t index = 0U; index < faces.size(); ++index) {
    const auto &face = faces[index];
    if (!face.is_array() || face.size() != 3U) {
      reject("invalid_selection_face",
             "a selection face must have exactly three node ids",
             field + ".face_node_ids[" + std::to_string(index) + "]");
    }
    std::array<int, 3> triangle{};
    for (std::size_t component = 0U; component < 3U; ++component) {
      if (!face[component].is_number_integer()) {
        reject("invalid_selection_face",
               "a selection face node id must be an integer",
               field + ".face_node_ids[" + std::to_string(index) + "]");
      }
      triangle[component] = face[component].get<int>();
    }
    result.push_back(triangle);
  }
  return result;
}

std::vector<int> parse_selection_nodes(const Json &value,
                                       const std::string &field) {
  const auto &nodes = require_array(value, "node_ids", field + ".node_ids",
                                    maximum_selection_nodes);
  if (nodes.empty()) {
    reject("invalid_selection_nodes",
           "a surface selection needs at least one node",
           field + ".node_ids");
  }
  std::vector<int> result;
  result.reserve(nodes.size());
  for (std::size_t index = 0U; index < nodes.size(); ++index) {
    if (!nodes[index].is_number_integer()) {
      reject("invalid_selection_nodes", "a selection node id must be an integer",
             field + ".node_ids[" + std::to_string(index) + "]");
    }
    result.push_back(nodes[index].get<int>());
  }
  return result;
}

double parse_selection_area(const Json &value, const std::string &field) {
  const auto area = require_finite_number(value, "area_m2", field + ".area_m2");
  if (area <= 0.0) {
    reject("invalid_selection_area",
           "a surface selection needs a positive area", field + ".area_m2");
  }
  return area;
}

LoadBinding parse_load_binding(const Json &value, const std::size_t index) {
  const auto field = "execution.load_bindings[" + std::to_string(index) + "]";
  require_exact_members(
      value,
      {"binding_revision", "supersedes_binding_revision", "geometry_sha256",
       "analysis_id", "selection_label", "face_node_ids", "node_ids",
       "area_m2", "force_x_n", "force_y_n", "force_z_n"},
      field);
  std::optional<std::uint64_t> supersedes;
  if (!value.at("supersedes_binding_revision").is_null()) {
    supersedes = require_unsigned(value, "supersedes_binding_revision",
                                  field + ".supersedes_binding_revision");
  }
  const auto &geometry =
      require_string(value, "geometry_sha256", field + ".geometry_sha256",
                     maximum_identity_bytes);
  const auto &analysis_id = require_string(
      value, "analysis_id", field + ".analysis_id", maximum_identity_bytes);
  const auto &label = require_string(value, "selection_label",
                                     field + ".selection_label", 512U);
  auto faces = parse_selection_faces(value, field);
  auto nodes = parse_selection_nodes(value, field);
  const auto area = parse_selection_area(value, field);
  return {require_unsigned(value, "binding_revision",
                           field + ".binding_revision"),
          supersedes,
          geometry,
          analysis_id,
          label,
          std::move(faces),
          std::move(nodes),
          area,
          require_finite_number(value, "force_x_n", field + ".force_x_n"),
          require_finite_number(value, "force_y_n", field + ".force_y_n"),
          require_finite_number(value, "force_z_n", field + ".force_z_n")};
}

RestraintBinding parse_restraint_binding(const Json &value,
                                         const std::size_t index) {
  const auto field =
      "execution.restraint_bindings[" + std::to_string(index) + "]";
  require_exact_members(
      value,
      {"binding_revision", "supersedes_binding_revision", "geometry_sha256",
       "analysis_id", "selection_label", "face_node_ids", "node_ids",
       "area_m2"},
      field);
  std::optional<std::uint64_t> supersedes;
  if (!value.at("supersedes_binding_revision").is_null()) {
    supersedes = require_unsigned(value, "supersedes_binding_revision",
                                  field + ".supersedes_binding_revision");
  }
  const auto &geometry =
      require_string(value, "geometry_sha256", field + ".geometry_sha256",
                     maximum_identity_bytes);
  const auto &analysis_id = require_string(
      value, "analysis_id", field + ".analysis_id", maximum_identity_bytes);
  const auto &label = require_string(value, "selection_label",
                                     field + ".selection_label", 512U);
  auto faces = parse_selection_faces(value, field);
  auto nodes = parse_selection_nodes(value, field);
  const auto area = parse_selection_area(value, field);
  return {require_unsigned(value, "binding_revision",
                           field + ".binding_revision"),
          supersedes,
          geometry,
          analysis_id,
          label,
          std::move(faces),
          std::move(nodes),
          area};
}

void validate_load_binding_graph(const std::vector<LoadBinding> &bindings) {
  struct Revision final {
    std::string geometry;
    bool superseded{false};
  };
  std::unordered_map<std::uint64_t, Revision> revisions;
  std::unordered_set<std::string> active_geometries;
  revisions.reserve(bindings.size());
  active_geometries.reserve(bindings.size());
  for (std::size_t index = 0U; index < bindings.size(); ++index) {
    const auto &binding = bindings[index];
    const auto expected = static_cast<std::uint64_t>(index) + 1U;
    if (binding.binding_revision != expected) {
      reject("load_binding_revision_order_invalid",
             "load binding revisions must be unique, contiguous, and ordered",
             "execution.load_bindings[" + std::to_string(index) +
                 "].binding_revision");
    }
    if (binding.supersedes_binding_revision.has_value()) {
      const auto prior = revisions.find(*binding.supersedes_binding_revision);
      if (prior == revisions.end()) {
        reject("load_binding_supersession_invalid",
               "load binding supersession must identify an earlier revision",
               "execution.load_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.geometry != binding.geometry_sha256) {
        reject("load_binding_supersession_cross_geometry",
               "a load binding cannot supersede a different geometry",
               "execution.load_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.superseded) {
        reject("load_binding_supersession_invalid",
               "a load binding revision cannot be superseded twice",
               "execution.load_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      prior->second.superseded = true;
      active_geometries.erase(binding.geometry_sha256);
    } else if (active_geometries.contains(binding.geometry_sha256)) {
      reject("multiple_active_load_bindings",
             "a geometry may have only one unsuperseded load binding",
             "execution.load_bindings[" + std::to_string(index) + "]");
    }
    active_geometries.insert(binding.geometry_sha256);
    revisions.emplace(binding.binding_revision,
                      Revision{binding.geometry_sha256, false});
  }
}

void validate_restraint_binding_graph(
    const std::vector<RestraintBinding> &bindings) {
  struct Revision final {
    std::string geometry;
    bool superseded{false};
  };
  std::unordered_map<std::uint64_t, Revision> revisions;
  std::unordered_set<std::string> active_geometries;
  revisions.reserve(bindings.size());
  active_geometries.reserve(bindings.size());
  for (std::size_t index = 0U; index < bindings.size(); ++index) {
    const auto &binding = bindings[index];
    const auto expected = static_cast<std::uint64_t>(index) + 1U;
    if (binding.binding_revision != expected) {
      reject("restraint_binding_revision_order_invalid",
             "restraint binding revisions must be unique, contiguous, and "
             "ordered",
             "execution.restraint_bindings[" + std::to_string(index) +
                 "].binding_revision");
    }
    if (binding.supersedes_binding_revision.has_value()) {
      const auto prior = revisions.find(*binding.supersedes_binding_revision);
      if (prior == revisions.end()) {
        reject("restraint_binding_supersession_invalid",
               "restraint binding supersession must identify an earlier "
               "revision",
               "execution.restraint_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.geometry != binding.geometry_sha256) {
        reject("restraint_binding_supersession_cross_geometry",
               "a restraint binding cannot supersede a different geometry",
               "execution.restraint_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.superseded) {
        reject("restraint_binding_supersession_invalid",
               "a restraint binding revision cannot be superseded twice",
               "execution.restraint_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      prior->second.superseded = true;
      active_geometries.erase(binding.geometry_sha256);
    } else if (active_geometries.contains(binding.geometry_sha256)) {
      reject("multiple_active_restraint_bindings",
             "a geometry may have only one unsuperseded restraint binding",
             "execution.restraint_bindings[" + std::to_string(index) + "]");
    }
    active_geometries.insert(binding.geometry_sha256);
    revisions.emplace(binding.binding_revision,
                      Revision{binding.geometry_sha256, false});
  }
}

void validate_binding_graph(const std::vector<PackageBinding> &bindings) {
  struct Revision final {
    std::string entity;
    bool superseded{false};
  };
  std::unordered_map<std::uint64_t, Revision> revisions;
  std::unordered_set<std::string> active_entities;
  revisions.reserve(bindings.size());
  active_entities.reserve(bindings.size());
  for (std::size_t index = 0U; index < bindings.size(); ++index) {
    const auto &binding = bindings[index];
    const auto expected = static_cast<std::uint64_t>(index) + 1U;
    if (binding.binding_revision != expected) {
      reject("binding_revision_order_invalid",
             "binding revisions must be unique, contiguous, and ordered",
             "execution.package_bindings[" + std::to_string(index) +
                 "].binding_revision");
    }
    if (binding.supersedes_binding_revision.has_value()) {
      const auto prior = revisions.find(*binding.supersedes_binding_revision);
      if (prior == revisions.end()) {
        reject("binding_supersession_invalid",
               "binding supersession must identify an earlier revision",
               "execution.package_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.entity != binding.cad_entity_id) {
        reject("binding_supersession_cross_entity",
               "a binding cannot supersede a different CAD entity",
               "execution.package_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      if (prior->second.superseded) {
        reject("binding_supersession_invalid",
               "a binding revision cannot be superseded twice",
               "execution.package_bindings[" + std::to_string(index) +
                   "].supersedes_binding_revision");
      }
      prior->second.superseded = true;
      active_entities.erase(binding.cad_entity_id);
    } else if (active_entities.contains(binding.cad_entity_id)) {
      reject("multiple_active_bindings",
             "a CAD entity may have only one unsuperseded package binding",
             "execution.package_bindings[" + std::to_string(index) + "]");
    }
    active_entities.insert(binding.cad_entity_id);
    revisions.emplace(binding.binding_revision,
                      Revision{binding.cad_entity_id, false});
  }
}

Event parse_event(const Json &value, const std::size_t index) {
  const auto field = "execution.events[" + std::to_string(index) + "]";
  require_exact_members(value,
                        {"sequence", "event_kind", "status", "related_hash",
                         "occurred_at_utc", "diagnostic_code"},
                        field);
  auto event_kind = require_string(value, "event_kind", field + ".event_kind",
                                   128U);
  auto status = require_string(value, "status", field + ".status", 128U);
  auto diagnostic_code = require_string(value, "diagnostic_code",
                                        field + ".diagnostic_code", 128U);
  if (!ascii_identifier(event_kind) || !ascii_identifier(status) ||
      !ascii_identifier(diagnostic_code)) {
    reject("invalid_event_identity",
           "event kind, status, and diagnostic code must be identifiers",
           field);
  }
  auto related = nullable_string(value, "related_hash", field + ".related_hash");
  if (related.has_value() && !is_valid_object_hash(*related)) {
    reject("invalid_hash", "event related hash is not lowercase SHA-256",
           field + ".related_hash");
  }
  const auto occurred = require_string(value, "occurred_at_utc",
                                       field + ".occurred_at_utc", 64U);
  if (occurred.size() < 20U || !occurred.ends_with('Z') ||
      occurred[4] != '-' || occurred[7] != '-' || occurred[10] != 'T' ||
      occurred[13] != ':' || occurred[16] != ':') {
    reject("invalid_event_time", "event time must be a bounded UTC timestamp",
           field + ".occurred_at_utc");
  }
  return {require_unsigned(value, "sequence", field + ".sequence"),
          std::move(event_kind), std::move(status), std::move(related), occurred,
          std::move(diagnostic_code)};
}

ExecutionIndex parse_execution(const Json &value) {
  require_exact_members(
      value,
      {"package_bindings", "current_scenario", "committed_runs", "events",
       "joint_bindings", "requirement_bindings", "material_bindings",
       "load_bindings", "restraint_bindings"},
      "execution");
  ExecutionIndex execution;
  const auto &bindings = require_array(value, "package_bindings",
                                       "execution.package_bindings",
                                       maximum_package_bindings);
  execution.package_bindings.reserve(bindings.size());
  for (std::size_t index = 0U; index < bindings.size(); ++index) {
    execution.package_bindings.push_back(
        parse_package_binding(bindings[index], index));
  }
  validate_binding_graph(execution.package_bindings);

  const auto &joint_bindings = require_array(value, "joint_bindings",
                                             "execution.joint_bindings",
                                             maximum_joint_bindings);
  execution.joint_bindings.reserve(joint_bindings.size());
  for (std::size_t index = 0U; index < joint_bindings.size(); ++index) {
    execution.joint_bindings.push_back(
        parse_joint_binding(joint_bindings[index], index));
  }
  validate_joint_binding_graph(execution.joint_bindings);

  const auto &requirement_bindings =
      require_array(value, "requirement_bindings",
                    "execution.requirement_bindings",
                    maximum_requirement_bindings);
  execution.requirement_bindings.reserve(requirement_bindings.size());
  for (std::size_t index = 0U; index < requirement_bindings.size(); ++index) {
    execution.requirement_bindings.push_back(
        parse_requirement_binding(requirement_bindings[index], index));
  }
  validate_requirement_binding_graph(execution.requirement_bindings);

  const auto &material_bindings =
      require_array(value, "material_bindings", "execution.material_bindings",
                    maximum_material_bindings);
  execution.material_bindings.reserve(material_bindings.size());
  for (std::size_t index = 0U; index < material_bindings.size(); ++index) {
    execution.material_bindings.push_back(
        parse_material_binding(material_bindings[index], index));
  }
  validate_material_binding_graph(execution.material_bindings);

  const auto &load_bindings = require_array(
      value, "load_bindings", "execution.load_bindings", maximum_load_bindings);
  execution.load_bindings.reserve(load_bindings.size());
  for (std::size_t index = 0U; index < load_bindings.size(); ++index) {
    execution.load_bindings.push_back(
        parse_load_binding(load_bindings[index], index));
  }
  validate_load_binding_graph(execution.load_bindings);

  const auto &restraint_bindings =
      require_array(value, "restraint_bindings", "execution.restraint_bindings",
                    maximum_restraint_bindings);
  execution.restraint_bindings.reserve(restraint_bindings.size());
  for (std::size_t index = 0U; index < restraint_bindings.size(); ++index) {
    execution.restraint_bindings.push_back(
        parse_restraint_binding(restraint_bindings[index], index));
  }
  validate_restraint_binding_graph(execution.restraint_bindings);

  if (!value.at("current_scenario").is_null()) {
    execution.current_scenario =
        parse_reference(value.at("current_scenario"),
                        "execution.current_scenario", ReferenceKind::scenario);
  }
  const auto &runs = require_array(value, "committed_runs",
                                   "execution.committed_runs",
                                   maximum_committed_runs);
  execution.committed_runs.reserve(runs.size());
  std::unordered_set<std::string> manifest_hashes;
  for (std::size_t index = 0U; index < runs.size(); ++index) {
    auto reference = parse_reference(
        runs[index], "execution.committed_runs[" + std::to_string(index) + "]",
        ReferenceKind::committed_manifest);
    if (!manifest_hashes.insert(reference.object_hash).second) {
      reject("duplicate_committed_run",
             "a manifest may be committed only once",
             "execution.committed_runs[" + std::to_string(index) + "]");
    }
    execution.committed_runs.push_back(std::move(reference));
  }

  const auto &events =
      require_array(value, "events", "execution.events", maximum_events);
  execution.events.reserve(events.size());
  for (std::size_t index = 0U; index < events.size(); ++index) {
    auto parsed = parse_event(events[index], index);
    if (parsed.sequence == 0U ||
        (index > 0U &&
         parsed.sequence != execution.events.back().sequence + 1U)) {
      reject("event_sequence_invalid",
             "retained events must have a positive contiguous sequence",
             "execution.events[" + std::to_string(index) + "].sequence");
    }
    execution.events.push_back(std::move(parsed));
  }
  return execution;
}

ProjectV2 parse_document(const Json &root) {
  require_exact_members(
      root,
      {"$schema", "schema_version", "execution_store_version", "name",
       "cad_source", "assembly_artifact_hash", "coordinate_system",
       "length_unit", "component_bindings", "placement_overrides",
       "connections", "interference_classifications", "engineering",
       "legacy_v1_engineering_state", "execution"},
      "project");
  if (require_string(root, "$schema", "$schema", maximum_identity_bytes) !=
      project_v2_schema_id) {
    reject("unsupported_schema", "project schema identity is unsupported",
           "$schema");
  }
  if (require_string(root, "schema_version", "schema_version", 32U) !=
      project_v2_schema_version) {
    reject("unsupported_schema_version",
           "project schema version is unsupported", "schema_version");
  }
  if (require_string(root, "execution_store_version", "execution_store_version",
                     32U) != execution_store_version) {
    reject("unsupported_store_version",
           "execution store version is unsupported", "execution_store_version");
  }

  ProjectV2 project;
  project.name =
      require_string(root, "name", "name", maximum_text_bytes);
  project.cad_source =
      require_string(root, "cad_source", "cad_source", maximum_text_bytes);
  project.assembly_artifact_hash = require_string(
      root, "assembly_artifact_hash", "assembly_artifact_hash", 71U);
  if (!is_valid_object_hash(project.assembly_artifact_hash)) {
    reject("invalid_hash", "assembly artifact hash is not lowercase SHA-256",
           "assembly_artifact_hash");
  }
  project.coordinate_system = require_string(
      root, "coordinate_system", "coordinate_system", maximum_identity_bytes);
  project.length_unit = require_string(root, "length_unit", "length_unit", 32U);
  if (project.length_unit != "m") {
    reject("unsupported_length_unit", "project length unit must be metres",
           "length_unit");
  }

  const auto &component_bindings = require_array(
      root, "component_bindings", "component_bindings", maximum_cad_records);
  project.component_bindings.reserve(component_bindings.size());
  for (std::size_t index = 0U; index < component_bindings.size(); ++index) {
    project.component_bindings.push_back(
        parse_component_binding(component_bindings[index], index));
  }
  const auto &placements = require_array(root, "placement_overrides",
                                         "placement_overrides",
                                         maximum_cad_records);
  project.placement_overrides.reserve(placements.size());
  for (std::size_t index = 0U; index < placements.size(); ++index) {
    project.placement_overrides.push_back(parse_placement(placements[index], index));
  }
  const auto &connections = require_array(root, "connections", "connections",
                                          maximum_cad_records);
  project.connections.reserve(connections.size());
  for (std::size_t index = 0U; index < connections.size(); ++index) {
    project.connections.push_back(parse_connection(connections[index], index));
  }
  const auto &classifications = require_array(
      root, "interference_classifications", "interference_classifications",
      maximum_cad_records);
  project.interference_classifications.reserve(classifications.size());
  for (std::size_t index = 0U; index < classifications.size(); ++index) {
    project.interference_classifications.push_back(
        parse_classification(classifications[index], index));
  }
  project.engineering = parse_engineering(root.at("engineering"));
  project.legacy_v1_engineering_state =
      parse_legacy(root.at("legacy_v1_engineering_state"));
  project.execution = parse_execution(root.at("execution"));
  return project;
}

Json reference_json(const StoredObjectReference &reference) {
  return Json{{"object_hash", reference.object_hash},
              {"byte_length", reference.byte_length},
              {"media_type", reference.media_type},
              {"schema_id", reference.schema_id},
              {"schema_version", reference.schema_version}};
}

Json project_json(const ProjectV2 &project) {
  Json component_bindings = Json::array();
  for (const auto &binding : project.component_bindings) {
    component_bindings.push_back(
        Json{{"cad_entity_id", binding.cad_entity_id},
             {"revision_id", binding.revision_id},
             {"label", binding.label}});
  }
  Json placements = Json::array();
  for (const auto &placement : project.placement_overrides) {
    placements.push_back(
        Json{{"cad_entity_id", placement.cad_entity_id},
             {"translation_x_m", placement.translation_x_m},
             {"translation_y_m", placement.translation_y_m},
             {"translation_z_m", placement.translation_z_m},
             {"rotation_x_deg", placement.rotation_x_deg},
             {"rotation_y_deg", placement.rotation_y_deg},
             {"rotation_z_deg", placement.rotation_z_deg},
             {"rotation_convention", placement.rotation_convention}});
  }
  Json connections = Json::array();
  for (const auto &connection : project.connections) {
    connections.push_back(
        Json{{"id", connection.id},
             {"source_part", connection.source_part},
             {"source_name", connection.source_name},
             {"source_anchor", connection.source_anchor},
             {"target_part", connection.target_part},
             {"target_name", connection.target_name},
             {"target_anchor", connection.target_anchor},
             {"connection_type", connection.connection_type},
             {"confirmed_by_user", connection.confirmed_by_user},
             {"anchor_origin", connection.anchor_origin},
             {"semantic_status", connection.semantic_status}});
  }
  Json classifications = Json::array();
  for (const auto &classification : project.interference_classifications) {
    classifications.push_back(
        Json{{"first_id", classification.first_id},
             {"second_id", classification.second_id},
             {"classification", classification.classification}});
  }

  Json joint = nullptr;
  if (project.engineering.joint.has_value()) {
    const auto &value = *project.engineering.joint;
    joint = Json{{"type", value.type},
                 {"source_index", value.source_index},
                 {"target_index", value.target_index},
                 {"axis", value.axis},
                 {"minimum_deg", value.minimum_deg},
                 {"maximum_deg", value.maximum_deg},
                 {"pivot_x", value.pivot_x},
                 {"pivot_y", value.pivot_y},
                 {"pivot_z", value.pivot_z},
                 {"confirmed_by_user", value.confirmed_by_user}};
  }
  Json geometry_findings = Json::array();
  for (const auto &finding : project.engineering.geometry_findings) {
    geometry_findings.push_back(
        Json{{"finding_kind", finding.finding_kind},
             {"status", finding.status},
             {"severity", finding.severity},
             {"title", finding.title},
             {"mechanism", finding.mechanism},
             {"calculated", finding.calculated},
             {"unit", finding.unit},
             {"available", finding.available},
             {"margin_fraction", finding.margin_fraction},
             {"evidence", finding.evidence},
             {"assumption", finding.assumption},
             {"estimated_range", finding.estimated_range},
             {"first_id", finding.first_id.has_value()
                              ? Json(*finding.first_id)
                              : Json(nullptr)},
             {"second_id", finding.second_id.has_value()
                               ? Json(*finding.second_id)
                               : Json(nullptr)}});
  }
  Json legacy = nullptr;
  if (project.legacy_v1_engineering_state.has_value()) {
    const auto canonical = integrity::canonicalize_json_bytes(
        *project.legacy_v1_engineering_state, project_limits());
    legacy = Json::parse(canonical);
  }

  Json package_bindings = Json::array();
  for (const auto &binding : project.execution.package_bindings) {
    package_bindings.push_back(
        Json{{"binding_revision", binding.binding_revision},
             {"supersedes_binding_revision",
              binding.supersedes_binding_revision.has_value()
                  ? Json(*binding.supersedes_binding_revision)
                  : Json(nullptr)},
             {"cad_entity_id", binding.cad_entity_id},
             {"package", reference_json(binding.package)}});
  }
  Json joint_bindings = Json::array();
  for (const auto &binding : project.execution.joint_bindings) {
    joint_bindings.push_back(
        Json{{"binding_revision", binding.binding_revision},
             {"supersedes_binding_revision",
              binding.supersedes_binding_revision.has_value()
                  ? Json(*binding.supersedes_binding_revision)
                  : Json(nullptr)},
             {"source_cad_entity_id", binding.source_cad_entity_id},
             {"target_cad_entity_id", binding.target_cad_entity_id},
             {"type", binding.type},
             {"axis", binding.axis},
             {"minimum_deg", binding.minimum_deg},
             {"maximum_deg", binding.maximum_deg},
             {"pivot_x", binding.pivot_x},
             {"pivot_y", binding.pivot_y},
             {"pivot_z", binding.pivot_z}});
  }
  Json requirement_bindings = Json::array();
  for (const auto &binding : project.execution.requirement_bindings) {
    requirement_bindings.push_back(
        Json{{"binding_revision", binding.binding_revision},
             {"supersedes_binding_revision",
              binding.supersedes_binding_revision.has_value()
                  ? Json(*binding.supersedes_binding_revision)
                  : Json(nullptr)},
             {"geometry_sha256", binding.geometry_sha256},
             {"analysis_id", binding.analysis_id},
             {"quantity", binding.quantity},
             {"other_quantity_description", binding.other_quantity_description},
             {"comparator", binding.comparator},
             {"limit_value", binding.limit_value},
             {"unit", binding.unit},
             {"applicability", binding.applicability},
             {"criticality", binding.criticality},
             {"source_or_exploratory_rationale",
              binding.source_or_exploratory_rationale}});
  }
  Json material_bindings = Json::array();
  for (const auto &binding : project.execution.material_bindings) {
    material_bindings.push_back(
        Json{{"binding_revision", binding.binding_revision},
             {"supersedes_binding_revision",
              binding.supersedes_binding_revision.has_value()
                  ? Json(*binding.supersedes_binding_revision)
                  : Json(nullptr)},
             {"geometry_sha256", binding.geometry_sha256},
             {"analysis_id", binding.analysis_id},
             {"designation", binding.designation},
             {"source_sha256", binding.source_sha256},
             {"applicability", binding.applicability},
             {"youngs_modulus_pa", binding.youngs_modulus_pa},
             {"poisson_ratio", binding.poisson_ratio},
             {"density_kg_m3", binding.density_kg_m3}});
  }
  const auto faces_json = [](const std::vector<std::array<int, 3>> &faces) {
    Json result = Json::array();
    for (const auto &face : faces) {
      result.push_back({face[0], face[1], face[2]});
    }
    return result;
  };
  Json load_bindings = Json::array();
  for (const auto &binding : project.execution.load_bindings) {
    load_bindings.push_back(
        Json{{"binding_revision", binding.binding_revision},
             {"supersedes_binding_revision",
              binding.supersedes_binding_revision.has_value()
                  ? Json(*binding.supersedes_binding_revision)
                  : Json(nullptr)},
             {"geometry_sha256", binding.geometry_sha256},
             {"analysis_id", binding.analysis_id},
             {"selection_label", binding.selection_label},
             {"face_node_ids", faces_json(binding.face_node_ids)},
             {"node_ids", binding.node_ids},
             {"area_m2", binding.area_m2},
             {"force_x_n", binding.force_x_n},
             {"force_y_n", binding.force_y_n},
             {"force_z_n", binding.force_z_n}});
  }
  Json restraint_bindings = Json::array();
  for (const auto &binding : project.execution.restraint_bindings) {
    restraint_bindings.push_back(
        Json{{"binding_revision", binding.binding_revision},
             {"supersedes_binding_revision",
              binding.supersedes_binding_revision.has_value()
                  ? Json(*binding.supersedes_binding_revision)
                  : Json(nullptr)},
             {"geometry_sha256", binding.geometry_sha256},
             {"analysis_id", binding.analysis_id},
             {"selection_label", binding.selection_label},
             {"face_node_ids", faces_json(binding.face_node_ids)},
             {"node_ids", binding.node_ids},
             {"area_m2", binding.area_m2}});
  }
  Json current_scenario = nullptr;
  if (project.execution.current_scenario.has_value()) {
    current_scenario = reference_json(*project.execution.current_scenario);
  }
  Json committed_runs = Json::array();
  for (const auto &run : project.execution.committed_runs) {
    committed_runs.push_back(reference_json(run));
  }
  Json events = Json::array();
  for (const auto &event : project.execution.events) {
    events.push_back(
        Json{{"sequence", event.sequence},
             {"event_kind", event.event_kind},
             {"status", event.status},
             {"related_hash", event.related_hash.has_value()
                                  ? Json(*event.related_hash)
                                  : Json(nullptr)},
             {"occurred_at_utc", event.occurred_at_utc},
             {"diagnostic_code", event.diagnostic_code}});
  }

  return Json{
      {"$schema", project_v2_schema_id},
      {"schema_version", project_v2_schema_version},
      {"execution_store_version", execution_store_version},
      {"name", project.name},
      {"cad_source", project.cad_source},
      {"assembly_artifact_hash", project.assembly_artifact_hash},
      {"coordinate_system", project.coordinate_system},
      {"length_unit", project.length_unit},
      {"component_bindings", std::move(component_bindings)},
      {"placement_overrides", std::move(placements)},
      {"connections", std::move(connections)},
      {"interference_classifications", std::move(classifications)},
      {"engineering", Json{{"joint", std::move(joint)},
                            {"geometry_findings", std::move(geometry_findings)},
                            {"geometry_status",
                             project.engineering.geometry_status}}},
      {"legacy_v1_engineering_state", std::move(legacy)},
      {"execution",
       Json{{"package_bindings", std::move(package_bindings)},
            {"current_scenario", std::move(current_scenario)},
            {"committed_runs", std::move(committed_runs)},
            {"events", std::move(events)},
            {"joint_bindings", std::move(joint_bindings)},
            {"requirement_bindings", std::move(requirement_bindings)},
            {"material_bindings", std::move(material_bindings)},
            {"load_bindings", std::move(load_bindings)},
            {"restraint_bindings", std::move(restraint_bindings)}}}};
}

} // namespace

bool is_valid_object_hash(const std::string_view value) noexcept {
  if (value.size() != 71U || !value.starts_with("sha256:")) {
    return false;
  }
  return std::all_of(value.begin() + 7, value.end(), [](const char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

bool is_supported_object_reference(
    const StoredObjectReference &reference) noexcept {
  return reference_matches(reference, ReferenceKind::package) ||
         reference_matches(reference, ReferenceKind::scenario) ||
         reference_matches(reference, ReferenceKind::request) ||
         reference_matches(reference, ReferenceKind::result) ||
         reference_matches(reference, ReferenceKind::manifest) ||
         reference_matches(reference, ReferenceKind::committed_manifest) ||
         (reference.media_type == structural_artifact_chunk_media_type &&
          reference.schema_id == structural_artifact_chunk_schema_id &&
          reference.schema_version == "1.0.0") ||
         (reference.media_type == project_evidence_chunk_media_type &&
          reference.schema_id == project_evidence_chunk_schema_id &&
          reference.schema_version == "1.0.0");
}

Result<ProjectV2> parse_project_v2(const std::string_view bytes) noexcept {
  try {
    const auto canonical =
        integrity::canonicalize_json_bytes(bytes, project_limits());
    return Result<ProjectV2>::success(parse_document(Json::parse(canonical)));
  } catch (const integrity::CanonicalJsonError &failure) {
    return Result<ProjectV2>::failure(
        diagnostic(failure.code(), failure.what()));
  } catch (const ProjectError &failure) {
    return Result<ProjectV2>::failure(
        diagnostic(failure.code(), failure.what(), failure.field()));
  } catch (const std::exception &failure) {
    return Result<ProjectV2>::failure(
        diagnostic("project_parse_failed", failure.what()));
  } catch (...) {
    return Result<ProjectV2>::failure(diagnostic(
        "project_parse_failed", "unknown project-v2 parsing failure"));
  }
}

Result<std::string>
serialize_project_v2(const ProjectV2 &project) noexcept {
  try {
    const auto document = project_json(project);
    auto bytes = document.dump(2, ' ', false, Json::error_handler_t::strict);
    bytes.push_back('\n');
    if (bytes.size() > maximum_project_bytes) {
      reject("max_raw_bytes_exceeded",
             "serialized project exceeds the 8 MiB project limit");
    }
    const auto validated = parse_project_v2(bytes);
    if (!validated.has_value()) {
      return Result<std::string>::failure(validated.diagnostic());
    }
    return Result<std::string>::success(std::move(bytes));
  } catch (const integrity::CanonicalJsonError &failure) {
    return Result<std::string>::failure(
        diagnostic(failure.code(), failure.what()));
  } catch (const ProjectError &failure) {
    return Result<std::string>::failure(
        diagnostic(failure.code(), failure.what(), failure.field()));
  } catch (const std::exception &failure) {
    return Result<std::string>::failure(
        diagnostic("project_serialize_failed", failure.what()));
  } catch (...) {
    return Result<std::string>::failure(diagnostic(
        "project_serialize_failed", "unknown project-v2 serialization failure"));
  }
}

} // namespace prometheus::run_store
