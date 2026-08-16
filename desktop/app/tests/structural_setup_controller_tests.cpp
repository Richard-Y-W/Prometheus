#include "structural_setup_controller.hpp"

#include "prometheus/structural/calculix_deck.hpp"
#include "prometheus/structural/structural_case.hpp"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace ps = prometheus::structural;

namespace {

[[noreturn]] void fail(const char *message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition)
    fail(message);
}

QString fixture(const QString &name) {
  return QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) +
         "/fixtures/structural/ui/" + name;
}

bool hasBlocker(const StructuralSetupController &controller,
                const QString &code) {
  for (const auto &value : controller.blockingIssues())
    if (value.toMap().value("code").toString() == code)
      return true;
  return false;
}

QVariantMap group(const StructuralSetupController &controller,
                  const QString &name) {
  for (const auto &value : controller.surfaceGroups()) {
    const auto candidate = value.toMap();
    if (candidate.value("name").toString() == name)
      return candidate;
  }
  return {};
}

std::string readBytes(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    fail("could not read exported structural case");
  return file.readAll().toStdString();
}

void copyFixture(const QString &name, const QString &directory) {
  if (!QFile::copy(fixture(name), directory + "/" + name))
    fail("could not copy structural controller fixture");
}

void writeBytes(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write(bytes) != bytes.size())
    fail("could not write structural controller fixture");
}

void candidateLoadAndTamperChecks() {
  StructuralSetupController controller;
  require(controller.loadCandidate(
              QUrl::fromLocalFile(fixture("two-group-tetra.candidate.json"))),
          "verified candidate loads");
  require(controller.sourcePath() ==
              QFileInfo(fixture("two-group-tetra.candidate.json"))
                  .absoluteFilePath() &&
              controller.geometrySha256() ==
                  "sha256:7dc1037366bd56045a9bc9489d3855ca0da178e8c8ff63d6aafb6f1d1604a3ec" &&
              controller.nodeCount() == 4 && controller.elementCount() == 1 &&
              controller.minimumMeanRatio() > 0.0,
          "candidate exposes verified identities and mesh diagnostics");
  require(controller.surfaceGroups().size() == 2 &&
              controller.meshGeometry() != nullptr &&
              controller.highlightGeometry() == nullptr,
          "complete boundary mesh is visualizable without implicit highlight");

  const auto fixed = group(controller, "FixedFaces");
  const auto loaded = group(controller, "LoadedFaces");
  require(fixed.value("triangle_count").toInt() == 1 &&
              fixed.value("area_m2").toDouble() > 0.0 &&
              fixed.value("centroid_m").toList().size() == 3 &&
              fixed.value("normal_m").toList().size() == 3 &&
              loaded.value("triangle_count").toInt() == 3,
          "surface groups expose area, centroid, normal, and triangle count");
  require(controller.restraintGroups().isEmpty() &&
              controller.loadGroups().isEmpty(),
          "candidate load creates no implicit engineering selections");
  require(hasBlocker(controller, "material_applicability_unreviewed"),
          "material applicability starts blocked");
  require(hasBlocker(controller, "restraint_surface_unselected"),
          "restraint surface starts blocked");
  require(hasBlocker(controller, "load_surface_unselected"),
          "load surface starts blocked");
  require(hasBlocker(controller, "scenario_unconfirmed"),
          "scenario starts blocked");

  controller.setActiveSurfaceGroup("LoadedFaces");
  require(controller.activeSurfaceGroup().value("name") == "LoadedFaces" &&
              controller.highlightGeometry() != nullptr,
          "active surface group has deterministic highlight geometry");

  QTemporaryDir temporary;
  require(temporary.isValid(), "temporary fixture directory exists");
  copyFixture("two-group-tetra.candidate.json", temporary.path());
  copyFixture("two-group-tetra.geo", temporary.path());
  copyFixture("two-group-tetra.inp", temporary.path());
  copyFixture("two-group-tetra.material-evidence.json", temporary.path());
  StructuralSetupController copied;
  require(copied.loadCandidate(QUrl::fromLocalFile(
              temporary.path() + "/two-group-tetra.candidate.json")),
          "identical relocated candidate verifies");
  QFile changed(temporary.path() + "/two-group-tetra.inp");
  require(changed.open(QIODevice::Append) && changed.write("\n") == 1,
          "mesh mutation fixture is written");
  changed.close();
  StructuralSetupController tampered;
  require(!tampered.loadCandidate(QUrl::fromLocalFile(
              temporary.path() + "/two-group-tetra.candidate.json")) &&
              hasBlocker(tampered, "candidate_identity_mismatch"),
          "mesh identity mismatch cannot load");

  auto traversalBytes = QByteArray::fromStdString(
      readBytes(fixture("two-group-tetra.candidate.json")));
  require(traversalBytes.contains("\"path\": \"two-group-tetra.inp\""),
          "candidate traversal mutation source exists");
  traversalBytes.replace("\"path\": \"two-group-tetra.inp\"",
                         "\"path\": \"../two-group-tetra.inp\"");
  writeBytes(temporary.path() + "/traversal.candidate.json", traversalBytes);
  StructuralSetupController traversal;
  require(!traversal.loadCandidate(QUrl::fromLocalFile(
              temporary.path() + "/traversal.candidate.json")) &&
              hasBlocker(traversal, "candidate_path_rejected"),
          "candidate artifact traversal cannot escape the manifest directory");
}

