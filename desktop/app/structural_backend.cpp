#include "structural_backend.hpp"

#include <exception>
#include <utility>

namespace ps = prometheus::structural;

namespace {

class LocalStructuralBackend final : public StructuralBackend {
public:
  ps::PreparedMesh prepareMesh(const std::string_view bytes,
                               const double coordinateScaleToM) const override {
    return ps::prepare_gmsh_abaqus_mesh(bytes, coordinateScaleToM);
  }

  std::vector<ps::SurfacePatch> groupPatches(
      const ps::PreparedMesh &mesh,
      const double angleDegrees) const override {
    return ps::group_boundary_faces(mesh.boundary_faces, angleDegrees);
  }

  ps::CompiledStructuralSetup compileSetup(
      const ps::StructuralSetup &setup) const override {
    return ps::compile_structural_setup(setup);
  }

  DesktopStructuralSampleResult executeSample(
      ps::SolverRunOptions options,
      ps::CompiledStructuralSetup setup,
      const ps::StructuralSampleRole role,
      ps::StructuralRefinementCriterion criterion) const override {
    DesktopStructuralSampleResult result;
    auto run = ps::run_calculix(options, setup);
    if (run.status != ps::SolverRunStatus::completed) {
      result.error = run.detail;
      result.failed_run = std::move(run);
      return result;
    }
    result.sample = ps::compile_completed_structural_sample(
        role, std::move(criterion), std::move(options), std::move(setup),
        std::move(run));
    return result;
  }

  DesktopStructuralRefinementResult finalizeRefinement(
      ps::CompletedStructuralSamplePtr coarse,
      ps::CompletedStructuralSamplePtr fine,
      const ps::ReviewedBoundaryCorrespondence &correspondence) const override {
    DesktopStructuralRefinementResult result;
    auto compiled = ps::compile_structural_refinement(
        std::move(coarse), std::move(fine), correspondence);
    if (!compiled.complete()) {
      result.issues = compiled.issues();
      return result;
    }
    result.comparison = compiled.value();
    result.evaluation = ps::compile_structural_findings(*result.comparison);
    try {
      result.archive = ps::write_structural_refinement_archive(
          *result.comparison, result.evaluation);
    } catch (const std::exception &error) {
      result.archive_error = error.what();
    }
    return result;
  }
};

} // namespace

std::shared_ptr<const StructuralBackend> makeLocalStructuralBackend() {
  return std::make_shared<const LocalStructuralBackend>();
}
