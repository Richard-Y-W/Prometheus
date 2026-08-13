#include "service_controller.hpp"

#include "review_payload.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUuid>
#include <QUrl>

#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr auto schemaId =
    "urn:prometheus:schema:execution-component:2.0.0";
constexpr auto schemaVersion = "2.0.0";
constexpr std::array<const char*, 3> fixtureIds{
    "prometheus.motor-a.fixture-1",
    "prometheus.motor-b.fixture-1",
    "prometheus.pm-36-gm.fixture-2",
};

bool supportedFixtureId(const QString& fixtureId)
{
    return std::any_of(fixtureIds.cbegin(), fixtureIds.cend(),
        [&fixtureId](const char* expected) {
            return fixtureId == QString::fromLatin1(expected);
        });
}

bool strictObjectHash(const QString& value)
{
    static const QRegularExpression expression(
        QStringLiteral("^sha256:[0-9a-f]{64}$"),
        QRegularExpression::DontCaptureOption);
    return expression.match(value).hasMatch();
}

} // namespace

ServiceController::ServiceController(QObject* parent)
    : ServiceController(
          QUrl(QStringLiteral("http://127.0.0.1:8000")), parent)
{
}

ServiceController::ServiceController(
    QUrl serviceBaseUrl, QObject* parent)
    : QObject(parent)
    , service_base_url_(std::move(serviceBaseUrl))
    , exact_package_download_(&network_)
{
    connect(&exact_package_download_,
        &prometheus::ExactPackageDownload::exactPackageAcquired, this,
        [this](QByteArray bytes, QString expectedObjectHash) {
            if (expectedObjectHash != object_hash_) {
                status_ = "published";
                setError(
                    "The verified package identity disagrees with the published revision.",
                    "package_publication_hash_mismatch");
                return;
            }
            clearError();
            setBusy(false);
            status_ = "exact_package_ready";
            emit changed();
            emit exactPackageAcquired(
                std::move(bytes), std::move(expectedObjectHash));
        });
    connect(&exact_package_download_,
        &prometheus::ExactPackageDownload::downloadFailed, this,
        [this](const QString& message, const QString& code) {
            status_ = "published";
            setError(message, code);
        });
    checkHealth();
}

QVariantList ServiceController::fixtureChoices() const
{
    return {
        QVariantMap{
            {"fixture_id", QStringLiteral("prometheus.motor-a.fixture-1")},
            {"label", QStringLiteral("Motor A — synthetic execution-ready input")},
        },
        QVariantMap{
            {"fixture_id", QStringLiteral("prometheus.motor-b.fixture-1")},
            {"label", QStringLiteral("Motor B — synthetic execution-ready input")},
        },
        QVariantMap{
            {"fixture_id", QStringLiteral("prometheus.pm-36-gm.fixture-2")},
            {"label", QStringLiteral("PM-36-GM — blocked conformance input")},
        },
    };
}

QNetworkRequest ServiceController::request(const QString& path) const
{
    QNetworkRequest value(service_base_url_.resolved(QUrl(path)));
    value.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return value;
}

void ServiceController::setBusy(const bool value)
{
    if (busy_ == value) {
        return;
    }
    busy_ = value;
    emit busyChanged();
}

void ServiceController::clearError()
{
    error_.clear();
    error_code_.clear();
}

void ServiceController::setError(
    const QString& message, const QString& code, const bool clearBusy)
{
    error_ = message;
    error_code_ = code;
    if (clearBusy) {
        setBusy(false);
    }
    emit changed();
}

void ServiceController::fail(
    QNetworkReply* reply, const QString& fallbackMessage)
{
    QString message;
    QString code;
    const auto response = QJsonDocument::fromJson(reply->readAll()).object();
    const auto detail = response.value("detail");
    if (detail.isObject()) {
        const auto detailObject = detail.toObject();
        message = detailObject.value("message").toString();
        code = detailObject.value("code").toString();
    } else if (detail.isString()) {
        message = detail.toString();
    }
    if (message.isEmpty()) {
        message = fallbackMessage + ": " + reply->errorString();
    }
    if (status_ == "loading_fixture") {
        status_ = "error";
    }
    setError(message, code);
}

void ServiceController::reset()
{
    exact_package_download_.cancel();
    setBusy(false);
    status_.clear();
    clearError();
    job_id_.clear();
    candidate_.clear();
    parameters_.clear();
    events_.clear();
    draft_version_ = -1;
    execution_readiness_.clear();
    object_hash_.clear();
    publication_integrity_.clear();
    publication_idempotency_key_.clear();
    emit changed();
}

void ServiceController::checkHealth()
{
    auto* reply = network_.get(request("/v1/health"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const bool value = reply->error() == QNetworkReply::NoError;
        if (online_ != value) {
            online_ = value;
            emit onlineChanged();
        }
        reply->deleteLater();
    });
}

void ServiceController::loadFixture(const QString& fixtureId)
{
    if (busy_) {
        setError("Another service operation is already active.",
            "service_busy", false);
        return;
    }
    if (!supportedFixtureId(fixtureId)) {
        setError(
            "Choose one of the three fixed local fixture catalog entries.",
            "unsupported_fixture_id");
        return;
    }
    reset();
    setBusy(true);
    status_ = "loading_fixture";
    emit changed();

    const QJsonObject body{
        {"fixture_id", fixtureId},
        {"schema_version", schemaVersion},
    };
    auto ingestionRequest = request("/api/v2/fixture-ingestions");
    ingestionRequest.setRawHeader(
        "Idempotency-Key",
        QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    auto* reply = network_.post(
        ingestionRequest,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply, "Fixture ingestion service unavailable");
            reply->deleteLater();
            return;
        }
        consumeFixtureIngestion(reply->readAll());
        reply->deleteLater();
    });
}

