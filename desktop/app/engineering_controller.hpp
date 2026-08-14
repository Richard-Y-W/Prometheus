#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class EngineeringController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool jointConfigured READ jointConfigured NOTIFY changed)
  Q_PROPERTY(QVariantMap joint READ joint NOTIFY changed)
  Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
  Q_PROPERTY(QVariantList unknowns READ unknowns NOTIFY changed)
  Q_PROPERTY(QVariantMap coverage READ coverage NOTIFY changed)
  Q_PROPERTY(QString geometryStatus READ geometryStatus NOTIFY changed)
  Q_PROPERTY(
      QVariantMap geometryState READ snapshotGeometryState NOTIFY changed)

public:
  explicit EngineeringController(QObject *parent = nullptr) : QObject(parent) {}

  bool jointConfigured() const { return !joint_.isEmpty(); }
  QVariantMap joint() const { return joint_; }
  QVariantList findings() const { return findings_; }
  QVariantList unknowns() const { return unknowns_; }
  QVariantMap coverage() const { return coverage_; }
  QString geometryStatus() const { return geometry_status_; }
  QVariantMap snapshotGeometryState() const;

  Q_INVOKABLE void defineRevoluteJoint(int source, int target,
                                       const QString &axis, double minimumDeg,
                                       double maximumDeg, double pivotX = 0,
                                       double pivotY = 0, double pivotZ = 0);
  Q_INVOKABLE void runGeometryChecks(const QVariantList &interferences = {},
                                     const QVariantList &sweepResults = {},
                                     bool sweepEvaluated = false,
                                     bool staticInterferenceEvaluated = true);
  Q_INVOKABLE void restoreGeometryState(const QVariantMap &state);

signals:
  void changed();

private:
  QVariantMap joint_;
  QVariantList findings_;
  QVariantList unknowns_;
  QVariantMap coverage_;
  QString geometry_status_{"not_evaluated"};
};
