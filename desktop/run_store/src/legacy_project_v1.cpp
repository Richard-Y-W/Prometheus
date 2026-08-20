#include <prometheus/run_store/legacy_project_v1.hpp>

#include "platform_io.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace prometheus::run_store {
namespace {

using Json = nlohmann::json;

constexpr std::string_view placeholder_hash =
    "sha256:0000000000000000000000000000000000000000000000000000000000000000";
constexpr std::size_t maximum_text_bytes = 4096U;

class LegacyError final : public std::runtime_error {
public:
  LegacyError(std::string code, std::string message,
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
  throw LegacyError(std::move(code), std::move(message), std::move(field));
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
    *field = bounded(std::move(*field), 512U);
  }
  return Diagnostic{"legacy_project_v1", bounded(std::move(code), 128U),
                    bounded(std::move(message), maximum_text_bytes),
                    std::move(field), std::nullopt};
}

integrity::Limits legacy_limits() {
  return integrity::Limits{maximum_project_bytes, 64U, 500000U, 10000U, 10000U,
                           1024U * 1024U};
}

bool contains(const std::initializer_list<std::string_view> allowed,
              const std::string_view candidate) {
  return std::find(allowed.begin(), allowed.end(), candidate) != allowed.end();
}

void require_exact_members(
    const Json &value, const std::initializer_list<std::string_view> members,
    const std::string_view field) {
  if (!value.is_object()) {
    reject("invalid_type", "legacy project member must be an object",
           std::string(field));
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!contains(members, iterator.key())) {
      reject("unknown_field", "legacy project object has an unknown field",
             std::string(field) + "." + iterator.key());
    }
  }
  for (const auto member : members) {
    if (!value.contains(std::string(member))) {
      reject("missing_field", "legacy project object is missing a field",
             std::string(field) + "." + std::string(member));
    }
  }
}

void require_optional_members(
    const Json &value, const std::initializer_list<std::string_view> members,
    const std::string_view field) {
  if (!value.is_object()) {
    reject("invalid_type", "legacy engineering member must be an object",
           std::string(field));
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!contains(members, iterator.key())) {
      reject("unknown_field", "legacy engineering object has an unknown field",
             std::string(field) + "." + iterator.key());
    }
  }
}

void validate_string(const Json &object, const std::string_view member,
                     const std::string_view field,
                     const bool allow_empty = false) {
  const auto &value = object.at(std::string(member));
  if (!value.is_string()) {
    reject("invalid_type", "legacy member must be a string",
           std::string(field));
  }
  const auto &text = value.get_ref<const std::string &>();
  if ((!allow_empty && text.empty()) || text.size() > maximum_text_bytes) {
    reject("invalid_string", "legacy string violates its byte bounds",
           std::string(field));
  }
}

void validate_number(const Json &object, const std::string_view member,
                     const std::string_view field) {
  const auto &value = object.at(std::string(member));
  if (!value.is_number()) {
    reject("invalid_type", "legacy member must be a number",
           std::string(field));
  }
  const auto number = value.get<double>();
  if (!std::isfinite(number) || (number == 0.0 && std::signbit(number))) {
    reject("invalid_number",
           "legacy number must be finite and use positive zero",
           std::string(field));
  }
}

void validate_scenario(const Json &value) {
  require_optional_members(value,
                           {"type", "payload_kg", "arm_m", "rotation_deg",
                            "move_s", "hold_s", "cycle_s", "ambient_c",
                            "motion_profile", "gearbox_efficiency_nominal",
                            "gearbox_efficiency_range"},
                           "engineering.scenario");
  for (const auto member :
       {"type", "motion_profile", "gearbox_efficiency_range"}) {
    if (value.contains(member)) {
      validate_string(value, member,
                      std::string("engineering.scenario.") + member);
    }
  }
  for (const auto member :
       {"payload_kg", "arm_m", "rotation_deg", "move_s", "hold_s", "cycle_s",
        "ambient_c", "gearbox_efficiency_nominal"}) {
    if (value.contains(member)) {
      validate_number(value, member,
                      std::string("engineering.scenario.") + member);
    }
  }
}

