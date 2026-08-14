#pragma once

#include "prometheus/cad/types.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQuick3D/QQuick3DGeometry>

#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class MeshGeometry final : public QQuick3DGeometry {
  Q_OBJECT

public:
  MeshGeometry(const std::vector<float> &positions,
               const std::vector<std::uint32_t> &indices,
               QObject *parent = nullptr);
};

class CadPart final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString name READ name CONSTANT)
  Q_PROPERTY(QString persistentId READ persistentId CONSTANT)
  Q_PROPERTY(QQuick3DGeometry *geometry READ geometry CONSTANT)
  Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
  Q_PROPERTY(QString componentRevisionId READ componentRevisionId NOTIFY
                 bindingChanged)
  Q_PROPERTY(QString componentLabel READ componentLabel NOTIFY bindingChanged)
  Q_PROPERTY(double centerX READ centerX CONSTANT)
  Q_PROPERTY(double centerY READ centerY CONSTANT)
  Q_PROPERTY(double centerZ READ centerZ CONSTANT)
  Q_PROPERTY(double sizeX READ sizeX CONSTANT)
  Q_PROPERTY(double sizeY READ sizeY CONSTANT)
  Q_PROPERTY(double sizeZ READ sizeZ CONSTANT)
  Q_PROPERTY(double volumeM3 READ volumeM3 CONSTANT)
  Q_PROPERTY(double surfaceAreaM2 READ surfaceAreaM2 CONSTANT)
  Q_PROPERTY(int faceCount READ faceCount CONSTANT)
  Q_PROPERTY(int edgeCount READ edgeCount CONSTANT)
  Q_PROPERTY(double translationX READ translationX NOTIFY placementChanged)
  Q_PROPERTY(double translationY READ translationY NOTIFY placementChanged)
  Q_PROPERTY(double translationZ READ translationZ NOTIFY placementChanged)
  Q_PROPERTY(double rotationX READ rotationX NOTIFY placementChanged)
  Q_PROPERTY(double rotationY READ rotationY NOTIFY placementChanged)
  Q_PROPERTY(double rotationZ READ rotationZ NOTIFY placementChanged)
  Q_PROPERTY(double aabbSizeX READ aabbSizeX NOTIFY placementChanged)
  Q_PROPERTY(double aabbSizeY READ aabbSizeY NOTIFY placementChanged)
  Q_PROPERTY(double aabbSizeZ READ aabbSizeZ NOTIFY placementChanged)

public:
  CadPart(QString name, QString id, const prometheus::cad::BoundingBox &bounds,
          double volume, double area, int faces, int edges,
          std::unique_ptr<MeshGeometry> geometry, QObject *parent = nullptr);

  QString name() const { return name_; }
  QString persistentId() const { return id_; }
  QQuick3DGeometry *geometry() const { return geometry_.get(); }
  bool visible() const { return visible_; }
  void setVisible(bool value) {
    if (visible_ == value) {
      return;
    }
    visible_ = value;
    emit visibleChanged();
  }
  QString componentRevisionId() const { return revision_id_; }
  QString componentLabel() const { return component_label_; }
  void bindComponent(QString revision, QString label) {
    revision_id_ = std::move(revision);
    component_label_ = std::move(label);
    emit bindingChanged();
  }
  double centerX() const { return (bounds_.min_x + bounds_.max_x) / 2; }
  double centerY() const { return (bounds_.min_y + bounds_.max_y) / 2; }
  double centerZ() const { return (bounds_.min_z + bounds_.max_z) / 2; }
  double sizeX() const { return bounds_.max_x - bounds_.min_x; }
  double sizeY() const { return bounds_.max_y - bounds_.min_y; }
  double sizeZ() const { return bounds_.max_z - bounds_.min_z; }
  double volumeM3() const { return volume_m3_; }
  double surfaceAreaM2() const { return surface_area_m2_; }
  int faceCount() const { return face_count_; }
  int edgeCount() const { return edge_count_; }
  double translationX() const { return tx_; }
  double translationY() const { return ty_; }
  double translationZ() const { return tz_; }
  double rotationX() const { return rx_; }
  double rotationY() const { return ry_; }
  double rotationZ() const { return rz_; }
  void setPlacement(double x, double y, double z, double rx, double ry,
                    double rz) {
    tx_ = x;
    ty_ = y;
    tz_ = z;
    rx_ = rx;
    ry_ = ry;
    rz_ = rz;
    emit placementChanged();
  }
  double aabbSizeX() const;
  double aabbSizeY() const;
  double aabbSizeZ() const;

