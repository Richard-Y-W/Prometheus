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

  DesktopStructuralRun execute(
      const ps::SolverRunOptions &options,
      const ps::CompiledStructuralSetup &setup,
      std::optional<ps::StructuralRefinementEvidence> refinement) const override {
    DesktopStructuralRun result;
    result.run = ps::run_calculix(options, setup);
    result.evaluation = ps::compile_structural_findings(
        setup.request, result.run.validated_result, std::move(refinement));
    if (result.run.status == ps::SolverRunStatus::completed) {
      try {
        result.archive = ps::write_structural_archive(
            options.working_directory, options.job_name, setup, result.run,
            result.evaluation);
      } catch (const std::exception &error) {
        result.archive_error = error.what();
      }
    }
    return result;
  }
};

} // namespace

std::shared_ptr<const StructuralBackend> makeLocalStructuralBackend() {
  return std::make_shared<const LocalStructuralBackend>();
}
