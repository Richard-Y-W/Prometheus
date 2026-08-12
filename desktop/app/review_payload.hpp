#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>

namespace prometheus {

struct ReviewPayloadResult {
    bool ok{false};
    QString error;
    QJsonObject payload;
};

[[nodiscard]] ReviewPayloadResult buildReviewPayload(
    const QVariantList& parameters,
    const QVariantList& decisions,
    const QString& reviewer,
    qint64 expectedDraftVersion);

} // namespace prometheus
