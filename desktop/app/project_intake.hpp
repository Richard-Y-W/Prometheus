#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

struct ProjectIntakeResult final {
  bool ok{};
  QString root_path;
  QString error;
  QVariantList artifacts;
  QString primary_step_path;
};

[[nodiscard]] ProjectIntakeResult scanProjectFolder(const QString &rootPath);

class ProjectIntakeController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString rootPath READ rootPath NOTIFY changed)
  Q_PROPERTY(QVariantList artifacts READ artifacts NOTIFY changed)
  Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
  Q_PROPERTY(int readyCount READ readyCount NOTIFY changed)
  Q_PROPERTY(int notEvaluatedCount READ notEvaluatedCount NOTIFY changed)
  Q_PROPERTY(int unsupportedCount READ unsupportedCount NOTIFY changed)
  Q_PROPERTY(QString primaryStepPath READ primaryStepPath NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
  explicit ProjectIntakeController(QObject *parent = nullptr);

  QString rootPath() const { return result_.root_path; }
  QVariantList artifacts() const { return result_.artifacts; }
  int totalCount() const { return result_.artifacts.size(); }
  int readyCount() const;
  int notEvaluatedCount() const;
  int unsupportedCount() const;
  QString primaryStepPath() const { return result_.primary_step_path; }
  QString status() const;
  QString error() const { return result_.error; }
  bool busy() const { return busy_; }

  Q_INVOKABLE void scanFolder(const QUrl &folder);

signals:
  void changed();
  void scanFinished(bool success);

private:
  void apply(ProjectIntakeResult result);

  ProjectIntakeResult result_;
  QFutureWatcher<ProjectIntakeResult> watcher_;
  bool busy_{};
};
