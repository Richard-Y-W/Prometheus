#pragma once

#include <QByteArray>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace prometheus {

class ExactPackageDownload final : public QObject {
    Q_OBJECT

public:
    static constexpr qsizetype maximumPackageBytes = 8 * 1024 * 1024;

    explicit ExactPackageDownload(
        QNetworkAccessManager* network, QObject* parent = nullptr);
    ~ExactPackageDownload() override;

    void acquire(QNetworkRequest request);
    void cancel() noexcept;
    [[nodiscard]] bool busy() const noexcept { return !reply_.isNull(); }

signals:
    void exactPackageAcquired(
        QByteArray bytes, QString expectedObjectHash);
    void downloadFailed(QString message, QString code);

private:
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QByteArray bytes_;
    QString expected_hash_;
    std::optional<quint64> declared_length_;
    bool headers_validated_{false};

    bool validateHeaders(QNetworkReply* reply);
    bool consumeAvailable(QNetworkReply* reply);
    void finish(QNetworkReply* reply);
    void failActive(
        QNetworkReply* reply, QString message, QString code);
};

} // namespace prometheus
