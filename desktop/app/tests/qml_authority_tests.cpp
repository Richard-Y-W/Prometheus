#include "cad_controller.hpp"
#include "engineering_controller.hpp"
#include "execution_controller.hpp"
#include "project_intake.hpp"
#include "project_controller.hpp"
#include "service_controller.hpp"

#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/run_store.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class FixtureCatalogProbe final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList fixtureChoices READ fixtureChoices CONSTANT)
  Q_PROPERTY(bool busy READ busy CONSTANT)
  Q_PROPERTY(QVariantMap candidate READ candidate CONSTANT)
  Q_PROPERTY(QVariantList parameters READ parameters CONSTANT)
  Q_PROPERTY(QString status READ status CONSTANT)
  Q_PROPERTY(QString error READ error CONSTANT)
  Q_PROPERTY(QString errorCode READ errorCode CONSTANT)
  Q_PROPERTY(QString executionReadiness READ executionReadiness CONSTANT)
  Q_PROPERTY(QString objectHash READ objectHash CONSTANT)
  Q_PROPERTY(QString publicationIntegrity READ publicationIntegrity CONSTANT)
  Q_PROPERTY(int draftVersion READ draftVersion CONSTANT)

public:
  explicit FixtureCatalogProbe(const bool reviewMode = false)
      : review_mode_(reviewMode) {}

  QVariantList fixtureChoices() const {
    return {QVariantMap{
        {"fixture_id", "prometheus.motor-a.fixture-1"},
        {"label", "Motor A — synthetic execution-ready input"},
    }};
  }
  bool busy() const { return false; }
  QVariantMap candidate() const {
    if (!review_mode_) {
      return {};
    }
    return {
        {"id", "review-candidate"},
        {"limitations", QVariantList{}},
    };
  }
  QVariantList parameters() const {
    if (!review_mode_) {
      return {};
    }
    const auto parameter = [](const QString &name, const QString &claimId) {
      return QVariantMap{
          {"name", name},
          {"selected_claim",
           QVariantMap{
               {"claim_id", claimId},
               {"claim_fingerprint", "sha256:test"},
               {"evidence_ids", QVariantList{}},
               {"provenance", "test"},
               {"unit", "1"},
               {"value",
                QVariantMap{
                    {"kind", "scalar"},
                    {"value", 1.0},
                }},
           }},
      };
    };
    return {
        parameter("first_parameter", "claim-1"),
        parameter("second_parameter", "claim-2"),
    };
  }
  QString status() const {
    return review_mode_ ? QStringLiteral("draft") : QString{};
  }
  QString error() const { return {}; }
  QString errorCode() const { return {}; }
  QString executionReadiness() const { return {}; }
  QString objectHash() const { return {}; }
  QString publicationIntegrity() const { return {}; }
  int draftVersion() const { return review_mode_ ? 0 : -1; }
  QString loadedFixtureId() const { return loaded_fixture_id_; }

  Q_INVOKABLE void loadFixture(const QString &fixtureId) {
    loaded_fixture_id_ = fixtureId;
  }

signals:
  void changed();

private:
  bool review_mode_{};
  QString loaded_fixture_id_;
};

class ProjectIntakeProbe final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString rootPath READ rootPath CONSTANT)
  Q_PROPERTY(QVariantList artifacts READ artifacts CONSTANT)
  Q_PROPERTY(int totalCount READ totalCount CONSTANT)
  Q_PROPERTY(int readyCount READ readyCount CONSTANT)
  Q_PROPERTY(int notEvaluatedCount READ notEvaluatedCount CONSTANT)
  Q_PROPERTY(int unsupportedCount READ unsupportedCount CONSTANT)
  Q_PROPERTY(int unreadableCount READ unreadableCount CONSTANT)
  Q_PROPERTY(QString primaryStepPath READ primaryStepPath CONSTANT)
  Q_PROPERTY(QString status READ status CONSTANT)
  Q_PROPERTY(QString error READ error CONSTANT)
  Q_PROPERTY(bool busy READ busy CONSTANT)

