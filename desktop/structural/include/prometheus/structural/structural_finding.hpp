#pragma once

#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prometheus::structural {

enum class StructuralMetric { maximum_displacement, maximum_von_mises };

enum class FindingStatus { pass, fail, indeterminate };

struct StructuralRefinementEvidence final {
  bool complete{};
  bool criteria_satisfied{};
  double medium_to_fine_displacement_change_fraction{};
  double maximum_allowed_change_fraction{};
  std::vector<std::string> evidence_sha256;
};

struct StructuralFinding final {
  StructuralMetric metric{};
  FindingStatus status{FindingStatus::indeterminate};
  std::string requirement_id;
  std::string scope;
  std::optional<double> observed_value;
  std::optional<double> limit_value;
  std::string unit;
  std::string basis;
  std::vector<std::string> evidence_sha256;
  std::vector<std::string> assumptions;
  std::string explanation;
};

[[nodiscard]] std::string_view finding_status_name(FindingStatus status);
[[nodiscard]] std::string_view structural_metric_name(StructuralMetric metric);

[[nodiscard]] std::vector<StructuralFinding>
compile_structural_findings(const StructuralRequest &request,
                            const CompiledCalculixResult &result,
                            const StructuralRefinementEvidence &refinement);

} // namespace prometheus::structural
