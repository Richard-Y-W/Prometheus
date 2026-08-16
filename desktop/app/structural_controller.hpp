#pragma once

#include "structural_backend.hpp"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQuick3D/QQuick3DGeometry>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ProjectController;

class StructuralMeshGeometry final : public QQuick3DGeometry {
  Q_OBJECT
public:
  StructuralMeshGeometry(
      const prometheus::structural::VolumeMesh &mesh,
      const std::vector<prometheus::structural::BoundaryFace> &boundary,
      const std::vector<std::array<int, 3>> &highlighted_faces = {},
      QObject *parent = nullptr);
};

class StructuralResultGeometry final : public QQuick3DGeometry {
  Q_OBJECT
public:
  StructuralResultGeometry(
      const prometheus::structural::VolumeMesh &mesh,
      const std::vector<prometheus::structural::BoundaryFace> &boundary,
      const prometheus::structural::CalculixDat &normalized,
      const prometheus::structural::CalculixMetrics &metrics,
      double deformation_scale, QObject *parent = nullptr);
};

class StructuralController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QVariantMap meshSummary READ meshSummary NOTIFY changed)
  Q_PROPERTY(QVariantList surfacePatches READ surfacePatches NOTIFY changed)
  Q_PROPERTY(QVariantMap activeSurfacePatch READ activeSurfacePatch NOTIFY changed)
  Q_PROPERTY(QVariantList selectedLoadPatchIds READ selectedLoadPatchIds NOTIFY changed)
  Q_PROPERTY(QVariantList selectedRestraintPatchIds READ selectedRestraintPatchIds NOTIFY changed)
  Q_PROPERTY(QVariantList materialCandidates READ materialCandidates NOTIFY changed)
  Q_PROPERTY(QVariantList blockers READ blockers NOTIFY changed)
  Q_PROPERTY(QVariantMap requestPreview READ requestPreview NOTIFY changed)
  Q_PROPERTY(bool canRun READ canRun NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QVariantMap lastRun READ lastRun NOTIFY changed)
  Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
  Q_PROPERTY(QQuick3DGeometry *meshGeometry READ meshGeometry NOTIFY changed)
  Q_PROPERTY(QQuick3DGeometry *highlightGeometry READ highlightGeometry NOTIFY changed)
  Q_PROPERTY(QQuick3DGeometry *resultGeometry READ resultGeometry NOTIFY changed)
  Q_PROPERTY(QVariantMap resultView READ resultView NOTIFY changed)
  Q_PROPERTY(QVariantList storedRuns READ storedRuns NOTIFY changed)
  Q_PROPERTY(QVariantMap setupDraft READ setupDraft NOTIFY changed)
  Q_PROPERTY(QString refinementStage READ refinementStage NOTIFY changed)
  Q_PROPERTY(bool hasRefinementBaseline READ hasRefinementBaseline NOTIFY changed)
  Q_PROPERTY(bool sharedInputsLocked READ sharedInputsLocked NOTIFY changed)
  Q_PROPERTY(QVariantMap baselineRun READ baselineRun NOTIFY changed)
  Q_PROPERTY(QVariantMap refinementComparison READ refinementComparison NOTIFY changed)

