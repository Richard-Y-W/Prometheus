#pragma once

#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_setup.hpp"

#include <memory>
#include <string>
#include <vector>

namespace prometheus::structural {

enum class StructuralSampleRole { coarse, fine };
enum class StructuralRefinementStatus { accepted, indeterminate };

struct StructuralRefinementIssue final {
  std::string code;
  std::string message;
};

class StructuralRefinementCriterion final {
public:
  StructuralRefinementCriterion(const StructuralRefinementCriterion &) =
      default;
  StructuralRefinementCriterion &
  operator=(const StructuralRefinementCriterion &) = default;

  [[nodiscard]] double maximum_change_fraction() const noexcept;
  [[nodiscard]] const std::string &identity() const noexcept;

private:
  double maximum_change_fraction_{};
  std::string identity_;

  StructuralRefinementCriterion(double maximum_change_fraction,
                                std::string identity);
  friend StructuralRefinementCriterion
  compile_structural_refinement_criterion(double);
};

class CompletedStructuralSample final {
public:
  [[nodiscard]] StructuralSampleRole role() const noexcept;
  [[nodiscard]] const StructuralRefinementCriterion &criterion() const
      noexcept;
  [[nodiscard]] const SolverRunOptions &options() const noexcept;
  [[nodiscard]] const CompiledStructuralSetup &setup() const noexcept;
  [[nodiscard]] const SolverRunResult &run() const noexcept;
  [[nodiscard]] const std::string &lineage_identity() const noexcept;

private:
  StructuralSampleRole role_;
  StructuralRefinementCriterion criterion_;
  SolverRunOptions options_;
  CompiledStructuralSetup setup_;
  SolverRunResult run_;
  std::string lineage_identity_;

  CompletedStructuralSample(StructuralSampleRole role,
                            StructuralRefinementCriterion criterion,
                            SolverRunOptions options,
                            CompiledStructuralSetup setup,
                            SolverRunResult run,
                            std::string lineage_identity);
  friend std::shared_ptr<const CompletedStructuralSample>
  compile_completed_structural_sample(
      StructuralSampleRole, StructuralRefinementCriterion,
      SolverRunOptions, CompiledStructuralSetup, SolverRunResult);
};

using CompletedStructuralSamplePtr =
    std::shared_ptr<const CompletedStructuralSample>;

class ReviewedBoundaryCorrespondence final {
public:
  ReviewedBoundaryCorrespondence(
      const ReviewedBoundaryCorrespondence &) = default;
  ReviewedBoundaryCorrespondence &
  operator=(const ReviewedBoundaryCorrespondence &) = default;

  [[nodiscard]] const std::string &coarse_setup_identity() const noexcept;
  [[nodiscard]] const std::string &fine_setup_identity() const noexcept;
  [[nodiscard]] bool load_region_confirmed() const noexcept;
  [[nodiscard]] bool restraint_region_confirmed() const noexcept;
  [[nodiscard]] double coarse_load_area_m2() const noexcept;
  [[nodiscard]] double fine_load_area_m2() const noexcept;
  [[nodiscard]] double coarse_restraint_area_m2() const noexcept;
  [[nodiscard]] double fine_restraint_area_m2() const noexcept;

private:
  std::string coarse_setup_identity_;
  std::string fine_setup_identity_;
  bool load_region_confirmed_{};
  bool restraint_region_confirmed_{};
  double coarse_load_area_m2_{};
  double fine_load_area_m2_{};
  double coarse_restraint_area_m2_{};
  double fine_restraint_area_m2_{};

  ReviewedBoundaryCorrespondence(
      std::string coarse_setup_identity,
      std::string fine_setup_identity,
      bool load_region_confirmed,
      bool restraint_region_confirmed,
      double coarse_load_area_m2,
      double fine_load_area_m2,
      double coarse_restraint_area_m2,
      double fine_restraint_area_m2);
  friend ReviewedBoundaryCorrespondence
  review_structural_boundary_correspondence(
      const CompiledStructuralSetup &, const CompiledStructuralSetup &,
      bool, bool);
};

class StructuralRefinementCompilation;
class VerifiedStructuralRefinement;

[[nodiscard]] StructuralRefinementCompilation compile_structural_refinement(
    CompletedStructuralSamplePtr coarse,
    CompletedStructuralSamplePtr fine,
    const ReviewedBoundaryCorrespondence &boundary_correspondence);

class VerifiedStructuralRefinement final {
public:
  [[nodiscard]] const CompletedStructuralSample &coarse() const noexcept;
  [[nodiscard]] const CompletedStructuralSample &fine() const noexcept;
  [[nodiscard]] const ReviewedBoundaryCorrespondence &
  boundary_correspondence() const noexcept;
  [[nodiscard]] double displacement_change_fraction() const noexcept;
  [[nodiscard]] double stress_change_fraction() const noexcept;
  [[nodiscard]] double maximum_change_fraction() const noexcept;
  [[nodiscard]] StructuralRefinementStatus status() const noexcept;

private:
  CompletedStructuralSamplePtr coarse_;
  CompletedStructuralSamplePtr fine_;
  ReviewedBoundaryCorrespondence boundary_correspondence_;
  double displacement_change_fraction_{};
  double stress_change_fraction_{};
  double maximum_change_fraction_{};
  StructuralRefinementStatus status_{
      StructuralRefinementStatus::indeterminate};

  VerifiedStructuralRefinement(
      CompletedStructuralSamplePtr coarse,
      CompletedStructuralSamplePtr fine,
      ReviewedBoundaryCorrespondence boundary_correspondence,
      double displacement_change_fraction,
      double stress_change_fraction,
      double maximum_change_fraction,
      StructuralRefinementStatus status);
  friend class StructuralRefinementCompilation;
  friend StructuralRefinementCompilation compile_structural_refinement(
      CompletedStructuralSamplePtr, CompletedStructuralSamplePtr,
      const ReviewedBoundaryCorrespondence &);
};

using VerifiedStructuralRefinementPtr =
    std::shared_ptr<const VerifiedStructuralRefinement>;

class StructuralRefinementCompilation final {
public:
  [[nodiscard]] bool complete() const noexcept;
  [[nodiscard]] const VerifiedStructuralRefinementPtr &value() const noexcept;
  [[nodiscard]] const std::vector<StructuralRefinementIssue> &issues() const
      noexcept;

private:
  VerifiedStructuralRefinementPtr value_;
  std::vector<StructuralRefinementIssue> issues_;

  StructuralRefinementCompilation(
      VerifiedStructuralRefinementPtr value,
      std::vector<StructuralRefinementIssue> issues);
  friend StructuralRefinementCompilation compile_structural_refinement(
      CompletedStructuralSamplePtr, CompletedStructuralSamplePtr,
      const ReviewedBoundaryCorrespondence &);
};

[[nodiscard]] StructuralRefinementCriterion
compile_structural_refinement_criterion(double maximum_change_fraction);

[[nodiscard]] CompletedStructuralSamplePtr
compile_completed_structural_sample(
    StructuralSampleRole role,
    StructuralRefinementCriterion criterion,
    SolverRunOptions options,
    CompiledStructuralSetup setup,
    SolverRunResult run);

[[nodiscard]] ReviewedBoundaryCorrespondence
review_structural_boundary_correspondence(
    const CompiledStructuralSetup &coarse,
    const CompiledStructuralSetup &fine,
    bool load_region_confirmed,
    bool restraint_region_confirmed);

} // namespace prometheus::structural
