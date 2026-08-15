#include "project_intake.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fail(const char *message) {
  std::cerr << "FAILED: " << message << '\n';
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition)
    fail(message);
}

void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "test file opens for writing");
  require(file.write(bytes) == bytes.size(), "test file writes completely");
}

QString digest(const QByteArray &bytes) {
  return "sha256:" + QString::fromLatin1(
                         QCryptographicHash::hash(bytes,
                                                 QCryptographicHash::Sha256)
                             .toHex());
}

QVariantMap artifact(const ProjectIntakeResult &result,
                     const QString &relativePath) {
  for (const auto &value : result.artifacts) {
    const auto candidate = value.toMap();
    if (candidate.value("relative_path").toString() == relativePath)
      return candidate;
  }
  return {};
}

void accountsForEveryFile() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "temporary project folder exists");
  require(QDir(temporary.path()).mkpath("docs"),
          "nested project directory creates");

  const QByteArray stepBytes("ISO-10303-21;\nEND-ISO-10303-21;\n");
  const QByteArray pdfBytes("%PDF-1.7\nfixture\n");
  const QByteArray noteBytes("mass is not specified\n");
  const QByteArray rawBytes("\x00\x01\x02", 3);
  writeFile(temporary.filePath("assembly.step"), stepBytes);
  writeFile(temporary.filePath("docs/spec.pdf"), pdfBytes);
  writeFile(temporary.filePath(".notes.txt"), noteBytes);
  writeFile(temporary.filePath("raw.bin"), rawBytes);

  const auto result = scanProjectFolder(temporary.path());
  require(result.ok, "project folder scans");
  require(result.artifacts.size() == 4, "every file is accounted for");
  require(result.primary_step_path == temporary.filePath("assembly.step"),
          "one STEP file is selected without guessing");

  QString previous;
  for (const auto &value : result.artifacts) {
    const auto row = value.toMap();
    const auto relative = row.value("relative_path").toString();
    require(previous.isEmpty() || previous < relative,
            "artifact rows have deterministic relative-path order");
    previous = relative;
  }

  const auto step = artifact(result, "assembly.step");
  require(!step.isEmpty(), "STEP artifact remains visible");
  require(step.value("byte_size").toLongLong() == stepBytes.size(),
          "STEP byte count is exact");
  require(step.value("sha256").toString() == digest(stepBytes),
          "STEP digest covers exact bytes");
  require(step.value("category") == "geometry" &&
              step.value("analysis_state") == "ready" &&
              step.value("loadable").toBool(),
          "STEP is the sole ready format");

  const auto pdf = artifact(result, "docs/spec.pdf");
  require(pdf.value("category") == "document" &&
              pdf.value("analysis_state") == "not_evaluated" &&
              !pdf.value("detail").toString().isEmpty(),
          "recognized document stays visible and unevaluated");
  require(pdf.value("sha256").toString() == digest(pdfBytes),
          "unevaluated readable file is still hashed");

  const auto note = artifact(result, ".notes.txt");
  require(note.value("analysis_state") == "not_evaluated",
          "hidden file is accounted for");

  const auto raw = artifact(result, "raw.bin");
  require(raw.value("category") == "other" &&
              raw.value("analysis_state") == "unsupported" &&
              raw.value("sha256").toString() == digest(rawBytes),
          "unsupported readable file remains hashed and visible");
}

void handlesEmptyInvalidAndAmbiguousFolders() {
  QTemporaryDir empty;
  require(empty.isValid(), "empty project folder exists");
  const auto emptyResult = scanProjectFolder(empty.path());
  require(emptyResult.ok && emptyResult.artifacts.isEmpty() &&
              emptyResult.primary_step_path.isEmpty(),
          "empty folder is a successful zero-file inventory");

  const auto missing = scanProjectFolder(empty.filePath("missing"));
  require(!missing.ok && !missing.error.isEmpty(),
          "missing folder fails explicitly");

  writeFile(empty.filePath("first.step"), "first");
  writeFile(empty.filePath("second.stp"), "second");
  const auto ambiguous = scanProjectFolder(empty.path());
  require(ambiguous.ok && ambiguous.artifacts.size() == 2 &&
              ambiguous.primary_step_path.isEmpty(),
          "multiple STEP files require a user choice");
}

void identifiesExactDuplicateCopies() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "duplicate project folder exists");
  require(QDir(temporary.path()).mkpath("vendor"),
          "duplicate subdirectory creates");
  const QByteArray shared("same exact artifact bytes\n");
  writeFile(temporary.filePath("datasheet.pdf"), shared);
  writeFile(temporary.filePath("vendor/datasheet-copy.pdf"), shared);
  writeFile(temporary.filePath("different.pdf"), "different bytes\n");

  const auto result = scanProjectFolder(temporary.path());
  const auto first = artifact(result, "datasheet.pdf");
  const auto copy = artifact(result, "vendor/datasheet-copy.pdf");
  const auto different = artifact(result, "different.pdf");
  require(first.value("identical_file_count").toInt() == 2 &&
              !first.value("duplicate_copy").toBool(),
          "first deterministic occurrence represents the duplicate group");
  require(copy.value("identical_file_count").toInt() == 2 &&
              copy.value("duplicate_copy").toBool(),
          "later identical artifact is visibly a duplicate copy");
  require(different.value("identical_file_count").toInt() == 1 &&
              !different.value("duplicate_copy").toBool(),
          "unique artifact is not labeled duplicate");
}