public:
  explicit ProjectIntakeProbe(QString stepPath)
      : step_path_(std::move(stepPath)) {}

  QString rootPath() const { return QFileInfo(step_path_).absolutePath(); }
  QVariantList artifacts() const {
    return {QVariantMap{{"relative_path", "assembly.step"},
                        {"absolute_path", step_path_},
                        {"name", "assembly.step"},
                        {"extension", "step"},
                        {"byte_size", 128},
                        {"sha256", "sha256:" + QString(64, 'a')},
                        {"category", "geometry"},
                        {"analysis_state", "ready"},
                        {"detail", "Ready for Open Cascade STEP import"},
                        {"loadable", true}}};
  }
  int totalCount() const { return 1; }
  int readyCount() const { return 1; }
  int notEvaluatedCount() const { return 0; }
  int unsupportedCount() const { return 0; }
  int unreadableCount() const { return 0; }
  QString primaryStepPath() const { return step_path_; }
  QString status() const { return "1 file accounted for"; }
  QString error() const { return {}; }
  bool busy() const { return false; }

signals:
  void changed();

private:
  QString step_path_;
};

namespace {

namespace integrity = prometheus::integrity;
namespace run_store = prometheus::run_store;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

QByteArray readBytes(const QString &path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly),
          "open test input " + path.toStdString());
  return file.readAll();
}

void writeBytes(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "open test output " + path.toStdString());
  require(file.write(bytes) == bytes.size(),
          "write test output " + path.toStdString());
  file.close();
}

std::filesystem::path nativePath(const QString &value) {
#ifdef _WIN32
  return std::filesystem::path(value.toStdWString());
#else
  const auto bytes = value.toUtf8();
  return std::filesystem::path(
      std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
#endif
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
  project.name = "QML authority motor arm";
  project.cad_source = cadPath.toStdString();
  project.assembly_artifact_hash = cadHash.toStdString();
  project.coordinate_system = "right-handed Z-up";
  project.length_unit = "m";
  project.component_bindings.push_back({"motor", "geometry-revision", "Motor"});
  project.engineering.geometry_status = "completed";
  project.engineering.geometry_findings.push_back(
      {"static_interference", "information", "information",
       "Geometry remains independently recorded", "Geometry-only evidence", 0.0,
       "m³", 0.0, 0.0, "Imported geometry", "", "", "motor", "base"});
  return project;
}

QVariantMap scenarioDraft() {
  return {
      {"payload_mass_kg", 8.0},        {"arm_radius_m", 0.2},
      {"rotation_degrees", 90.0},      {"move_duration_s", 1.2},
      {"hold_duration_s", 4.0},        {"cycle_duration_s", 10.0},
      {"ambient_temperature_c", 35.0},
  };
}

QObject *requiredChild(QObject *root, const char *objectName) {
  auto *child = root->findChild<QObject *>(QString::fromLatin1(objectName));
  require(child != nullptr, std::string("QML object exists: ") + objectName);
  return child;
}

QQuickItem *visualChild(QQuickItem *root, const QString &objectName) {
  if (root->objectName() == objectName)
    return root;
  for (auto *child : root->childItems()) {
    if (auto *found = visualChild(child, objectName))
      return found;
  }
  return nullptr;
}

QObject *requiredPropertyChild(QObject *root, const char *propertyName,
                               const QString &expectedValue) {
  const auto children = root->findChildren<QObject *>();
  const auto found = std::find_if(
      children.cbegin(), children.cend(), [&](const QObject *candidate) {
        return candidate->property(propertyName).toString() == expectedValue;
      });
  require(found != children.cend(),
          "QML object exists with " + std::string(propertyName) + "=" +
              expectedValue.toStdString());
  return *found;
}

void verifyAuthorityScan() {
  const QString uiRoot = QStringLiteral(PROMETHEUS_UI_DIR);
  const std::vector<QString> files{
      uiRoot + "/Main.qml", uiRoot + "/ComponentPackagePanel.qml",
      uiRoot + "/MotorScenarioDialog.qml", uiRoot + "/MotorRunPanel.qml",
      uiRoot + "/RunHistoryPanel.qml", uiRoot + "/ProjectInventoryPanel.qml"};
  QString source;
  for (const auto &path : files) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly),
            "production QML workflow file exists: " + path.toStdString());
    source += QString::fromUtf8(file.readAll());
    source += '\n';
  }

  const std::vector<QRegularExpression> forbidden{
      QRegularExpression(
          QStringLiteral("\\bMath\\.(?:PI|pow|sin|cos|sqrt)\\b")),
      QRegularExpression(QStringLiteral("(?:/\\s*180(?:\\.0)?\\b|"
                                        "\\b180(?:\\.0)?\\s*/\\s*Math)")),
      QRegularExpression(QStringLiteral("\\b(?:QByteArray|packageBytes|"
                                        "exactPackageAcquired)\\b")),
      QRegularExpression(QStringLiteral("\\b(?:gear_ratio|stall_torque_nm|"
                                        "continuous_torque_nm|"
                                        "torque_constant_nm_a|"
                                        "thermal_resistance_k_w|"
                                        "thermal_capacitance_j_k)\\b")),
      QRegularExpression(QStringLiteral("\\bauto(?:matic)?Review\\b"),
                         QRegularExpression::CaseInsensitiveOption),
  };
  for (const auto &pattern : forbidden) {
    require(!pattern.match(source).hasMatch(),
            "QML contains no engineering authority or raw package bytes: " +
                pattern.pattern().toStdString());
  }
  require(!source.contains("Project works", Qt::CaseInsensitive),
          "QML contains no unscoped project verdict");
}

