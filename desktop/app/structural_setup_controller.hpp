#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"
#include "prometheus/structural/surface_setup.hpp"
#include "prometheus/structural/types.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQuick3D/QQuick3DGeometry>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class StructuralDisplayGeometry final : public QQuick3DGeometry {
public:
  StructuralDisplayGeometry(const prometheus::structural::VolumeMesh &mesh,
                            const std::vector<std::string> &groups,
                            QObject *parent = nullptr);
};

class StructuralSetupController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY changed)
  Q_PROPERTY(QString geometrySha256 READ geometrySha256 NOTIFY changed)
  Q_PROPERTY(int nodeCount READ nodeCount NOTIFY changed)
  Q_PROPERTY(int elementCount READ elementCount NOTIFY changed)
  Q_PROPERTY(double minimumMeanRatio READ minimumMeanRatio NOTIFY changed)
  Q_PROPERTY(double candidateMeshTargetSizeM READ candidateMeshTargetSizeM
                 NOTIFY changed)
  Q_PROPERTY(QVariantList displayCenterM READ displayCenterM NOTIFY changed)
  Q_PROPERTY(double displayDiameterM READ displayDiameterM NOTIFY changed)
  Q_PROPERTY(QVariantList surfaceGroups READ surfaceGroups NOTIFY changed)
  Q_PROPERTY(QVariantMap activeSurfaceGroup READ activeSurfaceGroup NOTIFY
                 changed)
  Q_PROPERTY(QStringList restraintGroups READ restraintGroups NOTIFY changed)
  Q_PROPERTY(QStringList loadGroups READ loadGroups NOTIFY changed)
  Q_PROPERTY(double selectedLoadAreaM2 READ selectedLoadAreaM2 NOTIFY changed)
  Q_PROPERTY(QVariantList compiledResultantN READ compiledResultantN NOTIFY
                 changed)
  Q_PROPERTY(QVariantList materialCandidates READ materialCandidates NOTIFY
                 changed)
  Q_PROPERTY(QVariantList blockingIssues READ blockingIssues NOTIFY changed)
  Q_PROPERTY(bool readyToExport READ readyToExport NOTIFY changed)
  Q_PROPERTY(bool scenarioConfirmed READ scenarioConfirmed NOTIFY changed)
  Q_PROPERTY(bool meshReviewed READ meshReviewed NOTIFY changed)
  Q_PROPERTY(QQuick3DGeometry *meshGeometry READ meshGeometry NOTIFY changed)
  Q_PROPERTY(QQuick3DGeometry *highlightGeometry READ highlightGeometry NOTIFY
                 changed)

public:
  explicit StructuralSetupController(QObject *parent = nullptr);

  QString sourcePath() const { return source_path_; }
  QString geometrySha256() const { return geometry_sha256_; }
  int nodeCount() const { return static_cast<int>(mesh_.nodes.size()); }
  int elementCount() const { return static_cast<int>(mesh_.elements.size()); }
  double minimumMeanRatio() const {
    return mesh_.diagnostics.minimum_mean_ratio;
  }
  double candidateMeshTargetSizeM() const {
    return candidate_mesh_target_size_m_;
  }
  QVariantList displayCenterM() const {
    return {display_center_m_[0], display_center_m_[1], display_center_m_[2]};
  }
  double displayDiameterM() const { return display_diameter_m_; }
  QVariantList surfaceGroups() const;
  QVariantMap activeSurfaceGroup() const;
  QStringList restraintGroups() const;
  QStringList loadGroups() const;
  double selectedLoadAreaM2() const;
  QVariantList compiledResultantN() const;
  QVariantList materialCandidates() const { return material_candidates_; }
  QVariantList blockingIssues() const { return blocking_issues_; }
  bool readyToExport() const { return ready_to_export_; }
  bool scenarioConfirmed() const { return scenario_confirmed_; }
  bool meshReviewed() const { return mesh_reviewed_; }
  QQuick3DGeometry *meshGeometry() const { return mesh_geometry_.get(); }
  QQuick3DGeometry *highlightGeometry() const {
    return highlight_geometry_.get();
  }

  Q_INVOKABLE bool loadCandidate(const QUrl &manifest);
  Q_INVOKABLE void setActiveSurfaceGroup(const QString &name);
  Q_INVOKABLE void setSurfaceRole(const QString &name, const QString &role,
                                  bool selected);
  Q_INVOKABLE bool loadMaterialEvidence(const QUrl &source);
  Q_INVOKABLE void selectMaterialCandidate(const QString &candidateId,
                                            const QString &applicability);
  Q_INVOKABLE void setMaterialReview(const QVariantMap &review);
  Q_INVOKABLE void setForce(double magnitudeN, double directionX,
                            double directionY, double directionZ);
  Q_INVOKABLE void setLimits(const QVariantMap &limits);
  Q_INVOKABLE void setMeshReview(const QVariantMap &review);
  Q_INVOKABLE void confirmScenario(bool confirmed);
  Q_INVOKABLE bool exportReviewedCase(const QUrl &directory);

signals:
  void changed();
  void meshLoaded();

private:
  void resetCandidateState();
  void invalidateScenario();
  void recompute();
  [[nodiscard]] prometheus::structural::StructuralRequest requestFromState()
      const;
  [[nodiscard]] bool hasSurfaceGroup(const QString &name) const;

  QString source_path_;
  QString analysis_id_;
  QString component_name_;
  QString geometry_path_;
  QString geometry_sha256_;
  QString mesh_path_;
  QString mesh_sha256_;
  QString material_evidence_path_;
  QString material_evidence_sha256_;
  double candidate_mesh_target_size_m_{};
  double coordinate_scale_to_m_{};
  std::array<double, 3> display_center_m_{};
  double display_diameter_m_{};
  prometheus::structural::VolumeMesh mesh_;
  std::unique_ptr<StructuralDisplayGeometry> mesh_geometry_;
  std::unique_ptr<StructuralDisplayGeometry> highlight_geometry_;
  QString active_surface_group_;
  QStringList restraint_groups_;
  QStringList load_groups_;

  QString material_designation_;
  QString material_temper_;
  QString material_product_form_;
  QString material_applicability_;
  double youngs_modulus_pa_{};
  double poisson_ratio_{};
  bool material_reviewed_{};
  QVariantList material_candidates_;

  double force_magnitude_n_{};
  std::array<double, 3> force_direction_{};
  bool force_reviewed_{};
  std::optional<double> displacement_limit_m_;
  std::optional<double> von_mises_limit_pa_;
  QString displacement_limit_basis_;
  QString von_mises_limit_basis_;
  bool requirements_reviewed_{};
  double mesh_target_size_m_{};
  double minimum_mean_ratio_threshold_{};
  bool mesh_reviewed_{};
  bool scenario_confirmed_{};

  std::optional<prometheus::structural::CompiledSurfaceSetup> compiled_setup_;
  QVariantList blocking_issues_;
  bool ready_to_export_{};
  std::optional<std::pair<QString, QString>> operation_issue_;
};
