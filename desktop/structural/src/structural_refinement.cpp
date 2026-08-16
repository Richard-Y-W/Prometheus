#include "prometheus/structural/structural_refinement.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace prometheus::structural {
namespace {

using Json = nlohmann::json;

std::string lineage_identity(const StructuralSetup &setup) {
  const auto optionalNumber = [](const std::optional<double> value) {
    return value ? Json(*value) : Json(nullptr);
  };
  const Json lineage{
      {"$schema",
       "urn:prometheus:schema:structural-refinement-lineage:1.0.0"},
      {"schema_version", "1.0.0"},
      {"analysis_id", setup.analysis_id},
      {"component_name", setup.component_name},
      {"geometry_sha256", setup.geometry_sha256},
      {"material",
       {{"designation", setup.material.designation},
        {"source_sha256", setup.material.source_sha256},
        {"applicability", setup.material.applicability},
        {"youngs_modulus_pa", setup.material.youngs_modulus_pa},
        {"poisson_ratio", setup.material.poisson_ratio},
        {"reviewed", setup.material.reviewed},
        {"temper", setup.material.temper},
        {"product_form", setup.material.product_form}}},
      {"load",
       {{"label", setup.load.selection.label},
        {"total_force_n", setup.load.total_force_n},
        {"reviewed", setup.load.reviewed}}},
      {"restraint",
       {{"label", setup.restraint.selection.label},
        {"reviewed", setup.restraint.reviewed}}},
      {"requirement",
       {{"displacement_limit_m",
         optionalNumber(setup.requirement.displacement_limit_m)},
        {"von_mises_limit_pa",
         optionalNumber(setup.requirement.von_mises_limit_pa)},
        {"source_or_exploratory_rationale",
         setup.requirement.source_or_exploratory_rationale},
        {"reviewed", setup.requirement.reviewed},
        {"displacement_limit_basis",
         setup.requirement.displacement_limit_basis},
        {"von_mises_limit_basis",
         setup.requirement.von_mises_limit_basis}}},
      {"scenario",
       {{"description", setup.scenario_description},
        {"confirmed", setup.scenario_confirmed}}},
      {"coordinate_scale_to_m",
       setup.mesh_controls.coordinate_scale_to_m}};
  const auto canonical = integrity::canonicalize_json_bytes(lineage.dump());
  return integrity::sha256_bytes(canonical);
}

double relative_change(const double coarse, const double fine) {
  const double scale = std::max(std::abs(coarse), std::abs(fine));
  return scale == 0.0 ? 0.0 : std::abs(fine - coarse) / scale;
}

bool complete_and_bound(const CompletedStructuralSample &sample) {
  return sample.run().status == SolverRunStatus::completed &&
         sample.run().validated_result.has_value() &&
         sample.run().validated_result->complete() &&
         sample.run().validated_result->compiled_setup_identity ==
             sample.setup().identity;
}

void add_issue(std::vector<StructuralRefinementIssue> &issues,
               std::string code, std::string message) {
  if (std::ranges::none_of(issues, [&](const auto &existing) {
        return existing.code == code;
      }))
    issues.push_back({std::move(code), std::move(message)});
}

} // namespace

StructuralRefinementCriterion::StructuralRefinementCriterion(
    const double maximumChangeFraction, std::string identity)
    : maximum_change_fraction_(maximumChangeFraction),
      identity_(std::move(identity)) {}

double StructuralRefinementCriterion::maximum_change_fraction() const
    noexcept {
  return maximum_change_fraction_;
}

const std::string &StructuralRefinementCriterion::identity() const noexcept {
  return identity_;
}

CompletedStructuralSample::CompletedStructuralSample(
    const StructuralSampleRole role,
    StructuralRefinementCriterion criterion,
    SolverRunOptions options,
    CompiledStructuralSetup setup,
    SolverRunResult run,
    std::string lineageIdentity)
    : role_(role), criterion_(std::move(criterion)),
      options_(std::move(options)), setup_(std::move(setup)),
      run_(std::move(run)), lineage_identity_(std::move(lineageIdentity)) {}

