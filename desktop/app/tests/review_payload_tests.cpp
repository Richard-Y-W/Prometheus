#include "review_payload.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

namespace {

QVariantList parameters()
{
    return {
        QVariantMap{{"field_name", "continuous_torque_nm"}},
        QVariantMap{{"field_name", "nominal_voltage_v"}},
    };
}

QVariantList acceptedDecisions()
{
    return {
        QVariantMap{
            {"field_name", "continuous_torque_nm"}, {"status", "accepted"}},
        QVariantMap{{"field_name", "nominal_voltage_v"}, {"status", "accepted"}},
    };
}

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        qCritical("FAILED: %s", message);
        ++failures;
    }
}

} // namespace

int main()
{
    const auto valid = prometheus::buildReviewPayload(
        parameters(), acceptedDecisions(), "test-reviewer");
    expect(valid.ok, "one explicit decision per parameter must be valid");
    expect(valid.error.isEmpty(), "a valid payload must not contain an error");
    expect(
        valid.payload.value("reviewed_by").toString() == "test-reviewer",
        "the payload must preserve the reviewer");
    expect(
        valid.payload.value("decisions").toArray().size() == 2,
        "the payload must preserve every explicit decision");

    auto missing = acceptedDecisions();
    missing.removeLast();
    expect(
        !prometheus::buildReviewPayload(parameters(), missing, "reviewer").ok,
        "a missing decision must be invalid");

    auto duplicate = acceptedDecisions();
    duplicate[1] = duplicate[0];
    expect(
        !prometheus::buildReviewPayload(parameters(), duplicate, "reviewer").ok,
        "a duplicate decision must be invalid");

    auto unknown = acceptedDecisions();
    auto unknownMap = unknown[1].toMap();
    unknownMap["field_name"] = "not_a_parameter";
    unknown[1] = unknownMap;
    expect(
        !prometheus::buildReviewPayload(parameters(), unknown, "reviewer").ok,
        "an unknown decision field must be invalid");

    auto emptyStatus = acceptedDecisions();
    auto emptyStatusMap = emptyStatus[0].toMap();
    emptyStatusMap["status"] = "";
    emptyStatus[0] = emptyStatusMap;
    expect(
        !prometheus::buildReviewPayload(parameters(), emptyStatus, "reviewer").ok,
        "an empty status must be invalid");

    for (const auto& status : {QStringLiteral("ambiguous"), QStringLiteral("rejected")}) {
        auto missingNote = acceptedDecisions();
        auto missingNoteMap = missingNote[0].toMap();
        missingNoteMap["status"] = status;
        missingNoteMap["note"] = "";
        missingNote[0] = missingNoteMap;
        expect(
            !prometheus::buildReviewPayload(parameters(), missingNote, "reviewer").ok,
            "ambiguous and rejected decisions without a note must be invalid");
    }

    auto ambiguousWithNote = acceptedDecisions();
    auto ambiguousWithNoteMap = ambiguousWithNote[0].toMap();
    ambiguousWithNoteMap["status"] = "ambiguous";
    ambiguousWithNoteMap["note"] = "The validity condition is unclear.";
    ambiguousWithNote[0] = ambiguousWithNoteMap;
    expect(
        prometheus::buildReviewPayload(
            parameters(), ambiguousWithNote, "reviewer")
            .ok,
        "an ambiguous decision with a note must be valid");

    expect(
        !prometheus::buildReviewPayload(parameters(), {}, "reviewer").ok,
        "a parameter list alone must never generate accepted decisions");
    expect(
        !prometheus::buildReviewPayload(parameters(), acceptedDecisions(), "").ok,
        "an empty reviewer must be invalid");

    return failures == 0 ? 0 : 1;
}
