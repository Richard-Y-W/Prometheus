#include "structural_controller.hpp"
#include "structural_backend.hpp"
#include "cad_controller.hpp"
#include "engineering_controller.hpp"
#include "project_controller.hpp"
#include "prometheus/structural/structural_archive.hpp"
#include "prometheus/structural/structural_benchmarks.hpp"
#include "prometheus/run_store/run_store.hpp"
#include "prometheus/run_store/structural_archive_store.hpp"

#include <QGuiApplication>
#include <QFile>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <atomic>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <memory>

namespace {

namespace ps = prometheus::structural;

struct StageCounts final {
  int prepare_mesh{};
  int group_patches{};
  int compile_setup{};
  int execute_sample{};
  int finalize_refinement{};
};

class CountingStructuralBackend final : public StructuralBackend {
public:
  explicit CountingStructuralBackend(const bool failFineOnce = false)
      : delegate_(makeLocalStructuralBackend()),
        fail_fine_once_(failFineOnce) {}

  ps::PreparedMesh prepareMesh(const std::string_view bytes,
                               const double scale) const override {
    ++prepare_mesh_;
    return delegate_->prepareMesh(bytes, scale);
  }

  std::vector<ps::SurfacePatch> groupPatches(
      const ps::PreparedMesh &mesh, const double angle) const override {
    ++group_patches_;
    return delegate_->groupPatches(mesh, angle);
  }

  ps::CompiledStructuralSetup compileSetup(
      const ps::StructuralSetup &setup) const override {
    ++compile_setup_;
    auto compiled = delegate_->compileSetup(setup);
    last_setup_ = compiled;
    return compiled;
  }

  DesktopStructuralSampleResult executeSample(
      ps::SolverRunOptions options,
      ps::CompiledStructuralSetup setup,
      const ps::StructuralSampleRole role,
      ps::StructuralRefinementCriterion criterion) const override {
    ++execute_sample_;
    if (role == ps::StructuralSampleRole::fine &&
        fail_fine_once_.exchange(false)) {
      DesktopStructuralSampleResult failed;
      failed.failed_run = ps::SolverRunResult{
          .status = ps::SolverRunStatus::nonzero_exit,
          .exit_code = 7,
          .detail = "injected fine execution failure"};
      failed.error = "injected fine execution failure";
      return failed;
    }
    return delegate_->executeSample(
        std::move(options), std::move(setup), role, std::move(criterion));
  }

  DesktopStructuralRefinementResult finalizeRefinement(
      ps::CompletedStructuralSamplePtr coarse,
      ps::CompletedStructuralSamplePtr fine,
      const ps::ReviewedBoundaryCorrespondence &correspondence) const override {
    ++finalize_refinement_;
    return delegate_->finalizeRefinement(
        std::move(coarse), std::move(fine), correspondence);
  }

  [[nodiscard]] StageCounts counts() const {
    return {prepare_mesh_, group_patches_, compile_setup_, execute_sample_,
            finalize_refinement_};
  }

  [[nodiscard]] ps::CompiledStructuralSetup lastSetup() const {
    return *last_setup_;
  }

private:
  std::shared_ptr<const StructuralBackend> delegate_;
  mutable std::atomic<int> prepare_mesh_{};
  mutable std::atomic<int> group_patches_{};
  mutable std::atomic<int> compile_setup_{};
  mutable std::atomic<int> execute_sample_{};
  mutable std::atomic<int> finalize_refinement_{};
  mutable std::atomic<bool> fail_fine_once_{};
  mutable std::optional<ps::CompiledStructuralSetup> last_setup_;
};

[[noreturn]] void fail(const char *message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition) fail(message);
}

void writeQtFixture(const QString &path, const std::string_view bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "structural fixture opens");
  require(file.write(bytes.data(), static_cast<qint64>(bytes.size())) ==
              static_cast<qint64>(bytes.size()),
          "structural fixture writes completely");
  file.close();
}

void waitForRun(StructuralController &controller) {
  if (!controller.busy()) return;
  QEventLoop loop;
  QObject::connect(&controller, &StructuralController::runFinished,
                   &loop, &QEventLoop::quit);
  QTimer::singleShot(10000, &loop, &QEventLoop::quit);
  loop.exec();
}

