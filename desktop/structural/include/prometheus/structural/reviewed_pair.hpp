#pragma once

#include "prometheus/structural/structural_refinement.hpp"
#include "prometheus/structural/structural_setup.hpp"

#include <filesystem>
#include <string>

namespace prometheus::structural {

struct PreparedReviewedStructuralPair final {
  std::filesystem::path manifest_path;
  std::string manifest_identity;
  std::string coarse_job_name;
  std::string fine_job_name;
  StructuralRefinementCriterion criterion;
  CompiledStructuralSetup coarse_setup;
  CompiledStructuralSetup fine_setup;
  ReviewedBoundaryCorrespondence boundary_correspondence;
};

// Verifies and compiles the complete two-mesh review boundary. This function
// performs no solver execution and creates no output files.
[[nodiscard]] PreparedReviewedStructuralPair
preflight_reviewed_structural_pair(
    const std::filesystem::path &manifest_path);

} // namespace prometheus::structural
