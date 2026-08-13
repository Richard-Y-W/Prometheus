#pragma once

#include <prometheus/execution/contracts.hpp>
#include <prometheus/execution/package_consumer.hpp>
#include <prometheus/simulation/motor_arm_builtin_v1.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace prometheus::execution {

enum class EngineeringOutcome { pass, fail };
enum class FindingSeverity { info, caution, error };

struct CalculatedQuantity final {
  double value;
  std::string unit;
  bool operator==(const CalculatedQuantity &) const = default;
};

struct CalculationRecord final {
  std::string calculation_id;
  double value;
  std::string unit;
  bool operator==(const CalculationRecord &) const = default;
};

struct ObligationFinding final {
  std::string finding_id;
  std::string obligation_id;
  EngineeringOutcome outcome;
  FindingSeverity severity;
  std::string title;
  std::string mechanism;
  CalculatedQuantity calculated_quantity;
  CalculatedQuantity comparison_quantity;
  std::string comparison_operator;
  double signed_margin;
  std::string package_hash;
  std::string request_hash;
  std::string scenario_hash;
  std::vector<std::string> consumed_claim_ids;
  std::vector<std::string> assumptions;
  std::vector<std::string> limitations;
};

struct EfficiencySensitivity final {
  std::string sensitivity_id;
  std::string claim_id;
  double minimum_efficiency;
  double maximum_efficiency;
  double minimum_efficiency_hold_margin;
  double maximum_efficiency_hold_margin;
  bool crosses_zero;
};

struct MissingInformation final {
  std::string question_id;
  std::string reason;
  bool operator==(const MissingInformation &) const = default;
};

struct CoverageCounts final {
  std::size_t pass;
  std::size_t fail;
  std::size_t indeterminate;
  std::size_t not_evaluated;
};

struct MotorArmCoverage final {
  std::size_t requested_obligations;
  std::size_t evaluated_obligations;
  CoverageCounts counts;
  std::vector<MissingInformation> known_uncovered_questions;
};

struct CompiledMotorArmFindings final {
  std::array<CalculationRecord, 8> calculations;
  std::array<ClaimBinding, 12> calculation_inputs;
  std::array<ClaimBinding, 2> validation_inputs;
  std::vector<ClaimBinding> available_but_unused;
  EfficiencySensitivity sensitivity;
  std::array<ObligationFinding, 4> obligation_outcomes;
  std::vector<MissingInformation> missing_information;
  std::vector<std::string> assumptions;
  std::vector<std::string> limitations;
  std::array<std::string, 6> applicability;
  MotorArmCoverage coverage;
};

[[nodiscard]] Result<CompiledMotorArmFindings> compile_motor_arm_findings(
    const MotorComponentInput &component, const MotorArmScenario &scenario,
    const AnalysisRequest &request, std::string_view request_hash,
    std::string_view scenario_hash,
    const simulation::MotorArmBackendOutput &nominal,
    const simulation::MotorArmBackendOutput &minimum_efficiency,
    const simulation::MotorArmBackendOutput &maximum_efficiency);

} // namespace prometheus::execution
