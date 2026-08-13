#include "exact_package_download.hpp"

#include <prometheus/integrity/canonical_json.hpp>

#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariant>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>

namespace prometheus {
namespace {

constexpr auto packageMediaType =
    "application/vnd.prometheus.execution-component+json;version=2.0.0";
constexpr qsizetype maximumMessageBytes = 4096;
constexpr qsizetype maximumCodeBytes = 128;
constexpr qsizetype maximumHeaderBytes = 64 * 1024;
constexpr qsizetype maximumHeaderCount = 128;

QByteArray boundedUtf8(QString value, const qsizetype maximumBytes)
{
    auto bytes = value.toUtf8();
    if (bytes.size() <= maximumBytes) {
        return bytes;
    }
    auto boundary = maximumBytes;
    while (boundary > 0 && boundary < bytes.size()
        && (static_cast<unsigned char>(bytes.at(boundary)) & 0xC0U)
            == 0x80U) {
        --boundary;
    }
    bytes.truncate(boundary);
    return bytes;
}

QString bounded(QString value, const qsizetype maximumBytes)
{
    return QString::fromUtf8(boundedUtf8(std::move(value), maximumBytes));
}

bool headerNameEquals(const QByteArray& actual, const char* expected)
{
    return actual.compare(expected, Qt::CaseInsensitive) == 0;
}

QList<QByteArray> headerValues(
    const QNetworkReply* reply, const char* name)
{
    QList<QByteArray> values;
    for (const auto& [headerName, value] : reply->rawHeaderPairs()) {
        if (headerNameEquals(headerName, name)) {
            values.push_back(value);
        }
    }
    return values;
}

bool strictObjectHash(const QByteArray& value)
{
    if (value.size() != 71 || !value.startsWith("sha256:")) {
        return false;
    }
    return std::all_of(value.cbegin() + 7, value.cend(), [](const char item) {
        return (item >= '0' && item <= '9') || (item >= 'a' && item <= 'f');
    });
}

bool parseContentLength(const QByteArray& value, quint64& result)
{
    if (value.isEmpty() || (value.size() > 1 && value.front() == '0')
        || !std::all_of(value.cbegin(), value.cend(), [](const char item) {
               return item >= '0' && item <= '9';
           })) {
        return false;
    }
    bool ok = false;
    result = value.toULongLong(&ok, 10);
    return ok;
}

} // namespace

ExactPackageDownload::ExactPackageDownload(
    QNetworkAccessManager* network, QObject* parent)
    : QObject(parent)
    , network_(network)
{
    Q_ASSERT(network_ != nullptr);
}

ExactPackageDownload::~ExactPackageDownload()
{
    cancel();
}

void ExactPackageDownload::acquire(QNetworkRequest request)
{
    if (busy()) {
        emit downloadFailed(
            QStringLiteral("An exact package acquisition is already active."),
            QStringLiteral("package_download_busy"));
        return;
    }

    bytes_.clear();
    expected_hash_.clear();
    declared_length_.reset();
    headers_validated_ = false;
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", packageMediaType);

    auto* reply = network_->get(request);
    reply->setReadBufferSize(64 * 1024);
    reply_ = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this,
        [this, reply] {
            if (reply_ == reply) {
                validateHeaders(reply);
            }
        });
    connect(reply, &QIODevice::readyRead, this,
        [this, reply] {
            if (reply_ != reply) {
                return;
            }
            if ((!headers_validated_ && !validateHeaders(reply))
                || !consumeAvailable(reply)) {
                return;
            }
        });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply] { finish(reply); });
}

void ExactPackageDownload::cancel() noexcept
{
    auto* reply = reply_.data();
    if (reply == nullptr) {
        return;
    }
    reply_.clear();
    bytes_.clear();
    expected_hash_.clear();
    declared_length_.reset();
    headers_validated_ = false;
    reply->abort();
    reply->deleteLater();
}

