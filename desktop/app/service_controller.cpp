#include "service_controller.hpp"

#include "review_payload.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QUrl>

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

void ServiceController::setBusy(bool value)
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

void ServiceController::setError(const QString& message, const QString& code)
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
    if (status_ == "researching") {
        status_ = "error";
    }
    setError(message, code);
}

void ServiceController::reset()
{
    status_.clear();
    clearError();
    job_id_.clear();
    candidate_.clear();
    parameters_.clear();
    events_.clear();
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

void ServiceController::research(
    const QString& manufacturer, const QString& partNumber)
{
    reset();
    setBusy(true);
    status_ = "researching";
    emit changed();
    const QJsonObject body{
        {"manufacturer", manufacturer},
        {"part_number", partNumber},
    };
    auto researchRequest = request("/v1/research-jobs");
    researchRequest.setRawHeader(
        "Idempotency-Key",
        QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    auto* reply = network_.post(
        researchRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply, "Research service unavailable");
            reply->deleteLater();
            return;
        }
        consumeJob(reply->readAll());
        reply->deleteLater();
    });
}

void ServiceController::consumeJob(const QByteArray& data)
{
    const auto object = QJsonDocument::fromJson(data).object();
    job_id_ = object.value("id").toString();
    status_ = object.value("status").toString();
    candidate_ = object.value("candidate").toObject().toVariantMap();
    parameters_ =
        object.value("candidate").toObject().value("parameters").toArray().toVariantList();
    events_ = object.value("events").toArray().toVariantList();
    clearError();
    setBusy(false);
    emit changed();
}

void ServiceController::submitReview(
    const QVariantList& decisions, const QString& reviewer)
{
    if (job_id_.isEmpty()) {
        setError("Research a component before submitting a review.", "review_job_missing");
        return;
    }
    const auto result =
        prometheus::buildReviewPayload(parameters_, decisions, reviewer);
    if (!result.ok) {
        setError(result.error, "invalid_review_payload");
        return;
    }

    clearError();
    setBusy(true);
    const auto reviewRequest = request("/v1/research-jobs/" + job_id_ + "/review");
    auto* reply = network_.post(
        reviewRequest,
        QJsonDocument(result.payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply, "Evidence review failed");
            reply->deleteLater();
            return;
        }
        consumeJob(reply->readAll());
        reply->deleteLater();
    });
}

void ServiceController::publish()
{
    if (job_id_.isEmpty() || status_ != "reviewed") {
        setError(
            "Publication requires an accepted review for every parameter.",
            "publication_state_invalid");
        return;
    }

    clearError();
    setBusy(true);
    auto publishRequest = request("/v1/research-jobs/" + job_id_ + "/publish");
    publishRequest.setRawHeader(
        "Idempotency-Key",
        QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    auto* reply = network_.post(publishRequest, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply, "Publishing failed");
            reply->deleteLater();
            return;
        }
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        candidate_ = object.toVariantMap();
        parameters_ = object.value("parameters").toArray().toVariantList();
        status_ = object.value("status").toString();
        clearError();
        setBusy(false);
        emit changed();
        emit published(candidate_);
        reply->deleteLater();
    });
}