StructuralSampleRole CompletedStructuralSample::role() const noexcept {
  return role_;
}

const StructuralRefinementCriterion &
CompletedStructuralSample::criterion() const noexcept {
  return criterion_;
}

const SolverRunOptions &CompletedStructuralSample::options() const noexcept {
  return options_;
}

const CompiledStructuralSetup &CompletedStructuralSample::setup() const
    noexcept {
  return setup_;
}

const SolverRunResult &CompletedStructuralSample::run() const noexcept {
  return run_;
}

const std::string &CompletedStructuralSample::lineage_identity() const
    noexcept {
  return lineage_identity_;
}

ReviewedBoundaryCorrespondence::ReviewedBoundaryCorrespondence(
    std::string coarseSetupIdentity,
    std::string fineSetupIdentity,
    const bool loadRegionConfirmed,
    const bool restraintRegionConfirmed,
    const double coarseLoadAreaM2,
    const double fineLoadAreaM2,
    const double coarseRestraintAreaM2,
    const double fineRestraintAreaM2)
    : coarse_setup_identity_(std::move(coarseSetupIdentity)),
      fine_setup_identity_(std::move(fineSetupIdentity)),
      load_region_confirmed_(loadRegionConfirmed),
      restraint_region_confirmed_(restraintRegionConfirmed),
      coarse_load_area_m2_(coarseLoadAreaM2),
      fine_load_area_m2_(fineLoadAreaM2),
      coarse_restraint_area_m2_(coarseRestraintAreaM2),
      fine_restraint_area_m2_(fineRestraintAreaM2) {}

const std::string &
ReviewedBoundaryCorrespondence::coarse_setup_identity() const noexcept {
  return coarse_setup_identity_;
}

const std::string &
ReviewedBoundaryCorrespondence::fine_setup_identity() const noexcept {
  return fine_setup_identity_;
}

bool ReviewedBoundaryCorrespondence::load_region_confirmed() const noexcept {
  return load_region_confirmed_;
}

bool ReviewedBoundaryCorrespondence::restraint_region_confirmed() const
    noexcept {
  return restraint_region_confirmed_;
}

double ReviewedBoundaryCorrespondence::coarse_load_area_m2() const noexcept {
  return coarse_load_area_m2_;
}

double ReviewedBoundaryCorrespondence::fine_load_area_m2() const noexcept {
  return fine_load_area_m2_;
}

double ReviewedBoundaryCorrespondence::coarse_restraint_area_m2() const
    noexcept {
  return coarse_restraint_area_m2_;
}

double ReviewedBoundaryCorrespondence::fine_restraint_area_m2() const
    noexcept {
  return fine_restraint_area_m2_;
}

VerifiedStructuralRefinement::VerifiedStructuralRefinement(
    CompletedStructuralSamplePtr coarse,
    CompletedStructuralSamplePtr fine,
    ReviewedBoundaryCorrespondence boundaryCorrespondence,
    const double displacementChangeFraction,
    const double stressChangeFraction,
    const double maximumChangeFraction,
    const StructuralRefinementStatus status)
    : coarse_(std::move(coarse)), fine_(std::move(fine)),
      boundary_correspondence_(std::move(boundaryCorrespondence)),
      displacement_change_fraction_(displacementChangeFraction),
      stress_change_fraction_(stressChangeFraction),
      maximum_change_fraction_(maximumChangeFraction), status_(status) {}

const CompletedStructuralSample &VerifiedStructuralRefinement::coarse() const
    noexcept {
  return *coarse_;
}

const CompletedStructuralSample &VerifiedStructuralRefinement::fine() const
    noexcept {
  return *fine_;
}

const ReviewedBoundaryCorrespondence &
VerifiedStructuralRefinement::boundary_correspondence() const noexcept {
  return boundary_correspondence_;
}