void ServiceController::acquireExactPackage()
{
    const auto revisionId = candidate_.value("id").toString();
    if (busy_ || exact_package_download_.busy()) {
        setError(
            "Another service operation is already active.",
            "service_busy", false);
        return;
    }
    if (revisionId.isEmpty() || publication_integrity_ != "sealed_v2"
        || status_ != "published" || !strictObjectHash(object_hash_)) {
        setError(
            "Exact package acquisition requires a sealed published revision with a valid object hash.",
            "exact_package_state_invalid");
        return;
    }
    clearError();
    setBusy(true);
    status_ = "acquiring_exact_package";
    emit changed();
    const auto encodedRevision =
        QString::fromLatin1(QUrl::toPercentEncoding(revisionId));
    exact_package_download_.acquire(request(
        "/api/v2/revisions/" + encodedRevision + "/execution-package"));
}

void ServiceController::consumeRevision(const QJsonObject& revision)
{
    const auto previousRevisionId = candidate_.value("id").toString();
    const auto revisionId = revision.value("id").toString();
    if (!previousRevisionId.isEmpty() && revisionId != previousRevisionId) {
        publication_idempotency_key_.clear();
    }

    candidate_ = revision.toVariantMap();
    const auto component = revision.value("component").toObject().toVariantMap();
    candidate_.insert("manufacturer", component.value("manufacturer"));
    candidate_.insert("part_number", component.value("part_number"));
    candidate_.insert("revision", component.value("revision"));
    parameters_ = revision.value("parameters").toArray().toVariantList();
    draft_version_ = revision.value("draft_version").toInteger(-1);
    publication_integrity_ =
        revision.value("publication_integrity").toString();
    object_hash_ = revision.value("object_hash").toString();
    execution_readiness_.clear();

    status_ = revision.value("status").toString();
    if (status_ == "draft") {
        const auto gates = revision.value("capability_gates").toArray();
        for (const auto& gateValue : gates) {
            const auto gate = gateValue.toObject();
            if (gate.value("required_review_type") == "claim_review"
                && gate.value("state") == "satisfied") {
                status_ = "reviewed";
                break;
            }
        }
    }
}

void ServiceController::consumeFixtureIngestion(const QByteArray& data)
{
    const auto object = QJsonDocument::fromJson(data).object();
    job_id_ = object.value("id").toString();
    consumeRevision(object.value("revision").toObject());
    events_.clear();
    clearError();
    setBusy(false);
    emit changed();
}

void ServiceController::submitReview(
    const QVariantList& decisions, const QString& reviewer)
{
    const auto revisionId = candidate_.value("id").toString();
    if (revisionId.isEmpty() || draft_version_ < 0) {
        setError(
            "Load the conformance fixture before submitting a review.",
            "review_revision_missing");
        return;
    }
    const auto result = prometheus::buildReviewPayload(
        parameters_, decisions, reviewer, draft_version_);
    if (!result.ok) {
        setError(result.error, "invalid_review_payload");
        return;
    }

    clearError();
    setBusy(true);
    const auto reviewRequest =
        request("/api/v2/revisions/" + revisionId + "/reviews");
    auto* reply = network_.post(
        reviewRequest,
        QJsonDocument(result.payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply, "Evidence review failed");
            reply->deleteLater();
            return;
        }
        const auto previousDraftVersion = draft_version_;
        consumeRevision(QJsonDocument::fromJson(reply->readAll()).object());
        if (draft_version_ > previousDraftVersion) {
            publication_idempotency_key_.clear();
        }
        clearError();
        setBusy(false);
        emit changed();
        reply->deleteLater();
    });
}

void ServiceController::publish()
{
    const auto revisionId = candidate_.value("id").toString();
    if (revisionId.isEmpty() || draft_version_ < 0 || status_ != "reviewed"
        || publication_integrity_ != "v2_draft") {
        setError(
            "Publication requires an accepted review for every selected claim.",
            "publication_state_invalid");
        return;
    }

    if (publication_idempotency_key_.isEmpty()) {
        publication_idempotency_key_ =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    clearError();
    setBusy(true);
    auto publicationRequest =
        request("/api/v2/revisions/" + revisionId + "/publication");
    publicationRequest.setRawHeader(
        "Idempotency-Key", publication_idempotency_key_.toUtf8());
    const QJsonObject body{
        {"expected_draft_version", draft_version_},
        {"schema_id", schemaId},
        {"schema_version", schemaVersion},
    };
    auto* reply = network_.post(
        publicationRequest,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply, "Publishing failed");
            reply->deleteLater();
            return;
        }
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        status_ = object.value("status").toString();
        execution_readiness_ =
            object.value("execution_readiness").toString();
        object_hash_ = object.value("object_hash").toString();
        publication_integrity_ =
            object.value("publication_integrity").toString();
        candidate_.insert("status", status_);
        candidate_.insert("execution_readiness", execution_readiness_);
        candidate_.insert("object_hash", object_hash_);
        candidate_.insert("publication_integrity", publication_integrity_);
        clearError();
        setBusy(false);
        emit changed();
        emit published(candidate_);
        reply->deleteLater();
    });
}