void exposesOnlyHashMatchedCandidateEvidence() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "candidate project folder exists");
  const QByteArray manual("review me\n");
  writeFile(temporary.filePath("manual.pdf"), manual);
  const auto manualHash = digest(manual).mid(7);
  const auto manifest = QString(R"({
    "schema":"urn:prometheus:trial-source-manifest:0.1.0",
    "status":"candidate_evidence_only",
    "component":{"manufacturer":"DAMIAO","part_number":"DM-J4310-2EC V1.1","relationship":"joint candidate","source_file":"manual.pdf","source_sha256":"%1"},
    "candidate_claims":[{"id":"rated_torque","label":"Rated torque","quantity":"torque","original_value":"3","original_unit":"N m","value_si":3.0,"si_unit":"N m","source_page":9}],
    "review":{"published_component":false,"geometry_binding_confirmed":false,"specification_claims_reviewed":false}
  })").arg(manualHash).toUtf8();
  writeFile(temporary.filePath("prometheus-trial-source-manifest.json"),
            manifest);

  const auto accepted = scanProjectFolder(temporary.path());
  require(accepted.candidate_components.size() == 1,
          "hash-matched candidate source is exposed");
  const auto candidate = accepted.candidate_components.front().toMap();
  require(candidate.value("manufacturer") == "DAMIAO" &&
              candidate.value("part_number") == "DM-J4310-2EC V1.1" &&
              candidate.value("review_state") == "candidate_evidence_only",
          "candidate identity remains explicitly unreviewed");
  const auto claims = candidate.value("candidate_claims").toList();
  require(claims.size() == 1 &&
              claims.front().toMap().value("id") == "rated_torque" &&
              claims.front().toMap().value("review_state") == "unreviewed" &&
              claims.front().toMap().value("source_page") == 9,
          "source-located candidate claim remains unreviewed");

  ProjectIntakeController controller;
  QSignalSpy finished(&controller, &ProjectIntakeController::scanFinished);
  controller.scanFolder(QUrl::fromLocalFile(temporary.path()));
  require(finished.wait(5000) && finished.takeFirst().front().toBool(),
          "controller loads candidate claims");
  const auto candidateId =
      controller.candidateComponents().front().toMap().value("id").toString();
  controller.reviewCandidateClaim(candidateId, "rated_torque", "accepted");
  auto reviewedClaims = controller.candidateComponents()
                            .front().toMap()
                            .value("candidate_claims").toList();
  require(reviewedClaims.front().toMap().value("review_state") == "accepted",
          "explicit session review accepts one candidate claim");
  controller.reviewCandidateClaim(candidateId, "rated_torque", "rejected");
  reviewedClaims = controller.candidateComponents()
                       .front().toMap()
                       .value("candidate_claims").toList();
  require(reviewedClaims.front().toMap().value("review_state") == "rejected",
          "explicit session review can reject the candidate claim");
  controller.reviewCandidateClaim(candidateId, "rated_torque", "invalid");
  require(controller.candidateComponents()
              .front().toMap().value("candidate_claims").toList()
              .front().toMap().value("review_state") == "rejected",
          "unknown review decision cannot change state");

  writeFile(temporary.filePath("manual.pdf"), "changed\n");
  const auto rejected = scanProjectFolder(temporary.path());
  require(rejected.candidate_components.isEmpty(),
          "changed source cannot satisfy the candidate manifest");
  require(artifact(rejected, "prometheus-trial-source-manifest.json")
              .value("detail").toString().contains("wrong hash"),
          "source mismatch remains visible");
}

void controllerPublishesOnlySuccessfulInventory() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "controller project folder exists");
  writeFile(temporary.filePath("assembly.step"), "controller step");

  ProjectIntakeController controller;
  QSignalSpy finished(&controller, &ProjectIntakeController::scanFinished);
  controller.scanFolder(QUrl::fromLocalFile(temporary.path()));
  require(controller.busy(), "controller scans away from the UI boundary");
  require(finished.wait(5000), "controller scan finishes");
  require(finished.takeFirst().front().toBool(),
          "controller reports successful scan");
  require(!controller.busy() && controller.totalCount() == 1 &&
              controller.readyCount() == 1 &&
              controller.notEvaluatedCount() == 0 &&
              controller.unsupportedCount() == 0 &&
              controller.unreadableCount() == 0 &&
              controller.duplicateCopyCount() == 0 &&
              controller.status() == "1 file accounted for",
          "controller publishes inventory summary");

  QSignalSpy failed(&controller, &ProjectIntakeController::scanFinished);
  controller.scanFolder(
      QUrl::fromLocalFile(temporary.filePath("missing-directory")));
  require(failed.wait(5000), "invalid controller scan finishes");
  require(!failed.takeFirst().front().toBool() &&
              controller.totalCount() == 1 && !controller.error().isEmpty(),
          "failed scan preserves last successful inventory");
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  accountsForEveryFile();
  handlesEmptyInvalidAndAmbiguousFolders();
  identifiesExactDuplicateCopies();
  exposesOnlyHashMatchedCandidateEvidence();
  controllerPublishesOnlySuccessfulInventory();
  return 0;
}
