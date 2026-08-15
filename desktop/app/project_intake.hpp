#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <prometheus/run_store/run_store.hpp>

struct ProjectIntakeResult final {
  bool ok{};
  QString root_path;
  QString error;
  QVariantList artifacts;
  QVariantList candidate_components;
  QString primary_step_path;
};

[[nodiscard]] ProjectIntakeResult scanProjectFolder(const QString &rootPath);
[[nodiscard]] prometheus::run_store::ObjectToStore
buildProjectInventorySnapshot(const ProjectIntakeResult &result);

class ProjectIntakeController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString rootPath READ rootPath NOTIFY changed)
  Q_PROPERTY(QVariantList artifacts READ artifacts NOTIFY changed)
  Q_PROPERTY(QVariantList candidateComponents READ candidateComponents NOTIFY changed)
  Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
  Q_PROPERTY(int readyCount READ readyCount NOTIFY changed)
  Q_PROPERTY(int notEvaluatedCount READ notEvaluatedCount NOTIFY changed)
  Q_PROPERTY(int unsupportedCount READ unsupportedCount NOTIFY changed)
  Q_PROPERTY(int unreadableCount READ unreadableCount NOTIFY changed)
  Q_PROPERTY(int duplicateCopyCount READ duplicateCopyCount NOTIFY changed)
  Q_PROPERTY(QString primaryStepPath READ primaryStepPath NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
  explicit ProjectIntakeController(QObject *parent = nullptr);

  QString rootPath() const { return result_.root_path; }
  QVariantList artifacts() const { return result_.artifacts; }
  QVariantList candidateComponents() const { return result_.candidate_components; }
  int totalCount() const { return result_.artifacts.size(); }
  int readyCount() const;
  int notEvaluatedCount() const;
  int unsupportedCount() const;
  int unreadableCount() const;
  int duplicateCopyCount() const;
  QString primaryStepPath() const { return result_.primary_step_path; }
  QString status() const;
  QString error() const { return result_.error; }
  bool busy() const { return busy_; }
  const ProjectIntakeResult &result() const { return result_; }

  Q_INVOKABLE void scanFolder(const QUrl &folder);
  Q_INVOKABLE void reviewCandidateClaim(const QString &candidateId,
                                        const QString &claimId,
                                        const QString &decision);

signals:
  void changed();
  void scanFinished(bool success);

private:
  void apply(ProjectIntakeResult result);

  ProjectIntakeResult result_;
  QFutureWatcher<ProjectIntakeResult> watcher_;
  bool busy_{};
};
