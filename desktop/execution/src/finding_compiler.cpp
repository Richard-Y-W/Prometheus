#include <prometheus/execution/finding_compiler.hpp>

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prometheus::execution {
namespace {

using Json = nlohmann::json;

constexpr std::array<std::string_view, 8> calculation_ids{
    "holding_load_torque",
    "acceleration_load_torque",
    "required_hold_motor_torque",
    "required_move_motor_torque",
    "peak_motor_speed",
    "available_move_torque",
    "estimated_move_current",
    "estimated_peak_temperature",
};

constexpr std::array<std::string_view, 8> calculation_units{
    "N*m", "N*m", "N*m", "N*m", "rad/s", "N*m", "A", "degC",
};

constexpr std::string_view point_payload_assumption =
    "The payload is represented as a point mass at the reviewed arm radius.";
constexpr std::string_view gravity_assumption =
    "Gravity is horizontal to the reviewed arm at the evaluated load case.";
constexpr std::string_view thermal_limitation =
    "The thermal estimate is a one-node periodic RC model and excludes gearbox and housing heat paths.";
constexpr std::string_view synthetic_limitation =
    "The package uses synthetic conformance evidence and provides no physical validation.";
constexpr std::size_t maximum_result_collection = 256U;
constexpr std::size_t maximum_result_text_code_points = 4096U;

struct FindingFailure final : std::exception {
  FindingFailure(std::string failure_code, std::string failure_message)
      : code(std::move(failure_code)), message(std::move(failure_message)) {}
  [[nodiscard]] const char *what() const noexcept override {
    return message.c_str();
  }
  std::string code;
  std::string message;
};

[[noreturn]] void reject(std::string code, std::string message) {
  throw FindingFailure(std::move(code), std::move(message));
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

bool finite(const simulation::MotorArmCalculations &value) {
  const std::array values{
      value.holding_load_torque_nm,
      value.acceleration_load_torque_nm,
      value.required_hold_motor_torque_nm,
      value.required_move_motor_torque_nm,
      value.peak_motor_speed_rad_s,
      value.available_move_torque_nm,
      value.estimated_move_current_a,
      value.estimated_peak_temperature_c,
  };
  return std::all_of(values.begin(), values.end(),
                     [](const double item) { return std::isfinite(item); });
}

void validate_backend_output(const simulation::MotorArmBackendOutput &output) {
  if (!finite(output.calculations)) {
    reject("non_finite_calculation", "backend output contains a non-finite value");
  }
  if (output.applicability_ids != simulation::motor_arm_applicability_ids) {
    reject("applicability_mismatch",
           "backend applicability does not match its contract");
  }
}

bool unicode_whitespace(const std::uint32_t code_point) {
  return (code_point >= 0x0009U && code_point <= 0x000DU) ||
         (code_point >= 0x001CU && code_point <= 0x0020U) ||
         code_point == 0x0085U || code_point == 0x00A0U ||
         code_point == 0x1680U ||
         (code_point >= 0x2000U && code_point <= 0x200AU) ||
         code_point == 0x2028U || code_point == 0x2029U ||
         code_point == 0x202FU || code_point == 0x205FU ||
         code_point == 0x3000U;
}

bool decode_code_point(const std::string_view text, std::size_t &index,
                       std::uint32_t &code_point) {
  const auto first = static_cast<unsigned char>(text[index]);
  if (first <= 0x7FU) {
    code_point = first;
    ++index;
    return true;
  }
  std::size_t width = 0U;
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
    return false;
  }
  if (index + width > text.size()) {
    return false;
  }
  for (std::size_t offset = 1U; offset < width; ++offset) {
    const auto continuation =
        static_cast<unsigned char>(text[index + offset]);
    if ((continuation & 0xC0U) != 0x80U) {
      return false;
    }
    code_point = (code_point << 6U) | (continuation & 0x3FU);
  }
  if ((width == 3U && code_point < 0x0800U) ||
      (width == 3U && code_point >= 0xD800U && code_point <= 0xDFFFU) ||
      (width == 4U && code_point < 0x10000U) ||
      (width == 4U && code_point > 0x10FFFFU)) {
    return false;
  }
  index += width;
  return true;
}

bool valid_result_text(const std::string_view text) {
  if (text.empty()) {
    return false;
  }
  std::size_t index = 0U;
  std::size_t count = 0U;
  std::uint32_t first = 0U;
  std::uint32_t last = 0U;
  while (index < text.size()) {
    std::uint32_t current = 0U;
    if (!decode_code_point(text, index, current)) {
      return false;
    }
    if (count == 0U) {
      first = current;
    }
    last = current;
    ++count;
    if (count > maximum_result_text_code_points) {
      return false;
    }
  }
  return !unicode_whitespace(first) && !unicode_whitespace(last);
}

void validate_result_collection_bounds(const MotorComponentInput &component) {
  if (!std::all_of(component.package.limitations.begin(),
                   component.package.limitations.end(),
                   [](const std::string &value) {
                     return valid_result_text(value);
                   })) {
    reject("result_text_invalid",
           "package limitation text cannot be represented verbatim in the result contract");
  }
  const auto already_has_synthetic =
      std::find(component.package.limitations.begin(),
                component.package.limitations.end(), synthetic_limitation) !=
      component.package.limitations.end();
  const auto appended_limitations = already_has_synthetic ? 1U : 2U;
  if (component.package.limitations.size() >
      maximum_result_collection - appended_limitations) {
    reject("result_collection_limit_exceeded",
           "package limitations cannot fit the bounded result contract");
  }
}

const ClaimBinding &binding(const MotorComponentInput &component,
                            const std::string_view slot_name) {
  const auto found = std::find_if(
      component.calculation_inputs.begin(),
      component.calculation_inputs.end(), [&](const ClaimBinding &candidate) {
        return candidate.slot_name == slot_name;
      });
  if (found == component.calculation_inputs.end()) {
    reject("provenance_binding_missing",
           "calculation input provenance binding is absent");
  }
  return *found;
}

const ClaimBinding &validation_binding(const MotorComponentInput &component,
                                       const std::string_view slot_name) {
  const auto found = std::find_if(
      component.validation_inputs.begin(), component.validation_inputs.end(),
      [&](const ClaimBinding &candidate) {
        return candidate.slot_name == slot_name;
      });
  if (found == component.validation_inputs.end()) {
    reject("provenance_binding_missing",
           "validation input provenance binding is absent");
  }
  return *found;
}

std::string finding_identity(const std::string_view request_hash,
                             const std::string_view obligation_id) {
  const Json identity = {
      {"request_hash", request_hash},
      {"obligation_id", obligation_id},
  };
  const auto bytes = integrity::canonicalize_json_bytes(identity.dump());
  return integrity::object_hash(bytes);
}

EngineeringOutcome outcome_for(const double margin) {
  return margin >= 0.0 ? EngineeringOutcome::pass : EngineeringOutcome::fail;
}

FindingSeverity severity_for(const EngineeringOutcome outcome,
                             const bool thermal) {
  if (outcome == EngineeringOutcome::fail) {
    return FindingSeverity::error;
  }
  return thermal ? FindingSeverity::caution : FindingSeverity::info;
}

std::vector<std::string> claim_ids(
    const std::initializer_list<const ClaimBinding *> bindings) {
  std::vector<std::string> result;
  result.reserve(bindings.size());
  for (const auto *item : bindings) {
    result.push_back(item->claim_id);
  }
  return result;
}

std::vector<std::string>
finding_limitations(const MotorComponentInput &component,
                    const bool thermal = false) {
  auto result = component.package.limitations;
  if (std::find(result.begin(), result.end(), synthetic_limitation) ==
      result.end()) {
    result.emplace_back(synthetic_limitation);
  }
  if (thermal) {
    result.emplace_back(thermal_limitation);
  }
  return result;
}

ObligationFinding make_finding(
    const MotorComponentInput &component, const std::string_view request_hash,
    const std::string_view scenario_hash, const std::string_view obligation_id,
    std::string title, std::string mechanism,
    const CalculatedQuantity calculated,
    const CalculatedQuantity comparison, const double margin,
    std::vector<std::string> consumed_claim_ids, const bool thermal = false) {
  if (!std::isfinite(margin)) {
    reject("non_finite_calculation", "finding margin is not finite");
  }
  const auto outcome = outcome_for(margin);
  return ObligationFinding{
      finding_identity(request_hash, obligation_id),
      std::string(obligation_id),
      outcome,
      severity_for(outcome, thermal),
      std::move(title),
      std::move(mechanism),
      std::move(calculated),
      std::move(comparison),
      ">=",
      margin,
      component.package.package_hash,
      std::string(request_hash),
      std::string(scenario_hash),
      std::move(consumed_claim_ids),
      {std::string(point_payload_assumption), std::string(gravity_assumption)},
      finding_limitations(component, thermal),
  };
}

std::array<CalculationRecord, 8>
calculation_records(const simulation::MotorArmCalculations &value) {
  const std::array values{
      value.holding_load_torque_nm,
      value.acceleration_load_torque_nm,
      value.required_hold_motor_torque_nm,
      value.required_move_motor_torque_nm,
      value.peak_motor_speed_rad_s,
      value.available_move_torque_nm,
      value.estimated_move_current_a,
      value.estimated_peak_temperature_c,
  };
  std::array<CalculationRecord, 8> result;
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = CalculationRecord{std::string(calculation_ids[index]),
                                      values[index],
                                      std::string(calculation_units[index])};
  }
  return result;
}

void validate_request(const MotorComponentInput &component,
                      const MotorArmScenario &scenario,
                      const AnalysisRequest &request,
                      const std::string_view request_hash,
                      const std::string_view scenario_hash) {
  if (!is_hash(request_hash) || !is_hash(scenario_hash)) {
    reject("invalid_hash", "finding provenance hash spelling is invalid");
  }
  if (request.package_hash != component.package.package_hash ||
      request.scenario_hash != scenario_hash) {
    reject("request_reference_mismatch",
           "request references do not match supplied package and scenario");
  }
  if (request.backend_id != motor_arm_backend_id ||
      request.backend_contract_version != motor_arm_backend_contract_version) {
    reject("unsupported_backend", "request backend identity is unsupported");
  }
  for (std::size_t index = 0U; index < request.obligation_ids.size(); ++index) {
    if (request.obligation_ids[index] != motor_arm_obligation_ids[index]) {
      reject("obligation_order_mismatch",
             "request obligations do not match the contract order");
    }
  }
  if (!scenario.confirmed_by_user || scenario.intent.empty() ||
      scenario.motion_profile != "symmetric_triangular_velocity") {
    reject("scenario_not_confirmed",
           "finding compilation requires a confirmed supported scenario");
  }
}

} // namespace