void verifyPendingSaveAs(const QByteArray &motorA) {
  QTemporaryDir temporary;
  require(temporary.isValid(), "pending Save As test root exists");
  const auto cadPath = temporary.filePath("pending-arm.step");
  const QByteArray cadBytes("pending Save As CAD bytes");
  writeBytes(cadPath, cadBytes);

  CadController cad;
  EngineeringController geometry;
  ProjectController project(&cad, &geometry);
  ExecutionController execution(&project);
  cad.restoreCadState({
      {"name", "Pending motor arm"},
      {"cad_source", cadPath},
      {"resolved_cad_source", cadPath},
      {"assembly_artifact_hash", objectHash(cadBytes)},
      {"coordinate_system", "right-handed Z-up"},
      {"length_unit", "m"},
      {"component_bindings",
       QVariantList{QVariantMap{{"cad_entity_id", "motor"},
                                {"revision_id", "geometry-revision"},
                                {"label", "Motor"}}}},
      {"placement_overrides", QVariantList{}},
      {"connections", QVariantList{}},
      {"interference_classifications", QVariantList{}},
  });
  execution.setPendingCadEntityId("motor");
  execution.acceptExactPackage(motorA, objectHash(motorA));
  require(execution.errorCode() == "save_as_required" &&
              execution.property("pendingSaveAsAction").toString() ==
                  "package_binding",
          "first execution mutation is held behind explicit Save As");
  project.saveAsVersion2(QUrl{});
  require(project.errorCode() == "invalid_project_url" &&
              execution.pendingSaveAsAction() == "package_binding" &&
              !project.project().has_value(),
          "failed Save As preserves but does not perform the pending action");
  const auto projectPath = temporary.filePath("pending-arm.prometheus");
  project.saveAsVersion2(QUrl::fromLocalFile(projectPath));
  require(project.errorCode().isEmpty() && execution.errorCode().isEmpty() &&
              execution.property("pendingSaveAsAction").toString().isEmpty() &&
              project.project().has_value() &&
              project.project()->execution.package_bindings.size() == 1,
          "successful explicit Save As resumes the exact pending C++ action");

  CadController cancelledCad;
  EngineeringController cancelledGeometry;
  ProjectController cancelledProject(&cancelledCad, &cancelledGeometry);
  ExecutionController cancelledExecution(&cancelledProject);
  cancelledExecution.setPendingCadEntityId("motor");
  cancelledExecution.acceptExactPackage(motorA, objectHash(motorA));
  require(QMetaObject::invokeMethod(&cancelledExecution,
                                    "cancelPendingSaveAsAction"),
          "pending Save As action exposes explicit cancellation");
  require(
      cancelledExecution.property("pendingSaveAsAction").toString().isEmpty() &&
          !cancelledProject.project().has_value(),
      "cancelled Save As performs no execution mutation");

  CadController scenarioCad;
  EngineeringController scenarioGeometry;
  ProjectController scenarioProject(&scenarioCad, &scenarioGeometry);
  ExecutionController scenarioExecution(&scenarioProject);
  scenarioCad.restoreCadState({
      {"name", "Pending scenario arm"},
      {"cad_source", cadPath},
      {"resolved_cad_source", cadPath},
      {"assembly_artifact_hash", objectHash(cadBytes)},
      {"coordinate_system", "right-handed Z-up"},
      {"length_unit", "m"},
      {"component_bindings",
       QVariantList{QVariantMap{{"cad_entity_id", "motor"},
                                {"revision_id", "geometry-revision"},
                                {"label", "Motor"}}}},
      {"placement_overrides", QVariantList{}},
      {"connections", QVariantList{}},
      {"interference_classifications", QVariantList{}},
  });
  scenarioExecution.setScenarioDraft(scenarioDraft());
  scenarioExecution.previewScenarioDegrees();
  scenarioExecution.confirmScenario(
      "Persist the reviewed scenario only after explicit Save As.");
  require(scenarioExecution.errorCode() == "save_as_required" &&
              scenarioExecution.pendingSaveAsAction() ==
                  "scenario_confirmation" &&
              !scenarioExecution.scenarioConfirmed(),
          "first scenario mutation is held behind explicit Save As");
  const auto scenarioProjectPath =
      temporary.filePath("pending-scenario.prometheus");
  scenarioProject.saveAsVersion2(QUrl::fromLocalFile(scenarioProjectPath));
  require(scenarioProject.errorCode().isEmpty() &&
              scenarioExecution.errorCode().isEmpty() &&
              scenarioExecution.pendingSaveAsAction().isEmpty() &&
              scenarioExecution.scenarioConfirmed() &&
              scenarioProject.project().has_value() &&
              scenarioProject.project()->execution.current_scenario.has_value(),
          "successful Save As resumes the exact pending scenario confirmation");

  CadController cancelledScenarioCad;
  EngineeringController cancelledScenarioGeometry;
  ProjectController cancelledScenarioProject(&cancelledScenarioCad,
                                             &cancelledScenarioGeometry);
  ExecutionController cancelledScenarioExecution(&cancelledScenarioProject);
  cancelledScenarioExecution.setScenarioDraft(scenarioDraft());
  cancelledScenarioExecution.previewScenarioDegrees();
  cancelledScenarioExecution.confirmScenario(
      "This pending scenario will be cancelled.");
  cancelledScenarioExecution.cancelPendingSaveAsAction();
  require(cancelledScenarioExecution.pendingSaveAsAction().isEmpty() &&
              !cancelledScenarioExecution.scenarioConfirmed() &&
              !cancelledScenarioProject.project().has_value(),
          "cancelled scenario Save As performs no execution mutation");
}