double VerifiedStructuralRefinement::displacement_change_fraction() const
    noexcept {
  return displacement_change_fraction_;
}

double VerifiedStructuralRefinement::stress_change_fraction() const noexcept {
  return stress_change_fraction_;
}

double VerifiedStructuralRefinement::maximum_change_fraction() const
    noexcept {
  return maximum_change_fraction_;
}

StructuralRefinementStatus VerifiedStructuralRefinement::status() const
    noexcept {
  return status_;
}

StructuralRefinementCompilation::StructuralRefinementCompilation(
    VerifiedStructuralRefinementPtr value,
    std::vector<StructuralRefinementIssue> issues)
    : value_(std::move(value)), issues_(std::move(issues)) {}

bool StructuralRefinementCompilation::complete() const noexcept {
  return value_ != nullptr && issues_.empty();
}

const VerifiedStructuralRefinementPtr &
StructuralRefinementCompilation::value() const noexcept {
  return value_;
}

const std::vector<StructuralRefinementIssue> &
StructuralRefinementCompilation::issues() const noexcept {
  return issues_;
}

StructuralRefinementCriterion compile_structural_refinement_criterion(
    const double maximumChangeFraction) {
  if (!std::isfinite(maximumChangeFraction) ||
      maximumChangeFraction <= 0.0 || maximumChangeFraction > 1.0)
    throw std::invalid_argument(
        "refinement_criterion_invalid: maximum change must be finite and in "
        "(0, 1]");
  const Json criterion{
      {"$schema",
       "urn:prometheus:schema:structural-refinement-criterion:1.0.0"},
      {"schema_version", "1.0.0"},
      {"maximum_change_fraction", maximumChangeFraction}};
  const auto canonical =
      integrity::canonicalize_json_bytes(criterion.dump());
  return {maximumChangeFraction, integrity::sha256_bytes(canonical)};
}

CompletedStructuralSamplePtr compile_completed_structural_sample(
    const StructuralSampleRole role,
    StructuralRefinementCriterion criterion,
    SolverRunOptions options,
    CompiledStructuralSetup setup,
    SolverRunResult run) {
  auto lineageIdentity = lineage_identity(setup.reviewed_setup);
  return CompletedStructuralSamplePtr(new CompletedStructuralSample(
      role, std::move(criterion), std::move(options), std::move(setup),
      std::move(run), std::move(lineageIdentity)));
}

ReviewedBoundaryCorrespondence review_structural_boundary_correspondence(
    const CompiledStructuralSetup &coarse,
    const CompiledStructuralSetup &fine,
    const bool loadRegionConfirmed,
    const bool restraintRegionConfirmed) {
  return {
      coarse.identity,
      fine.identity,
      loadRegionConfirmed,
      restraintRegionConfirmed,
      coarse.reviewed_setup.load.selection.area_m2,
      fine.reviewed_setup.load.selection.area_m2,
      coarse.reviewed_setup.restraint.selection.area_m2,
      fine.reviewed_setup.restraint.selection.area_m2};
}

