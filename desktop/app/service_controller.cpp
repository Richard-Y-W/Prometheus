#include "service_controller.hpp"

#include "review_payload.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QUrl>

namespace {

constexpr auto fixtureId = "prometheus.pm-36-gm.fixture-2";
constexpr auto schemaId =
    "urn:prometheus:schema:execution-component:2.0.0";
constexpr auto schemaVersion = "2.0.0";

} // namespace

ServiceController::ServiceController(QObject* parent)
    : QObject(parent)
{
    checkHealth();
}

QNetworkRequest ServiceController::request(const QString& path) const
{
    QNetworkRequest value(QUrl("http://127.0.0.1:8000" + path));
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
    const QString& message, const QString& code)
{
    error_ = message;
    error_code_ = code;
    setBusy(false);
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

void ServiceController::loadFixture()
{
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