void reviewCompilationAndExport() {
  StructuralSetupController controller;
  require(controller.loadCandidate(
              QUrl::fromLocalFile(fixture("two-group-tetra.candidate.json"))),
          "review fixture loads");
  controller.setSurfaceRole("FixedFaces", "restraint", true);
  controller.setSurfaceRole("LoadedFaces", "load", true);
  const QVariantMap materialReview{
      {"designation", "2024 aluminum"},
      {"temper", "T351"},
      {"product_form", "plate"},
      {"youngs_modulus_pa", 70.0e9},
      {"poisson_ratio", 0.33},
      {"evidence_sha256",
       "sha256:08c3b87fca7875c81a7ede9b2f6deb7e4d0f835a886f693eb58810ad54dba104"},
      {"applicability", "assumed"}};
  auto forgedMaterialReview = materialReview;
  forgedMaterialReview["evidence_sha256"] =
      "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  controller.setMaterialReview(forgedMaterialReview);
  require(hasBlocker(controller, "material_evidence_identity_mismatch"),
          "typed material review cannot invent an unverified evidence hash");
  controller.setMaterialReview(materialReview);
  controller.setForce(100.0, 0.0, 0.0, -2.0);
  controller.setLimits(
      {{"displacement_limit_m", 0.001},
       {"displacement_limit_basis", "synthetic UI regression limit"},
       {"von_mises_limit_pa", 100.0e6},
       {"von_mises_limit_basis", "synthetic UI regression limit"}});
  controller.setMeshReview({{"target_size_m", 0.02},
                            {"minimum_mean_ratio_threshold", 0.05},
                            {"confirmed", true}});
  require(hasBlocker(controller, "mesh_control_identity_mismatch"),
          "mesh review cannot claim a target different from the candidate");
  controller.setMeshReview({{"target_size_m", 0.01},
                            {"minimum_mean_ratio_threshold", 0.05},
                            {"confirmed", true}});

  require(!controller.readyToExport() &&
              hasBlocker(controller, "scenario_unconfirmed"),
          "complete inputs remain blocked until scenario confirmation");
  controller.confirmScenario(true);
  require(controller.readyToExport() && controller.scenarioConfirmed(),
          "explicitly confirmed complete setup becomes exportable");
  const auto resultant = controller.compiledResultantN();
  require(resultant.size() == 3 &&
              std::abs(resultant[0].toDouble()) < 1.0e-10 &&
              std::abs(resultant[1].toDouble()) < 1.0e-10 &&
              std::abs(resultant[2].toDouble() + 100.0) < 1.0e-10 &&
              controller.selectedLoadAreaM2() > 0.0,
          "reviewed magnitude and normalized direction compile exactly");

  controller.setForce(101.0, 0.0, 0.0, -1.0);
  require(!controller.scenarioConfirmed() && !controller.readyToExport(),
          "changing reviewed force invalidates scenario confirmation");
  controller.setForce(100.0, 0.0, 0.0, -1.0);
  controller.confirmScenario(true);

  QTemporaryDir output;
  require(output.isValid() &&
              controller.exportReviewedCase(QUrl::fromLocalFile(output.path())),
          "ready reviewed case exports");
  const auto bytes = readBytes(output.path() +
                               "/reviewed-structural-case.json");
  const auto parsed = ps::parse_structural_case(bytes);
  require(parsed.request.restraint_surface_groups ==
                  std::vector<std::string>({"FixedFaces"}) &&
              parsed.request.load_surface_groups ==
                  std::vector<std::string>({"LoadedFaces"}) &&
              parsed.request.mesh_coordinate_scale_to_m == 0.001 &&
              ps::generate_calculix_deck(parsed.request) ==
                  ps::generate_calculix_deck(
                      ps::parse_structural_case(bytes).request),
          "exported canonical case reloads through Qt-free authority");

  QTemporaryDir mutableCandidate;
  require(mutableCandidate.isValid(), "mutable candidate directory exists");
  copyFixture("two-group-tetra.candidate.json", mutableCandidate.path());
  copyFixture("two-group-tetra.geo", mutableCandidate.path());
  copyFixture("two-group-tetra.inp", mutableCandidate.path());
  copyFixture("two-group-tetra.material-evidence.json",
              mutableCandidate.path());
  StructuralSetupController changedSource;
  require(changedSource.loadCandidate(QUrl::fromLocalFile(
              mutableCandidate.path() + "/two-group-tetra.candidate.json")),
          "mutable candidate initially verifies");
  changedSource.setSurfaceRole("FixedFaces", "restraint", true);
  changedSource.setSurfaceRole("LoadedFaces", "load", true);
  changedSource.setMaterialReview(materialReview);
  changedSource.setForce(100.0, 0.0, 0.0, -1.0);
  changedSource.setLimits(
      {{"displacement_limit_m", 0.001},
       {"displacement_limit_basis", "synthetic UI regression limit"}});
  changedSource.setMeshReview({{"target_size_m", 0.01},
                               {"minimum_mean_ratio_threshold", 0.05},
                               {"confirmed", true}});
  changedSource.confirmScenario(true);
  require(changedSource.readyToExport(),
          "mutable source setup is complete before mutation");
  QFile changedMesh(mutableCandidate.path() + "/two-group-tetra.inp");
  require(changedMesh.open(QIODevice::Append) &&
              changedMesh.write("\n") == 1,
          "post-review mesh mutation fixture is written");
  changedMesh.close();
  QTemporaryDir changedOutput;
  require(changedOutput.isValid() &&
              !changedSource.exportReviewedCase(
                  QUrl::fromLocalFile(changedOutput.path())) &&
              hasBlocker(changedSource, "export_identity_mismatch"),
          "source bytes changed after review cannot be exported");

  controller.setSurfaceRole("LoadedFaces", "load", false);
  require(!controller.exportReviewedCase(QUrl::fromLocalFile(output.path())),
          "controller refuses export while any blocker remains");
}

} // namespace

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  candidateLoadAndTamperChecks();
  reviewCompilationAndExport();
  return 0;
}
