#include "review_payload.hpp"

#include <QByteArray>
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
    const QString& reviewer,
    const qint64 expectedDraftVersion)
{
    constexpr qsizetype maximumReviewerBytes = 256;
    constexpr qsizetype maximumNoteBytes = 4096;
    constexpr qsizetype maximumDecisions = 1000;

    if (expectedDraftVersion < 0) {
        return invalid("Draft version must be non-negative.");
    }
    const auto normalizedReviewer = reviewer.trimmed();
    if (normalizedReviewer.isEmpty()) {
        return invalid("Reviewer is required.");
    }
    if (normalizedReviewer.toUtf8().size() > maximumReviewerBytes) {
        return invalid("Reviewer must not exceed 256 UTF-8 bytes.");
    }
    if (parameters.isEmpty()) {
        return invalid("There are no displayed parameters to review.");
    }
    if (parameters.size() > maximumDecisions || decisions.size() > maximumDecisions) {
        return invalid("A review may contain at most 1000 decisions.");
    }

    QStringList claimOrder;
    QSet<QString> displayedClaims;
    for (const auto& parameterValue : parameters) {
        const auto selectedClaim =
            parameterValue.toMap().value("selected_claim").toMap();
        const auto claimId = selectedClaim.value("claim_id").toString();
        if (claimId.isEmpty()) {
            return invalid("A displayed parameter is missing its selected claim ID.");
        }
        if (displayedClaims.contains(claimId)) {
            return invalid("The displayed parameter list contains a duplicate claim.");
        }
        displayedClaims.insert(claimId);
        claimOrder.append(claimId);
    }

    const QSet<QString> validStatuses{
        QStringLiteral("accepted"),
        QStringLiteral("ambiguous"),
        QStringLiteral("rejected"),
    };
    QHash<QString, QVariantMap> decisionsByClaim;
    for (const auto& decisionValue : decisions) {
        const auto decision = decisionValue.toMap();
        const auto claimId = decision.value("claim_id").toString();
        if (claimId.isEmpty()) {
            return invalid("Every review decision requires a claim ID.");
        }
        if (decisionsByClaim.contains(claimId)) {
            return invalid("Each claim requires exactly one review decision.");
        }
        if (!displayedClaims.contains(claimId)) {
            return invalid("A review decision references an unknown claim.");
        }

        const auto status = decision.value("status").toString();
        if (!validStatuses.contains(status)) {
            return invalid("Every selected claim requires an explicit review choice.");
        }
        const auto note = decision.value("note").toString().trimmed();
        if (note.isEmpty()) {
            return invalid("Every review decision requires a note.");
        }
        if (note.toUtf8().size() > maximumNoteBytes) {
            return invalid("A review note must not exceed 4096 UTF-8 bytes.");
        }
        decisionsByClaim.insert(
            claimId,
            QVariantMap{
                {"claim_id", claimId},
                {"status", status},
                {"note", note},
            });
    }

    if (decisionsByClaim.size() != displayedClaims.size()) {
        return invalid("Every displayed parameter requires exactly one review decision.");
    }

    QJsonArray normalizedDecisions;
    for (const auto& claimId : claimOrder) {
        const auto decision = decisionsByClaim.value(claimId);
        QJsonObject jsonDecision{
            {"claim_id", claimId},
            {"status", decision.value("status").toString()},
            {"note", decision.value("note").toString()},
        };
        normalizedDecisions.append(jsonDecision);
    }

    return {
        true,
        {},
        QJsonObject{
            {"expected_draft_version", expectedDraftVersion},
            {"reviewed_by", normalizedReviewer},
            {"decisions", normalizedDecisions},
        },
    };
}

} // namespace prometheus
