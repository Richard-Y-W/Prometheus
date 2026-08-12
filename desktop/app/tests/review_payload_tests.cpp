#include "review_payload.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

#include <cstdlib>

namespace {

const QString firstClaimId =
    QStringLiteral("00000000-0000-4000-8000-000000000001");
const QString secondClaimId =
    QStringLiteral("00000000-0000-4000-8000-000000000002");

[[noreturn]] void fail(const char* message)
{
    qCritical("FAILED: %s", message);
    std::exit(1);
}

void require(const bool condition, const char* message)
{
    if (!condition) {
        fail(message);
    }
}

QVariantMap parameter(const QString& claimId)
{
    return QVariantMap{
        {"name", "fixture_parameter"},
        {"selected_claim",
         QVariantMap{
             {"claim_id", claimId},
             {"claim_fingerprint", QStringLiteral("sha256:fixture")},
         }},
    };
}

QVariantList parameters()
{
    return {parameter(firstClaimId), parameter(secondClaimId)};
}

QVariantMap decision(
    const QString& claimId,
    const QString& status = QStringLiteral("accepted"),
    const QString& note = QStringLiteral("Accepted as synthetic input only."))
{
    return QVariantMap{
        {"claim_id", claimId},
        {"status", status},
        {"note", note},
    };
}

QVariantList acceptedDecisions()
{
    // Deliberately reverse the request order. The normalized body must follow
    // the displayed parameter order, not caller-controlled decision order.
    return {
        decision(
            secondClaimId,
            QStringLiteral("accepted"),
            QStringLiteral("  Accepted as synthetic input only.  ")),
        decision(
            firstClaimId,
            QStringLiteral("accepted"),
            QStringLiteral("  Accepted as synthetic input only.  ")),
    };
}

QString generatedClaimId(const int index)
{
    return QStringLiteral("00000000-0000-4000-8000-%1")
        .arg(index, 12, 10, QLatin1Char('0'));
}

void requireInvalid(
    const QVariantList& displayedParameters,
    const QVariantList& decisions,
    const QString& reviewer = QStringLiteral("fixture-reviewer"),
    const qint64 expectedDraftVersion = 4)
{
    require(
        !prometheus::buildReviewPayload(
             displayedParameters, decisions, reviewer, expectedDraftVersion)
             .ok,
        "invalid review payload was accepted");
}

void testValidClaimReviewPayload()
{
    const auto result = prometheus::buildReviewPayload(
        parameters(), acceptedDecisions(), "  fixture-reviewer  ", 4);
    require(result.ok, "claim review payload should be valid");
    require(result.error.isEmpty(), "valid payload must not contain an error");
    require(
        result.payload.size() == 3,
        "review payload must contain only the v2 contract fields");
    require(
        result.payload.value("expected_draft_version").toInteger() == 4,
        "draft version must be preserved");
    require(
        result.payload.value("reviewed_by").toString() == "fixture-reviewer",
        "reviewer must be trimmed");

    const auto normalized = result.payload.value("decisions").toArray();
    require(normalized.size() == 2, "every displayed claim must be emitted");
    require(
        normalized.at(0).toObject().value("claim_id").toString() == firstClaimId,
        "displayed claim order must be preserved");
    require(
        normalized.at(0).toObject().contains("claim_id"),
        "review must identify a claim");
    require(
        !normalized.at(0).toObject().contains("field_name"),
        "field labels are not review identity");
    require(
        normalized.at(0).toObject().size() == 3,
        "decision must contain only claim_id, status, and note");
    require(
        normalized.at(0).toObject().value("note").toString()
            == "Accepted as synthetic input only.",
        "review notes must be trimmed");
}

void testDraftVersionAndClaimIdentityFailures()
{
    requireInvalid(parameters(), acceptedDecisions(), "reviewer", -1);

    auto missing = acceptedDecisions();
    missing.removeLast();
    requireInvalid(parameters(), missing);

    auto missingClaimId = acceptedDecisions();
    auto missingClaimIdMap = missingClaimId[0].toMap();
    missingClaimIdMap.remove("claim_id");
    missingClaimId[0] = missingClaimIdMap;
    requireInvalid(parameters(), missingClaimId);

    auto duplicate = acceptedDecisions();
    duplicate[1] = duplicate[0];
    requireInvalid(parameters(), duplicate);

    auto unknown = acceptedDecisions();
    auto unknownMap = unknown[0].toMap();
    unknownMap["claim_id"] =
        QStringLiteral("00000000-0000-4000-8000-999999999999");
    unknown[0] = unknownMap;
    requireInvalid(parameters(), unknown);

    auto emptyStatus = acceptedDecisions();
    auto emptyStatusMap = emptyStatus[0].toMap();
    emptyStatusMap["status"] = "";
    emptyStatus[0] = emptyStatusMap;
    requireInvalid(parameters(), emptyStatus);

    auto duplicateParameters = parameters();
    duplicateParameters[1] = duplicateParameters[0];
    requireInvalid(duplicateParameters, acceptedDecisions());

    auto missingSelectedClaim = parameters();
    auto missingSelectedClaimMap = missingSelectedClaim[0].toMap();
    missingSelectedClaimMap.remove("selected_claim");
    missingSelectedClaim[0] = missingSelectedClaimMap;
    requireInvalid(missingSelectedClaim, acceptedDecisions());

    requireInvalid({}, {});
}

void testEveryDecisionStatusRequiresANote()
{
    for (const auto& status : {
             QStringLiteral("accepted"),
             QStringLiteral("ambiguous"),
             QStringLiteral("rejected"),
         }) {
        for (const auto& note : {QString{}, QStringLiteral("   \t\n")}) {
            auto values = acceptedDecisions();
            auto value = values[0].toMap();
            value["status"] = status;
            value["note"] = note;
            values[0] = value;
            requireInvalid(parameters(), values);
        }
    }

    auto unsupportedStatus = acceptedDecisions();
    auto unsupportedStatusMap = unsupportedStatus[0].toMap();
    unsupportedStatusMap["status"] = "approved";
    unsupportedStatus[0] = unsupportedStatusMap;
    requireInvalid(parameters(), unsupportedStatus);
}

void testUtf8ByteAndCollectionLimits()
{
    requireInvalid(parameters(), acceptedDecisions(), "   ");
    requireInvalid(parameters(), acceptedDecisions(), QString(257, 'r'));

    require(
        prometheus::buildReviewPayload(
            parameters(), acceptedDecisions(), QString(256, 'r'), 4)
            .ok,
        "256-byte reviewer must be accepted");
    require(
        prometheus::buildReviewPayload(
            parameters(), acceptedDecisions(), QString(128, QChar(0x00e9)), 4)
            .ok,
        "reviewer limit must count UTF-8 bytes, not UTF-16 code units");
    requireInvalid(
        parameters(), acceptedDecisions(), QString(129, QChar(0x00e9)));

    auto maximumNote = acceptedDecisions();
    auto maximumNoteMap = maximumNote[0].toMap();
    maximumNoteMap["note"] = QString(4096, 'n');
    maximumNote[0] = maximumNoteMap;
    require(
        prometheus::buildReviewPayload(
            parameters(), maximumNote, "reviewer", 4)
            .ok,
        "4096-byte note must be accepted");

    auto oversizedNote = acceptedDecisions();
    auto oversizedNoteMap = oversizedNote[0].toMap();
    oversizedNoteMap["note"] = QString(4097, 'n');
    oversizedNote[0] = oversizedNoteMap;
    requireInvalid(parameters(), oversizedNote);

    QVariantList manyParameters;
    QVariantList manyDecisions;
    for (int index = 0; index < 1001; ++index) {
        const auto claimId = generatedClaimId(index);
        manyParameters.append(parameter(claimId));
        manyDecisions.append(decision(claimId));
    }
    requireInvalid(manyParameters, manyDecisions);
}

} // namespace

int main()
{
    testValidClaimReviewPayload();
    testDraftVersionAndClaimIdentityFailures();
    testEveryDecisionStatusRequiresANote();
    testUtf8ByteAndCollectionLimits();
    return 0;
}
