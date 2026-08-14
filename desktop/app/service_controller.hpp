#pragma once

#include "exact_package_download.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QNetworkReply;

class ServiceController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
    Q_PROPERTY(QString jobId READ jobId NOTIFY changed)
    Q_PROPERTY(QVariantMap candidate READ candidate NOTIFY changed)
    Q_PROPERTY(QVariantList parameters READ parameters NOTIFY changed)
    Q_PROPERTY(QVariantList events READ events NOTIFY changed)
    Q_PROPERTY(qint64 draftVersion READ draftVersion NOTIFY changed)
    Q_PROPERTY(QString executionReadiness READ executionReadiness NOTIFY changed)
    Q_PROPERTY(QString objectHash READ objectHash NOTIFY changed)
    Q_PROPERTY(QString publicationIntegrity READ publicationIntegrity NOTIFY changed)
    Q_PROPERTY(QVariantList fixtureChoices READ fixtureChoices CONSTANT)

public:
    explicit ServiceController(QObject* parent = nullptr);
    explicit ServiceController(
        QUrl serviceBaseUrl, QObject* parent = nullptr);

    bool online() const { return online_; }
    bool busy() const { return busy_; }
    QString status() const { return status_; }
    QString error() const { return error_; }
    QString errorCode() const { return error_code_; }
    QString jobId() const { return job_id_; }
    QVariantMap candidate() const { return candidate_; }
    QVariantList parameters() const { return parameters_; }
    QVariantList events() const { return events_; }
    qint64 draftVersion() const { return draft_version_; }
    QString executionReadiness() const { return execution_readiness_; }
    QString objectHash() const { return object_hash_; }
    QString publicationIntegrity() const { return publication_integrity_; }
    QVariantList fixtureChoices() const;

    Q_INVOKABLE void checkHealth();
    Q_INVOKABLE void loadFixture(const QString& fixtureId);
    Q_INVOKABLE void submitReview(
        const QVariantList& decisions, const QString& reviewer);
    Q_INVOKABLE void publish();
    Q_INVOKABLE void acquireExactPackage();
    Q_INVOKABLE void reset();

signals:
    void onlineChanged();
    void busyChanged();
    void changed();
    void published(QVariantMap revision);
    void exactPackageAcquired(
        QByteArray bytes, QString expectedObjectHash);

private:
    QUrl service_base_url_;
    QNetworkAccessManager network_;
    prometheus::ExactPackageDownload exact_package_download_;
    bool online_{false};
    bool busy_{false};
    QString status_;
    QString error_;
    QString error_code_;
    QString job_id_;
    QVariantMap candidate_;
    QVariantList parameters_;
    QVariantList events_;
    qint64 draft_version_{-1};
    QString execution_readiness_;
    QString object_hash_;
    QString publication_integrity_;
    QString publication_idempotency_key_;

    void setBusy(bool value);
    void clearError();
    void setError(const QString& message, const QString& code,
        bool clearBusy = true);
    QNetworkRequest request(const QString& path) const;
    void consumeRevision(const QJsonObject& revision);
    void consumeFixtureIngestion(const QByteArray& data);
    void fail(QNetworkReply* reply, const QString& fallbackMessage);
};
