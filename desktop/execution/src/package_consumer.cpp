#include <prometheus/execution/package_consumer.hpp>

#include <prometheus/execution/supported_consumer_contract.hpp>
#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace prometheus::execution {
namespace {

using Json = nlohmann::json;

constexpr std::string_view supported_capability =
    "component_input.dc_gearmotor_v1";

enum class SlotUse { calculation, validation, available_but_unused };

struct SlotSpec final {
  std::string_view name;
  std::string_view quantity;
  std::string_view dimension;
  std::string_view value_shape;
  std::string_view unit;
  SlotUse use;
  bool required_for_execution;
};

constexpr std::array<SlotSpec, 17> slot_specs{{
    {"gear_ratio", "ratio", "dimensionless", "scalar", "1",
     SlotUse::calculation, true},
    {"gearbox_efficiency_nominal", "efficiency", "dimensionless", "scalar",
     "1", SlotUse::calculation, true},
    {"continuous_torque_nm", "torque", "torque", "scalar", "N*m",
     SlotUse::calculation, true},
    {"stall_torque_nm", "torque", "torque", "scalar", "N*m",
     SlotUse::calculation, true},
    {"no_load_speed_rad_s", "angular_velocity", "angle/time", "scalar",
     "rad/s", SlotUse::calculation, true},
    {"no_load_current_a", "electric_current", "electric_current", "scalar",
     "A", SlotUse::calculation, true},
    {"torque_constant_nm_a", "torque_constant", "torque/electric_current",
     "scalar", "N*m/A", SlotUse::calculation, true},
    {"driver_current_limit_a", "electric_current_limit", "electric_current",
     "scalar", "A", SlotUse::calculation, true},
    {"winding_resistance_ohm", "electrical_resistance",
     "electric_resistance", "scalar", "ohm", SlotUse::calculation, true},
    {"thermal_resistance_k_w", "thermal_resistance", "temperature/power",
     "scalar", "K/W", SlotUse::calculation, true},
    {"thermal_capacitance_j_k", "heat_capacity", "energy/temperature",
     "scalar", "J/K", SlotUse::calculation, true},
    {"maximum_temperature_c", "temperature_limit", "temperature", "scalar",
     "degC", SlotUse::calculation, true},
    {"gearbox_efficiency_range", "efficiency", "dimensionless", "range", "1",
     SlotUse::validation, true},
    {"torque_speed_curve", "torque_by_angular_velocity", "torque", "curve",
     "N*m", SlotUse::validation, true},
    {"nominal_voltage_v", "voltage", "electric_potential", "scalar", "V",
     SlotUse::available_but_unused, false},
    {"supply_current_limit_a", "electric_current_limit", "electric_current",
     "scalar", "A", SlotUse::available_but_unused, false},
    {"gearbox_lifetime", "service_life", "time", "scalar_or_unknown", "h",
     SlotUse::available_but_unused, false},
}};

struct PackageFailure final : std::exception {
  PackageFailure(std::string failure_stage, std::string failure_code,
                 std::string failure_message,
                 std::optional<std::string> failure_field = std::nullopt)
      : stage(std::move(failure_stage)), code(std::move(failure_code)),
        message(std::move(failure_message)), field(std::move(failure_field)) {}

  [[nodiscard]] const char *what() const noexcept override {
    return message.c_str();
  }