void verifyOffscreenWorkflow() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "QML workflow test root exists");
  const auto cadPath = temporary.filePath("arm.step");
  const QByteArray cadBytes("QML workflow exact CAD bytes");
  writeBytes(cadPath, cadBytes);
  const auto projectPath = temporary.filePath("arm.prometheus");
  require(run_store::create_project_v2(
              nativePath(projectPath),
              projectFixture(cadPath, objectHash(cadBytes)))
              .has_value(),
          "QML workflow project creates");

  CadController cad;
  ServiceController service;
  EngineeringController geometry;
  ProjectController project(&cad, &geometry);
  ProjectIntakeController intake;
  ExecutionController execution(&project, &service);
  project.openProject(QUrl::fromLocalFile(projectPath));
  require(project.errorCode().isEmpty(), "QML workflow project opens");
  execution.setPendingCadEntityId("motor");

  const QString contracts =
      QStringLiteral(PROMETHEUS_REPOSITORY_ROOT) + "/fixtures/contracts/";
  const auto motorA =
      readBytes(contracts + "execution-component-v2.motor-a.jcs");
  const auto motorB =
      readBytes(contracts + "execution-component-v2.motor-b.jcs");
  const auto blocked =
      readBytes(contracts + "execution-component-v2.pm-36-gm.jcs");
  execution.acceptExactPackage(blocked, objectHash(blocked));

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("cadController", &cad);
  engine.rootContext()->setContextProperty("serviceController", &service);
  engine.rootContext()->setContextProperty("engineeringController", &geometry);
  engine.rootContext()->setContextProperty("projectController", &project);
  engine.rootContext()->setContextProperty("projectIntakeController", &intake);
  engine.rootContext()->setContextProperty("executionController", &execution);
  engine.rootContext()->setContextProperty("demoResearch", false);
  engine.rootContext()->setContextProperty("demoEngineering", false);
  engine.rootContext()->setContextProperty("demoCadInspect", false);
  engine.rootContext()->setContextProperty("demoPlacement", false);
  engine.rootContext()->setContextProperty("startupStepPath", QString{});
  engine.rootContext()->setContextProperty("startupProjectFolder", QUrl{});
  QQmlComponent component(
      &engine,
      QUrl::fromLocalFile(QStringLiteral(PROMETHEUS_UI_DIR) + "/Main.qml"));
  std::unique_ptr<QObject> root(component.create());
  if (!root) {
    for (const auto &error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
  }
  require(root != nullptr, "production QML module instantiates offscreen");
  root->setProperty("visible", false);
  QCoreApplication::processEvents();
  auto *fileActions = requiredChild(root.get(), "fileActionsButton");
  auto *screenResults = requiredChild(root.get(), "screenResultsButton");
  require(fileActions->property("visible").toBool(),
          "secondary file actions remain reachable from the toolbar");
  require(screenResults->property("x").toDouble() +
                  screenResults->property("width").toDouble() <=
              root->property("width").toDouble(),
          "mechanical screen action remains inside the default window");

  FixtureCatalogProbe fixtureCatalog;
  QQmlComponent packagePanel(
      &engine, QUrl::fromLocalFile(QStringLiteral(PROMETHEUS_UI_DIR) +
                                  "/ComponentPackagePanel.qml"));
  std::unique_ptr<QObject> packagePanelRoot(packagePanel.createWithInitialProperties({
      {"serviceController", QVariant::fromValue<QObject *>(&fixtureCatalog)},
      {"executionController", QVariant::fromValue<QObject *>(&execution)},
      {"selectedEntityId", "motor"},
      {"selectedEntityName", "Motor"},
  }));
  if (!packagePanelRoot) {
    for (const auto &error : packagePanel.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
  }
  require(packagePanelRoot != nullptr,
          "component package panel instantiates with a fixed catalog");
  QCoreApplication::processEvents();
  auto *fixtureSelector =
      requiredPropertyChild(packagePanelRoot.get(), "valueRole", "fixture_id");
  fixtureSelector->setProperty("valueRole", "unavailable_fixture_id");
  QCoreApplication::processEvents();
  auto *loadEvidence =
      requiredPropertyChild(packagePanelRoot.get(), "text", "Load evidence");
  require(QMetaObject::invokeMethod(loadEvidence, "click"),
          "fixed catalog load action is callable");
  require(fixtureCatalog.loadedFixtureId() ==
              "prometheus.motor-a.fixture-1",
          "default fixed catalog entry submits the exact Motor A identity");

  FixtureCatalogProbe reviewCatalog(true);
  QQmlComponent reviewPanel(
      &engine, QUrl::fromLocalFile(QStringLiteral(PROMETHEUS_UI_DIR) +
                                  "/ComponentPackagePanel.qml"));
  std::unique_ptr<QObject> reviewPanelRoot(reviewPanel.createWithInitialProperties({
      {"serviceController", QVariant::fromValue<QObject *>(&reviewCatalog)},
      {"executionController", QVariant::fromValue<QObject *>(&execution)},
      {"selectedEntityId", "motor"},
      {"selectedEntityName", "Motor"},
      {"width", 1040},
      {"height", 780},
  }));
  if (!reviewPanelRoot) {
    for (const auto &error : reviewPanel.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
  }
  require(reviewPanelRoot != nullptr,
          "component package panel instantiates with two review claims");
  require(QMetaObject::invokeMethod(&reviewCatalog, "changed"),
          "review catalog change signal is callable");
  QCoreApplication::processEvents();
  require(reviewPanelRoot->property("reviewStateToken").toString() ==
              "review-candidate:0",
          "review catalog signal initializes the claim-review model");

  require(QMetaObject::invokeMethod(reviewPanelRoot.get(),
                                    "recordReviewDecision", Q_ARG(QVariant, 0),
                                    Q_ARG(QVariant, 1)),
          "first Accept decision records by explicit claim row");
  require(QMetaObject::invokeMethod(reviewPanelRoot.get(),
                                    "recordReviewDecision", Q_ARG(QVariant, 1),
                                    Q_ARG(QVariant, 1)),
          "second Accept decision records by explicit claim row");
  for (const int row : {0, 1}) {
    require(QMetaObject::invokeMethod(
                reviewPanelRoot.get(), "recordReviewNote",
                Q_ARG(QVariant, row),
                Q_ARG(QVariant,
                      QStringLiteral(
                          "Accepted in the QML authority regression."))),
            "review note records by explicit claim row");
  }
  QVariant reviewPayload;
  require(QMetaObject::invokeMethod(
              reviewPanelRoot.get(), "reviewDecisionPayload",
              Q_RETURN_ARG(QVariant, reviewPayload)),
          "normalized review payload is callable");
  const auto decisions = reviewPayload.toList();
  require(decisions.size() == 2,
          "normalized review payload retains both displayed claims");
  for (int row = 0; row < decisions.size(); ++row) {
    const auto decision = decisions.at(row).toMap();
    require(decision.value("claim_id").toString() ==
                    QStringLiteral("claim-%1").arg(row + 1) &&
                decision.value("status").toString() == "accepted" &&
                decision.value("note").toString() ==
                    "Accepted in the QML authority regression.",
            "each decision activation updates its own claim-review row");
  }

  auto *runButton = requiredChild(root.get(), "runMotorButton");
  auto *blockedReason = requiredChild(root.get(), "blockedReasonLabel");
  require(!runButton->property("enabled").toBool(),
          "Run motor analysis is disabled for a blocked package");
  require(blockedReason->property("text").toString() ==
              "Program 01A has no v2 package consumer or solver execution.",
          "blocked package remains visible with its authoritative reason");

  auto *identityValue = requiredChild(root.get(), "componentIdentityValue");
  auto *payloadField = requiredChild(root.get(), "payloadMassField");
  require(
      !identityValue->property("readOnly").isValid() &&
          !payloadField->property("readOnly").toBool(),
      "component identity is display-only while scenario fields are editable");
  requiredChild(root.get(), "armRadiusField")->setProperty("text", "0.2");
  requiredChild(root.get(), "rotationDegreesField")->setProperty("text", "90");
  requiredChild(root.get(), "moveDurationField")->setProperty("text", "1.2");
  requiredChild(root.get(), "holdDurationField")->setProperty("text", "4");
  requiredChild(root.get(), "cycleDurationField")->setProperty("text", "10");
  requiredChild(root.get(), "ambientTemperatureField")
      ->setProperty("text", "35");
  auto *scenarioDialog = requiredChild(root.get(), "motorScenarioDialog");
  require(QMetaObject::invokeMethod(scenarioDialog, "reviewTypedValues"),
          "scenario review action invokes the QML adapter");
  QCoreApplication::processEvents();
  const auto radians = requiredChild(root.get(), "rotationRadiansValue")
                           ->property("text")
                           .toString();
  require(radians.contains("1.5707963267948966") && radians.contains("rad"),
          "scenario preview shows the exact C++ radians and unit");
  requiredChild(root.get(), "scenarioIntentField")
      ->setProperty("text", "Evaluate the reviewed QML motor-arm scenario.");
  require(QMetaObject::invokeMethod(scenarioDialog, "confirmReviewedScenario"),
          "separate scenario confirmation action is invokable");
  require(execution.scenarioConfirmed() && !execution.canRun(),
          "confirmed scenario does not override a blocked package");

  execution.acceptExactPackage(motorA, objectHash(motorA));
  QCoreApplication::processEvents();
  require(
      execution.canRun() && runButton->property("enabled").toBool(),
      "Run motor analysis enables only with ready binding and confirmation");
  require(QMetaObject::invokeMethod(runButton, "click"),
          "enabled motor run button is callable");
  require(waitUntil([&execution] { return !execution.busy(); }) &&
              execution.errorCode().isEmpty(),
          "Motor A run completes through the QML action");
  execution.acceptExactPackage(motorB, objectHash(motorB));
  execution.runAnalysis();
  require(waitUntil([&execution] { return !execution.busy(); }) &&
              execution.errorCode().isEmpty() &&
              execution.runHistory().size() == 2,
          "Motor B appends a second run with the same confirmed scenario");
  execution.acceptExactPackage(motorA, objectHash(motorA));
  QCoreApplication::processEvents();
  auto *historyPanel = requiredChild(root.get(), "runHistoryPanel");
  require(historyPanel->property("historyCount").toInt() == 2,
          "Motor A/B history remains visible after switching packages");

  const auto geometryText = requiredChild(root.get(), "geometryStatusLabel")
                                ->property("text")
                                .toString();
  const auto motorText = requiredChild(root.get(), "motorStatusLabel")
                             ->property("text")
                             .toString();
  require(geometryText.contains("completed", Qt::CaseInsensitive) &&
              !motorText.isEmpty() && geometryText != motorText,
          "geometry and motor capabilities retain independent status labels");

  verifyPendingSaveAs(motorA);
}

void verifyProjectInventoryPanel() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "inventory-panel folder exists");
  const auto stepPath = temporary.filePath("assembly.step");
  writeBytes(stepPath, "inventory panel STEP bytes");
  ProjectIntakeProbe intake(stepPath);

  QQmlApplicationEngine engine;
  QQmlComponent panel(
      &engine, QUrl::fromLocalFile(QStringLiteral(PROMETHEUS_UI_DIR) +
                                   "/ProjectInventoryPanel.qml"));
  std::unique_ptr<QObject> root(panel.createWithInitialProperties({
      {"projectIntakeController", QVariant::fromValue<QObject *>(&intake)},
      {"width", 980},
      {"height", 700},
  }));
  if (!root) {
    for (const auto &error : panel.errors())
      std::cerr << error.toString().toStdString() << '\n';
  }
  require(root != nullptr, "project inventory panel instantiates offscreen");
  QQuickWindow window;
  window.setGeometry(0, 0, 980, 700);
  auto *panelItem = qobject_cast<QQuickItem *>(root.get());
  require(panelItem != nullptr, "inventory panel is a visual item");
  panelItem->setParentItem(window.contentItem());
  window.show();
  require(waitUntil([&window] {
            return visualChild(window.contentItem(), "loadArtifactButton_0") !=
                   nullptr;
          }),
          "virtualized STEP row is created in the offscreen scene");

  QSignalSpy requested(root.get(), SIGNAL(loadRequested(QString)));
  auto *load = visualChild(window.contentItem(), "loadArtifactButton_0");
  require(QMetaObject::invokeMethod(load, "click"),
          "STEP load action is callable");
  require(requested.size() == 1 &&
              requested.front().front().toString() == stepPath,
          "inventory panel preserves the exact selected STEP path");
}

} // namespace

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  verifyAuthorityScan();
  verifyProjectInventoryPanel();
  verifyOffscreenWorkflow();
  return 0;
}

#include "qml_authority_tests.moc"
