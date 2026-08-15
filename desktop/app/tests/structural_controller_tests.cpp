#include "structural_controller.hpp"
#include "prometheus/structural/structural_archive.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <filesystem>

namespace {

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
          {"material_applicability", "benchmark only"},
          {"youngs_modulus_pa", 7.0e10},
          {"poisson_ratio", 0.33},
          {"material_reviewed", true},
          {"force_x_n", 0.0}, {"force_y_n", 0.0}, {"force_z_n", -100.0},
          {"load_reviewed", true}, {"restraint_reviewed", true},
          {"displacement_limit_m", 0.001}, {"von_mises_limit_pa", 1.0e8},
          {"requirement_rationale", "explicit exploratory desktop test"},
          {"requirement_reviewed", true},
          {"mesh_minimum_size_m", 0.001}, {"mesh_maximum_size_m", 0.003},
          {"mesher_identity", "fixture mesher 1.0"},
          {"mesh_controls_reviewed", true},
          {"scenario_description", "bounded desktop structural preview"},
          {"scenario_confirmed", true}};
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
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

  StructuralController controller;
  controller.loadMesh(QUrl::fromLocalFile(mesh.fileName()), 0.001, 1.0);
  require(controller.status() == "setup_blocked" &&
              controller.meshSummary().value("nodes").toInt() == 4 &&
              controller.meshSummary().value("elements").toInt() == 1 &&
              controller.meshSummary().value("surface_patches").toInt() == 4 &&
              controller.surfacePatches().size() == 4 && !controller.canRun(),
          "desktop adapter exposes mesh and conservative surface patch statistics");
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
              controller.lastRun().value("displacement_rows").toInt() == 1 &&
              controller.findings().size() == 2 &&
              controller.lastRun().value("archived").toBool() &&
              !controller.lastRun().value("archive_manifest").toString().isEmpty(),
          "desktop executes asynchronously, archives, and exposes scoped findings");
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
  QFile changed(QDir(controller.lastRun().value("output_directory").toString())
                    .filePath("prometheus_structural_run.dat"));
  require(changed.open(QIODevice::Append), "archived DAT opens for corruption test");
  changed.write("changed\n");
  changed.close();
  require(!prometheus::structural::verify_structural_archive(
               std::filesystem::path(archivePath.toStdWString())).valid,
          "changed raw solver output invalidates offline archive verification");
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
  return 0;
}
