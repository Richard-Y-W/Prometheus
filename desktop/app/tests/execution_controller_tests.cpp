#include "cad_controller.hpp"
#include "engineering_controller.hpp"
#include "execution_controller.hpp"
#include "project_controller.hpp"

#include <prometheus/execution/contracts.hpp>
#include <prometheus/execution/package_consumer.hpp>
#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/object_store.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace integrity = prometheus::integrity;
namespace execution_contract = prometheus::execution;
namespace run_store = prometheus::run_store;

[[noreturn]] void fail(const char *message) {
  qCritical("FAILED: %s", message);
  std::exit(1);
}

void require(const bool condition, const char *message) {
  if (!condition) {
    fail(message);
  }
}

std::string utf8(const QString &value) {
  const auto bytes = value.toUtf8();
  return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

std::filesystem::path nativePath(const QString &value) {
#ifdef _WIN32
  return std::filesystem::path(value.toStdWString());
#else
  return std::filesystem::path(utf8(value));
#endif
}

QByteArray readBytes(const QString &path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "fixture opens");
  return file.readAll();
}

void writeBytes(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "test artifact opens for writing");
  require(file.write(bytes) == bytes.size(), "test artifact write completes");
}

QString objectHash(const QByteArray &bytes) {
  return QString::fromStdString(integrity::sha256_bytes(std::string_view(
      bytes.constData(), static_cast<std::size_t>(bytes.size()))));
}

bool waitUntil(const std::function<bool()> &predicate,
               const int timeoutMilliseconds = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return predicate();
}

run_store::ProjectV2 projectFixture(const QString &cadPath,
                                    const QString &cadHash) {
  run_store::ProjectV2 project;
  project.name = "Reviewed motor arm";
  project.cad_source = utf8(cadPath);
  project.assembly_artifact_hash = utf8(cadHash);
  project.coordinate_system = "right-handed Z-up";
  project.length_unit = "m";
  project.component_bindings.push_back({"motor", "evidence-revision", "Motor"});
  project.engineering.geometry_status = "completed";
  project.engineering.geometry_findings.push_back(
      {"static_interference", "information", "information", "Geometry retained",
       "Independent geometry evidence", 0.0, "m³", 0.0, 0.0, "geometry fixture",
       "", "", "motor", "arm"});
  return project;
}

QVariantMap acceptedScenarioDraft() {
  return {
      {"payload_mass_kg", 8.0},        {"arm_radius_m", 0.2},
      {"rotation_degrees", 90.0},      {"move_duration_s", 1.2},
      {"hold_duration_s", 4.0},        {"cycle_duration_s", 10.0},
      {"ambient_temperature_c", 35.0},
  };
}

QByteArray invalidReadyPackage(const QByteArray &source) {
  auto document = QJsonDocument::fromJson(source);
  require(document.isObject(), "ready package fixture parses");
  auto root = document.object();
  auto claims = root.value("claims").toArray();
  bool changed = false;
  QString changedClaimId;
  QString changedFingerprint;
  for (qsizetype index = 0; index < claims.size(); ++index) {
    auto claim = claims.at(index).toObject();
    if (claim.value("claim_id") != "14000000-0000-4000-8000-000000000001") {
      continue;
    }
    auto value = claim.value("value").toObject();
    value["value"] = -1.0;
    claim["value"] = value;
    QStringList evidenceIdStrings;
    for (const auto &item : claim.value("evidence_ids").toArray()) {
      evidenceIdStrings.append(item.toString());
    }
    std::sort(evidenceIdStrings.begin(), evidenceIdStrings.end());
    QJsonArray evidenceIds;
    for (const auto &item : evidenceIdStrings) {
      evidenceIds.append(item);
    }
    QJsonObject semantic{
        {"revision_id", claim.value("revision_id")},
        {"slot_id", claim.value("slot_id")},
        {"value_state", claim.value("value_state")},
        {"value", claim.value("value")},
        {"provenance", claim.value("provenance")},
        {"evidence_ids", evidenceIds},
        {"validity_conditions", claim.value("validity_conditions")},
        {"unit", claim.value("unit")},
        {"original_value", claim.value("original_value")},
        {"original_unit", claim.value("original_unit")},
    };
    const auto semanticSource =
        QJsonDocument(semantic).toJson(QJsonDocument::Compact);
    const auto semanticCanonical = integrity::canonicalize_json_bytes(
        std::string_view(semanticSource.constData(),
                         static_cast<std::size_t>(semanticSource.size())));
    changedFingerprint =
        QString::fromStdString(integrity::object_hash(semanticCanonical));
    changedClaimId = claim.value("claim_id").toString();
    claim["claim_fingerprint"] = changedFingerprint;
    claims[index] = claim;
    changed = true;
  }
  require(changed, "ready package calculation claim was found");
  root["claims"] = claims;
  auto reviews = root.value("claim_reviews").toArray();
  bool reviewChanged = false;
  for (qsizetype index = 0; index < reviews.size(); ++index) {
    auto review = reviews.at(index).toObject();
    if (review.value("claim_id").toString() != changedClaimId) {
      continue;
    }
    review["reviewed_claim_fingerprint"] = changedFingerprint;
    reviews[index] = review;
    reviewChanged = true;
  }
  require(reviewChanged, "ready package claim review was found");
  root["claim_reviews"] = reviews;
  const auto sourceBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
  const auto canonical = integrity::canonicalize_json_bytes(std::string_view(
      sourceBytes.constData(), static_cast<std::size_t>(sourceBytes.size())));
  return QByteArray(canonical.data(), static_cast<qsizetype>(canonical.size()));
}

