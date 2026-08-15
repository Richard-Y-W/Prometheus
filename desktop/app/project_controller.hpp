#pragma once

#include <prometheus/run_store/project_v2.hpp>

namespace prometheus::run_store { struct ObjectToStore; }

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <filesystem>
#include <optional>

class CadController;
class EngineeringController;

class ProjectController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY changed)
  Q_PROPERTY(QString schemaVersion READ schemaVersion NOTIFY changed)
  Q_PROPERTY(bool saveAsRequired READ saveAsRequired NOTIFY changed)
  Q_PROPERTY(bool cadAvailable READ cadAvailable NOTIFY changed)
  Q_PROPERTY(
      bool executionStoreAvailable READ executionStoreAvailable NOTIFY changed)
  Q_PROPERTY(
      bool assemblyArtifactCurrent READ assemblyArtifactCurrent NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
  Q_PROPERTY(int committedRunCount READ committedRunCount NOTIFY changed)
  Q_PROPERTY(int inventorySnapshotCount READ inventorySnapshotCount NOTIFY changed)
  Q_PROPERTY(QString latestInventoryHash READ latestInventoryHash NOTIFY changed)
  Q_PROPERTY(bool bundleBusy READ bundleBusy NOTIFY changed)
  Q_PROPERTY(QString lastBundlePath READ lastBundlePath NOTIFY changed)
  Q_PROPERTY(QVariantMap legacyEngineeringState READ legacyEngineeringState
                 NOTIFY changed)

public:
  explicit ProjectController(CadController *cad,
                             EngineeringController *engineering,
                             QObject *parent = nullptr);

  QString currentProjectPath() const { return current_project_path_; }
  QString schemaVersion() const { return schema_version_; }
  bool saveAsRequired() const { return save_as_required_; }
  bool cadAvailable() const { return cad_available_; }
  bool executionStoreAvailable() const { return execution_store_available_; }
  bool assemblyArtifactCurrent() const { return assembly_artifact_current_; }
  QString error() const { return error_; }
  QString errorCode() const { return error_code_; }
  int committedRunCount() const;
  int inventorySnapshotCount() const;
  QString latestInventoryHash() const;
  bool bundleBusy() const { return bundle_busy_; }
  QString lastBundlePath() const { return last_bundle_path_; }
  QVariantMap legacyEngineeringState() const;

  Q_INVOKABLE void openProject(const QUrl &path);
  Q_INVOKABLE void saveAsVersion2(const QUrl &destination);
  Q_INVOKABLE void saveCurrentProject();
  Q_INVOKABLE bool ensureExecutionWritable();
  Q_INVOKABLE bool verifyAssemblyArtifactCurrent();
  Q_INVOKABLE void exportPortableBundle(const QUrl &parentFolder);
  bool commitInventorySnapshot(
      const prometheus::run_store::ObjectToStore &snapshot);

  const std::optional<prometheus::run_store::ProjectV2> &project() const {
    return project_;
  }
  std::filesystem::path projectPath() const;
  bool hasCadEntityId(const QString &entityId) const;
  void acceptProject(prometheus::run_store::ProjectV2 project);

signals:
  void changed();
  void projectOpened();
  void projectSaved();

private:
  CadController *cad_;
  EngineeringController *engineering_;
  std::optional<prometheus::run_store::ProjectV2> project_;
  QString current_project_path_;
  QString source_v1_path_;
  QString schema_version_;
  bool save_as_required_{true};
  bool cad_available_{false};
  bool execution_store_available_{false};
  bool assembly_artifact_current_{false};
  QString error_;
  QString error_code_;
  bool bundle_busy_{};
  QString last_bundle_path_;

  void clearError();
  void setError(QString message, QString code);
  void restoreProject(const prometheus::run_store::ProjectV2 &project,
                      const QString &projectPath,
                      const QString &degradedStoreCode = {});
};
