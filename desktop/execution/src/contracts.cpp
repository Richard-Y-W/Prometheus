#include <prometheus/execution/contracts.hpp>

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prometheus::execution {
namespace {

using Json = nlohmann::json;

constexpr std::string_view schema_version = "1.0.0";
constexpr std::string_view scenario_kind = "motor_arm";
constexpr std::string_view motion_profile =
    "symmetric_triangular_velocity";
constexpr std::string_view request_kind = "motor_arm_analysis";
constexpr std::size_t maximum_text_code_points = 4096U;
constexpr std::size_t maximum_diagnostic_bytes = 4096U;
constexpr std::size_t maximum_diagnostic_field_bytes = 512U;

struct ContractFailure final : std::exception {
  ContractFailure(std::string failure_code, std::string failure_message,
                  std::optional<std::string> failure_field = std::nullopt)
      : code(std::move(failure_code)), message(std::move(failure_message)),
        field(std::move(failure_field)) {}

  [[nodiscard]] const char *what() const noexcept override {
    return message.c_str();
  }

  std::string code;
  std::string message;
  std::optional<std::string> field;
};

[[noreturn]] void reject(std::string code, std::string message,
                         std::optional<std::string> field = std::nullopt) {
  throw ContractFailure(std::move(code), std::move(message), std::move(field));
}

std::string bounded_utf8(std::string value, const std::size_t maximum_bytes) {
  if (value.size() <= maximum_bytes) {
    return value;
  }
  auto boundary = maximum_bytes;
  while (boundary > 0U &&
         (static_cast<unsigned char>(value[boundary]) & 0xC0U) == 0x80U) {
    --boundary;
  }
  value.resize(boundary);
  return value;
}

std::string bounded_message(std::string message) {
  if (message.empty()) {
    return "contract validation failed";
  }
  return bounded_utf8(std::move(message), maximum_diagnostic_bytes);
}

Diagnostic make_diagnostic(
    const std::string_view stage, std::string code, std::string message,
    std::optional<std::string> field = std::nullopt,
    std::optional<std::string> object_hash = std::nullopt) {
  if (field.has_value()) {
    field = bounded_utf8(std::move(*field), maximum_diagnostic_field_bytes);
  }
  if (object_hash.has_value()) {
    object_hash = bounded_utf8(std::move(*object_hash), 128U);
  }
  return Diagnostic{bounded_utf8(std::string(stage), 128U),
                    bounded_utf8(std::move(code), 128U),
                    bounded_message(std::move(message)),
                    std::move(object_hash), std::move(field)};
}

template <typename T, typename Function>
Result<T> guarded(const std::string_view stage, Function function) {
  try {
    return Result<T>::success(function());
  } catch (const ContractFailure &failure) {
    return Result<T>::failure(make_diagnostic(
        stage, failure.code, failure.message, failure.field));
  } catch (const integrity::CanonicalJsonError &failure) {
    return Result<T>::failure(
        make_diagnostic(stage, failure.code(), failure.what()));
  } catch (const std::exception &failure) {
    return Result<T>::failure(
        make_diagnostic(stage, "internal_error", failure.what()));
  } catch (...) {
    return Result<T>::failure(make_diagnostic(
        stage, "internal_error", "unknown contract validation failure"));
  }
}

bool contains(const std::initializer_list<std::string_view> values,
              const std::string_view candidate) {
  return std::find(values.begin(), values.end(), candidate) != values.end();
}

void require_exact_keys(const Json &value,
                        const std::initializer_list<std::string_view> keys,
                        const std::string_view field_prefix = {}) {
  if (!value.is_object()) {
    reject("invalid_type", "value must be a JSON object",
           std::string(field_prefix));
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!contains(keys, iterator.key())) {
      const auto field = field_prefix.empty()
                             ? iterator.key()
                             : std::string(field_prefix) + "." + iterator.key();
      reject("unknown_field", "object contains an unknown field", field);
    }
  }
  for (const auto key : keys) {
    if (!value.contains(std::string(key))) {
      const auto field = field_prefix.empty()
                             ? std::string(key)
                             : std::string(field_prefix) + "." +
                                   std::string(key);
      reject("missing_field", "object is missing a required field", field);
    }
  }
}

const std::string &require_string(const Json &object, const std::string_view key,
                                  const std::string_view field = {}) {
  const auto &value = object.at(std::string(key));
  if (!value.is_string()) {
    reject("invalid_type", "field must be a string",
           field.empty() ? std::optional<std::string>(std::string(key))
                         : std::optional<std::string>(std::string(field)));
  }
  return value.get_ref<const std::string &>();
}

struct DecodedCodePoint final {
  std::uint32_t value;
  std::size_t next;
};

DecodedCodePoint decode_utf8(const std::string_view source,
                             const std::size_t index,
                             const std::string_view field) {
  const auto first = static_cast<unsigned char>(source[index]);
  if (first <= 0x7FU) {
    return {first, index + 1U};
  }

  std::size_t width = 0U;
  std::uint32_t code_point = 0U;
  if (first >= 0xC2U && first <= 0xDFU) {
    width = 2U;
    code_point = first & 0x1FU;
  } else if (first >= 0xE0U && first <= 0xEFU) {
    width = 3U;
    code_point = first & 0x0FU;
  } else if (first >= 0xF0U && first <= 0xF4U) {
    width = 4U;
    code_point = first & 0x07U;
  } else {
    reject("invalid_utf8", "text field is not valid UTF-8",
           std::string(field));
  }
  if (index + width > source.size()) {
    reject("invalid_utf8", "text field is not valid UTF-8",
           std::string(field));
  }
  for (std::size_t offset = 1U; offset < width; ++offset) {
    const auto continuation =
        static_cast<unsigned char>(source[index + offset]);
    if ((continuation & 0xC0U) != 0x80U) {
      reject("invalid_utf8", "text field is not valid UTF-8",
             std::string(field));
    }
    code_point = (code_point << 6U) | (continuation & 0x3FU);
  }
  if ((width == 3U && code_point < 0x0800U) ||
      (width == 3U && code_point >= 0xD800U && code_point <= 0xDFFFU) ||
      (width == 4U && code_point < 0x10000U) ||
      (width == 4U && code_point > 0x10FFFFU)) {
    reject("invalid_utf8", "text field is not valid UTF-8",
           std::string(field));
  }
  return {code_point, index + width};
}

bool is_unicode_whitespace(const std::uint32_t code_point) {
  return (code_point >= 0x0009U && code_point <= 0x000DU) ||
         (code_point >= 0x001CU && code_point <= 0x0020U) ||
         code_point == 0x0085U || code_point == 0x00A0U ||
         code_point == 0x1680U ||
         (code_point >= 0x2000U && code_point <= 0x200AU) ||
         code_point == 0x2028U || code_point == 0x2029U ||
         code_point == 0x202FU || code_point == 0x205FU ||
         code_point == 0x3000U;
}

struct NormalizedText final {
  std::string value;
  std::size_t code_points;
};

NormalizedText normalize_text(const std::string_view source,
                              const std::string_view field) {
  std::size_t index = 0U;
  std::size_t first_non_whitespace = std::string_view::npos;
  std::size_t last_non_whitespace_end = 0U;
  std::size_t code_points_since_first = 0U;
  std::size_t trimmed_code_points = 0U;
  while (index < source.size()) {
    const auto start = index;
    const auto decoded = decode_utf8(source, index, field);
    index = decoded.next;
    if (first_non_whitespace == std::string_view::npos) {
      if (is_unicode_whitespace(decoded.value)) {
        continue;
      }
      first_non_whitespace = start;
      code_points_since_first = 1U;
      trimmed_code_points = 1U;
      last_non_whitespace_end = index;
      continue;
    }
    ++code_points_since_first;
    if (!is_unicode_whitespace(decoded.value)) {
      trimmed_code_points = code_points_since_first;
      last_non_whitespace_end = index;
    }
  }
  if (first_non_whitespace == std::string_view::npos) {
    return {{}, 0U};
  }
  return {std::string(source.substr(
              first_non_whitespace,
              last_non_whitespace_end - first_non_whitespace)),
          trimmed_code_points};
}

std::string require_trimmed_text(const Json &object, const std::string_view key,
                                 const std::string_view empty_code) {
  const auto &value = require_string(object, key);
  const auto normalized = normalize_text(value, key);
  if (normalized.value.empty()) {
    reject(std::string(empty_code), "text field must not be empty",
           std::string(key));
  }
  if (normalized.code_points > maximum_text_code_points) {
    reject("text_too_long", "text field exceeds the character limit",
           std::string(key));
  }
  if (normalized.value != value) {
    reject("text_not_trimmed", "stored text must use trimmed spelling",
           std::string(key));
  }
  return normalized.value;
}

void require_finite(const double value, const std::string_view field) {
  if (!std::isfinite(value)) {
    reject("non_finite_number", "numeric field must be finite",
           std::string(field));
  }
  if (value == 0.0 && std::signbit(value)) {
    reject("negative_zero", "negative zero is not permitted",
           std::string(field));
  }
}

void require_positive(const double value, const std::string_view field) {
  require_finite(value, field);
  if (!(value > 0.0)) {
    reject("value_not_positive", "numeric field must be greater than zero",
           std::string(field));
  }
}

void require_nonnegative(const double value, const std::string_view field) {
  require_finite(value, field);
  if (value < 0.0) {
    reject("value_negative", "numeric field must be at least zero",
           std::string(field));
  }
}

void require_complete_cycle(const double move_duration_s,
                            const double hold_duration_s,
                            const double cycle_duration_s) {
  const auto occupied_duration = move_duration_s + hold_duration_s;
  if (!std::isfinite(occupied_duration)) {
    reject("non_finite_number", "move plus hold duration is not finite",
           "cycle_duration");
  }
  if (cycle_duration_s < occupied_duration) {
    reject("cycle_duration_too_short",
           "cycle duration must cover move duration plus hold duration",
           "cycle_duration");
  }
}

void validate_preview_values(const ScenarioPreview &preview) {
  require_positive(preview.payload_mass_kg, "payload_mass");
  require_positive(preview.arm_radius_m, "arm_radius");
  require_positive(preview.rotation_rad, "rotation");
  require_positive(preview.move_duration_s, "move_duration");
  require_nonnegative(preview.hold_duration_s, "hold_duration");
  require_positive(preview.cycle_duration_s, "cycle_duration");
  require_finite(preview.ambient_temperature_c, "ambient_temperature");
  require_complete_cycle(preview.move_duration_s, preview.hold_duration_s,
                         preview.cycle_duration_s);
  if (preview.motion_profile != motion_profile) {
    reject("unsupported_motion_profile",
           "motion profile is not supported by this contract",
           "motion_profile");
  }
}

double require_quantity(const Json &root, const std::string_view name,
                        const std::string_view unit, const bool positive,
                        const bool nonnegative) {
  const auto &quantity = root.at(std::string(name));
  require_exact_keys(quantity, {"value", "unit"}, name);
  const auto &unit_value = require_string(quantity, "unit", name);
  if (unit_value != unit) {
    reject("invalid_unit", "quantity uses an unsupported canonical unit",
           std::string(name));
  }
  const auto &numeric = quantity.at("value");
  if (!numeric.is_number()) {
    reject("invalid_type", "quantity value must be a JSON number",
           std::string(name));
  }
  const auto result = numeric.get<double>();
  if (positive) {
    require_positive(result, name);
  } else if (nonnegative) {
    require_nonnegative(result, name);
  } else {
    require_finite(result, name);
  }
  return result;
}

bool is_hash(const std::string_view value) {
  if (value.size() != 71U || !value.starts_with("sha256:")) {
    return false;
  }
  return std::all_of(value.begin() + 7, value.end(), [](const char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

void require_hash(const std::string_view value, const std::string_view field) {
  if (!is_hash(value)) {
    reject("invalid_hash", "hash must use sha256: plus 64 lowercase hex digits",
           std::string(field));
  }
}

void require_obligation_order(const std::vector<std::string> &actual) {
  if (actual.size() != motor_arm_obligation_ids.size()) {
    reject("obligation_order_mismatch",
           "request must contain exactly four ordered obligations",
           "obligation_ids");
  }
  for (std::size_t index = 0U; index < actual.size(); ++index) {
    if (actual[index] != motor_arm_obligation_ids[index]) {
      reject("obligation_order_mismatch",
             "request obligations do not match the contract order",
             "obligation_ids");
    }
  }
}

MotorArmScenario parse_scenario_verified(const std::string &canonical) {
  const auto root = Json::parse(canonical);
  require_exact_keys(
      root,
      {"$schema", "schema_version", "scenario_kind", "payload_mass",
       "arm_radius", "rotation", "move_duration", "hold_duration",
       "cycle_duration", "ambient_temperature", "motion_profile", "review"});

  if (require_string(root, "$schema") != motor_arm_scenario_schema_id) {
    reject("unsupported_schema", "scenario schema identity is not supported",
           "$schema");
  }
  if (require_string(root, "schema_version") != schema_version) {
    reject("unsupported_schema_version",
           "scenario schema version is not supported", "schema_version");
  }
  if (require_string(root, "scenario_kind") != scenario_kind) {
    reject("unsupported_scenario_kind", "scenario kind is not supported",
           "scenario_kind");
  }

  const auto payload_mass_kg =
      require_quantity(root, "payload_mass", "kg", true, false);
  const auto arm_radius_m =
      require_quantity(root, "arm_radius", "m", true, false);
  const auto rotation_rad =
      require_quantity(root, "rotation", "rad", true, false);
  const auto move_duration_s =
      require_quantity(root, "move_duration", "s", true, false);
  const auto hold_duration_s =
      require_quantity(root, "hold_duration", "s", false, true);
  const auto cycle_duration_s =
      require_quantity(root, "cycle_duration", "s", true, false);
  const auto ambient_temperature_c =
      require_quantity(root, "ambient_temperature", "degC", false, false);
  const auto parsed_motion_profile = require_string(root, "motion_profile");
  if (parsed_motion_profile != motion_profile) {
    reject("unsupported_motion_profile",
           "motion profile is not supported by this contract",
           "motion_profile");
  }
  require_complete_cycle(move_duration_s, hold_duration_s, cycle_duration_s);

  const auto &review = root.at("review");
  require_exact_keys(review, {"confirmed_by_user", "intent"}, "review");
  if (!review.at("confirmed_by_user").is_boolean()) {
    reject("invalid_type", "confirmation must be a boolean",
           "review.confirmed_by_user");
  }
  const auto confirmed_by_user = review.at("confirmed_by_user").get<bool>();
  if (!confirmed_by_user) {
    reject("scenario_not_confirmed", "scenario is not explicitly confirmed",
           "review.confirmed_by_user");
  }
  const auto intent =
      require_trimmed_text(review, "intent", "intent_empty");
  return MotorArmScenario{payload_mass_kg,
                          arm_radius_m,
                          rotation_rad,
                          move_duration_s,
                          hold_duration_s,
                          cycle_duration_s,
                          ambient_temperature_c,
                          parsed_motion_profile,
                          confirmed_by_user,
                          intent};
}

AnalysisRequest parse_request_verified(const std::string &canonical) {
  const auto root = Json::parse(canonical);
  require_exact_keys(
      root,
      {"$schema", "schema_version", "request_kind", "package_hash",
       "scenario_hash", "assembly_artifact_hash", "bound_cad_entity_id",
       "backend_id", "backend_contract_version",
       "package_consumer_contract_hash", "obligation_ids"});

  if (require_string(root, "$schema") != analysis_request_schema_id) {
    reject("unsupported_schema", "request schema identity is not supported",
           "$schema");
  }
  if (require_string(root, "schema_version") != schema_version) {
    reject("unsupported_schema_version",
           "request schema version is not supported", "schema_version");
  }
  if (require_string(root, "request_kind") != request_kind) {
    reject("unsupported_request_kind", "request kind is not supported",
           "request_kind");
  }

  const auto package_hash = require_string(root, "package_hash");
  const auto scenario_hash = require_string(root, "scenario_hash");
  const auto assembly_artifact_hash =
      require_string(root, "assembly_artifact_hash");
  const auto bound_cad_entity_id =
      require_trimmed_text(root, "bound_cad_entity_id", "bound_entity_empty");
  const auto backend_id = require_string(root, "backend_id");
  const auto backend_contract_version =
      require_string(root, "backend_contract_version");
  const auto package_consumer_contract_hash =
      require_string(root, "package_consumer_contract_hash");

  require_hash(package_hash, "package_hash");
  require_hash(scenario_hash, "scenario_hash");
  require_hash(assembly_artifact_hash, "assembly_artifact_hash");
  require_hash(package_consumer_contract_hash, "package_consumer_contract_hash");
  if (backend_id != motor_arm_backend_id) {
    reject("unsupported_backend", "request backend is not supported",
           "backend_id");
  }
  if (backend_contract_version != motor_arm_backend_contract_version) {
    reject("unsupported_backend_version",
           "request backend contract version is not supported",
           "backend_contract_version");
  }

  const auto &obligation_values = root.at("obligation_ids");
  if (!obligation_values.is_array()) {
    reject("invalid_type", "obligation_ids must be an array",
           "obligation_ids");
  }
  std::vector<std::string> parsed_obligations;
  parsed_obligations.reserve(obligation_values.size());
  for (const auto &value : obligation_values) {
    if (!value.is_string()) {
      reject("invalid_type", "each obligation ID must be a string",
             "obligation_ids");
    }
    parsed_obligations.push_back(value.get<std::string>());
  }
  require_obligation_order(parsed_obligations);
  std::array<std::string, 4> ordered_obligations;
  std::copy(parsed_obligations.begin(), parsed_obligations.end(),
            ordered_obligations.begin());
  return AnalysisRequest{package_hash,
                         scenario_hash,
                         assembly_artifact_hash,
                         bound_cad_entity_id,
                         backend_id,
                         backend_contract_version,
                         package_consumer_contract_hash,
                         std::move(ordered_obligations)};
}

CanonicalObject scenario_object(const ScenarioPreview &preview,
                                const std::string &intent) {
  const Json value = {
      {"$schema", motor_arm_scenario_schema_id},
      {"schema_version", schema_version},
      {"scenario_kind", scenario_kind},
      {"payload_mass", {{"value", preview.payload_mass_kg}, {"unit", "kg"}}},
      {"arm_radius", {{"value", preview.arm_radius_m}, {"unit", "m"}}},
      {"rotation", {{"value", preview.rotation_rad}, {"unit", "rad"}}},
      {"move_duration",
       {{"value", preview.move_duration_s}, {"unit", "s"}}},
      {"hold_duration",
       {{"value", preview.hold_duration_s}, {"unit", "s"}}},
      {"cycle_duration",
       {{"value", preview.cycle_duration_s}, {"unit", "s"}}},
      {"ambient_temperature",
       {{"value", preview.ambient_temperature_c}, {"unit", "degC"}}},
      {"motion_profile", preview.motion_profile},
      {"review", {{"confirmed_by_user", true}, {"intent", intent}}},
  };
  const auto bytes = integrity::canonicalize_json_bytes(value.dump());
  static_cast<void>(parse_scenario_verified(bytes));
  return CanonicalObject{bytes,
                         integrity::object_hash(bytes),
                         std::string(motor_arm_scenario_media_type),
                         std::string(motor_arm_scenario_schema_id),
                         std::string(schema_version)};
}

CanonicalObject request_object(const AnalysisRequestDraft &draft,
                               const std::string &bound_entity) {
  const Json value = {
      {"$schema", analysis_request_schema_id},
      {"schema_version", schema_version},
      {"request_kind", request_kind},
      {"package_hash", draft.package_hash},
      {"scenario_hash", draft.scenario_hash},
      {"assembly_artifact_hash", draft.assembly_artifact_hash},
      {"bound_cad_entity_id", bound_entity},
      {"backend_id", draft.backend_id},
      {"backend_contract_version", draft.backend_contract_version},
      {"package_consumer_contract_hash",
       draft.package_consumer_contract_hash},
      {"obligation_ids", draft.obligation_ids},
  };
  const auto bytes = integrity::canonicalize_json_bytes(value.dump());
  static_cast<void>(parse_request_verified(bytes));
  return CanonicalObject{bytes,
                         integrity::object_hash(bytes),
                         std::string(analysis_request_media_type),
                         std::string(analysis_request_schema_id),
                         std::string(schema_version)};
}

} // namespace

Result<ScenarioPreview>
preview_motor_arm_scenario(const ScenarioDraftDegrees &draft) {
  return guarded<ScenarioPreview>("scenario_preview", [&] {
    require_positive(draft.payload_mass_kg, "payload_mass");
    require_positive(draft.arm_radius_m, "arm_radius");
    require_positive(draft.rotation_degrees, "rotation");
    require_positive(draft.move_duration_s, "move_duration");
    require_nonnegative(draft.hold_duration_s, "hold_duration");
    require_positive(draft.cycle_duration_s, "cycle_duration");
    require_finite(draft.ambient_temperature_c, "ambient_temperature");
    require_complete_cycle(draft.move_duration_s, draft.hold_duration_s,
                           draft.cycle_duration_s);

    const auto rotation_rad =
        draft.rotation_degrees * std::numbers::pi_v<double> / 180.0;
    require_positive(rotation_rad, "rotation");
    return ScenarioPreview{draft.payload_mass_kg,
                           draft.arm_radius_m,
                           rotation_rad,
                           draft.move_duration_s,
                           draft.hold_duration_s,
                           draft.cycle_duration_s,
                           draft.ambient_temperature_c,
                           std::string(motion_profile),
                           false};
  });
}

Result<CanonicalObject>
confirm_motor_arm_scenario(const ScenarioPreview &preview,
                           const std::string_view intent) {
  return guarded<CanonicalObject>("scenario_confirmation", [&] {
    validate_preview_values(preview);
    if (!preview.confirmed_by_user) {
      reject("scenario_not_confirmed", "scenario is not explicitly confirmed",
             "confirmed_by_user");
    }
    const auto normalized_intent = normalize_text(intent, "intent");
    if (normalized_intent.value.empty()) {
      reject("intent_empty", "scenario intent must not be empty", "intent");
    }
    if (normalized_intent.code_points > maximum_text_code_points) {
      reject("text_too_long", "scenario intent exceeds the character limit",
             "intent");
    }
    return scenario_object(preview, normalized_intent.value);
  });
}

Result<MotorArmScenario>
parse_motor_arm_scenario(const std::string_view stored_bytes) {
  return guarded<MotorArmScenario>("scenario_contract", [&] {
    const auto canonical = integrity::verify_canonical_bytes(stored_bytes);
    return parse_scenario_verified(canonical);
  });
}

Result<CanonicalObject>
build_analysis_request(const AnalysisRequestDraft &draft) {
  return guarded<CanonicalObject>("request_contract", [&] {
    require_hash(draft.package_hash, "package_hash");
    require_hash(draft.scenario_hash, "scenario_hash");
    require_hash(draft.assembly_artifact_hash, "assembly_artifact_hash");
    require_hash(draft.package_consumer_contract_hash,
                 "package_consumer_contract_hash");
    if (draft.backend_id != motor_arm_backend_id) {
      reject("unsupported_backend", "request backend is not supported",
             "backend_id");
    }
    if (draft.backend_contract_version != motor_arm_backend_contract_version) {
      reject("unsupported_backend_version",
             "request backend contract version is not supported",
             "backend_contract_version");
    }
    require_obligation_order(draft.obligation_ids);
    const auto bound_entity =
        normalize_text(draft.bound_cad_entity_id, "bound_cad_entity_id");
    if (bound_entity.value.empty()) {
      reject("bound_entity_empty", "bound CAD entity ID must not be empty",
             "bound_cad_entity_id");
    }
    if (bound_entity.code_points > maximum_text_code_points) {
      reject("text_too_long",
             "bound CAD entity ID exceeds the character limit",
             "bound_cad_entity_id");
    }
    return request_object(draft, bound_entity.value);
  });
}

Result<AnalysisRequest>
parse_analysis_request(const std::string_view stored_bytes) {
  return guarded<AnalysisRequest>("request_contract", [&] {
    const auto canonical = integrity::verify_canonical_bytes(stored_bytes);
    return parse_request_verified(canonical);
  });
}

} // namespace prometheus::execution