StructuralRefinementCompilation compile_structural_refinement(
    CompletedStructuralSamplePtr coarse,
    CompletedStructuralSamplePtr fine,
    const ReviewedBoundaryCorrespondence &boundaryCorrespondence) {
  std::vector<StructuralRefinementIssue> issues;
  if (!coarse) {
    add_issue(issues, "refinement_baseline_required",
              "A completed coarse baseline is required.");
  }
  if (!fine) {
    add_issue(issues, "refinement_result_incomplete",
              "A completed fine result is required.");
  }
  if (!coarse || !fine)
    return {nullptr, std::move(issues)};

  if (coarse->role() != StructuralSampleRole::coarse ||
      fine->role() != StructuralSampleRole::fine)
    add_issue(issues, "refinement_sample_role_invalid",
              "Refinement samples must occupy their declared coarse and fine "
              "roles.");

  if (coarse->criterion().identity() != fine->criterion().identity())
    add_issue(issues, "refinement_lineage_mismatch",
              "Both samples must retain the same predeclared criterion.");
  if (coarse->lineage_identity() != fine->lineage_identity())
    add_issue(issues, "refinement_lineage_mismatch",
              "Shared reviewed physics differs between the two samples.");

  const auto &coarseSetup = coarse->setup();
  const auto &fineSetup = fine->setup();
  const auto &coarseReviewed = coarseSetup.reviewed_setup;
  const auto &fineReviewed = fineSetup.reviewed_setup;
  if (coarseReviewed.mesh_controls.mesh_sha256 ==
      fineReviewed.mesh_controls.mesh_sha256)
    add_issue(issues, "refinement_mesh_identity_reused",
              "Coarse and fine samples require different mesh identities.");
  if (fineReviewed.mesh.elements.size() <=
          coarseReviewed.mesh.elements.size() ||
      fineReviewed.mesh_controls.target_size_m >=
          coarseReviewed.mesh_controls.target_size_m)
    add_issue(issues, "refinement_mesh_not_finer",
              "The fine sample requires more elements and a smaller target "
              "size.");

  if (!boundaryCorrespondence.load_region_confirmed() ||
      !boundaryCorrespondence.restraint_region_confirmed() ||
      boundaryCorrespondence.coarse_setup_identity() != coarseSetup.identity ||
      boundaryCorrespondence.fine_setup_identity() != fineSetup.identity)
    add_issue(issues, "refinement_boundary_review_required",
              "Load and restraint correspondence must be reviewed for these "
              "exact setups.");

  const bool coarseComplete = complete_and_bound(*coarse);
  const bool fineComplete = complete_and_bound(*fine);
  if (!coarseComplete || !fineComplete)
    add_issue(issues, "refinement_result_incomplete",
              "Both solver results must be complete and bound to their exact "
              "compiled setups.");

  if (coarse->run().validated_result && fine->run().validated_result &&
      coarse->run().validated_result->identity ==
          fine->run().validated_result->identity)
    add_issue(issues, "refinement_result_identity_reused",
              "Coarse and fine samples require different result identities.");

  if (coarseComplete && fineComplete) {
    const auto &coarseBackend = coarse->run().validated_result->backend;
    const auto &fineBackend = fine->run().validated_result->backend;
    if (coarseBackend.executable_sha256 != fineBackend.executable_sha256 ||
        coarseBackend.version != fineBackend.version)
      add_issue(issues, "refinement_backend_mismatch",
                "Both samples must use the same authoritative backend "
                "identity and version.");
  }

  if (!issues.empty())
    return {nullptr, std::move(issues)};

  const auto &coarseMetrics = *coarse->run().validated_result->metrics;
  const auto &fineMetrics = *fine->run().validated_result->metrics;
  const double displacementChange = relative_change(
      coarseMetrics.maximum_displacement_m,
      fineMetrics.maximum_displacement_m);
  const double stressChange = relative_change(
      coarseMetrics.maximum_von_mises_pa,
      fineMetrics.maximum_von_mises_pa);
  const double maximumChange = std::max(displacementChange, stressChange);
  if (!std::isfinite(displacementChange) || !std::isfinite(stressChange) ||
      !std::isfinite(maximumChange))
    return {nullptr,
            {{"refinement_result_incomplete",
              "Derived refinement changes must be finite."}}};
  const auto status =
      maximumChange <= coarse->criterion().maximum_change_fraction()
          ? StructuralRefinementStatus::accepted
          : StructuralRefinementStatus::indeterminate;
  VerifiedStructuralRefinementPtr value(new VerifiedStructuralRefinement(
      std::move(coarse), std::move(fine), boundaryCorrespondence,
      displacementChange, stressChange, maximumChange, status));
  return {std::move(value), {}};
}

} // namespace prometheus::structural
