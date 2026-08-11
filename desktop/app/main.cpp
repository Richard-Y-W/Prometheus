#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QDebug>
#include "cad_controller.hpp"
#include "service_controller.hpp"
#include "engineering_controller.hpp"
int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationName("Prometheus");
  QQmlApplicationEngine engine;
  CadController cad;
  ServiceController service;
  EngineeringController engineering;
  const bool demo_research=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_RESEARCH");
  const bool demo_auto_publish=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_AUTO_PUBLISH");
  const bool demo_engineering=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_ENGINEERING");
  const bool demo_cad_inspect=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_CAD_INSPECT");
  const bool demo_placement=qEnvironmentVariableIsSet("PROMETHEUS_DEMO_PLACEMENT");
  engine.rootContext()->setContextProperty("cadController",&cad);
  engine.rootContext()->setContextProperty("serviceController",&service);
  engine.rootContext()->setContextProperty("engineeringController",&engineering);
  engine.rootContext()->setContextProperty("demoResearch",demo_research);
  engine.rootContext()->setContextProperty("demoAutoPublish",demo_auto_publish);
  engine.rootContext()->setContextProperty("demoEngineering",demo_engineering);
  engine.rootContext()->setContextProperty("demoCadInspect",demo_cad_inspect);
  engine.rootContext()->setContextProperty("demoPlacement",demo_placement);
  engine.loadFromModule("Prometheus", "Main");
  if (engine.rootObjects().isEmpty()) return -1;
  const auto screenshot_path=qEnvironmentVariable("PROMETHEUS_SCREENSHOT_PATH");
  if(!screenshot_path.isEmpty())QTimer::singleShot(2500,&app,[&engine,&app,screenshot_path]{bool saved=false;if(auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front()))saved=window->grabWindow().save(screenshot_path);if(!saved)qWarning()<<"Unable to save verification screenshot to"<<screenshot_path;app.quit();});
  if(demo_research)QTimer::singleShot(400,&service,[&service]{service.research("Prometheus Fixture Works","PM-36-GM");});
  return app.exec();
}