execution_contract::CanonicalObject requestFor(const QString &packageHash,
                                               const QString &scenarioHash,
                                               const QString &assemblyHash) {
  std::vector<std::string> obligations;
  obligations.reserve(execution_contract::motor_arm_obligation_ids.size());
  for (const auto obligation : execution_contract::motor_arm_obligation_ids) {
    obligations.emplace_back(obligation);
  }
  const auto result = execution_contract::build_analysis_request(
      execution_contract::AnalysisRequestDraft{
          utf8(packageHash), utf8(scenarioHash), utf8(assemblyHash), "motor",
          std::string(execution_contract::motor_arm_backend_id),
          std::string(execution_contract::motor_arm_backend_contract_version),
          std::string(
              execution_contract::supported_motor_consumer_contract_hash()),
          std::move(obligations)});
  require(result.has_value(), "test request compiles");
  return result.value();
}

QString pathText(const std::filesystem::path &path) {
#ifdef _WIN32
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

run_store::StoredObjectReference
referenceFor(const QByteArray &objectBytes, const std::string &mediaType,
             const std::string &schemaId, const std::string &schemaVersion) {
  return {utf8(objectHash(objectBytes)),
          static_cast<std::uint64_t>(objectBytes.size()), mediaType, schemaId,
          schemaVersion};
}

QJsonObject referenceJson(const run_store::StoredObjectReference &reference) {
  return {
      {"object_hash", QString::fromStdString(reference.object_hash)},
      {"byte_length", static_cast<qint64>(reference.byte_length)},
      {"media_type", QString::fromStdString(reference.media_type)},
      {"schema_id", QString::fromStdString(reference.schema_id)},
      {"schema_version", QString::fromStdString(reference.schema_version)},
  };
}

run_store::StoredObjectReference referenceFromJson(const QJsonObject &value) {
  return {
      utf8(value.value("object_hash").toString()),
      static_cast<std::uint64_t>(value.value("byte_length").toInteger()),
      utf8(value.value("media_type").toString()),
      utf8(value.value("schema_id").toString()),
      utf8(value.value("schema_version").toString()),
  };
}

QByteArray canonicalObject(const QJsonObject &value) {
  const auto source = QJsonDocument(value).toJson(QJsonDocument::Compact);
  const auto canonical = integrity::canonicalize_json_bytes(std::string_view(
      source.constData(), static_cast<std::size_t>(source.size())));
  return QByteArray(canonical.data(), static_cast<qsizetype>(canonical.size()));
}

void replaceProjectIndex(const QString &path,
                         const run_store::ProjectV2 &project) {
  const auto serialized = run_store::serialize_project_v2(project);
  require(serialized.has_value(), "variant project index serializes");
  writeBytes(path,
             QByteArray(serialized.value().data(),
                        static_cast<qsizetype>(serialized.value().size())));
}

void installVariantObject(const QString &projectPath,
                          const run_store::StoredObjectReference &reference,
                          const QByteArray &objectBytes) {
  const auto stored = run_store::install_object(
      nativePath(projectPath), reference,
      std::string_view(objectBytes.constData(),
                       static_cast<std::size_t>(objectBytes.size())));
  require(stored.has_value(), "variant immutable object installs");
}

void requireRecorded(const QVariantList &history, const int index,
                     const QString &packageHash) {
  require(index >= 0 && index < history.size(), "history index exists");
  const auto item = history.at(index).toMap();
  require(item.value("status") == "Recorded", "run is recorded, not rerun");
  require(item.value("package_hash") == packageHash,
          "recorded run retains exact package identity");
  require(!item.value("manifest_hash").toString().isEmpty(),
          "recorded run exposes manifest identity");
  require(!item.value("result_hash").toString().isEmpty(),
          "recorded run exposes result identity");
}

void fullMotorAAndBWorkflow() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "temporary project root exists");
  const auto cadPath = temporary.filePath("arm.step");
  const QByteArray cadBytes("exact motor-arm CAD bytes");
  writeBytes(cadPath, cadBytes);
  const auto projectPath = temporary.filePath("arm.prometheus");
  const auto initial = projectFixture(cadPath, objectHash(cadBytes));
  require(run_store::create_project_v2(nativePath(projectPath), initial)
              .has_value(),
          "v2 project fixture creates");

  CadController cad;
  EngineeringController geometry;
  ProjectController project(&cad, &geometry);
  ExecutionController execution(&project);
  project.openProject(QUrl::fromLocalFile(projectPath));
  require(project.errorCode().isEmpty(), "v2 project opens for execution");
  require(execution.runHistory().isEmpty(), "new project has no run history");

  const QString contracts =
      QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) + "/fixtures/contracts/";
  const auto motorA =
      readBytes(contracts + "execution-component-v2.motor-a.jcs");
  const auto motorB =
      readBytes(contracts + "execution-component-v2.motor-b.jcs");
  const auto motorAHash = objectHash(motorA);
  const auto motorBHash = objectHash(motorB);

  execution.setPendingCadEntityId("motor");
  execution.acceptExactPackage(motorA, motorAHash);
  require(execution.errorCode().isEmpty(), "Motor A exact package binds");
  require(execution.activePackage().value("package_hash") == motorAHash,
          "active package reports Motor A hash");
  require(execution.activePackage().value("execution_readiness") == "ready",
          "Motor A reports reviewed execution readiness");

  execution.setScenarioDraft(acceptedScenarioDraft());
  execution.previewScenarioDegrees();
  require(execution.errorCode().isEmpty(), "scenario preview succeeds");
  const auto preview = execution.scenarioPreview();
  require(std::abs(preview.value("rotation_rad").toDouble() -
                   1.5707963267948966) < 1.0e-15,
          "preview exposes authoritative radians");
  require(preview.value("rotation_unit") == "rad",
          "preview exposes exact rotation unit");
  execution.confirmScenario(
      "Evaluate the bound motor for the reviewed motor-arm operating cycle.");
  require(execution.scenarioConfirmed(),
          "scenario requires explicit confirmation");
  const auto scenarioHash = execution.confirmedScenarioHash();
  require(!scenarioHash.isEmpty(), "confirmed scenario has immutable identity");
  require(execution.canRun(), "confirmed Motor A workflow can run");

  execution.runAnalysis();
  require(waitUntil([&execution] { return !execution.busy(); }),
          "Motor A execution completes");
  require(execution.errorCode().isEmpty(), "Motor A execution publishes");
  require(execution.status() == "completed", "Motor A has completed status");
  require(execution.runHistory().size() == 1, "Motor A adds one committed run");
  requireRecorded(execution.runHistory(), 0, motorAHash);
  execution.runAnalysis();
  require(waitUntil([&execution] { return !execution.busy(); }) &&
              execution.errorCode().isEmpty() &&
              execution.runHistory().size() == 1,
          "identical rerun is idempotent and adds no duplicate history row");
  const auto geometryBefore = geometry.findings();

  execution.acceptExactPackage(motorB, motorBHash);
  require(execution.errorCode().isEmpty(), "Motor B exact package binds");
  require(execution.scenarioConfirmed(),
          "package switch preserves unchanged confirmed scenario");
  require(execution.confirmedScenarioHash() == scenarioHash,
          "package switch reuses exact scenario bytes");
  require(geometry.findings() == geometryBefore,
          "motor package changes do not alter geometry findings");
  execution.runAnalysis();
  require(waitUntil([&execution] { return !execution.busy(); }),
          "Motor B execution completes");
  require(execution.errorCode().isEmpty(), "Motor B execution publishes");
  require(execution.runHistory().size() == 2,
          "Motor B appends without replacing Motor A");
  requireRecorded(execution.runHistory(), 0, motorAHash);
  requireRecorded(execution.runHistory(), 1, motorBHash);

  CadController reopenedCad;
  EngineeringController reopenedGeometry;
  ProjectController reopenedProject(&reopenedCad, &reopenedGeometry);
  ExecutionController reopenedExecution(&reopenedProject);
  reopenedProject.openProject(QUrl::fromLocalFile(projectPath));
  require(reopenedExecution.runHistory().size() == 2,
          "offline reopen loads both recorded runs without execution");
  requireRecorded(reopenedExecution.runHistory(), 0, motorAHash);
  requireRecorded(reopenedExecution.runHistory(), 1, motorBHash);
  require(reopenedGeometry.findings() == geometryBefore,
          "offline reopen keeps geometry evidence separate");

  for (int index = 0; index < 2; ++index) {
    reopenedExecution.selectRun(index);
    require(reopenedExecution.selectedResult().value("execution_disposition") ==
                "completed",
            "recorded result displays before replay");
    require(reopenedExecution.replayState() == "Recorded",
            "displaying a recorded result does not imply reproduction");
    reopenedExecution.replaySelected();
    require(
        waitUntil([&reopenedExecution] { return !reopenedExecution.busy(); }),
        "offline replay completes");
    require(reopenedExecution.replayState() == "Exact match",
            "offline replay exactly matches recorded bytes");
  }

  std::error_code cadRemovalError;
  require(std::filesystem::remove(nativePath(cadPath), cadRemovalError) &&
              !cadRemovalError,
          "CAD can be removed for degraded-history test");
  CadController missingCad;
  EngineeringController missingGeometry;
  ProjectController missingProject(&missingCad, &missingGeometry);
  ExecutionController missingExecution(&missingProject);
  missingProject.openProject(QUrl::fromLocalFile(projectPath));
  require(missingProject.errorCode() == "cad_missing" &&
              missingExecution.runHistory().size() == 2 &&
              missingExecution.runHistory().front().toMap().value("status") ==
                  "Recorded",
          "missing CAD leaves verified recorded history visible");
  missingExecution.selectRun(0);
  missingExecution.replaySelected();
  require(waitUntil([&missingExecution] { return !missingExecution.busy(); }),
          "missing-CAD replay terminates");
  require(missingExecution.replayState() == "Reproduction failed" &&
              missingExecution.errorCode() == "assembly_missing",
          "missing CAD blocks reproduction without hiding the record");
  writeBytes(cadPath, cadBytes);

  const auto firstResultHash = reopenedExecution.runHistory()
                                   .front()
                                   .toMap()
                                   .value("result_hash")
                                   .toString();
  const auto resultObject = run_store::object_path_for_hash(
      run_store::sidecar_path_for_project(nativePath(projectPath)),
      utf8(firstResultHash));
  require(resultObject.has_value(), "recorded result object path resolves");
  std::error_code removalError;
  require(std::filesystem::remove(resultObject.value(), removalError) &&
              !removalError,
          "temporary recorded result can be removed for failure test");
  reopenedExecution.reloadProject();
  require(reopenedExecution.runHistory().size() == 2,
          "missing result does not hide run history");
  require(reopenedExecution.runHistory().front().toMap().value("status") ==
              "Reproduction failed",
          "missing immutable result is never shown as recorded success");
  require(reopenedExecution.runHistory().back().toMap().value("status") ==
              "Recorded",
          "one damaged run does not hide an independent recorded run");
  reopenedExecution.selectRun(0);
  require(reopenedExecution.selectedResult().isEmpty(),
          "missing result cannot be displayed as verified output");
  reopenedExecution.replaySelected();
  require(waitUntil([&reopenedExecution] { return !reopenedExecution.busy(); }),
          "missing-result replay terminates");
  require(reopenedExecution.replayState() == "Reproduction failed" &&
              !reopenedExecution.errorCode().isEmpty(),
          "missing-result replay exposes a typed failure");
}

