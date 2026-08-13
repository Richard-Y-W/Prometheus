#include <prometheus/execution/execute.hpp>

#include <prometheus/execution/finding_compiler.hpp>
#include <prometheus/execution/numeric_profile.hpp>
#include <prometheus/execution/package_consumer.hpp>
#include <prometheus/execution/supported_consumer_contract.hpp>
#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/simulation/motor_arm_builtin_v1.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cfenv>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace prometheus::execution {
namespace {

using Json = nlohmann::json;

constexpr std::string_view schema_version = "1.0.0";
constexpr std::string_view package_schema_id =
    "urn:prometheus:schema:execution-component:2.0.0";
constexpr std::string_view package_schema_version = "2.0.0";
constexpr std::string_view package_media_type =
    "application/vnd.prometheus.execution-component+json;version=2.0.0";
constexpr std::size_t maximum_diagnostic_message_bytes = 4096U;

static_assert(motor_arm_backend_id == simulation::motor_arm_backend_id);
static_assert(motor_arm_backend_contract_version ==
              simulation::motor_arm_backend_contract_version);

std::string bounded(std::string value, const std::size_t maximum_bytes) {
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

Diagnostic diagnostic(std::string stage, std::string code,
                      std::string message,
                      std::optional<std::string> object_hash = std::nullopt,
                      std::optional<std::string> field = std::nullopt) {
  if (object_hash.has_value()) {
    object_hash = bounded(std::move(*object_hash), 128U);
  }
  if (field.has_value()) {
    field = bounded(std::move(*field), 512U);
  }
  return Diagnostic{bounded(std::move(stage), 128U),
                    bounded(std::move(code), 128U),
                    bounded(std::move(message),
                            maximum_diagnostic_message_bytes),
                    std::move(object_hash), std::move(field)};
}

ExecutionOutcome fail(const ExecutionDisposition disposition,
                      Diagnostic value) {
  return ExecutionFailure{disposition,
                          std::vector<Diagnostic>{std::move(value)}};
}

ExecutionDisposition disposition_for(const Diagnostic &value,
                                     const ExecutionDisposition fallback) {
  if (value.code.starts_with("unsupported_")) {
    return ExecutionDisposition::unsupported;
  }
  if (value.code == "internal_error" ||
      value.code == "integrity_processing_failed" ||
      value.code == "serialization_failed") {
    return ExecutionDisposition::failed;
  }
  if (value.code == "result_collection_limit_exceeded" ||
      value.code == "result_text_invalid") {
    return ExecutionDisposition::rejected_input;
  }
  return fallback;
}

std::optional<Diagnostic>
verify_exact_input(const std::string_view bytes,
                   const std::string_view expected_hash,
                   const std::string_view object_name) {
  try {
    const auto canonical = integrity::verify_canonical_bytes(bytes);
    const auto actual_hash = integrity::sha256_bytes(canonical);
    if (actual_hash != expected_hash) {
      return diagnostic(
          "input_integrity", "object_hash_mismatch",
          std::string(object_name) +
              " bytes do not match the expected object hash",
          std::string(expected_hash));
    }
    return std::nullopt;
  } catch (const integrity::CanonicalJsonError &failure) {
    return diagnostic("input_integrity", failure.code(), failure.what(),
                      std::string(expected_hash));
  } catch (const std::exception &failure) {
    return diagnostic("input_integrity", "integrity_processing_failed",
                      failure.what(), std::string(expected_hash));
  } catch (...) {
    return diagnostic("input_integrity", "integrity_processing_failed",
                      "unknown exact-byte verification failure",
                      std::string(expected_hash));
  }
}

std::optional<Diagnostic>
validate_request_bindings(const AnalysisRequest &request,
                          const ExecutionInput &input) {
  if (request.package_hash != input.expected_package_hash) {
    return diagnostic("request_binding", "package_hash_mismatch",
                      "request package hash does not match the supplied package",
                      input.expected_request_hash, "package_hash");
  }
  if (request.scenario_hash != input.expected_scenario_hash) {
    return diagnostic(
        "request_binding", "scenario_hash_mismatch",
        "request scenario hash does not match the supplied scenario",
        input.expected_request_hash, "scenario_hash");
  }
  if (request.package_consumer_contract_hash !=
      detail::supported_consumer_contract_hash) {
    return diagnostic(
        "request_binding", "unsupported_consumer_contract",
        "request package-consumer identity is not available",
        input.expected_request_hash, "package_consumer_contract_hash");
  }
  return std::nullopt;
}

simulation::MotorComponentInput
backend_component(const MotorComponentInput &component,
                  const double efficiency) {
  return simulation::MotorComponentInput{
      component.gear_ratio,
      efficiency,
      component.continuous_torque_nm,
      component.stall_torque_nm,
      component.no_load_speed_rad_s,
      component.no_load_current_a,
      component.torque_constant_nm_a,
      component.driver_current_limit_a,
      component.winding_resistance_ohm,
      component.thermal_resistance_k_w,
      component.thermal_capacitance_j_k,
      component.maximum_temperature_c,
  };
}

simulation::MotorArmScenario backend_scenario(const MotorArmScenario &scenario) {
  return simulation::MotorArmScenario{
      scenario.payload_mass_kg,
      scenario.arm_radius_m,
      scenario.rotation_rad,
      scenario.move_duration_s,
      scenario.hold_duration_s,
      scenario.cycle_duration_s,
      scenario.ambient_temperature_c,
  };
}

using BackendAttempt =
    std::variant<simulation::MotorArmBackendOutput, ExecutionFailure>;

std::string backend_error_code(const simulation::MotorArmBackendError error) {
  switch (error) {
  case simulation::MotorArmBackendError::invalid_domain:
    return "invalid_domain";
  case simulation::MotorArmBackendError::non_finite_input:
    return "non_finite_input";
  case simulation::MotorArmBackendError::unsupported_rounding_mode:
    return "unsupported_rounding_mode";
  case simulation::MotorArmBackendError::non_finite_output:
    return "non_finite_output";
  }
  return "unknown_backend_failure";
}

BackendAttempt run_backend(const simulation::MotorComponentInput &component,
                           const simulation::MotorArmScenario &scenario) {
  const auto result = simulation::run_motor_arm_builtin_v1(component, scenario);
  if (const auto *output =
          std::get_if<simulation::MotorArmBackendOutput>(&result)) {
    return *output;
  }
  const auto error =
      std::get<simulation::MotorArmBackendFailure>(result).code;
  auto disposition = ExecutionDisposition::rejected_input;
  auto message = std::string("motor-arm backend rejected its typed input");
  if (error == simulation::MotorArmBackendError::unsupported_rounding_mode) {
    disposition = ExecutionDisposition::unsupported;
    message = "motor-arm numeric rounding mode is unavailable";
  } else if (error == simulation::MotorArmBackendError::non_finite_output) {
    disposition = ExecutionDisposition::failed;
    message = "motor-arm backend produced a non-finite calculation";
  }
  return ExecutionFailure{
      disposition,
      {diagnostic("motor_arm_backend", backend_error_code(error), message)},
  };
}

Json platform_identity_json(const PlatformIdentity &identity) {
  return Json{{"name", identity.name},
              {"release", identity.release},
              {"architecture", identity.architecture}};
}

Json tool_identity_json(const ToolIdentity &identity) {
  return Json{{"id", identity.id}, {"version", identity.version}};
}

Json numeric_profile_json(const NumericProfile &profile) {
  return Json{
      {"operating_system", platform_identity_json(profile.operating_system)},
      {"compiler", tool_identity_json(profile.compiler)},
      {"standard_library", tool_identity_json(profile.standard_library)},
      {"math_runtime", tool_identity_json(profile.math_runtime)},
      {"backend_build_fingerprint", profile.backend_build_fingerprint},
      {"floating_point",
       {{"contraction", profile.floating_point.contraction},
        {"fast_math", profile.floating_point.fast_math},
        {"rounding_mode", profile.floating_point.rounding_mode}}},
      {"numeric_serialization_version",
       profile.numeric_serialization_version},
  };
}

Json input_binding_json(const ClaimBinding &binding,
                        const std::string_view use) {
  return Json{{"slot_name", binding.slot_name},
              {"claim_id", binding.claim_id},
              {"input_use", use}};
}

template <typename Collection>
Json input_bindings_json(const Collection &bindings,
                         const std::string_view use) {
  auto result = Json::array();
  for (const auto &binding : bindings) {
    result.push_back(input_binding_json(binding, use));
  }
  return result;
}

Json missing_information_json(const MissingInformation &value) {
  return Json{{"question_id", value.question_id}, {"reason", value.reason}};
}

std::string outcome_text(const EngineeringOutcome outcome) {
  return outcome == EngineeringOutcome::pass ? "pass" : "fail";
}

std::string severity_text(const FindingSeverity severity) {
  switch (severity) {
  case FindingSeverity::info:
    return "info";
  case FindingSeverity::caution:
    return "caution";
  case FindingSeverity::error:
    return "error";
  }
  return "error";
}

Json finding_json(const ObligationFinding &finding) {
  return Json{
      {"finding_id", finding.finding_id},
      {"obligation_id", finding.obligation_id},
      {"outcome", outcome_text(finding.outcome)},
      {"severity", severity_text(finding.severity)},
      {"title", finding.title},
      {"mechanism", finding.mechanism},
      {"calculated_quantity",
       {{"value", finding.calculated_quantity.value},
        {"unit", finding.calculated_quantity.unit}}},
      {"comparison_quantity",
       {{"value", finding.comparison_quantity.value},
        {"unit", finding.comparison_quantity.unit}}},
      {"comparison_operator", finding.comparison_operator},
      {"signed_margin", finding.signed_margin},
      {"package_hash", finding.package_hash},
      {"request_hash", finding.request_hash},
      {"scenario_hash", finding.scenario_hash},
      {"consumed_claim_ids", finding.consumed_claim_ids},
      {"assumptions", finding.assumptions},
      {"limitations", finding.limitations},
  };
}

CanonicalObject serialize_result(const CompiledMotorArmFindings &findings,
                                 const NumericProfile &profile,
                                 const std::string_view request_hash,
                                 const std::string_view package_hash) {
  auto calculations = Json::array();
  for (const auto &calculation : findings.calculations) {
    calculations.push_back({{"calculation_id", calculation.calculation_id},
                            {"value", calculation.value},
                            {"unit", calculation.unit}});
  }
  auto outcomes = Json::array();
  for (const auto &finding : findings.obligation_outcomes) {
    outcomes.push_back(finding_json(finding));
  }
  auto missing = Json::array();
  for (const auto &item : findings.missing_information) {
    missing.push_back(missing_information_json(item));
  }
  auto uncovered = Json::array();
  for (const auto &item : findings.coverage.known_uncovered_questions) {
    uncovered.push_back(missing_information_json(item));
  }
  const auto &sensitivity = findings.sensitivity;
  const Json value = {
      {"$schema", analysis_result_schema_id},
      {"schema_version", schema_version},
      {"execution_disposition", "completed"},
      {"request_hash", request_hash},
      {"package_hash", package_hash},
      {"backend",
       {{"backend_id", motor_arm_backend_id},
        {"contract_version", motor_arm_backend_contract_version},
        {"numeric_profile", numeric_profile_json(profile)}}},
      {"calculations", std::move(calculations)},
      {"consumed_inputs",
       {{"calculation_inputs",
         input_bindings_json(findings.calculation_inputs,
                             "calculation_input")},
        {"validation_inputs",
         input_bindings_json(findings.validation_inputs, "validation_input")},
        {"available_but_unused",
         input_bindings_json(findings.available_but_unused,
                             "available_but_unused")}}},
      {"sensitivities",
       Json::array({{{"sensitivity_id", sensitivity.sensitivity_id},
                     {"claim_id", sensitivity.claim_id},
                     {"minimum_efficiency", sensitivity.minimum_efficiency},
                     {"maximum_efficiency", sensitivity.maximum_efficiency},
                     {"hold_margin_at_minimum",
                      sensitivity.minimum_efficiency_hold_margin},
                     {"hold_margin_at_maximum",
                      sensitivity.maximum_efficiency_hold_margin},
                     {"crosses_zero", sensitivity.crosses_zero}}})},
      {"obligation_outcomes", std::move(outcomes)},
      {"missing_information", std::move(missing)},
      {"assumptions", findings.assumptions},
      {"limitations", findings.limitations},
      {"applicability", findings.applicability},
      {"coverage",
       {{"requested_obligations", findings.coverage.requested_obligations},
        {"evaluated_obligations", findings.coverage.evaluated_obligations},
        {"counts",
         {{"pass", findings.coverage.counts.pass},
          {"fail", findings.coverage.counts.fail},
          {"indeterminate", findings.coverage.counts.indeterminate},
          {"not_evaluated", findings.coverage.counts.not_evaluated}}},
        {"known_uncovered_questions", std::move(uncovered)}}},
  };
  const auto bytes = integrity::canonicalize_json_bytes(value.dump());
  return CanonicalObject{bytes,
                         integrity::object_hash(bytes),
                         std::string(analysis_result_media_type),
                         std::string(analysis_result_schema_id),
                         std::string(schema_version)};
}

Json object_reference(const std::string_view object_hash,
                      const std::size_t byte_length,
                      const std::string_view media_type,
                      const std::string_view schema_id,
                      const std::string_view object_schema_version) {
  return Json{{"object_hash", object_hash},
              {"byte_length", byte_length},
              {"media_type", media_type},
              {"schema_id", schema_id},
              {"schema_version", object_schema_version}};
}

CanonicalObject serialize_manifest(const ExecutionInput &input,
                                   const AnalysisRequest &request,
                                   const CanonicalObject &result,
                                   const NumericProfile &profile) {
  const Json value = {
      {"$schema", run_manifest_schema_id},
      {"schema_version", schema_version},
      {"manifest_kind", "completed_analysis_run"},
      {"package", object_reference(input.expected_package_hash,
                                   input.package_bytes.size(), package_media_type,
                                   package_schema_id, package_schema_version)},
      {"scenario",
       object_reference(input.expected_scenario_hash, input.scenario_bytes.size(),
                        motor_arm_scenario_media_type,
                        motor_arm_scenario_schema_id, schema_version)},
      {"request",
       object_reference(input.expected_request_hash, input.request_bytes.size(),
                        analysis_request_media_type, analysis_request_schema_id,
                        schema_version)},
      {"result", object_reference(result.object_hash, result.bytes.size(),
                                  result.media_type, result.schema_id,
                                  result.schema_version)},
      {"assembly_artifact_hash", request.assembly_artifact_hash},
      {"backend_id", request.backend_id},
      {"backend_contract_version", request.backend_contract_version},
      {"package_consumer_contract_hash",
       request.package_consumer_contract_hash},
      {"numeric_profile", numeric_profile_json(profile)},
  };
  const auto bytes = integrity::canonicalize_json_bytes(value.dump());
  return CanonicalObject{bytes,
                         integrity::object_hash(bytes),
                         std::string(run_manifest_media_type),
                         std::string(run_manifest_schema_id),
                         std::string(schema_version)};
}

} // namespace

ExecutionOutcome execute(const ExecutionInput &input) noexcept {
  try {
    if (std::fegetround() != FE_TONEAREST) {
      return fail(
          ExecutionDisposition::unsupported,
          diagnostic("numeric_profile", "unsupported_numeric_profile",
                     "floating-point rounding mode is not to-nearest"));
    }
    for (const auto &[bytes, expected_hash, object_name] :
         std::array<std::tuple<std::string_view, std::string_view,
                               std::string_view>,
                    3>{
             std::tuple<std::string_view, std::string_view, std::string_view>{
                 input.package_bytes, input.expected_package_hash, "package"},
             {input.scenario_bytes, input.expected_scenario_hash, "scenario"},
             {input.request_bytes, input.expected_request_hash, "request"},
         }) {
      if (auto failure = verify_exact_input(bytes, expected_hash, object_name)) {
        return fail(disposition_for(*failure,
                                    ExecutionDisposition::rejected_input),
                    std::move(*failure));
      }
    }

    auto component_result = consume_motor_component(
        input.package_bytes, input.expected_package_hash);
    if (!component_result.has_value()) {
      auto value = component_result.diagnostic();
      return fail(disposition_for(value, ExecutionDisposition::rejected_input),
                  std::move(value));
    }
    auto scenario_result = parse_motor_arm_scenario(input.scenario_bytes);
    if (!scenario_result.has_value()) {
      auto value = scenario_result.diagnostic();
      return fail(disposition_for(value, ExecutionDisposition::rejected_input),
                  std::move(value));
    }
    auto request_result = parse_analysis_request(input.request_bytes);
    if (!request_result.has_value()) {
      auto value = request_result.diagnostic();
      return fail(disposition_for(value, ExecutionDisposition::rejected_input),
                  std::move(value));
    }
    const auto &component = component_result.value();
    const auto &scenario = scenario_result.value();
    const auto &request = request_result.value();
    if (auto failure = validate_request_bindings(request, input)) {
      return fail(disposition_for(*failure,
                                  ExecutionDisposition::rejected_input),
                  std::move(*failure));
    }

    auto profile_result = collect_numeric_profile();
    if (!profile_result.has_value()) {
      return fail(ExecutionDisposition::unsupported,
                  profile_result.diagnostic());
    }
    const auto &profile = profile_result.value();
    const auto scenario_values = backend_scenario(scenario);
    const auto nominal = run_backend(
        backend_component(component, component.gearbox_efficiency_nominal),
        scenario_values);
    if (const auto *failure = std::get_if<ExecutionFailure>(&nominal)) {
      return *failure;
    }
    const auto minimum = run_backend(
        backend_component(component, component.gearbox_efficiency_minimum),
        scenario_values);
    if (const auto *failure = std::get_if<ExecutionFailure>(&minimum)) {
      return *failure;
    }
    const auto maximum = run_backend(
        backend_component(component, component.gearbox_efficiency_maximum),
        scenario_values);
    if (const auto *failure = std::get_if<ExecutionFailure>(&maximum)) {
      return *failure;
    }

    auto findings_result = compile_motor_arm_findings(
        component, scenario, request, input.expected_request_hash,
        input.expected_scenario_hash,
        std::get<simulation::MotorArmBackendOutput>(nominal),
        std::get<simulation::MotorArmBackendOutput>(minimum),
        std::get<simulation::MotorArmBackendOutput>(maximum));
    if (!findings_result.has_value()) {
      auto value = findings_result.diagnostic();
      return fail(disposition_for(value, ExecutionDisposition::failed),
                  std::move(value));
    }

    CanonicalObject result;
    try {
      result = serialize_result(findings_result.value(), profile,
                                input.expected_request_hash,
                                input.expected_package_hash);
    } catch (const integrity::CanonicalJsonError &failure) {
      return fail(ExecutionDisposition::failed,
                  diagnostic("result_serialization", failure.code(),
                             failure.what()));
    } catch (const std::exception &failure) {
      return fail(ExecutionDisposition::failed,
                  diagnostic("result_serialization", "serialization_failed",
                             failure.what()));
    }

    try {
      auto manifest = serialize_manifest(input, request, result, profile);
      return CompletedExecution{std::move(result), std::move(manifest)};
    } catch (const integrity::CanonicalJsonError &failure) {
      return fail(ExecutionDisposition::failed,
                  diagnostic("manifest_serialization", failure.code(),
                             failure.what()));
    } catch (const std::exception &failure) {
      return fail(ExecutionDisposition::failed,
                  diagnostic("manifest_serialization", "serialization_failed",
                             failure.what()));
    }
  } catch (const std::exception &failure) {
    return fail(ExecutionDisposition::failed,
                diagnostic("execution_boundary", "internal_error",
                           failure.what()));
  } catch (...) {
    return fail(ExecutionDisposition::failed,
                diagnostic("execution_boundary", "internal_error",
                           "unknown execution failure"));
  }
}

} // namespace prometheus::execution