Result<CompiledMotorArmFindings> compile_motor_arm_findings(
    const MotorComponentInput &component, const MotorArmScenario &scenario,
    const AnalysisRequest &request, const std::string_view request_hash,
    const std::string_view scenario_hash,
    const simulation::MotorArmBackendOutput &nominal,
    const simulation::MotorArmBackendOutput &minimum_efficiency,
    const simulation::MotorArmBackendOutput &maximum_efficiency) {
  try {
    validate_request(component, scenario, request, request_hash, scenario_hash);
    validate_result_collection_bounds(component);
    validate_backend_output(nominal);
    validate_backend_output(minimum_efficiency);
    validate_backend_output(maximum_efficiency);
    const auto &value = nominal.calculations;

    const auto move_margin = simulation::available_over_required_margin(
        value.available_move_torque_nm, value.required_move_motor_torque_nm);
    const auto hold_margin = simulation::available_over_required_margin(
        component.continuous_torque_nm, value.required_hold_motor_torque_nm);
    const auto current_margin = simulation::available_over_required_margin(
        component.driver_current_limit_a, value.estimated_move_current_a);
    const auto thermal_margin = simulation::remaining_limit_fraction(
        component.maximum_temperature_c,
        value.estimated_peak_temperature_c);

    const auto &gear_ratio = binding(component, "gear_ratio");
    const auto &efficiency =
        binding(component, "gearbox_efficiency_nominal");
    const auto &continuous_torque =
        binding(component, "continuous_torque_nm");
    const auto &stall_torque = binding(component, "stall_torque_nm");
    const auto &no_load_speed = binding(component, "no_load_speed_rad_s");
    const auto &no_load_current = binding(component, "no_load_current_a");
    const auto &torque_constant = binding(component, "torque_constant_nm_a");
    const auto &driver_limit = binding(component, "driver_current_limit_a");
    const auto &winding_resistance =
        binding(component, "winding_resistance_ohm");
    const auto &thermal_resistance =
        binding(component, "thermal_resistance_k_w");
    const auto &thermal_capacitance =
        binding(component, "thermal_capacitance_j_k");
    const auto &maximum_temperature =
        binding(component, "maximum_temperature_c");

    std::array<ObligationFinding, 4> findings{
        make_finding(
            component, request_hash, scenario_hash,
            motor_arm_obligation_ids[0], "Movement torque-speed availability",
            "Available motor torque at peak speed is compared with the reviewed movement requirement.",
            {value.available_move_torque_nm, "N*m"},
            {value.required_move_motor_torque_nm, "N*m"}, move_margin,
            claim_ids({&gear_ratio, &efficiency, &stall_torque,
                       &no_load_speed})),
        make_finding(
            component, request_hash, scenario_hash,
            motor_arm_obligation_ids[1], "Continuous horizontal holding torque",
            "Continuous motor rating is compared with the reviewed horizontal holding requirement.",
            {component.continuous_torque_nm, "N*m"},
            {value.required_hold_motor_torque_nm, "N*m"}, hold_margin,
            claim_ids({&gear_ratio, &efficiency, &continuous_torque})),
        make_finding(
            component, request_hash, scenario_hash,
            motor_arm_obligation_ids[2], "Driver current-limit compatibility",
            "Driver current limit is compared with the algebraic movement-current estimate.",
            {component.driver_current_limit_a, "A"},
            {value.estimated_move_current_a, "A"}, current_margin,
            claim_ids({&gear_ratio, &efficiency, &no_load_current,
                       &torque_constant, &driver_limit})),
        make_finding(
            component, request_hash, scenario_hash,
            motor_arm_obligation_ids[3], "Simplified intermittent thermal peak",
            "Published temperature limit is compared with the periodic one-node RC peak estimate.",
            {component.maximum_temperature_c, "degC"},
            {value.estimated_peak_temperature_c, "degC"}, thermal_margin,
            claim_ids({&gear_ratio, &efficiency, &no_load_current,
                       &torque_constant, &winding_resistance,
                       &thermal_resistance, &thermal_capacitance,
                       &maximum_temperature}),
            true),
    };

    const auto minimum_hold_margin =
        simulation::available_over_required_margin(
            component.continuous_torque_nm,
            minimum_efficiency.calculations.required_hold_motor_torque_nm);
    const auto maximum_hold_margin =
        simulation::available_over_required_margin(
            component.continuous_torque_nm,
            maximum_efficiency.calculations.required_hold_motor_torque_nm);
    if (!std::isfinite(minimum_hold_margin) ||
        !std::isfinite(maximum_hold_margin)) {
      reject("non_finite_calculation",
             "efficiency sensitivity margin is not finite");
    }
    const auto &efficiency_range =
        validation_binding(component, "gearbox_efficiency_range");
    const EfficiencySensitivity sensitivity{
        "gearbox_efficiency_range",
        efficiency_range.claim_id,
        component.gearbox_efficiency_minimum,
        component.gearbox_efficiency_maximum,
        minimum_hold_margin,
        maximum_hold_margin,
        (minimum_hold_margin < 0.0 && maximum_hold_margin >= 0.0) ||
            (maximum_hold_margin < 0.0 && minimum_hold_margin >= 0.0),
    };

    std::size_t pass_count = 0U;
    for (const auto &finding : findings) {
      if (finding.outcome == EngineeringOutcome::pass) {
        ++pass_count;
      }
    }
    const std::vector<MissingInformation> uncovered{
        {"assembly.center_of_gravity",
         "The point-payload model has no assembly part-mass distribution."},
    };
    std::array<std::string, 6> applicability;
    std::transform(nominal.applicability_ids.begin(),
                   nominal.applicability_ids.end(), applicability.begin(),
                   [](const std::string_view item) { return std::string(item); });
    auto limitations = finding_limitations(component, true);
    return Result<CompiledMotorArmFindings>::success(
        CompiledMotorArmFindings{
            calculation_records(value),
            component.calculation_inputs,
            component.validation_inputs,
            component.available_but_unused,
            sensitivity,
            std::move(findings),
            uncovered,
            {std::string(point_payload_assumption),
             std::string(gravity_assumption)},
            std::move(limitations),
            std::move(applicability),
            MotorArmCoverage{4U,
                             4U,
                             CoverageCounts{pass_count, 4U - pass_count, 0U,
                                            0U},
                             uncovered},
        });
  } catch (const FindingFailure &failure) {
    return Result<CompiledMotorArmFindings>::failure(
        Diagnostic{"finding_compiler", failure.code, failure.message,
                   std::nullopt, std::nullopt});
  } catch (const std::exception &failure) {
    return Result<CompiledMotorArmFindings>::failure(
        Diagnostic{"finding_compiler", "finding_compilation_failed",
                   failure.what(), std::nullopt, std::nullopt});
  }
}

} // namespace prometheus::execution