void failClosedPreconditionsAndCancellation() {
  const QString contracts =
      QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) + "/fixtures/contracts/";
  const auto motorA =
      readBytes(contracts + "execution-component-v2.motor-a.jcs");
  const auto blockedMotor =
      readBytes(contracts + "execution-component-v2.pm-36-gm.jcs");
  const auto invalidMotor = invalidReadyPackage(motorA);
  const auto motorAHash = objectHash(motorA);
  const auto blockedHash = objectHash(blockedMotor);

  CadController unsavedCad;
  EngineeringController unsavedGeometry;
  ProjectController unsavedProject(&unsavedCad, &unsavedGeometry);
  ExecutionController unsavedExecution(&unsavedProject);
  unsavedExecution.setPendingCadEntityId("motor");
  unsavedExecution.acceptExactPackage(motorA, motorAHash);
  require(unsavedExecution.errorCode() == "save_as_required",
          "execution state cannot mutate an unsaved or v1 project");

  QTemporaryDir temporary;
  require(temporary.isValid(), "precondition test root exists");
  const auto cadPath = temporary.filePath("arm.step");
  const QByteArray cadBytes("exact motor-arm CAD bytes");
  writeBytes(cadPath, cadBytes);
  const auto projectPath = temporary.filePath("arm.prometheus");
  require(run_store::create_project_v2(
              nativePath(projectPath),
              projectFixture(cadPath, objectHash(cadBytes)))
              .has_value(),
          "precondition project creates");

  CadController cad;
  EngineeringController geometry;
  ProjectController project(&cad, &geometry);
  ExecutionController execution(&project);
  project.openProject(QUrl::fromLocalFile(projectPath));
  const auto geometryBefore = geometry.findings();

  execution.runAnalysis();
  require(execution.errorCode() == "package_missing" &&
              project.committedRunCount() == 0,
          "no-package run is typed and noncommitting");

  execution.setPendingCadEntityId("not-a-real-part");
  execution.acceptExactPackage(motorA, motorAHash);
  require(execution.errorCode() == "cad_entity_not_found" &&
              project.project()->execution.package_bindings.empty(),
          "wrong CAD entity cannot receive a package binding");

  execution.setPendingCadEntityId("motor");
  execution.acceptExactPackage(
      motorA,
      "sha256:"
      "0000000000000000000000000000000000000000000000000000000000000000");
  require(!execution.errorCode().isEmpty() &&
              project.project()->execution.package_bindings.empty(),
          "package hash disagreement cannot create a binding");
  execution.acceptExactPackage(motorA, motorAHash);
  require(execution.errorCode().isEmpty(),
          "valid package binds after rejection");

  execution.runAnalysis();
  require(execution.errorCode() == "scenario_unconfirmed" &&
              project.committedRunCount() == 0,
          "unconfirmed scenario cannot run");
  auto invalidDraft = acceptedScenarioDraft();
  invalidDraft["move_duration_s"] = 0.0;
  execution.setScenarioDraft(invalidDraft);
  execution.previewScenarioDegrees();
  require(!execution.errorCode().isEmpty() && !execution.scenarioConfirmed(),
          "invalid scenario remains unconfirmed");
  execution.setScenarioDraft(acceptedScenarioDraft());
  execution.previewScenarioDegrees();
  execution.confirmScenario("");
  require(!execution.errorCode().isEmpty() && !execution.scenarioConfirmed(),
          "empty review intent cannot confirm scenario bytes");
  execution.confirmScenario(
      "Evaluate the bound motor for the reviewed motor-arm operating cycle.");
  require(execution.scenarioConfirmed(),
          "valid review intent confirms scenario");

  writeBytes(cadPath, QByteArray("tampered motor-arm CAD byt"));
  execution.runAnalysis();
  require(execution.errorCode() == "assembly_artifact_changed" &&
              project.committedRunCount() == 0,
          "stale assembly hash blocks execution without a commit");
  writeBytes(cadPath, cadBytes);

  execution.acceptExactPackage(blockedMotor, blockedHash);
  require(
      execution.errorCode().isEmpty() &&
          execution.activePackage().value("execution_readiness") == "blocked" &&
          execution.activePackage().value("blocked_reason") ==
              "Program 01A has no v2 package consumer or solver execution." &&
          !execution.canRun(),
      "valid blocked package exposes its gate reason but cannot run");
  execution.runAnalysis();
  require(execution.errorCode() == "package_blocked" &&
              project.committedRunCount() == 0,
          "blocked package cannot create a run");
  require(project.project()->execution.package_bindings.size() == 2 &&
              project.project()
                      ->execution.package_bindings.back()
                      .supersedes_binding_revision ==
                  project.project()
                      ->execution.package_bindings.front()
                      .binding_revision,
          "package replacement records a superseding revision");
  require(execution.scenarioConfirmed(),
          "package changes preserve an unchanged confirmed scenario");

  execution.acceptExactPackage(motorA, motorAHash);
  require(execution.canRun(), "ready package restores runnable state");

  execution.acceptExactPackage(invalidMotor, objectHash(invalidMotor));
  require(execution.errorCode().isEmpty() && execution.canRun(),
          "structurally valid ready package reaches authoritative consumer");
  execution.runAnalysis();
  require(waitUntil([&execution] { return !execution.busy(); }),
          "invalid typed calculation input terminates");
  require(execution.errorCode() == "invalid_value_domain" &&
              execution.status() == "failed" &&
              execution.runHistory().isEmpty(),
          "authoritative input failure is typed and never recorded");

  execution.acceptExactPackage(motorA, motorAHash);
  const auto poisonedRequest = requestFor(
      motorAHash, execution.confirmedScenarioHash(),
      QString::fromStdString(project.project()->assembly_artifact_hash));
  const auto poisonedPath = run_store::object_path_for_hash(
      run_store::sidecar_path_for_project(nativePath(projectPath)),
      poisonedRequest.object_hash);
  require(poisonedPath.has_value(), "request object path resolves");
  std::error_code poisonDirectoryError;
  std::filesystem::create_directories(poisonedPath.value().parent_path(),
                                      poisonDirectoryError);
  require(!poisonDirectoryError, "poisoned object shard directory creates");
  writeBytes(pathText(poisonedPath.value()), "not the request bytes");
  execution.runAnalysis();
  require(waitUntil([&execution] { return !execution.busy(); }),
          "object-store publication failure terminates");
  require(!execution.errorCode().isEmpty() && execution.status() == "failed" &&
              execution.runHistory().isEmpty() &&
              execution.selectedResult().isEmpty(),
          "calculated bytes stay hidden when transactional publication fails");
  std::error_code poisonRemovalError;
  require(std::filesystem::remove(poisonedPath.value(), poisonRemovalError) &&
              !poisonRemovalError,
          "poisoned unreferenced test object is removed");

  execution.runAnalysis();
  require(execution.busy(), "execution enters worker state");
  execution.cancelExecution();
  require(execution.busy() && execution.status() == "cancelling",
          "cancellation waits for the worker to acknowledge the stop");
  require(waitUntil([&execution] { return !execution.busy(); }) &&
              execution.errorCode() == "operation_cancelled" &&
              execution.status() == "cancelled",
          "acknowledged cancellation has a typed terminal state");
  const auto afterCancel = run_store::open_read_only(nativePath(projectPath));
  require(afterCancel.has_value() &&
              afterCancel.value().execution.committed_runs.empty(),
          "cancellation before commit publishes no run reference");

  execution.runAnalysis();
  execution.runAnalysis();
  require(execution.errorCode() == "execution_busy",
          "double run click is rejected while one worker owns execution");
  require(waitUntil([&execution] { return !execution.busy(); }),
          "single surviving execution completes");
  require(execution.errorCode().isEmpty() && execution.runHistory().size() == 1,
          "double click produces exactly one committed run");
  const auto firstScenarioHash = execution.confirmedScenarioHash();
  auto replacementDraft = acceptedScenarioDraft();
  replacementDraft["payload_mass_kg"] = 9.0;
  execution.setScenarioDraft(replacementDraft);
  require(!execution.scenarioConfirmed() && execution.runHistory().size() == 1,
          "scenario editing clears confirmation without changing history");
  execution.previewScenarioDegrees();
  execution.confirmScenario(
      "Evaluate a second explicitly reviewed operating scenario.");
  require(execution.scenarioConfirmed() &&
              execution.confirmedScenarioHash() != firstScenarioHash &&
              execution.runHistory().size() == 1,
          "scenario switch installs new bytes and preserves prior runs");
  const auto missingScenarioReference =
      *project.project()->execution.current_scenario;
  const auto missingScenarioBytes =
      run_store::read_object(nativePath(projectPath), missingScenarioReference);
  require(missingScenarioBytes.has_value(),
          "current scenario object loads before failure injection");
  const auto missingScenarioPath = run_store::object_path_for_hash(
      run_store::sidecar_path_for_project(nativePath(projectPath)),
      missingScenarioReference.object_hash);
  require(missingScenarioPath.has_value(),
          "current scenario object path resolves");
  std::error_code scenarioRemovalError;
  require(std::filesystem::remove(missingScenarioPath.value(),
                                  scenarioRemovalError) &&
              !scenarioRemovalError,
          "unreferenced current scenario can be removed for failure test");
  execution.reloadProject();
  require(execution.errorCode() == "object_read_failed" &&
              !execution.scenarioConfirmed() &&
              execution.runHistory().size() == 1,
          "missing current scenario preserves its exact store failure and "
          "prior history");
  execution.runAnalysis();
  require(execution.errorCode() == "object_read_failed" &&
              project.committedRunCount() == 1,
          "missing current scenario stays typed and cannot create a run");
  require(run_store::install_object(nativePath(projectPath),
                                    missingScenarioReference,
                                    missingScenarioBytes.value())
              .has_value(),
          "current scenario is restored after failure injection");

  const auto missingPackageReference =
      project.project()->execution.package_bindings.back().package;
  const auto missingPackagePath = run_store::object_path_for_hash(
      run_store::sidecar_path_for_project(nativePath(projectPath)),
      missingPackageReference.object_hash);
  require(missingPackagePath.has_value(),
          "active package object path resolves");
  std::error_code packageRemovalError;
  require(std::filesystem::remove(missingPackagePath.value(),
                                  packageRemovalError) &&
              !packageRemovalError,
          "active package can be removed for failure test");
  execution.reloadProject();
  require(execution.errorCode() == "object_read_failed" &&
              execution.activePackage().value("error_code") ==
                  "object_read_failed" &&
              execution.runHistory().size() == 1,
          "missing active package preserves its exact store failure and run "
          "history row");
  execution.runAnalysis();
  require(execution.errorCode() == "object_read_failed" &&
              project.committedRunCount() == 1,
          "missing active package stays typed and cannot create a run");
  require(geometry.findings() == geometryBefore,
          "all motor workflow failures leave geometry findings unchanged");
}

