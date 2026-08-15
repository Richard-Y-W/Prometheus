#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/calculix_result.hpp"
#include "prometheus/structural/surface_groups.hpp"
#include "prometheus/structural/types.hpp"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQuick3D/QQuick3DGeometry>

#include <optional>
#include <string>

class ProjectController;

class StructuralResultGeometry final : public QQuick3DGeometry {
  Q_OBJECT
public:
  StructuralResultGeometry(
      const prometheus::structural::VolumeMesh &mesh,
      const std::vector<prometheus::structural::BoundaryFace> &boundary,
      const prometheus::structural::CalculixMetrics &fields,
      double deformation_scale, QObject *parent = nullptr);
};

class StructuralController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QVariantMap meshSummary READ meshSummary NOTIFY changed)
  Q_PROPERTY(QVariantList surfacePatches READ surfacePatches NOTIFY changed)
  Q_PROPERTY(QVariantList selectedLoadPatchIds READ selectedLoadPatchIds NOTIFY changed)
  Q_PROPERTY(QVariantList selectedRestraintPatchIds READ selectedRestraintPatchIds NOTIFY changed)
  Q_PROPERTY(QVariantList blockers READ blockers NOTIFY changed)
  Q_PROPERTY(QVariantMap requestPreview READ requestPreview NOTIFY changed)
  Q_PROPERTY(bool canRun READ canRun NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QVariantMap lastRun READ lastRun NOTIFY changed)
  Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
  Q_PROPERTY(QQuick3DGeometry *resultGeometry READ resultGeometry NOTIFY changed)
  Q_PROPERTY(QVariantMap resultView READ resultView NOTIFY changed)

public:
  explicit StructuralController(ProjectController *project = nullptr,
                                QObject *parent = nullptr);

  QString status() const { return status_; }
  QString error() const { return error_; }
  QVariantMap meshSummary() const { return mesh_summary_; }
  QVariantList surfacePatches() const { return surface_patches_; }
  QVariantList selectedLoadPatchIds() const;
  QVariantList selectedRestraintPatchIds() const;
  QVariantList blockers() const { return blockers_; }
  QVariantMap requestPreview() const { return request_preview_; }
  bool canRun() const { return can_run_; }
  bool busy() const { return busy_; }
  QVariantMap lastRun() const { return last_run_; }
  QVariantList findings() const { return findings_; }
  QQuick3DGeometry *resultGeometry() const { return result_geometry_; }
  QVariantMap resultView() const { return result_view_; }

  Q_INVOKABLE void loadMesh(const QUrl &path, double coordinateScaleToM,
                            double patchAngleDegrees = 15.0);
  Q_INVOKABLE void setPatchSelected(int patchId, const QString &role,
                                    bool selected);
  Q_INVOKABLE void reviewSetup(const QVariantMap &draft);
  Q_INVOKABLE void runAnalysis(const QUrl &calculixExecutable,
                               const QUrl &outputRoot);
  Q_INVOKABLE void commitLastRun();
  Q_INVOKABLE void reset();

signals:
  void changed();
  void runFinished();

private:
  void rebuildPreview();
  QString status_{"mesh_required"};
  QString error_;
  QVariantMap mesh_summary_;
  QVariantList surface_patches_;
  QVariantMap draft_;
  QVariantList blockers_;
  QVariantMap request_preview_;
  bool can_run_{};
  bool busy_{};
  QVariantMap last_run_;
  QVariantList findings_;
  std::optional<prometheus::structural::StructuralRequest> compiled_request_;
  std::string compiled_setup_evidence_;
  prometheus::structural::VolumeMesh mesh_;
  std::vector<prometheus::structural::BoundaryFace> boundary_;
  std::vector<prometheus::structural::SurfacePatch> patches_;
  std::vector<int> load_patch_ids_;
  std::vector<int> restraint_patch_ids_;
  ProjectController *project_{};
  StructuralResultGeometry *result_geometry_{};
  QVariantMap result_view_;
};
