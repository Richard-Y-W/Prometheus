#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QDebug>
#include <QUrl>
#include "cad_controller.hpp"
#include "engineering_controller.hpp"
#include "execution_controller.hpp"
#include "project_intake.hpp"
#include "project_controller.hpp"
#include "service_controller.hpp"
#include "structural_controller.hpp"
#include <algorithm>
int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationName("Prometheus");
  CadController cad;
  ServiceController service;
  EngineeringController engineering;
  ProjectController project(&cad,&engineering);
  ProjectIntakeController intake;
  ExecutionController execution(&project,&service);
  StructuralController structural(&project);
  const auto anchorInventory = [&intake, &project] {
    if (!intake.result().ok || !project.project().has_value()) return;
    auto source = QString::fromStdString(project.project()->cad_source);
    if (QFileInfo(source).isRelative())
      source = QFileInfo(QFileInfo(project.currentProjectPath()).absoluteDir(),
                         source).absoluteFilePath();
    const auto expected = QString::fromStdString(
        project.project()->assembly_artifact_hash);
    QString sourceRelativePath;
    QString accountedSourceHash;
    const auto sourceAccounted = std::any_of(
        intake.result().artifacts.cbegin(), intake.result().artifacts.cend(),
        [&](const QVariant &value) {
          const auto artifact = value.toMap();
          if (QFileInfo(artifact.value("absolute_path").toString()) !=
              QFileInfo(source))
            return false;
          sourceRelativePath = artifact.value("relative_path").toString();
          accountedSourceHash = artifact.value("sha256").toString();
          return true;
        });
    if (!sourceAccounted) {
      const auto relative = QDir::fromNativeSeparators(
          QDir(intake.result().root_path).relativeFilePath(source));
      if (relative == ".." || relative.startsWith("../") ||
          QFileInfo(relative).isAbsolute())
        return;
      sourceRelativePath = relative;
    }
    const auto snapshot = buildProjectInventorySnapshot(intake.result());
    if (project.latestInventoryHash() ==
            QString::fromStdString(snapshot.reference.object_hash) &&
        accountedSourceHash == expected)
      return;
    project.assessInventorySnapshot(snapshot, sourceRelativePath,
                                    accountedSourceHash == expected);
  };
  QObject::connect(&intake, &ProjectIntakeController::scanFinished, &project,
                   [anchorInventory](const bool success) {
    if (success) anchorInventory();
  });
  QObject::connect(&project, &ProjectController::projectSaved, &project,
                   anchorInventory);
  QQmlApplicationEngine engine;
  const bool demo_research=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_RESEARCH");
  const bool demo_engineering=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_ENGINEERING");
  const bool demo_cad_inspect=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_CAD_INSPECT");
  const bool demo_placement=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_PLACEMENT");
  const bool demo_structural=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_STRUCTURAL");
  const auto startup_step_path=qEnvironmentVariable("PROMETHEUS_STARTUP_STEP");
  const auto startup_project_folder_path=qEnvironmentVariable("PROMETHEUS_STARTUP_PROJECT_FOLDER");
  const auto startup_structural_mesh_path=qEnvironmentVariable("PROMETHEUS_STARTUP_STRUCTURAL_MESH");
  const auto startup_project_folder=startup_project_folder_path.isEmpty()?QUrl{}:QUrl::fromLocalFile(startup_project_folder_path);
  engine.rootContext()->setContextProperty("cadController",&cad);
  engine.rootContext()->setContextProperty("serviceController",&service);
  engine.rootContext()->setContextProperty("engineeringController",&engineering);
  engine.rootContext()->setContextProperty("projectController",&project);
  engine.rootContext()->setContextProperty("projectIntakeController",&intake);
  engine.rootContext()->setContextProperty("executionController",&execution);
  engine.rootContext()->setContextProperty("structuralController",&structural);
  engine.rootContext()->setContextProperty("demoResearch",demo_research);
  engine.rootContext()->setContextProperty("demoEngineering",demo_engineering);
  engine.rootContext()->setContextProperty("demoCadInspect",demo_cad_inspect);
  engine.rootContext()->setContextProperty("demoPlacement",demo_placement);
  engine.rootContext()->setContextProperty("demoStructural",demo_structural);
  engine.rootContext()->setContextProperty("startupStepPath",startup_step_path);
  engine.rootContext()->setContextProperty("startupProjectFolder",startup_project_folder);
  if(!startup_structural_mesh_path.isEmpty())structural.loadMesh(QUrl::fromLocalFile(startup_structural_mesh_path),0.001,15.0);
  engine.loadFromModule("Prometheus", "Main");
  if (engine.rootObjects().isEmpty()) return -1;
  const auto screenshot_path=qEnvironmentVariable("PROMETHEUS_SCREENSHOT_PATH");
  if(!screenshot_path.isEmpty()){const auto capture=[&engine,&app,screenshot_path]{bool saved=false;if(auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front()))saved=window->grabWindow().save(screenshot_path);if(!saved)qWarning()<<"Unable to save verification screenshot to"<<screenshot_path;app.quit();};if(startup_step_path.isEmpty()&&startup_project_folder_path.isEmpty())QTimer::singleShot(2500,&app,capture);else QObject::connect(&cad,&CadController::importFinished,&app,[capture](bool ok){if(ok)QTimer::singleShot(1500,capture);});}
  if(demo_research)QTimer::singleShot(400,&service,[&service]{service.loadFixture("prometheus.motor-a.fixture-1");});
  return app.exec();
}
