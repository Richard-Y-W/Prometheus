#include "cad_controller.hpp"
#include "engineering_controller.hpp"
#include "execution_controller.hpp"
#include "project_controller.hpp"
#include <prometheus/integrity/canonical_json.hpp>
#include <prometheus/run_store/run_store.hpp>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <iostream>
#include <functional>
#include <string_view>

namespace {
bool waitUntil(const std::function<bool()>& predicate){QElapsedTimer timer;timer.start();while(!predicate()&&timer.elapsed()<10000){QCoreApplication::processEvents(QEventLoop::AllEvents,20);QThread::msleep(2);}return predicate();}
QByteArray readFixture(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
QString hash(const QByteArray& value){return QString::fromStdString(prometheus::integrity::object_hash(std::string_view(value.constData(),static_cast<std::size_t>(value.size()))));}
}

int main(int argc,char** argv){
  QGuiApplication app(argc,argv);
  const QString fixture=QStringLiteral(PROMETHEUS_SOURCE_DIR)+"/fixtures/assemblies/motor-arm.step";
  QTemporaryDir temporary;if(!temporary.isValid())return 1;
  const QString project=temporary.filePath("arm.prometheus");
  CadController first;
  if(!first.importStep(fixture)||first.parts().size()!=3||first.interferences().size()!=1){std::cerr<<"fixture import failed\n";return 2;}
  if(first.measureBetween(0,1).value("center_distance_m").toDouble()<=0){std::cerr<<"measurement metadata failed\n";return 3;}
  EngineeringController checks;checks.defineRevoluteJoint(1,2,"Z",0,90);checks.runGeometryChecks(first.interferences());
  if(checks.findings().size()!=1||checks.findings().front().toMap().value("status")!="caution"){std::cerr<<"unclassified interference rule failed\n";return 4;}
  const auto hit=first.interferences().front().toMap();first.classifyInterference(hit.value("first_id").toString(),hit.value("second_id").toString(),"intended_engagement");
  checks.runGeometryChecks(first.interferences());if(checks.findings().front().toMap().value("status")!="information"){std::cerr<<"intended engagement rule failed\n";return 5;}
  checks.runGeometryChecks(first.interferences(),{QVariantMap{{"moving_id","arm"},{"other_id","base"},{"moving_name","Arm"},{"other_name","Base plate"},{"first_angle_deg",35.0},{"maximum_volume_m3",1e-6},{"samples_tested",19}}},true);if(checks.findings()[1].toMap().value("status")!="fail"){std::cerr<<"motion collision rule failed\n";return 5;}
  QEventLoop placement_loop;QObject::connect(&first,&CadController::geometryFinished,&placement_loop,&QEventLoop::quit);QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.setPartPlacement(2,0.01,0,0,0,0,30);placement_loop.exec();auto* placed=qobject_cast<CadPart*>(first.parts()[2].value<QObject*>());if(first.geometryBusy()||placed->translationX()!=0.01||placed->rotationZ()!=30||placed->aabbSizeY()<=placed->sizeY()){std::cerr<<"placement recompute failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.undoPlacement();placement_loop.exec();if(placed->rotationZ()!=0||!first.canRedo()){std::cerr<<"placement undo failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.redoPlacement();placement_loop.exec();if(placed->rotationZ()!=30){std::cerr<<"placement redo failed\n";return 5;}
  if(!first.beginPlacementPreview(2)){std::cerr<<"placement preview did not begin\n";return 5;}first.previewPartPlacement(2,0.02,0,0,0,0,45);if(first.geometryBusy()||placed->translationX()!=0.02||placed->rotationZ()!=45){std::cerr<<"placement preview was not lightweight\n";return 5;}first.cancelPlacementPreview();if(placed->translationX()!=0.01||placed->rotationZ()!=30){std::cerr<<"placement preview cancel failed\n";return 5;}if(!first.beginPlacementPreview(2)){std::cerr<<"second placement preview did not begin\n";return 5;}first.previewPartPlacement(2,0.015,0,0,0,0,35);QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.commitPlacementPreview();placement_loop.exec();if(first.geometryBusy()||placed->translationX()!=0.015||placed->rotationZ()!=35){std::cerr<<"placement preview commit failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.undoPlacement();placement_loop.exec();if(placed->translationX()!=0.01||placed->rotationZ()!=30){std::cerr<<"placement preview undo failed\n";return 5;}
  first.setPartPlacement(2,0.01,0,0,0,0,90);QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);placement_loop.exec();const auto local_x=first.localAxisDirection(2,"X");if(std::abs(local_x.value("x").toDouble())>1e-9||std::abs(local_x.value("y").toDouble()-1)>1e-9){std::cerr<<"local axis direction failed\n";return 5;}const auto composed=first.composeLocalRotation(0,0,90,"X",30);if(std::abs(composed.value("rx").toDouble()-30)>1e-9||std::abs(composed.value("rz").toDouble()-90)>1e-9){std::cerr<<"local rotation composition failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.undoPlacement();placement_loop.exec();
  if(first.placementAnchors(2).size()!=7){std::cerr<<"placement anchor generation failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);if(!first.snapPlacementAnchors(2,"x_min",0,"x_max")){std::cerr<<"placement anchor snap rejected\n";return 5;}placement_loop.exec();const auto moving_anchor=first.placementAnchors(2)[1].toMap(),target_anchor=first.placementAnchors(0)[2].toMap();if(std::abs(moving_anchor.value("x").toDouble()-target_anchor.value("x").toDouble())>1e-9||std::abs(moving_anchor.value("y").toDouble()-target_anchor.value("y").toDouble())>1e-9||std::abs(moving_anchor.value("z").toDouble()-target_anchor.value("z").toDouble())>1e-9){std::cerr<<"placement anchors did not coincide\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.undoPlacement();placement_loop.exec();
  QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);if(!first.confirmAnchorConnection(2,"x_min",0,"x_max","fixed")){std::cerr<<"semantic anchor connection rejected\n";return 5;}placement_loop.exec();if(first.connections().size()!=1||first.connections().front().toMap().value("semantic_status")!="provisional_geometry_anchor"){std::cerr<<"semantic anchor connection missing\n";return 5;}const double saved_translation_x=placed->translationX();
  QStringList ids;for(const auto& value:first.parts())ids<<qobject_cast<CadPart*>(value.value<QObject*>())->persistentId();
  first.bindComponent(1,{{"id","revision-123"},{"manufacturer","Fixture Works"},{"part_number","PM-36"}});ProjectController first_project(&first,&checks);
  first_project.saveAsVersion2(QUrl::fromLocalFile(project));if(first_project.errorCode()!=""){std::cerr<<"atomic Save As failed\n";return 6;}
  QFile file(project);if(!file.open(QIODevice::ReadOnly)||QJsonDocument::fromJson(file.readAll()).object().value("schema_version")!="2.0.0"){std::cerr<<"manifest invalid\n";return 7;}file.close();
  auto unresolved=prometheus::run_store::open_read_only(project.toStdString());if(!unresolved.has_value()){std::cerr<<"saved project did not reopen for unresolved-state fixture\n";return 7;}unresolved.value().name="Reviewed arm project";unresolved.value().component_bindings.push_back({"retired-motor","revision-retired","Retired motor"});unresolved.value().placement_overrides.push_back({"retired-arm",0.02,0,0,0,0,5,"extrinsic_X_then_Y_then_Z_about_imported_bounds_center"});unresolved.value().connections.push_back({"retired-arm:x_min->base:x_max","retired-arm","Retired arm","x_min","base","Base plate","x_max","fixed",true,"derived_bounds","provisional_geometry_anchor"});const auto unresolved_save=prometheus::run_store::save_project_snapshot(project.toStdString(),unresolved.value());if(!unresolved_save.has_value()){std::cerr<<"unresolved-state fixture did not save: "<<unresolved_save.diagnostic().code<<": "<<unresolved_save.diagnostic().message<<"\n";return 7;}
  CadController reopened;EngineeringController reopened_checks;ProjectController reopened_project(&reopened,&reopened_checks);QEventLoop loop;bool success=false;QObject::connect(&reopened,&CadController::importFinished,&loop,[&](bool ok){success=ok;loop.quit();});QTimer::singleShot(5000,&loop,&QEventLoop::quit);reopened_project.openProject(QUrl::fromLocalFile(project));loop.exec();
  if(!success||reopened.parts().size()!=3||reopened.sourceName()!="motor-arm.step"){std::cerr<<"reopen failed\n";return 8;}
  QStringList reopened_ids;for(const auto& value:reopened.parts())reopened_ids<<qobject_cast<CadPart*>(value.value<QObject*>())->persistentId();if(ids!=reopened_ids){std::cerr<<"persistent CAD identifiers changed\n";return 9;}
  if(qobject_cast<CadPart*>(reopened.parts()[1].value<QObject*>())->componentRevisionId()!="revision-123"){std::cerr<<"component binding was not restored\n";return 10;}
  if(!reopened_checks.jointConfigured()||reopened_checks.findings().size()!=2||reopened_project.committedRunCount()!=0){std::cerr<<"geometry-only engineering state was not restored\n";return 11;}
  if(reopened.interferences().front().toMap().value("classification")!="intended_engagement"){std::cerr<<"interference classification was not restored\n";return 12;}
  if(std::abs(qobject_cast<CadPart*>(reopened.parts()[2].value<QObject*>())->translationX()-saved_translation_x)>1e-9){std::cerr<<"placement override was not restored\n";return 13;}
  if(qobject_cast<CadPart*>(reopened.parts()[2].value<QObject*>())->rotationZ()!=30){std::cerr<<"rotation override was not restored\n";return 14;}
  if(reopened.connections().size()!=1||reopened.connections().front().toMap().value("connection_type")!="fixed"){std::cerr<<"semantic connection was not restored\n";return 15;}
  ExecutionController execution(&reopened_project);const auto motor_entity=qobject_cast<CadPart*>(reopened.parts()[1].value<QObject*>())->persistentId();execution.setPendingCadEntityId(motor_entity);const QString contracts=QStringLiteral(PROMETHEUS_SOURCE_DIR)+"/fixtures/contracts/";const auto motor_a=readFixture(contracts+"execution-component-v2.motor-a.jcs");if(motor_a.isEmpty()){std::cerr<<"motor package fixture missing\n";return 16;}execution.acceptExactPackage(motor_a,hash(motor_a));execution.setScenarioDraft({{"payload_mass_kg",8.0},{"arm_radius_m",0.2},{"rotation_degrees",90.0},{"move_duration_s",1.2},{"hold_duration_s",4.0},{"cycle_duration_s",10.0},{"ambient_temperature_c",35.0}});execution.previewScenarioDegrees();execution.confirmScenario("Evaluate the bound motor for the reviewed motor-arm operating cycle.");execution.runAnalysis();if(!waitUntil([&execution]{return !execution.busy();})||!execution.errorCode().isEmpty()||execution.runHistory().size()!=1){std::cerr<<"OCCT package-driven execution failed: "<<execution.status().toStdString()<<"/"<<execution.errorCode().toStdString()<<" "<<execution.error().toStdString()<<" history="<<execution.runHistory().size()<<"\n";return 16;}execution.selectRun(0);execution.replaySelected();if(!waitUntil([&execution]{return !execution.busy();})||execution.replayState()!="Exact match"){std::cerr<<"OCCT package-driven replay failed: "<<execution.status().toStdString()<<"/"<<execution.errorCode().toStdString()<<" "<<execution.error().toStdString()<<" replay="<<execution.replayState().toStdString()<<"\n";return 16;}if(reopened_checks.findings().size()!=2){std::cerr<<"motor execution altered geometry findings\n";return 16;}
  reopened_project.saveCurrentProject();if(!reopened_project.errorCode().isEmpty()){std::cerr<<"reopened project did not resave\n";return 16;}const auto preserved=prometheus::run_store::open_read_only(project.toStdString());if(!preserved.has_value()||preserved.value().component_bindings.size()!=2||preserved.value().placement_overrides.size()!=2||preserved.value().connections.size()!=2){std::cerr<<"unresolved CAD relationships disappeared on save\n";return 17;}if(preserved.value().name!="Reviewed arm project"){std::cerr<<"CAD source name overwrote project identity\n";return 18;}
  return 0;
}