public:
  explicit StructuralController(
      ProjectController *project = nullptr, QObject *parent = nullptr,
      std::shared_ptr<const StructuralBackend> backend =
          makeLocalStructuralBackend());

  QString status() const { return status_; }
  QString error() const { return error_; }
  QVariantMap meshSummary() const { return mesh_summary_; }
  QVariantList surfacePatches() const { return surface_patches_; }
  QVariantMap activeSurfacePatch() const { return active_surface_patch_; }
  QVariantList selectedLoadPatchIds() const;
  QVariantList selectedRestraintPatchIds() const;
  QVariantList materialCandidates() const { return material_candidates_; }
  QVariantList blockers() const { return blockers_; }
  QVariantMap requestPreview() const { return request_preview_; }
  bool canRun() const { return can_run_; }
  bool busy() const { return busy_; }
  QVariantMap lastRun() const { return last_run_; }
  QVariantList findings() const { return findings_; }
  QQuick3DGeometry *meshGeometry() const { return mesh_geometry_; }
  QQuick3DGeometry *highlightGeometry() const { return highlight_geometry_; }
  QQuick3DGeometry *resultGeometry() const { return result_geometry_; }
  QVariantMap resultView() const { return result_view_; }
  QVariantList storedRuns() const { return stored_runs_; }
  QVariantMap setupDraft() const { return draft_; }
  QString refinementStage() const { return refinement_stage_; }
  bool hasRefinementBaseline() const { return baseline_sample_ != nullptr; }
  bool sharedInputsLocked() const { return baseline_sample_ != nullptr; }
  QVariantMap baselineRun() const { return baseline_run_; }
  QVariantMap refinementComparison() const { return refinement_comparison_; }

  Q_INVOKABLE void loadMesh(const QUrl &path, double coordinateScaleToM,
                            double patchAngleDegrees = 15.0);
  Q_INVOKABLE void setPatchAngle(double patchAngleDegrees);
  Q_INVOKABLE void setActiveSurfacePatch(int patchId);
  Q_INVOKABLE void setPatchSelected(int patchId, const QString &role,
                                    bool selected);
  Q_INVOKABLE bool loadMaterialEvidence(const QUrl &source);
  Q_INVOKABLE void selectMaterialCandidate(const QString &candidateId,
                                            const QString &applicability);
  Q_INVOKABLE void reviewSetup(const QVariantMap &draft);
  Q_INVOKABLE void runAnalysis(const QUrl &calculixExecutable,
                               const QUrl &outputRoot);
  Q_INVOKABLE void commitLastRun();
  Q_INVOKABLE void restoreStoredRun(int index, const QUrl &outputRoot);
  Q_INVOKABLE void discardRefinementBaseline();
  Q_INVOKABLE void reloadProject();
  Q_INVOKABLE void reset();

signals:
  void changed();
  void runFinished();

private:
  void rebuildPreview();
  void rebuildPatchPresentation();
  void applyCompiledPreview(
      const prometheus::structural::StructuralSetup &setup);
  void clearCompletedRun();
  void invalidateRefinementEvidence();

  std::shared_ptr<const StructuralBackend> backend_;
  QString status_{"mesh_required"};
  QString error_;
  QVariantMap mesh_summary_;
  QVariantList surface_patches_;
  QVariantMap active_surface_patch_;
  QVariantMap draft_;
  QVariantList material_candidates_;
  QString material_evidence_path_;
  QString material_evidence_sha256_;
  QVariantList blockers_;
  QVariantMap request_preview_;
  bool can_run_{};
  bool busy_{};
  QVariantMap last_run_;
  QVariantList findings_;
  std::optional<prometheus::structural::PreparedMesh> prepared_mesh_;
  std::vector<prometheus::structural::SurfacePatch> patches_;
  std::optional<prometheus::structural::CompiledStructuralSetup>
      compiled_setup_;
  QString refinement_stage_{"coarse"};
  QVariantMap baseline_run_;
  QVariantMap refinement_comparison_;
  std::optional<prometheus::structural::StructuralRefinementCriterion>
      refinement_criterion_;
  prometheus::structural::CompletedStructuralSamplePtr baseline_sample_;
  std::optional<prometheus::structural::ReviewedBoundaryCorrespondence>
      boundary_correspondence_;
  std::optional<DesktopStructuralRefinementResult> completed_refinement_;
  std::optional<prometheus::structural::StructuralArchiveVerification>
      restored_verification_;
  std::vector<int> load_patch_ids_;
  std::vector<int> restraint_patch_ids_;
  std::optional<int> active_patch_id_;
  ProjectController *project_{};
  StructuralMeshGeometry *mesh_geometry_{};
  StructuralMeshGeometry *highlight_geometry_{};
  StructuralResultGeometry *result_geometry_{};
  QVariantMap result_view_;
  QVariantList stored_runs_;
};
