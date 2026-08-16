#include "structural_controller.hpp"
#include "structural_backend.hpp"
#include "cad_controller.hpp"
#include "engineering_controller.hpp"
#include "project_controller.hpp"
#include "prometheus/structural/structural_archive.hpp"
#include "prometheus/run_store/run_store.hpp"
#include "prometheus/run_store/structural_archive_store.hpp"

#include <QGuiApplication>
#include <QDir>
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
  int execute{};
};

class CountingStructuralBackend final : public StructuralBackend {
public:
  CountingStructuralBackend() : delegate_(makeLocalStructuralBackend()) {}

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
    return delegate_->compileSetup(setup);
  }

  DesktopStructuralRun execute(
      const ps::SolverRunOptions &options,
      const ps::CompiledStructuralSetup &setup,
      std::optional<ps::StructuralRefinementEvidence> refinement) const override {
    ++execute_;
    return delegate_->execute(options, setup, std::move(refinement));
  }

  [[nodiscard]] StageCounts counts() const {
    return {prepare_mesh_, group_patches_, compile_setup_, execute_};
  }

private:
  std::shared_ptr<const StructuralBackend> delegate_;
  mutable std::atomic<int> prepare_mesh_{};
  mutable std::atomic<int> group_patches_{};
  mutable std::atomic<int> compile_setup_{};
  mutable std::atomic<int> execute_{};
};

