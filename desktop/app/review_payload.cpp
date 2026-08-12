#include "review_payload.hpp"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

namespace prometheus {
namespace {

ReviewPayloadResult invalid(const QString& message)
{
    return {false, message, {}};
}

} // namespace

ReviewPayloadResult buildReviewPayload(
    const QVariantList& parameters,
    const QVariantList& decisions,
    const QString& reviewer)
{
    const auto normalizedReviewer = reviewer.trimmed();
    if (normalizedReviewer.isEmpty()) {
        return invalid("Reviewer is required.");
    }
    if (parameters.isEmpty()) {
        return invalid("There are no displayed parameters to review.");
    }

    QStringList parameterOrder;
    QSet<QString> parameterFields;
    for (const auto& parameterValue : parameters) {
        const auto fieldName = parameterValue.toMap().value("field_name").toString();
        if (fieldName.isEmpty()) {
            return invalid("A displayed parameter is missing its field name.");
        }
        if (parameterFields.contains(fieldName)) {
            return invalid("The displayed parameter list contains a duplicate field.");
        }
        parameterFields.insert(fieldName);
        parameterOrder.append(fieldName);
    }

    const QSet<QString> validStatuses{
        QStringLiteral("accepted"),
        QStringLiteral("ambiguous"),
        QStringLiteral("rejected"),
    };
    QHash<QString, QVariantMap> decisionsByField;
    for (const auto& decisionValue : decisions) {
        const auto decision = decisionValue.toMap();
        const auto fieldName = decision.value("field_name").toString();
        if (fieldName.isEmpty()) {
            return invalid("Every review decision requires a field name.");
        }
        if (decisionsByField.contains(fieldName)) {
            return invalid("Each parameter requires exactly one review decision.");
        }
        if (!parameterFields.contains(fieldName)) {
            return invalid("A review decision references an unknown parameter.");
        }

        const auto status = decision.value("status").toString();
        if (!validStatuses.contains(status)) {
            return invalid("Every parameter requires an explicit review choice.");
        }
        const auto note = decision.value("note").toString().trimmed();
        if ((status == "ambiguous" || status == "rejected") && note.isEmpty()) {
            return invalid("Ambiguous and rejected decisions require a review note.");
        }
        decisionsByField.insert(
            fieldName,
            QVariantMap{
                {"field_name", fieldName},
                {"status", status},
                {"note", note},
            });
    }

    if (decisionsByField.size() != parameterFields.size()) {
        return invalid("Every displayed parameter requires exactly one review decision.");
    }

    QJsonArray normalizedDecisions;
    for (const auto& fieldName : parameterOrder) {
        const auto decision = decisionsByField.value(fieldName);
        QJsonObject jsonDecision{
            {"field_name", fieldName},
            {"status", decision.value("status").toString()},
        };
        const auto note = decision.value("note").toString();
        if (!note.isEmpty()) {
            jsonDecision.insert("note", note);
        }
        normalizedDecisions.append(jsonDecision);
    }

    return {
        true,
        {},
        QJsonObject{
            {"reviewed_by", normalizedReviewer},
            {"decisions", normalizedDecisions},
        },
    };
}

} // namespace prometheus
