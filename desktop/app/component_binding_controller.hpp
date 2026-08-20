#pragma once

#include "exact_package_download.hpp"

#include <prometheus/execution/package_consumer.hpp>

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

class ProjectController;

// Fetches a single published component revision's exact execution-component
// package, hash-verifies it via the same generic path
// (ExactPackageDownload + execution::inspect_execution_component) that
// ServiceController/ExecutionController already use for the motor fixture
// flow, and reports a verified display map for binding to a CAD entity.
//
// Deliberately owns its own ExactPackageDownload rather than sharing
// ServiceController's: that instance's exactPackageAcquired signal carries no
// entity tag, and is already consumed by ExecutionController for the fixture
// flow. Reusing it here would risk a package fetched for one CAD entity being
// misapplied to whatever entity ExecutionController currently has pending.
class ComponentBindingController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)

public:
  explicit ComponentBindingController(QObject *parent = nullptr);
  explicit ComponentBindingController(QUrl serviceBaseUrl,
                                      QObject *parent = nullptr);
  ComponentBindingController(QUrl serviceBaseUrl, ProjectController *project,
                             QObject *parent = nullptr);

  bool busy() const { return busy_; }
  QString error() const { return error_; }
  QString errorCode() const { return error_code_; }

  Q_INVOKABLE void bindRevision(const QString &cadEntityId,
                                const QString &revisionId);
  Q_INVOKABLE void cancel();

  // Phase 6 checkpoint 7: a pure, on-demand read of this CAD entity's full
  // PackageBinding supersession chain -- {revision, active, package_hash}
  // per entry, most recent first. No cached state and no new signal: reads
  // project_->project() fresh on every call, the same way persistBindingEdge
  // already does, so it can never drift from what persistBindingEdge itself
  // just wrote.
  Q_INVOKABLE QVariantList bindingHistory(const QString &cadEntityId) const;

signals:
  void changed();
  void componentBindingVerified(QString cadEntityId, QVariantMap component);
  void componentBindingFailed(QString cadEntityId, QString message,
                              QString code);

private:
  QUrl service_base_url_;
  QNetworkAccessManager network_;
  prometheus::ExactPackageDownload exact_package_download_;
  ProjectController *project_{nullptr};
  QString pending_cad_entity_id_;
  bool busy_{false};
  QString error_;
  QString error_code_;

  void setBusy(bool value);
  void setError(const QString &message, const QString &code,
                bool clearBusy = true);
  void fetchSupersessionAndEmit(
      const QString &cadEntityId, QVariantMap component,
      const prometheus::execution::PackageInspection &inspection,
      const QByteArray &bytes);
  void persistBindingEdge(
      const QString &cadEntityId,
      const prometheus::execution::PackageInspection &inspection,
      const QByteArray &bytes);
};