void projectContentionIsTypedAndNoncommitting() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "contention test root exists");
  const auto cadPath = temporary.filePath("arm.step");
  const QByteArray cadBytes("exact motor-arm CAD bytes");
  writeBytes(cadPath, cadBytes);
  const auto projectPath = temporary.filePath("arm.prometheus");
  require(run_store::create_project_v2(
              nativePath(projectPath),
              projectFixture(cadPath, objectHash(cadBytes)))
              .has_value(),
          "contention project creates");

  CadController cad;
  EngineeringController geometry;
  ProjectController project(&cad, &geometry);
  ExecutionController execution(&project);
  project.openProject(QUrl::fromLocalFile(projectPath));
  execution.setPendingCadEntityId("motor");

  const auto readyPath = temporary.filePath("writer.ready");
  const auto releasePath = temporary.filePath("writer.release");
  QProcess writer;
  writer.start(QStringLiteral(PROMETHEUS_RUN_STORE_CONTENTION_HELPER),
               {"hold", projectPath, readyPath, releasePath, "motor"});
  require(writer.waitForStarted(5000), "contention helper starts");
  require(waitUntil([&readyPath] { return QFile::exists(readyPath); }),
          "contention helper holds the project writer lock");

  const QString contracts =
      QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) + "/fixtures/contracts/";
  const auto motorB =
      readBytes(contracts + "execution-component-v2.motor-b.jcs");
  execution.acceptExactPackage(motorB, objectHash(motorB));
  require(execution.errorCode() == "project_busy",
          "writer-lock timeout is exposed as project_busy");
  writeBytes(releasePath, "release\n");
  require(writer.waitForFinished(10000) && writer.exitCode() == 0,
          "contention helper releases and commits its own update");
  const auto stored = run_store::open_read_only(nativePath(projectPath));
  require(stored.has_value() &&
              stored.value().execution.package_bindings.size() == 1,
          "timed-out controller mutation added no second binding");
}

