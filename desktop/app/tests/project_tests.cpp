#include "cad_controller.hpp"
#include "engineering_controller.hpp"
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

int main(int argc,char** argv){
  QGuiApplication app(argc,argv);
  const QString fixture=QStringLiteral(PROMETHEUS_SOURCE_DIR)+"/fixtures/assemblies/motor-arm.step";
  QTemporaryDir temporary;if(!temporary.isValid())return 1;
  const QString project=temporary.filePath("arm.prometheus");
  CadController first;
  if(!first.importStep(fixture)||first.parts().size()!=3||first.interferences().size()!=1){std::cerr<<"fixture import failed\n";return 2;}
  if(first.measureBetween(0,1).value("center_distance_m").toDouble()<=0){std::cerr<<"measurement metadata failed\n";return 3;}
  EngineeringController checks;checks.defineRevoluteJoint(1,2,"Z",0,90);checks.defineMotorArmScenario(8,0.2,90,1.2,4,10,35);checks.runChecks(first.interferences());
  if(checks.findings().size()!=6||checks.findings().front().toMap().value("status")!="caution"){std::cerr<<"unclassified interference rule failed\n";return 4;}
  const auto hit=first.interferences().front().toMap();first.classifyInterference(hit.value("first_id").toString(),hit.value("second_id").toString(),"intended_engagement");
  checks.runChecks(first.interferences());if(checks.findings().front().toMap().value("status")!="information"){std::cerr<<"intended engagement rule failed\n";return 5;}
  checks.runChecks(first.interferences(),{QVariantMap{{"moving_id","arm"},{"other_id","base"},{"moving_name","Arm"},{"other_name","Base plate"},{"first_angle_deg",35.0},{"maximum_volume_m3",1e-6},{"samples_tested",19}}},true);if(checks.findings()[1].toMap().value("status")!="fail"){std::cerr<<"motion collision rule failed\n";return 5;}
  QEventLoop placement_loop;QObject::connect(&first,&CadController::geometryFinished,&placement_loop,&QEventLoop::quit);QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.setPartPlacement(2,0.01,0,0,0,0,30);placement_loop.exec();auto* placed=qobject_cast<CadPart*>(first.parts()[2].value<QObject*>());if(first.geometryBusy()||placed->translationX()!=0.01||placed->rotationZ()!=30||placed->aabbSizeY()<=placed->sizeY()){std::cerr<<"placement recompute failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.undoPlacement();placement_loop.exec();if(placed->rotationZ()!=0||!first.canRedo()){std::cerr<<"placement undo failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.redoPlacement();placement_loop.exec();if(placed->rotationZ()!=30){std::cerr<<"placement redo failed\n";return 5;}
  if(!first.beginPlacementPreview(2)){std::cerr<<"placement preview did not begin\n";return 5;}first.previewPartPlacement(2,0.02,0,0,0,0,45);if(first.geometryBusy()||placed->translationX()!=0.02||placed->rotationZ()!=45){std::cerr<<"placement preview was not lightweight\n";return 5;}first.cancelPlacementPreview();if(placed->translationX()!=0.01||placed->rotationZ()!=30){std::cerr<<"placement preview cancel failed\n";return 5;}if(!first.beginPlacementPreview(2)){std::cerr<<"second placement preview did not begin\n";return 5;}first.previewPartPlacement(2,0.015,0,0,0,0,35);QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.commitPlacementPreview();placement_loop.exec();if(first.geometryBusy()||placed->translationX()!=0.015||placed->rotationZ()!=35){std::cerr<<"placement preview commit failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.undoPlacement();placement_loop.exec();if(placed->translationX()!=0.01||placed->rotationZ()!=30){std::cerr<<"placement preview undo failed\n";return 5;}
  first.setPartPlacement(2,0.01,0,0,0,0,90);QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);placement_loop.exec();const auto local_x=first.localAxisDirection(2,"X");if(std::abs(local_x.value("x").toDouble())>1e-9||std::abs(local_x.value("y").toDouble()-1)>1e-9){std::cerr<<"local axis direction failed\n";return 5;}const auto composed=first.composeLocalRotation(0,0,90,"X",30);if(std::abs(composed.value("rx").toDouble()-30)>1e-9||std::abs(composed.value("rz").toDouble()-90)>1e-9){std::cerr<<"local rotation composition failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.undoPlacement();placement_loop.exec();
  if(first.placementAnchors(2).size()!=7){std::cerr<<"placement anchor generation failed\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);if(!first.snapPlacementAnchors(2,"x_min",0,"x_max")){std::cerr<<"placement anchor snap rejected\n";return 5;}placement_loop.exec();const auto moving_anchor=first.placementAnchors(2)[1].toMap(),target_anchor=first.placementAnchors(0)[2].toMap();if(std::abs(moving_anchor.value("x").toDouble()-target_anchor.value("x").toDouble())>1e-9||std::abs(moving_anchor.value("y").toDouble()-target_anchor.value("y").toDouble())>1e-9||std::abs(moving_anchor.value("z").toDouble()-target_anchor.value("z").toDouble())>1e-9){std::cerr<<"placement anchors did not coincide\n";return 5;}QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);first.undoPlacement();placement_loop.exec();
  QTimer::singleShot(5000,&placement_loop,&QEventLoop::quit);if(!first.confirmAnchorConnection(2,"x_min",0,"x_max","fixed")){std::cerr<<"semantic anchor connection rejected\n";return 5;}placement_loop.exec();if(first.connections().size()!=1||first.connections().front().toMap().value("semantic_status")!="provisional_geometry_anchor"){std::cerr<<"semantic anchor connection missing\n";return 5;}const double saved_translation_x=placed->translationX();
  QStringList ids;for(const auto& value:first.parts())ids<<qobject_cast<CadPart*>(value.value<QObject*>())->persistentId();
  first.bindComponent(1,{{"id","revision-123"},{"manufacturer","Fixture Works"},{"part_number","PM-36"}});first.setEngineeringState(checks.projectState());
  if(!first.saveProject(project)){std::cerr<<"atomic save failed\n";return 6;}
  QFile file(project);if(!file.open(QIODevice::ReadOnly)||QJsonDocument::fromJson(file.readAll()).object().value("schema_version")!="1.0.0"){std::cerr<<"manifest invalid\n";return 7;}
  CadController reopened;QEventLoop loop;bool success=false;QObject::connect(&reopened,&CadController::importFinished,&loop,[&](bool ok){success=ok;loop.quit();});QTimer::singleShot(5000,&loop,&QEventLoop::quit);reopened.openProject(project);loop.exec();
  if(!success||reopened.parts().size()!=3||reopened.sourceName()!="motor-arm.step"){std::cerr<<"reopen failed\n";return 8;}
  QStringList reopened_ids;for(const auto& value:reopened.parts())reopened_ids<<qobject_cast<CadPart*>(value.value<QObject*>())->persistentId();if(ids!=reopened_ids){std::cerr<<"persistent CAD identifiers changed\n";return 9;}
  if(qobject_cast<CadPart*>(reopened.parts()[1].value<QObject*>())->componentRevisionId()!="revision-123"){std::cerr<<"component binding was not restored\n";return 10;}
  if(reopened.engineeringState().value("scenario").toMap().value("payload_kg").toDouble()!=8.0){std::cerr<<"engineering state was not restored\n";return 11;}
  if(reopened.interferences().front().toMap().value("classification")!="intended_engagement"){std::cerr<<"interference classification was not restored\n";return 12;}
  if(std::abs(qobject_cast<CadPart*>(reopened.parts()[2].value<QObject*>())->translationX()-saved_translation_x)>1e-9){std::cerr<<"placement override was not restored\n";return 13;}
  if(qobject_cast<CadPart*>(reopened.parts()[2].value<QObject*>())->rotationZ()!=30){std::cerr<<"rotation override was not restored\n";return 14;}
  if(reopened.connections().size()!=1||reopened.connections().front().toMap().value("connection_type")!="fixed"){std::cerr<<"semantic connection was not restored\n";return 15;}
  return 0;
}