void validate_finding(const Json &value, const std::size_t index) {
  const auto field = "engineering.findings[" + std::to_string(index) + "]";
  if (!value.is_object()) {
    reject("invalid_type", "legacy finding must be an object", field);
  }
  const std::initializer_list<std::string_view> allowed{
      "status",     "severity",   "title",           "mechanism",
      "calculated", "unit",       "available",       "margin_fraction",
      "evidence",   "assumption", "estimated_range", "first_id",
      "second_id"};
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!contains(allowed, iterator.key())) {
      reject("unknown_field", "legacy finding has an unknown field",
             field + "." + iterator.key());
    }
  }
  for (const auto member : {"status", "severity", "title", "mechanism", "unit",
                            "evidence", "assumption", "estimated_range"}) {
    if (!value.contains(member)) {
      reject("missing_field", "legacy finding is missing a field",
             field + "." + member);
    }
    validate_string(value, member, field + "." + member,
                    std::string_view(member) == "assumption" ||
                        std::string_view(member) == "estimated_range");
  }
  for (const auto member : {"calculated", "available", "margin_fraction"}) {
    if (!value.contains(member)) {
      reject("missing_field", "legacy finding is missing a field",
             field + "." + member);
    }
    validate_number(value, member, field + "." + member);
  }
  for (const auto member : {"first_id", "second_id"}) {
    if (value.contains(member)) {
      validate_string(value, member, field + "." + member);
    }
  }
}

std::optional<std::string> geometry_kind(const Json &finding) {
  const auto &unit = finding.at("unit").get_ref<const std::string &>();
  const auto &title = finding.at("title").get_ref<const std::string &>();
  const bool has_entity_pair =
      finding.contains("first_id") && finding.contains("second_id");
  if (unit == "m³" && has_entity_pair &&
      contains({"Intentional solid engagement",
                "Prohibited static part interference",
                "Unclassified static part interference"},
               title)) {
    return "static_interference";
  }
  if (unit == "m³" && has_entity_pair &&
      title == "Collision in sampled joint range") {
    return "sampled_joint_sweep";
  }
  if (unit == "samples" &&
      title == "No collision found at sampled joint positions") {
    return "sampled_joint_sweep";
  }
  return std::nullopt;
}