void staleWorkerCompletionCannotOverwriteANewerProject() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "stale-completion test root exists");
  const QByteArray firstCadBytes("first exact motor-arm CAD bytes");
  const QByteArray secondCadBytes("second exact motor-arm CAD bytes");
  const auto firstCadPath = temporary.filePath("first-arm.step");
  const auto secondCadPath = temporary.filePath("second-arm.step");
  const auto firstProjectPath = temporary.filePath("first.prometheus");
  const auto secondProjectPath = temporary.filePath("second.prometheus");
  writeBytes(firstCadPath, firstCadBytes);
  writeBytes(secondCadPath, secondCadBytes);
  require(run_store::create_project_v2(
              nativePath(firstProjectPath),
              projectFixture(firstCadPath, objectHash(firstCadBytes)))
                  .has_value() &&
              run_store::create_project_v2(
                  nativePath(secondProjectPath),
                  projectFixture(secondCadPath, objectHash(secondCadBytes)))
                  .has_value(),
          "both stale-completion projects create");

  CadController cad;
  EngineeringController geometry;
  ProjectController project(&cad, &geometry);
  ExecutionController execution(&project);
  project.openProject(QUrl::fromLocalFile(firstProjectPath));
  execution.setPendingCadEntityId("motor");
  const QString contracts =
      QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) + "/fixtures/contracts/";
  const auto motorA =
      readBytes(contracts + "execution-component-v2.motor-a.jcs");
  execution.acceptExactPackage(motorA, objectHash(motorA));
  execution.setScenarioDraft(acceptedScenarioDraft());
  execution.previewScenarioDegrees();
  execution.confirmScenario(
      "Evaluate the first project before switching project generations.");
  require(execution.canRun(),
          "first project is ready before generation switch");

  const auto readyPath = temporary.filePath("stale-writer.ready");
  const auto releasePath = temporary.filePath("stale-writer.release");
  QProcess writer;
  writer.start(QStringLiteral(PROMETHEUS_RUN_STORE_CONTENTION_HELPER),
               {"hold", firstProjectPath, readyPath, releasePath, "motor"});
  require(writer.waitForStarted(5000), "stale-completion writer starts");
  require(waitUntil([&readyPath] { return QFile::exists(readyPath); }),
          "stale-completion writer holds the first project lock");

  execution.runAnalysis();
  require(execution.busy(), "first-project worker starts");
  project.openProject(QUrl::fromLocalFile(secondProjectPath));
  require(!execution.busy() &&
              project.currentProjectPath() == secondProjectPath &&
              execution.status() == "project_loaded" &&
              execution.runHistory().isEmpty(),
          "opening a newer project generation immediately replaces UI state");

  writeBytes(releasePath, "release\n");
  require(writer.waitForFinished(10000) && writer.exitCode() == 0,
          "stale-completion writer releases the first project");
  require(waitUntil([&execution] {
            return execution.findChildren<QFutureWatcherBase *>().isEmpty();
          }),
          "stale first-project worker terminates and is discarded");
  const auto firstStored =
      run_store::open_read_only(nativePath(firstProjectPath));
  const auto secondStored =
      run_store::open_read_only(nativePath(secondProjectPath));
  require(firstStored.has_value() && secondStored.has_value() &&
              firstStored.value().execution.committed_runs.empty() &&
              secondStored.value().execution.committed_runs.empty(),
          "stale completion commits to neither project");
  require(project.currentProjectPath() == secondProjectPath &&
              execution.status() == "project_loaded" &&
              execution.errorCode().isEmpty() &&
              execution.runHistory().isEmpty(),
          "stale completion cannot overwrite the newer project generation");
}