bool ExactPackageDownload::validateHeaders(QNetworkReply* reply)
{
    if (reply_ != reply) {
        return false;
    }
    const auto status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!status.isValid()) {
        if (reply->error() != QNetworkReply::NoError) {
            failActive(reply,
                QStringLiteral("The exact package response failed before HTTP metadata was available: %1")
                    .arg(reply->errorString()),
                QStringLiteral("package_network_error"));
        } else {
            failActive(reply,
                QStringLiteral("The exact package response has no HTTP status."),
                QStringLiteral("package_status_missing"));
        }
        return false;
    }
    if (status.toInt() != 200) {
        failActive(reply,
            QStringLiteral("Exact package export requires HTTP status 200."),
            QStringLiteral("package_status_invalid"));
        return false;
    }
    const auto rawHeaders = reply->rawHeaderPairs();
    qsizetype totalHeaderBytes = 0;
    for (const auto& [name, value] : rawHeaders) {
        totalHeaderBytes += name.size() + value.size();
        if (totalHeaderBytes > maximumHeaderBytes) {
            break;
        }
    }
    if (rawHeaders.size() > maximumHeaderCount
        || totalHeaderBytes > maximumHeaderBytes) {
        failActive(reply,
            QStringLiteral("The exact package response headers exceed the configured limit."),
            QStringLiteral("package_headers_too_large"));
        return false;
    }
    if (reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid()
        || !headerValues(reply, "Location").isEmpty()) {
        failActive(reply,
            QStringLiteral("Redirected exact package responses are forbidden."),
            QStringLiteral("package_redirect_forbidden"));
        return false;
    }

    const auto contentTypes = headerValues(reply, "Content-Type");
    if (contentTypes.size() != 1
        || contentTypes.front() != packageMediaType) {
        failActive(reply,
            QStringLiteral("The exact package response must contain one exact versioned Content-Type."),
            QStringLiteral("package_content_type_invalid"));
        return false;
    }
    const auto etags = headerValues(reply, "ETag");
    if (etags.size() != 1 || etags.front().size() != 73
        || etags.front().front() != '"' || etags.front().back() != '"'
        || !strictObjectHash(etags.front().mid(1, 71))) {
        failActive(reply,
            QStringLiteral("The exact package response must contain one strong quoted lowercase SHA-256 ETag."),
            QStringLiteral("package_etag_invalid"));
        return false;
    }
    expected_hash_ = QString::fromLatin1(etags.front().mid(1, 71));

    if (!headerValues(reply, "Content-Encoding").isEmpty()) {
        failActive(reply,
            QStringLiteral("Encoded exact package responses are forbidden."),
            QStringLiteral("package_content_encoding_forbidden"));
        return false;
    }
    if (!headerValues(reply, "Content-Range").isEmpty()) {
        failActive(reply,
            QStringLiteral("Partial exact package responses are forbidden."),
            QStringLiteral("package_content_range_forbidden"));
        return false;
    }

    const auto lengths = headerValues(reply, "Content-Length");
    if (lengths.size() > 1) {
        failActive(reply,
            QStringLiteral("Duplicate Content-Length headers are forbidden."),
            QStringLiteral("package_content_length_duplicate"));
        return false;
    }
    const auto transferEncodings = headerValues(reply, "Transfer-Encoding");
    if (transferEncodings.size() > 1
        || (!transferEncodings.isEmpty()
            && (transferEncodings.front() != "chunked"
                || !lengths.isEmpty()))) {
        failActive(reply,
            QStringLiteral("Transfer-Encoding must be one exact chunked header and cannot accompany Content-Length."),
            QStringLiteral("package_transfer_encoding_invalid"));
        return false;
    }
    declared_length_.reset();
    if (!lengths.isEmpty()) {
        quint64 length = 0;
        if (!parseContentLength(lengths.front(), length)) {
            failActive(reply,
                QStringLiteral("Content-Length must be canonical unsigned decimal."),
                QStringLiteral("package_content_length_invalid"));
            return false;
        }
        if (length > static_cast<quint64>(maximumPackageBytes)) {
            failActive(reply,
                QStringLiteral("The exact package response exceeds the 8 MiB limit."),
                QStringLiteral("package_too_large"));
            return false;
        }
        declared_length_ = length;
    }
    headers_validated_ = true;
    return true;
}

bool ExactPackageDownload::consumeAvailable(QNetworkReply* reply)
{
    while (reply_ == reply && reply->bytesAvailable() > 0) {
        const auto remaining = maximumPackageBytes - bytes_.size();
        if (remaining <= 0) {
            failActive(reply,
                QStringLiteral("The exact package response exceeds the 8 MiB limit."),
                QStringLiteral("package_too_large"));
            return false;
        }
        const auto requested = std::min<qint64>(
            static_cast<qint64>(remaining) + 1, 64 * 1024);
        const auto chunk = reply->read(requested);
        if (chunk.isEmpty()) {
            break;
        }
        if (chunk.size() > remaining) {
            failActive(reply,
                QStringLiteral("The exact package response exceeds the 8 MiB limit."),
                QStringLiteral("package_too_large"));
            return false;
        }
        bytes_.append(chunk);
    }
    return reply_ == reply;
}

void ExactPackageDownload::finish(QNetworkReply* reply)
{
    if (reply_ != reply) {
        reply->deleteLater();
        return;
    }
    if (!validateHeaders(reply) || !consumeAvailable(reply)) {
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        failActive(reply,
            QStringLiteral("The exact package download failed: %1")
                .arg(reply->errorString()),
            QStringLiteral("package_network_error"));
        return;
    }
    if (declared_length_.has_value()
        && *declared_length_ != static_cast<quint64>(bytes_.size())) {
        failActive(reply,
            QStringLiteral("The exact package body length disagrees with Content-Length."),
            QStringLiteral("package_content_length_mismatch"));
        return;
    }

    try {
        const auto hash = expected_hash_.toStdString();
        const std::string_view storedBytes(
            bytes_.constData(), static_cast<std::size_t>(bytes_.size()));
        static_cast<void>(
            integrity::verify_execution_component(storedBytes, hash));
    } catch (const integrity::CanonicalJsonError& failure) {
        failActive(reply, QString::fromUtf8(failure.what()),
            QString::fromStdString(failure.code()));
        return;
    } catch (const std::exception& failure) {
        failActive(reply, QString::fromUtf8(failure.what()),
            QStringLiteral("package_verification_failed"));
        return;
    } catch (...) {
        failActive(reply,
            QStringLiteral("Unknown exact package verification failure."),
            QStringLiteral("package_verification_failed"));
        return;
    }

    const auto completedBytes = bytes_;
    const auto completedHash = expected_hash_;
    reply_.clear();
    bytes_.clear();
    expected_hash_.clear();
    declared_length_.reset();
    headers_validated_ = false;
    reply->deleteLater();
    emit exactPackageAcquired(completedBytes, completedHash);
}

void ExactPackageDownload::failActive(
    QNetworkReply* reply, QString message, QString code)
{
    if (reply_ != reply) {
        return;
    }
    reply_.clear();
    bytes_.clear();
    expected_hash_.clear();
    declared_length_.reset();
    headers_validated_ = false;
    reply->abort();
    reply->deleteLater();
    emit downloadFailed(
        bounded(std::move(message), maximumMessageBytes),
        bounded(std::move(code), maximumCodeBytes));
}

} // namespace prometheus