Json convert_document(const Json &legacy) {
  require_exact_members(
      legacy,
      {"schema_version", "name", "cad_source", "coordinate_system",
       "length_unit", "component_bindings", "placement_overrides",
       "connections", "interference_classifications", "engineering"},
      "project");
  validate_string(legacy, "schema_version", "schema_version");
  if (legacy.at("schema_version") != "1.0.0") {
    reject("unsupported_schema_version",
           "legacy project schema version is unsupported", "schema_version");
  }
  const auto &engineering = legacy.at("engineering");
  require_optional_members(engineering,
                           {"joint", "scenario", "findings", "run_status"},
                           "engineering");
  if (engineering.contains("scenario")) {
    validate_scenario(engineering.at("scenario"));
  }
  if (engineering.contains("run_status")) {
    validate_string(engineering, "run_status", "engineering.run_status");
  }

  Json joint = nullptr;
  if (engineering.contains("joint")) {
    if (!engineering.at("joint").is_object()) {
      reject("invalid_type", "legacy joint must be an object",
             "engineering.joint");
    }
    if (!engineering.at("joint").empty()) {
      joint = engineering.at("joint");
    }
  }

  Json geometry_findings = Json::array();
  if (engineering.contains("findings")) {
    const auto &findings = engineering.at("findings");
    if (!findings.is_array() || findings.size() > 256U) {
      reject("invalid_legacy_state",
             "legacy findings must be an array of at most 256 entries",
             "engineering.findings");
    }
    for (std::size_t index = 0U; index < findings.size(); ++index) {
      const auto &finding = findings[index];
      validate_finding(finding, index);
      const auto kind = geometry_kind(finding);
      if (!kind.has_value()) {
        continue;
      }
      geometry_findings.push_back(Json{
          {"finding_kind", *kind},
          {"status", finding.at("status")},
          {"severity", finding.at("severity")},
          {"title", finding.at("title")},
          {"mechanism", finding.at("mechanism")},
          {"calculated", finding.at("calculated")},
          {"unit", finding.at("unit")},
          {"available", finding.at("available")},
          {"margin_fraction", finding.at("margin_fraction")},
          {"evidence", finding.at("evidence")},
          {"assumption", finding.at("assumption")},
          {"estimated_range", finding.at("estimated_range")},
          {"first_id", finding.contains("first_id") ? finding.at("first_id")
                                                    : Json(nullptr)},
          {"second_id", finding.contains("second_id") ? finding.at("second_id")
                                                      : Json(nullptr)}});
    }
  }

  const auto geometry_evaluated = !geometry_findings.empty();
  const auto legacy_state = engineering.empty() ? Json(nullptr) : engineering;
  return Json{{"$schema", project_v2_schema_id},
              {"schema_version", project_v2_schema_version},
              {"execution_store_version", execution_store_version},
              {"name", legacy.at("name")},
              {"cad_source", legacy.at("cad_source")},
              {"assembly_artifact_hash", placeholder_hash},
              {"coordinate_system", legacy.at("coordinate_system")},
              {"length_unit", legacy.at("length_unit")},
              {"component_bindings", legacy.at("component_bindings")},
              {"placement_overrides", legacy.at("placement_overrides")},
              {"connections", legacy.at("connections")},
              {"interference_classifications",
               legacy.at("interference_classifications")},
              {"engineering",
               Json{{"joint", std::move(joint)},
                    {"geometry_findings", std::move(geometry_findings)},
                    {"geometry_status",
                     geometry_evaluated ? "completed" : "not_evaluated"}}},
              {"legacy_v1_engineering_state", legacy_state},
              {"execution", Json{{"package_bindings", Json::array()},
                                 {"current_scenario", nullptr},
                                 {"committed_runs", Json::array()},
                                 {"events", Json::array()},
                                 {"joint_bindings", Json::array()}}}};
}

} // namespace

Result<LegacyProjectV1>
parse_legacy_project_v1(const std::string_view bytes) noexcept {
  try {
    const auto canonical =
        integrity::canonicalize_json_bytes(bytes, legacy_limits());
    const auto converted = convert_document(Json::parse(canonical));
    const auto parsed = parse_project_v2(converted.dump());
    if (!parsed.has_value()) {
      return Result<LegacyProjectV1>::failure(
          diagnostic(parsed.diagnostic().code, parsed.diagnostic().message,
                     parsed.diagnostic().field));
    }
    return Result<LegacyProjectV1>::success(LegacyProjectV1{parsed.value()});
  } catch (const integrity::CanonicalJsonError &failure) {
    return Result<LegacyProjectV1>::failure(
        diagnostic(failure.code(), failure.what()));
  } catch (const LegacyError &failure) {
    return Result<LegacyProjectV1>::failure(
        diagnostic(failure.code(), failure.what(), failure.field()));
  } catch (const std::exception &failure) {
    return Result<LegacyProjectV1>::failure(
        diagnostic("legacy_project_parse_failed", failure.what()));
  } catch (...) {
    return Result<LegacyProjectV1>::failure(
        diagnostic("legacy_project_parse_failed",
                   "unknown legacy project parsing failure"));
  }
}

Result<LegacyProjectV1>
open_legacy_project_v1(const std::filesystem::path &project_path) noexcept {
  const auto bytes = detail::read_project_index_file(project_path);
  if (!bytes.has_value()) {
    return Result<LegacyProjectV1>::failure(bytes.diagnostic());
  }
  auto parsed = parse_legacy_project_v1(bytes.value());
  if (!parsed.has_value()) {
    auto value = parsed.diagnostic();
    value.path = project_path.generic_string();
    return Result<LegacyProjectV1>::failure(std::move(value));
  }
  return parsed;
}

} // namespace prometheus::run_store