void replayOutcomeStatesRemainDistinct() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "replay-state test root exists");
  const auto cadPath = temporary.filePath("arm.step");
  const QByteArray cadBytes("exact motor-arm CAD bytes");
  writeBytes(cadPath, cadBytes);
  const auto projectPath = temporary.filePath("arm.prometheus");
  require(run_store::create_project_v2(
              nativePath(projectPath),
              projectFixture(cadPath, objectHash(cadBytes)))
              .has_value(),
          "replay-state project creates");

  CadController cad;
  EngineeringController geometry;
  ProjectController project(&cad, &geometry);
  ExecutionController execution(&project);
  project.openProject(QUrl::fromLocalFile(projectPath));
  execution.setPendingCadEntityId("motor");
  const QString contracts =
      QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) + "/fixtures/contracts/";
  const auto motorA =
      readBytes(contracts + "execution-component-v2.motor-a.jcs");
  execution.acceptExactPackage(motorA, objectHash(motorA));
  execution.setScenarioDraft(acceptedScenarioDraft());
  execution.previewScenarioDegrees();
  execution.confirmScenario(
      "Evaluate the bound motor for replay-state verification.");
  execution.runAnalysis();
  require(waitUntil([&execution] { return !execution.busy(); }) &&
              execution.errorCode().isEmpty(),
          "replay-state baseline run completes");

  const auto baseOpened = run_store::open_read_only(nativePath(projectPath));
  require(baseOpened.has_value() &&
              baseOpened.value().execution.committed_runs.size() == 2,
          "replay-state baseline run is committed");
  const auto baseProject = baseOpened.value();
  const auto baseManifestReference =
      baseProject.execution.committed_runs.front();
  const auto baseManifestObject =
      run_store::read_object(nativePath(projectPath), baseManifestReference);
  require(baseManifestObject.has_value(), "baseline manifest loads");
  const auto baseManifest =
      QJsonDocument::fromJson(
          QByteArray::fromStdString(baseManifestObject.value()))
          .object();
  const auto baseResultReference =
      referenceFromJson(baseManifest.value("result").toObject());
  const auto baseResultObject =
      run_store::read_object(nativePath(projectPath), baseResultReference);
  require(baseResultObject.has_value(), "baseline result loads");
  const auto baseResult = QJsonDocument::fromJson(QByteArray::fromStdString(
                                                      baseResultObject.value()))
                              .object();

  const auto installVariant = [&](QJsonObject manifest, QJsonObject result) {
    const auto resultBytes = canonicalObject(result);
    const auto resultReference = referenceFor(
        resultBytes,
        "application/vnd.prometheus.analysis-result+json;version=1.0.0",
        "urn:prometheus:schema:analysis-result:1.0.0", "1.0.0");
    installVariantObject(projectPath, resultReference, resultBytes);
    manifest["result"] = referenceJson(resultReference);
    const auto manifestBytes = canonicalObject(manifest);
    const auto manifestReference = referenceFor(
        manifestBytes,
        "application/vnd.prometheus.run-manifest+json;version=1.0.0",
        "urn:prometheus:schema:run-manifest:1.0.0", "1.0.0");
    installVariantObject(projectPath, manifestReference, manifestBytes);
    auto variantProject = baseProject;
    variantProject.execution.committed_runs.front() = manifestReference;
    replaceProjectIndex(projectPath, variantProject);
  };

  auto unavailableManifest = baseManifest;
  auto unavailableResult = baseResult;
  const auto unavailableFingerprint =
      QStringLiteral("sha256:") + QString(64, QLatin1Char('f'));
  auto manifestProfile =
      unavailableManifest.value("numeric_profile").toObject();
  manifestProfile["backend_build_fingerprint"] = unavailableFingerprint;
  unavailableManifest["numeric_profile"] = manifestProfile;
  auto resultBackend = unavailableResult.value("backend").toObject();
  auto resultProfile = resultBackend.value("numeric_profile").toObject();
  resultProfile["backend_build_fingerprint"] = unavailableFingerprint;
  resultBackend["numeric_profile"] = resultProfile;
  unavailableResult["backend"] = resultBackend;
  installVariant(unavailableManifest, unavailableResult);

  CadController unavailableCad;
  EngineeringController unavailableGeometry;
  ProjectController unavailableProject(&unavailableCad, &unavailableGeometry);
  ExecutionController unavailableExecution(&unavailableProject);
  unavailableProject.openProject(QUrl::fromLocalFile(projectPath));
  require(unavailableExecution.runHistory().size() == 1 &&
              unavailableExecution.runHistory().front().toMap().value(
                  "status") == "Recorded",
          "numeric-profile variant remains a verified recorded result");
  unavailableExecution.selectRun(0);
  unavailableExecution.replaySelected();
  require(waitUntil([&unavailableExecution] {
            return !unavailableExecution.busy();
          }) &&
              unavailableExecution.replayState() ==
                  "Backend identity unavailable" &&
              unavailableExecution.errorCode() == "numeric_profile_mismatch",
          "numeric identity mismatch is not mislabeled as an exact match");

  auto mismatchResult = baseResult;
  auto calculations = mismatchResult.value("calculations").toArray();
  auto firstCalculation = calculations.at(0).toObject();
  firstCalculation["value"] =
      firstCalculation.value("value").toDouble() + 0.125;
  calculations[0] = firstCalculation;
  mismatchResult["calculations"] = calculations;
  installVariant(baseManifest, mismatchResult);

  CadController mismatchCad;
  EngineeringController mismatchGeometry;
  ProjectController mismatchProject(&mismatchCad, &mismatchGeometry);
  ExecutionController mismatchExecution(&mismatchProject);
  mismatchProject.openProject(QUrl::fromLocalFile(projectPath));
  mismatchExecution.selectRun(0);
  mismatchExecution.replaySelected();
  require(
      waitUntil([&mismatchExecution] { return !mismatchExecution.busy(); }) &&
          mismatchExecution.replayState() == "Reproduction failed" &&
          mismatchExecution.errorCode() == "result_mismatch",
      "exact result mismatch is a typed reproduction failure");
}

} // namespace

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  fullMotorAAndBWorkflow();
  failClosedPreconditionsAndCancellation();
  projectContentionIsTypedAndNoncommitting();
  staleWorkerCompletionCannotOverwriteANewerProject();
  replayOutcomeStatesRemainDistinct();
  return 0;
}
