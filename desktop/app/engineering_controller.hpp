#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Forward-declared only: project_controller.hpp already takes an
// EngineeringController*, so a full include here would cycle. The .cpp
// includes it for the persist call, matching
// component_binding_controller.hpp/.cpp's existing pattern.
class ProjectController;

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

  // A ProjectController is not available at construction (main.cpp builds
  // EngineeringController before ProjectController), so it is injected here
  // once both exist, mirroring ComponentBindingController's optional
  // ProjectController* but via a setter instead of a constructor argument.
  void setProjectController(ProjectController *project) { project_ = project; }

  Q_INVOKABLE void defineRevoluteJoint(int source, int target,
                                       const QString &axis, double minimumDeg,
                                       double maximumDeg, double pivotX = 0,
                                       double pivotY = 0, double pivotZ = 0,
                                       const QString &sourceEntityId = {},
                                       const QString &targetEntityId = {});
  Q_INVOKABLE void runGeometryChecks(const QVariantList &interferences = {},
                                     const QVariantList &sweepResults = {},
                                     bool sweepEvaluated = false,
                                     bool staticInterferenceEvaluated = true);
  Q_INVOKABLE void restoreGeometryState(const QVariantMap &state);

  // Phase 6 checkpoint 7: a pure, on-demand read of every JointBinding
  // involving this CAD entity (as either source or target) --
  // {revision, active, other_entity_id, axis, minimum_deg, maximum_deg} per
  // entry, most recent first. Mirrors ComponentBindingController::
  // bindingHistory: no cached state, no new signal, reads project_->
  // project() fresh on every call.
  Q_INVOKABLE QVariantList jointHistory(const QString &cadEntityId) const;

signals:
  void changed();

private:
  QVariantMap joint_;
  QVariantList findings_;
  QVariantList unknowns_;
  QVariantMap coverage_;
  QString geometry_status_{"not_evaluated"};
  ProjectController *project_{nullptr};

  void persistJointBindingEdge(const QString &sourceEntityId,
                               const QString &targetEntityId,
                               const QString &axis, double minimumDeg,
                               double maximumDeg, double pivotX,
                               double pivotY, double pivotZ);
};