void waitForPublication(StructuralController &controller) {
  if (!controller.busy()) return;
  QEventLoop loop;
  QObject::connect(&controller, &StructuralController::changed, &loop, [&] {
    if (!controller.busy()) loop.quit();
  });
  QTimer::singleShot(10000, &loop, &QEventLoop::quit);
  loop.exec();
}

QVariantMap reviewedDraft() {
  return {{"analysis_id", "desktop-structural-preview"},
          {"component_name", "reviewed tetrahedron"},
          {"geometry_sha256", "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
          {"material_designation", "benchmark material"},
          {"material_source_sha256", "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
          {"material_applicability", "known"},
          {"material_temper", "not_applicable"},
          {"material_product_form", "synthetic benchmark"},
          {"youngs_modulus_pa", 7.0e10},
          {"poisson_ratio", 0.33},
          {"material_reviewed", true},
          {"force_x_n", 0.0}, {"force_y_n", 0.0}, {"force_z_n", -100.0},
          {"load_reviewed", true}, {"restraint_reviewed", true},
          {"displacement_limit_m", 0.001}, {"von_mises_limit_pa", 1.0e8},
          {"displacement_limit_basis", "reviewed benchmark displacement limit"},
          {"von_mises_limit_basis", "reviewed benchmark stress limit"},
          {"requirement_rationale", "explicit exploratory desktop test"},
          {"requirement_reviewed", true},
          {"mesh_minimum_size_m", 0.001}, {"mesh_maximum_size_m", 0.003},
          {"mesh_target_size_m", 0.002},
          {"minimum_mean_ratio_threshold", 0.05},
          {"mesher_identity", "fixture mesher 1.0"},
          {"mesh_controls_reviewed", true},
          {"scenario_description", "bounded desktop structural preview"},
          {"scenario_confirmed", true},
          {"refinement_complete", true},
          {"refinement_criteria_satisfied", true},
          {"refinement_change_fraction", 0.04},
          {"refinement_maximum_allowed_change_fraction", 0.10},
          {"refinement_result_sha256",
           QVariantList{
               "sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
               "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"}}};
}

QVariantMap projectBoundDraft() {
  auto draft = reviewedDraft();
  draft["geometry_sha256"] =
      "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
  draft["refinement_maximum_change_fraction"] = 0.10;
  return draft;
}

} // namespace

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  QTemporaryDir temporary;
  require(temporary.isValid(), "temporary structural workspace exists");
  constexpr std::string_view coarseMesh = R"(*Heading
*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
*ELEMENT, TYPE=C3D4, ELSET=Volume1
1, 1, 2, 3, 4
)";
  constexpr std::string_view fineMesh = R"(*Heading
