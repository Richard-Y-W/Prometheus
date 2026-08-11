#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class EngineeringController final:public QObject {
  Q_OBJECT
  Q_PROPERTY(bool jointConfigured READ jointConfigured NOTIFY changed)
  Q_PROPERTY(bool scenarioConfigured READ scenarioConfigured NOTIFY changed)
  Q_PROPERTY(QVariantMap joint READ joint NOTIFY changed)
  Q_PROPERTY(QVariantMap scenario READ scenario NOTIFY changed)
  Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
  Q_PROPERTY(QString runStatus READ runStatus NOTIFY changed)
  Q_PROPERTY(QVariantMap projectState READ projectState NOTIFY changed)
public:
  explicit EngineeringController(QObject* parent=nullptr):QObject(parent){}
  bool jointConfigured()const{return !joint_.isEmpty();}bool scenarioConfigured()const{return !scenario_.isEmpty();}
  QVariantMap joint()const{return joint_;}QVariantMap scenario()const{return scenario_;}QVariantList findings()const{return findings_;}QString runStatus()const{return run_status_;}
  QVariantMap projectState()const{return {{"joint",joint_},{"scenario",scenario_},{"findings",findings_},{"run_status",run_status_}};}
  Q_INVOKABLE void defineRevoluteJoint(int source,int target,const QString& axis,double minimum_deg,double maximum_deg,double pivot_x=0,double pivot_y=0,double pivot_z=0);
  Q_INVOKABLE void defineMotorArmScenario(double payload_kg,double arm_m,double rotation_deg,double move_s,double hold_s,double cycle_s,double ambient_c);
  Q_INVOKABLE void runChecks(const QVariantList& interferences=QVariantList{},const QVariantList& sweep_results=QVariantList{},bool sweep_evaluated=false);Q_INVOKABLE void restore(const QVariantMap& state);
signals:void changed();
private:QVariantMap joint_,scenario_;QVariantList findings_;QString run_status_="Not run";
};