signals:
  void visibleChanged();
  void bindingChanged();
  void placementChanged();

private:
  QString name_;
  QString id_;
  QString revision_id_;
  QString component_label_;
  prometheus::cad::BoundingBox bounds_;
  double volume_m3_{};
  double surface_area_m2_{};
  double tx_{};
  double ty_{};
  double tz_{};
  double rx_{};
  double ry_{};
  double rz_{};
  int face_count_{};
  int edge_count_{};
  std::unique_ptr<MeshGeometry> geometry_;
  bool visible_{true};
};

struct CadImportSnapshot final {
  prometheus::cad::StepImportResult result;
  QString assembly_artifact_hash;
};

class CadController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList parts READ parts NOTIFY partsChanged)
  Q_PROPERTY(QString sourceName READ sourceName NOTIFY partsChanged)
  Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY partsChanged)
  Q_PROPERTY(QString assemblyArtifactHash READ assemblyArtifactHash NOTIFY
                 partsChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  Q_PROPERTY(QStringList warnings READ warnings NOTIFY partsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(double centerX READ centerX NOTIFY partsChanged)
  Q_PROPERTY(double centerY READ centerY NOTIFY partsChanged)
  Q_PROPERTY(double centerZ READ centerZ NOTIFY partsChanged)
  Q_PROPERTY(double sceneDiameter READ sceneDiameter NOTIFY partsChanged)
  Q_PROPERTY(double sceneMinZ READ sceneMinZ NOTIFY partsChanged)
  Q_PROPERTY(QVariantList interferences READ interferences NOTIFY partsChanged)
  Q_PROPERTY(
      QVariantList connections READ connections NOTIFY connectionsChanged)
  Q_PROPERTY(QVariantList sweepResults READ sweepResults NOTIFY sweepFinished)
  Q_PROPERTY(bool sweepBusy READ sweepBusy NOTIFY sweepBusyChanged)
  Q_PROPERTY(bool geometryBusy READ geometryBusy NOTIFY geometryBusyChanged)
  Q_PROPERTY(bool collisionDeferred READ collisionDeferred NOTIFY partsChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
  explicit CadController(QObject *parent = nullptr);

  QVariantList parts() const { return parts_; }
  QString sourceName() const { return source_name_; }
  QString sourcePath() const { return source_path_; }
  QString assemblyArtifactHash() const { return assembly_artifact_hash_; }
  QString error() const { return error_; }
  QStringList warnings() const { return warnings_; }
  bool busy() const { return busy_; }
  double centerX() const { return center_x_; }
  double centerY() const { return center_y_; }
  double centerZ() const { return center_z_; }
  double sceneDiameter() const { return scene_diameter_; }
  double sceneMinZ() const { return scene_min_z_; }
  QVariantList interferences() const { return interferences_; }
  QVariantList connections() const { return connections_; }
  QVariantList sweepResults() const { return sweep_results_; }
  bool sweepBusy() const { return sweep_busy_; }
  bool geometryBusy() const { return geometry_busy_; }
  bool collisionDeferred() const { return collision_deferred_; }
  bool canUndo() const { return !undo_stack_.isEmpty(); }
  bool canRedo() const { return !redo_stack_.isEmpty(); }

  Q_INVOKABLE bool importStep(const QString &path);
  Q_INVOKABLE void importStepAsync(const QString &path);
  Q_INVOKABLE void cancelImport();
  Q_INVOKABLE QVariantMap snapshotCadState() const;
  Q_INVOKABLE void restoreCadState(const QVariantMap &state);
  Q_INVOKABLE QString captureAssemblyArtifactHash();
  Q_INVOKABLE bool recheckAssemblyArtifact(const QString &expectedHash);
  Q_INVOKABLE void toggleVisible(int index);
  Q_INVOKABLE void isolate(int index);
  Q_INVOKABLE void showAll();
  Q_INVOKABLE void bindComponent(int index, const QVariantMap &revision);
  Q_INVOKABLE QVariantMap measureBetween(int first, int second) const;
  Q_INVOKABLE void classifyInterference(const QString &firstId,
                                        const QString &secondId,
                                        const QString &classification);
  Q_INVOKABLE void runJointSweepAsync(int movingIndex, int excludedIndex,
                                      double pivotX, double pivotY,
                                      double pivotZ, const QString &axis,
                                      double minimumDeg, double maximumDeg);
  Q_INVOKABLE void setPartTranslation(int index, double xM, double yM,
                                      double zM);
  Q_INVOKABLE void resetPartTranslation(int index);
  Q_INVOKABLE void setPartPlacement(int index, double xM, double yM, double zM,
                                    double rxDeg, double ryDeg, double rzDeg);
  Q_INVOKABLE void undoPlacement();
  Q_INVOKABLE void redoPlacement();
  Q_INVOKABLE bool beginPlacementPreview(int index);
  Q_INVOKABLE void previewPartPlacement(int index, double xM, double yM,
                                        double zM, double rxDeg, double ryDeg,
                                        double rzDeg);
  Q_INVOKABLE void commitPlacementPreview();
  Q_INVOKABLE void cancelPlacementPreview();
  Q_INVOKABLE QVariantMap localAxisDirection(int index,
                                             const QString &axis) const;
  Q_INVOKABLE QVariantMap composeLocalRotation(double rxDeg, double ryDeg,
                                               double rzDeg,
                                               const QString &axis,
                                               double deltaDeg) const;
  Q_INVOKABLE QVariantList placementAnchors(int index) const;
  Q_INVOKABLE bool snapPlacementAnchors(int movingIndex,
                                        const QString &movingAnchor,
                                        int targetIndex,
                                        const QString &targetAnchor);
  Q_INVOKABLE bool confirmAnchorConnection(int sourceIndex,
                                           const QString &sourceAnchor,
                                           int targetIndex,
                                           const QString &targetAnchor,
                                           const QString &connectionType);
  Q_INVOKABLE void removeConnection(int index);

signals:
  void partsChanged();
  void connectionsChanged();
  void errorChanged();
  void busyChanged();
  void sweepBusyChanged();
  void geometryBusyChanged();
  void historyChanged();
  void sweepFinished();
  void geometryFinished();
  void importFinished(bool success);

private:
  QVariantList parts_;
  QVariantList interferences_;
  QVariantList connections_;
  QVariantList sweep_results_;
  QVariantList undo_stack_;
  QVariantList redo_stack_;
  QString source_name_;
  QString source_path_;
  QString source_reference_;
  QString assembly_artifact_hash_;
  QString error_;
  QString error_before_artifact_check_;
  QStringList warnings_;
  QVariantList pending_bindings_;
  QVariantList pending_interference_classifications_;
  QVariantList pending_placements_;
  QVariantList pending_connections_;
  QVariantMap active_placement_preview_;
  bool busy_{};
  bool sweep_busy_{};
  bool geometry_busy_{};
  bool collision_deferred_{};
  bool artifact_check_error_{};
  double center_x_{};
  double center_y_{};
  double center_z_{};
  double scene_min_z_{};
  double scene_diameter_{1};
  QFutureWatcher<CadImportSnapshot> watcher_;
  QFutureWatcher<std::vector<prometheus::cad::SweepInterference>>
      sweep_watcher_;
  QFutureWatcher<std::vector<prometheus::cad::StaticInterference>>
      geometry_watcher_;

  void clearParts();
  void setArtifactCheckError(QString message);
  void clearArtifactCheckError();
  void applyResult(const prometheus::cad::StepImportResult &result);
  void setBusy(bool value);
  void startImport(const QString &resolvedPath, const QString &sourceReference);
  void applyBindings();
  void applyInterferenceClassifications();
  void applyPlacements();
  void applyConnections();
  void refreshInterferencesAsync();
  std::vector<prometheus::cad::PartPlacement> placements() const;
  QVariantMap placementState(int index) const;
  void applyPlacementState(const QVariantMap &state);
};