*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
5, 2.5, 2.5, 2.5
*ELEMENT, TYPE=C3D4, ELSET=Volume1
1, 1, 2, 3, 5
2, 1, 4, 2, 5
3, 1, 3, 4, 5
4, 2, 4, 3, 5
)";
  const auto coarseMeshPath = temporary.filePath("tetra-coarse.inp");
  const auto fineMeshPath = temporary.filePath("tetra-fine.inp");
  writeQtFixture(coarseMeshPath, coarseMesh);
  writeQtFixture(fineMeshPath, fineMesh);

  prometheus::run_store::ProjectV2 initialProject;
  initialProject.name = "Structural controller fixture";
  initialProject.cad_source = "missing-fixture.step";
  initialProject.assembly_artifact_hash =
      "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
  initialProject.coordinate_system = "right-handed Z-up";
  initialProject.length_unit = "m";
  initialProject.engineering.geometry_status = "not_evaluated";
  const auto projectPath = std::filesystem::path(temporary.path().toStdWString()) /
                           "structural.prometheus";
  require(prometheus::run_store::create_project_v2(projectPath, initialProject)
              .has_value(),
          "structural project fixture is created");
  CadController cad;
  EngineeringController engineering;
  ProjectController project(&cad, &engineering);
  project.openProject(QUrl::fromLocalFile(
      QString::fromStdWString(projectPath.wstring())));
  require(project.currentProjectPath().size() > 0,
          "structural project fixture opens");

  auto coarseReviewed =
      ps::cantilever_benchmark(8, 2, 2).setup.reviewed_setup;
  auto fineReviewed =
      ps::cantilever_benchmark(12, 3, 3).setup.reviewed_setup;
  fineReviewed.analysis_id = coarseReviewed.analysis_id;
  fineReviewed.component_name = coarseReviewed.component_name;
  fineReviewed.geometry_sha256 = coarseReviewed.geometry_sha256;
  const auto coarseSetup = ps::compile_structural_setup(coarseReviewed);
  const auto fineSetup = ps::compile_structural_setup(fineReviewed);
  const auto correspondence = ps::review_structural_boundary_correspondence(
      coarseSetup, fineSetup, true, true);
  const auto backendSolverFixture =
      std::filesystem::path(PROMETHEUS_SOLVER_FIXTURE_PATH);

  const auto runBackendRefinement =
      [&](const std::shared_ptr<CountingStructuralBackend> &backend,
          const std::filesystem::path &studyDirectory,
          const ps::StructuralRefinementCriterion criterion) {
        require(std::filesystem::create_directory(studyDirectory),
                "backend refinement study directory is created");
        const ps::SolverRunOptions coarseOptions{
            backendSolverFixture, studyDirectory, "backend_cantilever_coarse",
            std::chrono::seconds(5)};
        const ps::SolverRunOptions fineOptions{
            backendSolverFixture, studyDirectory, "backend_cantilever_fine",
            std::chrono::seconds(5)};
        const auto coarse = backend->executeSample(
            coarseOptions, coarseSetup, ps::StructuralSampleRole::coarse,
            criterion);
        const auto fine = backend->executeSample(
            fineOptions, fineSetup, ps::StructuralSampleRole::fine,
            criterion);
        require(coarse.sample && fine.sample &&
                    coarse.run().status == ps::SolverRunStatus::completed &&
                    fine.run().status == ps::SolverRunStatus::completed,
                "backend retains two immutable completed samples");
        return backend->finalizeRefinement(coarse.sample, fine.sample,
                                           correspondence);
      };

  auto acceptedBackend = std::make_shared<CountingStructuralBackend>();
  const auto acceptedCriterion =
      ps::compile_structural_refinement_criterion(
          ps::global_structural_observable_specs(0.10));
  require(acceptedCriterion.observables().size() == 2U &&
              acceptedCriterion.observables()[0].spec.region.kind ==
                  ps::StructuralObservableRegionKind::all_nodes &&
              acceptedCriterion.observables()[1].spec.region.kind ==
                  ps::StructuralObservableRegionKind::all_elements,
          "desktop default declarations conservatively cover both global obligations");
  const auto completed = runBackendRefinement(
      acceptedBackend,
      std::filesystem::path(temporary.path().toStdWString()) /
          "accepted-backend-refinement",
      acceptedCriterion);
  require(acceptedBackend->counts().execute_sample == 2 &&
              acceptedBackend->counts().finalize_refinement == 1,
          "one refinement study executes exactly two samples and finalizes once");
  require(completed.comparison && completed.archive &&
              completed.archive->schema_version == "4.0.0" &&
              completed.evaluation.comparison.has_value(),
          "backend finalization returns the typed pair, evaluation, and v4 archive");

  auto strictBackend = std::make_shared<CountingStructuralBackend>();
  const auto strictCriterion =
      ps::compile_structural_refinement_criterion(
          ps::global_structural_observable_specs(0.01));
  const auto indeterminate = runBackendRefinement(
      strictBackend,
      std::filesystem::path(temporary.path().toStdWString()) /
          "indeterminate-backend-refinement",
      strictCriterion);
  require(strictBackend->counts().execute_sample == 2 &&
              strictBackend->counts().finalize_refinement == 1 &&
              indeterminate.comparison && indeterminate.archive &&
              indeterminate.comparison->status() ==
                  ps::StructuralRefinementStatus::indeterminate &&
              indeterminate.evaluation.evaluated_obligations == 0 &&
              indeterminate.evaluation.findings.empty() &&
              indeterminate.evaluation.unknowns.size() == 2U &&
              indeterminate.archive->schema_version == "4.0.0",
          "a predeclared strict criterion archives one honest indeterminate pair");

  auto countingBackend = std::make_shared<CountingStructuralBackend>();
  StructuralController controller(&project, nullptr, countingBackend);
  controller.loadMesh(QUrl::fromLocalFile(coarseMeshPath), 0.001, 1.0);
  (void)controller.meshSummary();
  (void)controller.surfacePatches();
  (void)controller.meshSummary();
  require(countingBackend->counts().prepare_mesh == 1 &&
              countingBackend->counts().group_patches == 1,
          "mesh preparation and initial grouping run once despite repeated reads");
  require(controller.status() == "setup_blocked" &&
              controller.meshSummary().value("nodes").toInt() == 4 &&
              controller.meshSummary().value("elements").toInt() == 1 &&
              controller.meshSummary().value("surface_patches").toInt() == 4 &&
              controller.surfacePatches().size() == 4 && !controller.canRun(),
          "desktop adapter exposes mesh and conservative surface patch statistics");
  require(controller.meshGeometry() != nullptr,
          "prepared mesh has one stored display geometry");
  controller.setActiveSurfacePatch(1);
  require(controller.activeSurfacePatch().value("id").toInt() == 1 &&
              controller.highlightGeometry() != nullptr,
          "surface inspection exposes the selected patch and highlight geometry");
  const auto materialEvidence =
      QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) +
      "/fixtures/evidence/aluminum-2024-candidates-v1.json";
  require(controller.loadMaterialEvidence(
              QUrl::fromLocalFile(materialEvidence)) &&
              controller.materialCandidates().size() == 3 &&
              controller.materialCandidates().front().toMap()
                      .value("applicability") == "unresolved",
          "bounded checked-in material evidence loads as unresolved candidates");
  for (const auto &value : controller.materialCandidates()) {
    const auto candidate = value.toMap();
    require(!candidate.contains("yield_strength_pa") &&
                !candidate.contains("allowable_stress_pa") &&
                !candidate.contains("von_mises_limit_pa"),
            "material candidates cannot supply an implicit stress requirement");
  }
  controller.selectMaterialCandidate(
      "mil-hdbk-5j-bare-2024-sheet-plate-ge-0p250in", "assumed");
  const auto candidateDraft = controller.setupDraft();
  require(candidateDraft.value("material_designation") ==
                  "2024 aluminum" &&
              candidateDraft.value("material_temper") == "T351" &&
              candidateDraft.value("material_product_form") == "bare plate" &&
              candidateDraft.value("material_applicability") == "assumed" &&
              std::abs(candidateDraft.value("youngs_modulus_pa").toDouble() -
                       73773903036.9) < 0.5 &&
              !candidateDraft.value("material_reviewed").toBool() &&
              !candidateDraft.value("scenario_confirmed").toBool(),
          "candidate selection populates fields without silently approving material or scenario");
  controller.setPatchSelected(1, "load", true);
  controller.setPatchSelected(2, "restraint", true);
  controller.reviewSetup(reviewedDraft());
  bool sourceMismatchBlocked = false;
  bool criterionBlocked = false;
  for (const auto &value : controller.blockers()) {
    sourceMismatchBlocked = sourceMismatchBlocked ||
        value.toMap().value("code") == "structural_geometry_binding_mismatch";
    criterionBlocked = criterionBlocked ||
        value.toMap().value("code") == "refinement_criterion_invalid";
  }
  require(!controller.canRun() && sourceMismatchBlocked && criterionBlocked &&
              project.committedRunCount() == 0,
          "desktop rejects mismatched geometry and a missing predeclared criterion");
  auto coarseDraft = projectBoundDraft();
  controller.reviewSetup(coarseDraft);
  require(controller.status() == "ready_for_execution" && controller.canRun() &&
              controller.blockers().isEmpty() &&
              controller.requestPreview().value("fixed_nodes").toInt() == 3 &&
              controller.requestPreview().value("loaded_nodes").toInt() == 3,
          "reviewed desktop setup compiles through authoritative structural validation");
  const auto solverUrl =
      QUrl::fromLocalFile(QString::fromUtf8(PROMETHEUS_SOLVER_FIXTURE_PATH));
  const auto outputUrl = QUrl::fromLocalFile(temporary.path());
  controller.runAnalysis(
      solverUrl, outputUrl);
  waitForRun(controller);
  require(!controller.busy() && controller.hasRefinementBaseline() &&
              controller.sharedInputsLocked() &&
              controller.refinementStage() == "fine" &&
              controller.status() ==
                  "execution_completed_evaluation_pending" &&
              controller.baselineRun().value("elements").toInt() == 1 &&
              controller.baselineRun()
                      .value("selected_load_area_m2").toDouble() > 0.0 &&
              controller.baselineRun()
                      .value("selected_restraint_area_m2").toDouble() > 0.0 &&
              controller.lastRun().value("status") == "completed" &&
              controller.lastRun().value("evaluated_obligations").toInt() == 0 &&
              controller.lastRun().value("maximum_displacement_node_id").toInt() == 1 &&
              controller.lastRun().value("maximum_stress_element_id").toInt() == 1 &&
              controller.lastRun().value("displacement_rows").toInt() == 4 &&
              controller.resultGeometry() != nullptr &&
              controller.resultView().value("deformation_scale").toDouble() >= 1.0 &&
              controller.resultView().value("color_max_pa").toDouble() == 1.0e6 &&
              controller.findings().isEmpty() &&
              !controller.lastRun().value("archived").toBool() &&
              !controller.setupDraft().contains("refinement_complete"),
          "one completed coarse solve becomes a retained baseline, not a pass");
  require(countingBackend->counts().prepare_mesh == 1 &&
              countingBackend->counts().group_patches == 1 &&
              countingBackend->counts().compile_setup == 1 &&
              countingBackend->counts().execute_sample == 1 &&
              countingBackend->counts().finalize_refinement == 0,
          "the production controller executes one coarse sample exactly once");
  const auto activeOutput =
      controller.lastRun().value("output_directory").toString();
  require(controller.loadMaterialEvidence(
              QUrl::fromLocalFile(materialEvidence)) &&
              controller.lastRun().value("output_directory") == activeOutput &&
              countingBackend->counts().prepare_mesh == 1 &&
              countingBackend->counts().group_patches == 1 &&
              countingBackend->counts().compile_setup == 1 &&
              countingBackend->counts().execute_sample == 1,
          "browsing material candidates is read-only and retains the baseline");
  controller.commitLastRun();
  require(project.committedRunCount() == 0 &&
              controller.error().contains("refinement archive"),
          "a coarse-only baseline cannot mutate the project");

  controller.loadMesh(QUrl::fromLocalFile(fineMeshPath), 0.001, 1.0);
  require(controller.hasRefinementBaseline() &&
              controller.refinementStage() == "fine" &&
              controller.meshSummary().value("elements").toInt() == 4,
          "loading a distinct fine mesh retains the completed baseline");
  controller.setPatchSelected(1, "load", true);
  controller.setPatchSelected(2, "restraint", true);
  const auto coarseExecutionCount =
      countingBackend->counts().execute_sample;
  controller.setPatchSelected(3, "load", true);
  require(controller.hasRefinementBaseline() &&
              !controller.lastRun().value("archived").toBool() &&
              countingBackend->counts().execute_sample == coarseExecutionCount,
          "fine-only edits retain the baseline and invalidate only fine state");
  controller.setPatchSelected(3, "load", false);
  auto fineDraft = projectBoundDraft();
  fineDraft["mesh_target_size_m"] = 0.001;
  fineDraft["boundary_load_correspondence_reviewed"] = true;
  fineDraft["boundary_restraint_correspondence_reviewed"] = true;
  controller.reviewSetup(fineDraft);
  require(controller.canRun() && controller.status() == "ready_for_execution" &&
              controller.requestPreview().value("elements").toInt() == 4 &&
              controller.requestPreview()
                      .value("selected_restraint_area_m2").toDouble() > 0.0,
          "reviewed finer setup is ready without repeating the coarse solve");
  controller.runAnalysis(solverUrl, outputUrl);
  waitForRun(controller);
  require(controller.status() == "comparison_accepted" &&
              controller.refinementStage() == "completed" &&
              controller.lastRun().value("archived").toBool() &&
              controller.lastRun().value("archive_schema_version") == "4.0.0" &&
              controller.refinementComparison().value("status") == "accepted" &&
              controller.findings().size() == 2,
          "the second controller run creates accepted findings and a v4 archive");
  const auto archivePath = std::filesystem::path(
      controller.lastRun().value("archive_manifest").toString().toStdString());
  const auto verified =
      prometheus::structural::verify_structural_archive(archivePath);
  require(verified.valid && verified.schema_version == "4.0.0" &&
              verified.refinement && verified.evaluated_obligations == 2 &&
              verified.refinement->coarse().criterion().observables().size() ==
                  2U &&
              verified.refinement->coarse()
                      .criterion()
                      .observables()[0]
                      .spec.region.kind ==
                  ps::StructuralObservableRegionKind::all_nodes &&
              verified.refinement->coarse()
                      .criterion()
                      .observables()[1]
                      .spec.region.kind ==
                  ps::StructuralObservableRegionKind::all_elements,
          "offline archive verification replays both retained samples");
  const auto exportedDirectory = std::filesystem::path(temporary.path().toStdWString()) /
                                 "relocated-structural-run";
  const auto exported = prometheus::structural::export_structural_archive(
      archivePath, exportedDirectory);
  require(prometheus::structural::verify_structural_archive(
              exported.manifest_path).valid &&
              exported.manifest_sha256 ==
                  controller.lastRun().value("archive_sha256")
                      .toString().toStdString(),
          "verified archive relocates with identical manifest identity");
  (void)controller.meshSummary();
  (void)controller.findings();
  (void)controller.refinementComparison();
  controller.commitLastRun();
  waitForPublication(controller);
  require(project.committedRunCount() == 1 &&
              controller.lastRun().value("project_anchored").toBool(),
          "the valid production controller path publishes its retained archive");
  require(countingBackend->counts().prepare_mesh == 2 &&
              countingBackend->counts().group_patches == 2 &&
              countingBackend->counts().compile_setup == 2 &&
              countingBackend->counts().execute_sample == 2 &&
              countingBackend->counts().finalize_refinement == 1,
          "reads, export, and publication never repeat structural calculation");
  (void)controller.lastRun();
  (void)controller.lastRun();
  (void)controller.storedRuns();
  (void)controller.storedRuns();
  controller.commitLastRun();
  waitForPublication(controller);
  require(project.committedRunCount() == 1 &&
              controller.lastRun().value("project_already_committed").toBool() &&
              countingBackend->counts().execute_sample == 2 &&
              countingBackend->counts().finalize_refinement == 1,
          "repeated display reads and idempotent save do not repeat structural calculations");
  CadController reopenedCad;
  EngineeringController reopenedEngineering;
  ProjectController reopened(&reopenedCad, &reopenedEngineering);
  reopened.openProject(QUrl::fromLocalFile(
      QString::fromStdWString(projectPath.wstring())));
  require(reopened.committedRunCount() == 1,
          "embedded structural run survives project close and reopen");
  auto restoreCountingBackend = std::make_shared<CountingStructuralBackend>();
  StructuralController restoredController(&reopened, nullptr,
                                          restoreCountingBackend);
  require(restoredController.storedRuns().size() == 1 &&
              restoredController.storedRuns().front().toMap()
                  .value("restorable").toBool(),
          "reopened desktop enumerates the embedded structural run");
  QEventLoop restoreLoop;
  QObject::connect(&restoredController, &StructuralController::changed,
                   &restoreLoop, [&] {
    if (!restoredController.busy() &&
        (restoredController.status() == "structural_archive_restored" ||
         restoredController.status() == "structural_archive_restore_failed"))
      restoreLoop.quit();
  });
  QTimer::singleShot(10000, &restoreLoop, &QEventLoop::quit);
  restoredController.restoreStoredRun(
      0, QUrl::fromLocalFile(temporary.path()));
  restoreLoop.exec();
  if (restoredController.status() != "structural_archive_restored")
    std::cerr << "restore error: "
              << restoredController.error().toStdString() << '\n';
  require(restoredController.status() == "structural_archive_restored" &&
              restoredController.resultGeometry() != nullptr &&
              !restoredController.canRun() &&
              restoredController.hasRefinementBaseline() &&
              restoredController.sharedInputsLocked() &&
              restoredController.refinementStage() == "completed" &&
              restoredController.refinementComparison().value("status") ==
                  "accepted" &&
              restoredController.setupDraft().value("analysis_id") ==
                  "desktop-structural-preview" &&
              restoredController.selectedLoadPatchIds().size() == 1 &&
              restoredController.selectedRestraintPatchIds().size() == 1 &&
              restoredController.findings().size() == 2 &&
              restoredController.lastRun().value("status") ==
                  "restored_verified",
          "reopened desktop restores the verified pair and fine result visualization");
  (void)restoredController.meshSummary();
  (void)restoredController.findings();
  require(restoreCountingBackend->counts().prepare_mesh == 0 &&
              restoreCountingBackend->counts().group_patches == 1 &&
              restoreCountingBackend->counts().compile_setup == 0 &&
              restoreCountingBackend->counts().execute_sample == 0 &&
              restoreCountingBackend->counts().finalize_refinement == 0,
          "restore verifies once, regroups once, and does not recompile or execute");
  auto changedSnapshot = *reopened.project();
  changedSnapshot.assembly_artifact_hash =
      "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
  require(prometheus::run_store::save_project_snapshot(projectPath,
                                                        changedSnapshot)
              .has_value(),
          "project source identity changes for stale structural test");
  CadController staleCad;
  EngineeringController staleEngineering;
  ProjectController staleProject(&staleCad, &staleEngineering);
  staleProject.openProject(QUrl::fromLocalFile(
      QString::fromStdWString(projectPath.wstring())));
  StructuralController staleController(&staleProject);
  require(staleController.storedRuns().front().toMap()
              .value("source_current").toBool() == false,
          "reopened structural history marks changed assembly source stale");
  QEventLoop staleLoop;
  QObject::connect(&staleController, &StructuralController::changed,
                   &staleLoop, [&] {
    if (!staleController.busy() &&
        (staleController.status() == "structural_archive_restored_stale" ||
         staleController.status() == "structural_archive_restore_failed"))
      staleLoop.quit();
  });
  QTimer::singleShot(10000, &staleLoop, &QEventLoop::quit);
  staleController.restoreStoredRun(0, QUrl::fromLocalFile(temporary.path()));
  staleLoop.exec();
  require(staleController.status() == "structural_archive_restored_stale" &&
              staleController.resultGeometry() != nullptr &&
              !staleController.canRun() &&
              staleController.blockers().back().toMap().value("code") ==
                  "source_artifact_changed",
          "stale structural evidence remains viewable but cannot be rerun");
  const auto restoredDirectory =
      std::filesystem::path(temporary.path().toStdWString()) / "restored-from-project";
  const auto restored = prometheus::run_store::reconstruct_structural_archive(
      projectPath, reopened.project()->execution.committed_runs.front(),
      restoredDirectory);
  require(restored.has_value() &&
              prometheus::structural::verify_structural_archive(restored.value()).valid,
          "reopened project reconstructs a fully replayable structural archive");
  const auto fineDat =
      archivePath.parent_path() /
      (verified.refinement->fine().options().job_name + ".dat");
  QFile changed(QString::fromStdWString(fineDat.wstring()));
  require(changed.open(QIODevice::Append), "archived DAT opens for corruption test");
  changed.write("changed\n");
  changed.close();
  require(!prometheus::structural::verify_structural_archive(
               archivePath).valid,
          "changed raw solver output invalidates offline archive verification");

  auto retryBackend = std::make_shared<CountingStructuralBackend>(true);
  StructuralController retryController(&project, nullptr, retryBackend);
  retryController.loadMesh(QUrl::fromLocalFile(coarseMeshPath), 0.001, 1.0);
  retryController.setPatchSelected(1, "load", true);
  retryController.setPatchSelected(2, "restraint", true);
  retryController.reviewSetup(projectBoundDraft());
  retryController.runAnalysis(solverUrl, outputUrl);
  waitForRun(retryController);
  require(retryController.hasRefinementBaseline() &&
              retryBackend->counts().execute_sample == 1,
          "retry fixture retains one coarse execution");

  auto lockedFineDraft = projectBoundDraft();
  lockedFineDraft["analysis_id"] = "tampered-analysis";
  lockedFineDraft["component_name"] = "tampered-component";
  lockedFineDraft["geometry_sha256"] =
      "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
  lockedFineDraft["material_designation"] = "tampered material";
  lockedFineDraft["force_z_n"] = -999.0;
  lockedFineDraft["displacement_limit_m"] = 0.5;
  lockedFineDraft["scenario_description"] = "tampered scenario";
  lockedFineDraft["boundary_load_correspondence_reviewed"] = true;
  lockedFineDraft["boundary_restraint_correspondence_reviewed"] = true;
  retryController.reviewSetup(lockedFineDraft);
  bool notFinerBlocked = false;
  for (const auto &value : retryController.blockers())
    notFinerBlocked = notFinerBlocked ||
        value.toMap().value("code") == "refinement_mesh_not_finer";
  require(notFinerBlocked && !retryController.canRun() &&
              retryBackend->counts().execute_sample == 1 &&
              retryController.setupDraft().value("analysis_id") ==
                  "desktop-structural-preview" &&
              retryController.setupDraft().value("component_name") ==
                  "reviewed tetrahedron" &&
              retryController.setupDraft().value("geometry_sha256") ==
                  initialProject.assembly_artifact_hash.c_str() &&
              retryController.setupDraft().value("material_designation") ==
                  "benchmark material" &&
              retryController.setupDraft().value("force_z_n").toDouble() ==
                  -100.0 &&
              retryController.setupDraft()
                      .value("displacement_limit_m").toDouble() == 0.001 &&
              retryController.setupDraft().value("scenario_description") ==
                  "bounded desktop structural preview",
          "a direct fine draft cannot change locked physics and a reused mesh is blocked");

  retryController.loadMesh(QUrl::fromLocalFile(fineMeshPath), 0.001, 1.0);
  retryController.setPatchSelected(1, "load", true);
  retryController.setPatchSelected(2, "restraint", true);
  lockedFineDraft["mesh_target_size_m"] = 0.001;
  retryController.reviewSetup(lockedFineDraft);
  require(retryController.canRun() &&
              retryController.hasRefinementBaseline(),
          "locked shared physics permits a reviewed distinct fine mesh");
  retryController.runAnalysis(solverUrl, outputUrl);
  waitForRun(retryController);
  require(retryController.hasRefinementBaseline() &&
              retryController.refinementStage() == "fine" &&
              retryController.status() == "execution_failed" &&
              !retryController.lastRun().value("archived").toBool() &&
              retryBackend->counts().execute_sample == 2 &&
              retryBackend->counts().finalize_refinement == 0,
          "a failed fine run retains the baseline and cannot finalize");
  retryController.runAnalysis(solverUrl, outputUrl);
  waitForRun(retryController);
  require(retryController.status() == "comparison_accepted" &&
              retryBackend->counts().execute_sample == 3 &&
              retryBackend->counts().finalize_refinement == 1,
          "a unique fine retry succeeds without another coarse execution");
  retryController.discardRefinementBaseline();
  require(!retryController.hasRefinementBaseline() &&
              !retryController.sharedInputsLocked() &&
              retryController.refinementStage() == "coarse" &&
              retryController.baselineRun().isEmpty() &&
              retryController.refinementComparison().isEmpty() &&
              retryController.lastRun().isEmpty(),
          "discarding the baseline clears both active refinement stages");

  controller.reset();
  require(controller.status() == "mesh_required" &&
              controller.surfacePatches().isEmpty() && !controller.canRun(),
          "reset removes transient structural setup authority");

  StructuralController reviewInvalidation;
  reviewInvalidation.loadMesh(QUrl::fromLocalFile(coarseMeshPath), 0.001,
                              1.0);
  reviewInvalidation.setPatchSelected(1, "load", true);
  reviewInvalidation.setPatchSelected(2, "restraint", true);
  auto invalidationDraft = reviewedDraft();
  invalidationDraft["refinement_maximum_change_fraction"] = 0.10;
  reviewInvalidation.reviewSetup(invalidationDraft);
  require(reviewInvalidation.canRun(),
          "review-invalidation fixture begins with a compiled setup");
  reviewInvalidation.setPatchSelected(3, "load", true);
  bool loadReviewInvalidated = false;
  bool scenarioInvalidated = false;
  for (const auto &value : reviewInvalidation.blockers()) {
    const auto code = value.toMap().value("code").toString();
    loadReviewInvalidated = loadReviewInvalidated || code == "load_unreviewed";
    scenarioInvalidated = scenarioInvalidated || code == "scenario_unconfirmed";
  }
  require(!reviewInvalidation.canRun() && loadReviewInvalidated &&
              scenarioInvalidated &&
              !reviewInvalidation.setupDraft()
                   .value("refinement_complete")
                   .toBool(),
          "changing a reviewed boundary condition requires review and invalidates refinement");
  return 0;
}