  std::string stage;
  std::string code;
  std::string message;
  std::optional<std::string> field;
};

[[noreturn]] void reject(std::string stage, std::string code,
                         std::string message,
                         std::optional<std::string> field = std::nullopt) {
  throw PackageFailure(std::move(stage), std::move(code), std::move(message),
                       std::move(field));
}

std::string bounded(std::string value, const std::size_t maximum) {
  if (value.size() > maximum) {
    value.resize(maximum);
  }
  return value;
}

Diagnostic diagnostic(std::string stage, std::string code, std::string message,
                      std::optional<std::string> field = std::nullopt) {
  if (field.has_value()) {
    field = bounded(std::move(*field), 512U);
  }
  if (message.empty()) {
    message = "package validation failed";
  }
  return Diagnostic{bounded(std::move(stage), 128U),
                    bounded(std::move(code), 128U),
                    bounded(std::move(message), 4096U), std::nullopt,
                    std::move(field)};
}

template <typename T, typename Function> Result<T> guarded(Function function) {
  try {
    return Result<T>::success(function());
  } catch (const PackageFailure &failure) {
    return Result<T>::failure(diagnostic(failure.stage, failure.code,
                                         failure.message, failure.field));
  } catch (const integrity::CanonicalJsonError &failure) {
    return Result<T>::failure(diagnostic("package_integrity", failure.code(),
                                         failure.what()));
  } catch (const std::exception &failure) {
    return Result<T>::failure(
        diagnostic("package_consumer", "internal_error", failure.what()));
  } catch (...) {
    return Result<T>::failure(diagnostic(
        "package_consumer", "internal_error", "unknown package failure"));
  }
}

const std::string &string_member(const Json &object,
                                 const std::string_view key) {
  const auto &value = object.at(std::string(key));
  if (!value.is_string()) {
    reject("package_consumer", "invalid_type", "member must be a string",
           std::string(key));
  }
  return value.get_ref<const std::string &>();
}

PackageInspection inspect_parsed(const Json &root,
                                 const std::string &package_hash) {
  const auto &component = root.at("component");
  std::vector<std::string> limitations;
  limitations.reserve(root.at("limitations").size());
  for (const auto &limitation : root.at("limitations")) {
    limitations.push_back(string_member(limitation, "statement"));
  }
  std::optional<std::string> blocked_reason;
  for (const auto &gate : root.at("gates")) {
    if (string_member(gate, "phase") != "execution" ||
        string_member(gate, "state") == "satisfied" ||
        gate.at("reason").is_null()) {
      continue;
    }
    blocked_reason = string_member(gate, "reason");
    break;
  }
  return PackageInspection{
      package_hash,
      string_member(root, "revision_id"),
      string_member(component, "component_id"),
      string_member(component, "manufacturer"),
      string_member(component, "part_number"),
      string_member(component, "revision"),
      string_member(component, "component_class"),
      string_member(root, "package_kind"),
      string_member(root, "capability_id"),
      string_member(root, "execution_readiness"),
      std::move(limitations),
      std::move(blocked_reason),
  };
}

struct ParsedPackage final {
  Json root;
  PackageInspection inspection;
};

ParsedPackage verify_and_parse(const std::string_view stored_bytes,
                               const std::string_view expected_object_hash) {
  const auto actual_hash = integrity::verify_execution_component(
      stored_bytes, expected_object_hash);
  auto root = Json::parse(stored_bytes);
  auto inspection = inspect_parsed(root, actual_hash);
  return {std::move(root), std::move(inspection)};
}

void require_supported_consumer(const Json &root) {
  const Json *matching_hash = nullptr;
  for (const auto &artifact : root.at("artifacts")) {
    if (string_member(artifact, "artifact_hash") ==
        detail::supported_consumer_contract_hash) {
      matching_hash = &artifact;
      break;
    }
  }
  if (matching_hash == nullptr) {
    reject("package_consumer", "consumer_artifact_hash_mismatch",
           "package does not reference the compiled consumer contract");
  }
  if (string_member(*matching_hash, "artifact_role") != "supporting_input") {
    reject("package_consumer", "consumer_artifact_role_mismatch",
           "consumer contract artifact has the wrong role");
  }
  if (string_member(*matching_hash, "media_type") !=
      detail::supported_consumer_contract_media_type) {
    reject("package_consumer", "consumer_artifact_media_type_mismatch",
           "consumer contract artifact has the wrong media type");
  }
  const auto &byte_length_value = matching_hash->at("byte_length");
  if (!byte_length_value.is_number_unsigned() &&
      !byte_length_value.is_number_integer()) {
    reject("package_consumer", "consumer_artifact_length_mismatch",
           "consumer contract byte length is invalid");
  }
  const auto byte_length = byte_length_value.get<std::uint64_t>();
  if (byte_length != detail::supported_consumer_contract_byte_length) {
    reject("package_consumer", "consumer_artifact_length_mismatch",
           "consumer contract artifact has the wrong byte length");
  }

  const Json *consumer_gate = nullptr;
  for (const auto &gate : root.at("gates")) {
    if (string_member(gate, "required_review_type") == "package_consumer") {
      if (consumer_gate != nullptr) {
        reject("package_consumer", "duplicate_consumer_gate",
               "package contains multiple consumer gates");
      }
      consumer_gate = &gate;
    }
  }
  if (consumer_gate == nullptr) {
    reject("package_consumer", "consumer_gate_missing",
           "package has no consumer execution gate");
  }
  if (string_member(*consumer_gate, "phase") != "execution" ||
      string_member(*consumer_gate, "state") != "satisfied") {
    reject("package_consumer", "consumer_gate_unsatisfied",
           "consumer execution gate is not satisfied");
  }
  const auto &references = consumer_gate->at("satisfying_reference_ids");
  if (!references.is_array() || references.size() != 1U ||
      !references[0].is_string() ||
      references[0].get_ref<const std::string &>() !=
          detail::supported_consumer_contract_hash) {
    reject("package_consumer", "consumer_gate_reference_mismatch",
           "consumer gate does not bind the compiled consumer contract");
  }
}

const SlotSpec *find_spec(const std::string_view name) {
  const auto spec = std::find_if(slot_specs.begin(), slot_specs.end(),
                                 [&](const SlotSpec &candidate) {
                                   return candidate.name == name;
                                 });
  return spec == slot_specs.end() ? nullptr : &*spec;
}

struct SlotClaim final {
  const Json *slot;
  const Json *claim;
};

std::unordered_map<std::string, SlotClaim> index_slot_claims(const Json &root) {
  std::unordered_map<std::string, const Json *> claims;
  for (const auto &claim : root.at("claims")) {
    claims.emplace(string_member(claim, "claim_id"), &claim);
  }
  std::unordered_map<std::string, SlotClaim> result;
  for (const auto &slot : root.at("parameter_slots")) {
    const auto &name = string_member(slot, "name");
    const auto claim = claims.find(string_member(slot, "selected_claim_id"));
    if (claim == claims.end()) {
      reject("package_consumer", "selected_claim_missing",
             "parameter slot selects an absent claim", name);
    }
    result.emplace(name, SlotClaim{&slot, claim->second});
  }
  return result;
}

void validate_slot_metadata(const SlotClaim &value, const SlotSpec &spec) {
  const auto &slot = *value.slot;
  if (string_member(slot, "quantity") != spec.quantity ||
      string_member(slot, "dimension") != spec.dimension) {
    reject("package_consumer", "slot_contract_mismatch",
           "parameter slot quantity or dimension is unsupported",
           std::string(spec.name));
  }
  if (!slot.at("required_for_execution").is_boolean() ||
      slot.at("required_for_execution").get<bool>() !=
          spec.required_for_execution) {
    reject("package_consumer", "slot_contract_mismatch",
           "parameter slot required flag is unsupported",
           std::string(spec.name));
  }
}

ClaimBinding binding(const SlotClaim &value) {
  const auto &claim = *value.claim;
  const auto value_known = string_member(claim, "value_state") == "known";
  std::optional<std::string> reason;
  if (!value_known) {
    reason = string_member(claim.at("value"), "reason");
  }
  return ClaimBinding{string_member(*value.slot, "name"),
                      string_member(*value.slot, "slot_id"),
                      string_member(claim, "claim_id"),
                      string_member(claim, "claim_fingerprint"), value_known,
                      std::move(reason)};
}

void require_known(const SlotClaim &value, const SlotSpec &spec) {
  if (string_member(*value.claim, "value_state") != "known") {
    reject("package_consumer", "required_value_unknown",
           "required package value is unknown", std::string(spec.name));
  }
}

void require_unit(const SlotClaim &value, const SlotSpec &spec) {
  if (string_member(*value.claim, "unit") != spec.unit) {
    reject("package_consumer", "invalid_unit",
           "claim does not use the consumer's canonical unit",
           std::string(spec.name));
  }
}

double scalar_value(const SlotClaim &value, const SlotSpec &spec) {
  validate_slot_metadata(value, spec);
  require_known(value, spec);
  require_unit(value, spec);
  const auto &engineering_value = value.claim->at("value");
  if (string_member(engineering_value, "kind") != "scalar" ||
      !engineering_value.at("value").is_number()) {
    reject("package_consumer", "invalid_value_shape",
           "claim must contain a scalar value", std::string(spec.name));
  }
  const auto result = engineering_value.at("value").get<double>();
  if (!std::isfinite(result)) {
    reject("package_consumer", "non_finite_number",
           "claim scalar is not finite", std::string(spec.name));
  }
  return result;
}

const SlotClaim &required_slot(
    const std::unordered_map<std::string, SlotClaim> &slots,
    const std::string_view name) {
  const auto value = slots.find(std::string(name));
  if (value == slots.end()) {
    reject("package_consumer", "missing_required_slot",
           "consumer-required package slot is absent", std::string(name));
  }
  return value->second;
}

MotorComponentInput map_motor_component(ParsedPackage package) {
  if (package.inspection.execution_readiness != "ready") {
    reject("package_consumer", "package_not_ready",
           "execution-component package is not ready");
  }
  if (package.inspection.capability_id != supported_capability) {
    reject("package_consumer", "unsupported_capability",
           "execution-component capability is unsupported");
  }
  require_supported_consumer(package.root);
  const auto slots = index_slot_claims(package.root);

  for (const auto &[name, value] : slots) {
    const auto spec = find_spec(name);
    if (spec == nullptr) {
      if (value.slot->at("required_for_execution").get<bool>()) {
        reject("package_consumer", "unexpected_required_slot",
               "package contains an undeclared required slot", name);
      }
      continue;
    }
    validate_slot_metadata(value, *spec);
  }
  for (const auto &spec : slot_specs) {
    if (!slots.contains(std::string(spec.name))) {
      reject("package_consumer", "missing_required_slot",
             "consumer contract slot is absent", std::string(spec.name));
    }
  }

  const auto &gear_ratio = required_slot(slots, "gear_ratio");
  const auto &nominal_efficiency =
      required_slot(slots, "gearbox_efficiency_nominal");
  const auto &continuous_torque =
      required_slot(slots, "continuous_torque_nm");
  const auto &stall_torque = required_slot(slots, "stall_torque_nm");
  const auto &no_load_speed = required_slot(slots, "no_load_speed_rad_s");
  const auto &no_load_current = required_slot(slots, "no_load_current_a");
  const auto &torque_constant = required_slot(slots, "torque_constant_nm_a");
  const auto &driver_limit = required_slot(slots, "driver_current_limit_a");
  const auto &winding_resistance =
      required_slot(slots, "winding_resistance_ohm");
  const auto &thermal_resistance =
      required_slot(slots, "thermal_resistance_k_w");
  const auto &thermal_capacitance =
      required_slot(slots, "thermal_capacitance_j_k");
  const auto &maximum_temperature =
      required_slot(slots, "maximum_temperature_c");

  const auto gear_ratio_value =
      scalar_value(gear_ratio, *find_spec("gear_ratio"));
  const auto nominal_efficiency_value =
      scalar_value(nominal_efficiency,
                   *find_spec("gearbox_efficiency_nominal"));
  const auto continuous_torque_value =
      scalar_value(continuous_torque, *find_spec("continuous_torque_nm"));
  const auto stall_torque_value =
      scalar_value(stall_torque, *find_spec("stall_torque_nm"));
  const auto no_load_speed_value =
      scalar_value(no_load_speed, *find_spec("no_load_speed_rad_s"));
  const auto no_load_current_value =
      scalar_value(no_load_current, *find_spec("no_load_current_a"));
  const auto torque_constant_value =
      scalar_value(torque_constant, *find_spec("torque_constant_nm_a"));
  const auto driver_limit_value =
      scalar_value(driver_limit, *find_spec("driver_current_limit_a"));
  const auto winding_resistance_value =
      scalar_value(winding_resistance, *find_spec("winding_resistance_ohm"));
  const auto thermal_resistance_value =
      scalar_value(thermal_resistance, *find_spec("thermal_resistance_k_w"));
  const auto thermal_capacitance_value =
      scalar_value(thermal_capacitance,
                   *find_spec("thermal_capacitance_j_k"));
  const auto maximum_temperature_value =
      scalar_value(maximum_temperature, *find_spec("maximum_temperature_c"));

  if (!(gear_ratio_value > 0.0) || !(nominal_efficiency_value > 0.0) ||
      nominal_efficiency_value > 1.0 || !(continuous_torque_value > 0.0) ||
      !(stall_torque_value > 0.0) || !(no_load_speed_value > 0.0) ||
      no_load_current_value < 0.0 || !(torque_constant_value > 0.0) ||
      !(driver_limit_value > 0.0) || !(winding_resistance_value > 0.0) ||
      !(thermal_resistance_value > 0.0) ||
      !(thermal_capacitance_value > 0.0)) {
    reject("package_consumer", "invalid_value_domain",
           "calculation input is outside the supported domain");
  }

  const auto &efficiency_range =
      required_slot(slots, "gearbox_efficiency_range");
  const auto &efficiency_spec = *find_spec("gearbox_efficiency_range");
  validate_slot_metadata(efficiency_range, efficiency_spec);
  require_known(efficiency_range, efficiency_spec);
  require_unit(efficiency_range, efficiency_spec);
  const auto &range_value = efficiency_range.claim->at("value");
  if (string_member(range_value, "kind") != "range" ||
      !range_value.at("minimum").is_number() ||
      !range_value.at("maximum").is_number()) {
    reject("package_consumer", "invalid_value_shape",
           "efficiency validation claim must be a range",
           "gearbox_efficiency_range");
  }
  const auto efficiency_minimum = range_value.at("minimum").get<double>();
  const auto efficiency_maximum = range_value.at("maximum").get<double>();
  if (!std::isfinite(efficiency_minimum) ||
      !std::isfinite(efficiency_maximum) || !(efficiency_minimum > 0.0) ||
      efficiency_minimum > nominal_efficiency_value ||
      nominal_efficiency_value > efficiency_maximum ||
      efficiency_maximum > 1.0) {
    reject("package_consumer", "invalid_efficiency_range",
           "efficiency range does not contain the nominal efficiency",
           "gearbox_efficiency_range");
  }

  const auto &curve = required_slot(slots, "torque_speed_curve");
  const auto &curve_spec = *find_spec("torque_speed_curve");
  validate_slot_metadata(curve, curve_spec);
  require_known(curve, curve_spec);
  require_unit(curve, curve_spec);
  const auto &curve_value = curve.claim->at("value");
  if (string_member(curve_value, "kind") != "curve" ||
      string_member(curve_value, "independent_quantity") !=
          "angular_velocity" ||
      string_member(curve_value, "independent_unit") != "rad/s" ||
      string_member(curve_value, "interpolation") != "linear" ||
      !curve_value.at("points").is_array() ||
      curve_value.at("points").size() != 2U) {
    reject("package_consumer", "invalid_torque_speed_curve",
           "torque-speed validation curve has unsupported metadata",
           "torque_speed_curve");
  }
  const auto &points = curve_value.at("points");
  const auto first_x = points[0].at("x").get<double>();
  const auto first_y = points[0].at("y").get<double>();
  const auto second_x = points[1].at("x").get<double>();
  const auto second_y = points[1].at("y").get<double>();
  if (first_x != 0.0 || first_y != stall_torque_value ||
      second_x != no_load_speed_value || second_y != 0.0) {
    reject("package_consumer", "invalid_torque_speed_curve",
           "torque-speed endpoints disagree with selected scalar claims",
           "torque_speed_curve");
  }

  std::array<ClaimBinding, 12> calculation_bindings{
      binding(gear_ratio),          binding(nominal_efficiency),
      binding(continuous_torque),   binding(stall_torque),
      binding(no_load_speed),       binding(no_load_current),
      binding(torque_constant),     binding(driver_limit),
      binding(winding_resistance),  binding(thermal_resistance),
      binding(thermal_capacitance), binding(maximum_temperature),
  };
  std::array<ClaimBinding, 2> validation_bindings{
      binding(efficiency_range),
      binding(curve),
  };

  std::vector<ClaimBinding> unused_bindings;
  for (const auto &spec : slot_specs) {
    if (spec.use != SlotUse::available_but_unused) {
      continue;
    }
    const auto &value = required_slot(slots, spec.name);
    if (string_member(*value.claim, "value_state") == "known") {
      require_unit(value, spec);
      const auto &kind = string_member(value.claim->at("value"), "kind");
      if (kind != "scalar") {
        reject("package_consumer", "invalid_value_shape",
               "known optional claim must be scalar", std::string(spec.name));
      }
    } else if (spec.value_shape != "scalar_or_unknown") {
      reject("package_consumer", "optional_value_unknown",
             "optional package claim is unexpectedly unknown",
             std::string(spec.name));
    }
    unused_bindings.push_back(binding(value));
  }
  for (const auto &[name, value] : slots) {
    if (find_spec(name) == nullptr) {
      unused_bindings.push_back(binding(value));
    }
  }
  std::sort(unused_bindings.begin() + 3, unused_bindings.end(),
            [](const ClaimBinding &left, const ClaimBinding &right) {
              return left.slot_name < right.slot_name;
            });

  return MotorComponentInput{
      std::move(package.inspection),
      gear_ratio_value,
      nominal_efficiency_value,
      continuous_torque_value,
      stall_torque_value,
      no_load_speed_value,
      no_load_current_value,
      torque_constant_value,
      driver_limit_value,
      winding_resistance_value,
      thermal_resistance_value,
      thermal_capacitance_value,
      maximum_temperature_value,
      efficiency_minimum,
      efficiency_maximum,
      {{{first_x, first_y}, {second_x, second_y}}},
      std::move(calculation_bindings),
      std::move(validation_bindings),
      std::move(unused_bindings),
  };
}

} // namespace

Result<PackageInspection>
inspect_execution_component(const std::string_view stored_bytes,
                            const std::string_view expected_object_hash) {
  return guarded<PackageInspection>([&] {
    auto package = verify_and_parse(stored_bytes, expected_object_hash);
    return std::move(package.inspection);
  });
}

Result<MotorComponentInput>
consume_motor_component(const std::string_view stored_bytes,
                        const std::string_view expected_object_hash) {
  return guarded<MotorComponentInput>([&] {
    return map_motor_component(
        verify_and_parse(stored_bytes, expected_object_hash));
  });
}

std::string_view supported_motor_consumer_contract_hash() noexcept {
  return detail::supported_consumer_contract_hash;
}

} // namespace prometheus::execution