[[noreturn]] void fail(const char *message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition) fail(message);
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

} // namespace

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  QTemporaryDir temporary;
  require(temporary.isValid(), "temporary structural workspace exists");
  QFile mesh(temporary.filePath("tetra.inp"));
  require(mesh.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "structural fixture mesh opens");
  mesh.write(R"(*Heading
*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
*ELEMENT, TYPE=C3D4, ELSET=Volume1
1, 1, 2, 3, 4
)");
  mesh.close();

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
  auto countingBackend = std::make_shared<CountingStructuralBackend>();
  StructuralController controller(&project, nullptr, countingBackend);
  controller.loadMesh(QUrl::fromLocalFile(mesh.fileName()), 0.001, 1.0);
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
  require(controller.status() == "ready_for_execution" && controller.canRun() &&
              controller.blockers().isEmpty() &&
              controller.requestPreview().value("fixed_nodes").toInt() == 3 &&
              controller.requestPreview().value("loaded_nodes").toInt() == 3,
          "reviewed desktop setup compiles through authoritative structural validation");
  QEventLoop runLoop;
  QObject::connect(&controller, &StructuralController::runFinished,
                   &runLoop, &QEventLoop::quit);
  QTimer::singleShot(5000, &runLoop, &QEventLoop::quit);
  controller.runAnalysis(
      QUrl::fromLocalFile(QString::fromUtf8(PROMETHEUS_SOLVER_FIXTURE_PATH)),
      QUrl::fromLocalFile(temporary.path()));
  runLoop.exec();
  require(!controller.busy() && controller.status() == "execution_completed" &&
              controller.lastRun().value("status") == "completed" &&
              controller.lastRun().value("evaluated_obligations").toInt() == 2 &&
              controller.lastRun().value("maximum_displacement_node_id").toInt() == 1 &&
              controller.lastRun().value("maximum_stress_element_id").toInt() == 1 &&
              controller.lastRun().value("displacement_rows").toInt() == 4 &&
              controller.resultGeometry() != nullptr &&
              controller.resultView().value("deformation_scale").toDouble() >= 1.0 &&
              controller.resultView().value("color_max_pa").toDouble() == 1.0e6 &&
              controller.findings().size() == 2 &&
              controller.lastRun().value("archived").toBool() &&
              !controller.lastRun().value("archive_manifest").toString().isEmpty(),
          "desktop executes, archives, and builds a traceable deformed stress view");
  require(countingBackend->counts().prepare_mesh == 1 &&
              countingBackend->counts().group_patches == 1 &&
              countingBackend->counts().compile_setup == 1 &&
              countingBackend->counts().execute == 1,
          "reviewed structural stages execute exactly once");
  const auto activeArchive =
      controller.lastRun().value("archive_manifest").toString();
  require(controller.loadMaterialEvidence(
              QUrl::fromLocalFile(materialEvidence)) &&
              controller.lastRun().value("archive_manifest") == activeArchive &&
              countingBackend->counts().prepare_mesh == 1 &&
              countingBackend->counts().group_patches == 1 &&
              countingBackend->counts().compile_setup == 1 &&
              countingBackend->counts().execute == 1,
          "browsing material candidates is read-only and retains the active run");
  const auto archivePath = controller.lastRun().value("archive_manifest").toString();
  const auto verified = prometheus::structural::verify_structural_archive(
      std::filesystem::path(archivePath.toStdWString()));
  require(verified.valid && verified.evaluated_obligations == 2,
          "offline archive verification replays exact raw DAT metrics");
  const auto exportedDirectory = std::filesystem::path(temporary.path().toStdWString()) /
                                 "relocated-structural-run";
  const auto exported = prometheus::structural::export_structural_archive(
      std::filesystem::path(archivePath.toStdWString()), exportedDirectory);
  require(prometheus::structural::verify_structural_archive(
              exported.manifest_path).valid &&
              exported.manifest_sha256 ==
                  controller.lastRun().value("archive_sha256").toString().toStdString(),
          "verified archive relocates with identical manifest identity");
  QEventLoop commitLoop;
  QObject::connect(&controller, &StructuralController::changed, &commitLoop,
                   [&] {
    if (!controller.busy() &&
        (controller.status() == "structural_archive_published" ||
         controller.status() == "structural_archive_publication_failed"))
      commitLoop.quit();
  });
  QTimer::singleShot(10000, &commitLoop, &QEventLoop::quit);
  controller.commitLastRun();
  commitLoop.exec();
  require(!controller.busy() &&
              controller.status() == "structural_archive_published" &&
              controller.lastRun().value("project_artifacts_embedded").toBool() &&
              project.committedRunCount() == 1,
          "desktop asynchronously embeds the complete structural archive graph");
  require(countingBackend->counts().compile_setup == 1 &&
              countingBackend->counts().execute == 1,
          "archive publication does not recompile or re-execute the active run");
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
              restoredController.canRun() &&
              restoredController.setupDraft().value("analysis_id") ==
                  "desktop-structural-preview" &&
              restoredController.selectedLoadPatchIds().size() == 1 &&
              restoredController.selectedRestraintPatchIds().size() == 1 &&
              restoredController.findings().size() == 2 &&
              restoredController.lastRun().value("status") ==
                  "restored_verified",
          "reopened desktop restores editable reviewed setup and result visualization");
  (void)restoredController.meshSummary();
  (void)restoredController.findings();
  require(restoreCountingBackend->counts().prepare_mesh == 0 &&
              restoreCountingBackend->counts().group_patches == 1 &&
              restoreCountingBackend->counts().compile_setup == 0 &&
              restoreCountingBackend->counts().execute == 0,
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
  QFile changed(QDir(controller.lastRun().value("output_directory").toString())
                    .filePath("prometheus_structural_run.dat"));
  require(changed.open(QIODevice::Append), "archived DAT opens for corruption test");
  changed.write("changed\n");
  changed.close();
  require(!prometheus::structural::verify_structural_archive(
               std::filesystem::path(archivePath.toStdWString())).valid,
          "changed raw solver output invalidates offline archive verification");
  auto changedForce = reviewedDraft();
  changedForce["force_z_n"] = -101.0;
  controller.reviewSetup(changedForce);
  require(countingBackend->counts().prepare_mesh == 1 &&
              countingBackend->counts().compile_setup == 2,
          "reviewed force edit recompiles setup without preparing the mesh again");
  controller.setPatchAngle(5.0);
  (void)controller.meshSummary();
  (void)controller.surfacePatches();
  require(countingBackend->counts().prepare_mesh == 1 &&
              countingBackend->counts().group_patches == 2,
          "patch-angle edit regroups the prepared boundary without reparsing mesh bytes");
  auto blocked = reviewedDraft();
  blocked["material_reviewed"] = false;
  controller.reviewSetup(blocked);
  bool materialBlocked = false;
  for (const auto &value : controller.blockers())
    materialBlocked = materialBlocked ||
        value.toMap().value("code") == "material_unreviewed";
  require(!controller.canRun() && controller.status() == "setup_blocked" &&
              materialBlocked && controller.lastRun().isEmpty() &&
              controller.findings().isEmpty(),
          "desktop cannot bypass review or retain stale execution after setup changes");
  controller.reset();
  require(controller.status() == "mesh_required" &&
              controller.surfacePatches().isEmpty() && !controller.canRun(),
          "reset removes transient structural setup authority");

  StructuralController reviewInvalidation;
  reviewInvalidation.loadMesh(QUrl::fromLocalFile(mesh.fileName()), 0.001,
                              1.0);
  reviewInvalidation.setPatchSelected(1, "load", true);
  reviewInvalidation.setPatchSelected(2, "restraint", true);
  reviewInvalidation.reviewSetup(reviewedDraft());
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
