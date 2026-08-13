from __future__ import annotations

from copy import deepcopy
import json
import math
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator
from pydantic import TypeAdapter, ValidationError

from app.canonical_json import canonicalize_value, object_hash
from app.contracts_v2 import (
    SCHEMA_ID,
    SCHEMA_VERSION,
    CandidateClaimV2,
    EngineeringValueV2,
    EvidenceRecordV2,
    ExecutionComponentV2,
    ProjectSummaryV2,
    PublicationRequestV2,
    ReviewRequestV2,
)


ROOT = Path(__file__).parents[2]
FIXTURE_PATH = ROOT / "fixtures/contracts/execution-component-v2.pm-36-gm.json"
HASH = "sha256:" + "a" * 64
REVISION_ID = "11111111-1111-4111-8111-111111111111"
SLOT_ID = "33333333-3333-4333-8333-333333333333"
CLAIM_ID = "44444444-4444-4444-8444-444444444444"
EVIDENCE_ID = "55555555-5555-4555-8555-555555555555"


def _load_fixture() -> dict[str, object]:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))


def _known_claim() -> dict[str, object]:
    return {
        "claim_id": CLAIM_ID,
        "revision_id": REVISION_ID,
        "slot_id": SLOT_ID,
        "value_state": "known",
        "value": {"kind": "scalar", "value": 36.0},
        "unit": "V",
        "original_value": "36.0",
        "original_unit": "V",
        "validity_conditions": ["synthetic conformance fixture only"],
        "provenance": "fixture_json_v2",
        "evidence_ids": [EVIDENCE_ID],
        "claim_fingerprint": HASH,
    }


def _evidence_base(evidence_class: str, source_authority: str) -> dict[str, object]:
    return {
        "evidence_id": EVIDENCE_ID,
        "revision_id": REVISION_ID,
        "evidence_class": evidence_class,
        "source_authority": source_authority,
        "physical_validation_status": "unvalidated",
        "extraction_confidence": None,
        "limitations": ["test vector only"],
    }


def test_checked_in_schemas_are_current(tmp_path):
    from scripts.export_contract_schemas import render_schemas

    rendered = render_schemas()
    assert set(rendered) == {
        "analysis-request-v1.schema.json",
        "analysis-result-v1.schema.json",
        "engineering-value-v2.schema.json",
        "evidence-record-v2.schema.json",
        "review-request-v2.schema.json",
        "publication-request-v2.schema.json",
        "execution-component-v2.schema.json",
        "motor-arm-scenario-v1.schema.json",
        "package-consumer-contract-v1.schema.json",
        "project-summary-v2.schema.json",
        "run-manifest-v1.schema.json",
    }
    for name, payload in rendered.items():
        checked_in = (ROOT / "schemas" / name).read_bytes()
        assert checked_in == payload
        schema = json.loads(payload)
        Draft202012Validator.check_schema(schema)
        assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
        assert schema["$id"].endswith((":1.0.0", ":2.0.0"))


def test_execution_and_summary_schemas_enforce_their_declarative_invariants():
    from scripts.export_contract_schemas import render_schemas

    rendered = render_schemas()
    execution_schema = json.loads(
        rendered["execution-component-v2.schema.json"]
    )
    Draft202012Validator(execution_schema).validate(_load_fixture())

    summary_schema = json.loads(rendered["project-summary-v2.schema.json"])
    invalid_satisfaction = {
        "schema_version": "2.0.0",
        "verdict": "satisfied_within_scope",
        "coverage": "insufficient",
        "execution_state": "completed_with_blocked_work",
        "counts": {
            "satisfied_within_scope": 1,
            "violated": 0,
            "indeterminate": 0,
            "not_applicable": 0,
            "not_evaluated": 1,
        },
        "obligation_total": 2,
        "assessment_scope_id": "sha256:" + "0" * 64,
        "decision_core": {"name": "prometheus_cpp", "version": "1.0.0"},
    }
    assert list(Draft202012Validator(summary_schema).iter_errors(invalid_satisfaction))


def test_complete_package_vector_is_exact():
    semantic = _load_fixture()
    validated = ExecutionComponentV2.model_validate(semantic).model_dump(
        mode="json", by_alias=True
    )
    expected = (ROOT / "fixtures/contracts/execution-component-v2.pm-36-gm.jcs").read_bytes()
    expected_hash = (
        ROOT / "fixtures/contracts/execution-component-v2.pm-36-gm.sha256"
    ).read_text(encoding="ascii").strip()
    assert canonicalize_value(validated) == expected
    assert object_hash(expected) == expected_hash
    assert "content_hash" not in validated
    assert "schema_id" not in validated
    assert validated["$schema"] == SCHEMA_ID


def test_complete_package_vector_is_reproducible():
    from scripts.export_contract_fixture import render_fixture_files

    for filename, payload in render_fixture_files().items():
        assert (ROOT / "fixtures/contracts" / filename).read_bytes() == payload


@pytest.mark.parametrize(
    "payload",
    [
        {"kind": "scalar", "value": 1.25},
        {"kind": "range", "minimum": -2.0, "maximum": 4.5},
        {"kind": "enumeration", "values": ["steel", 2.5, True]},
        {
            "kind": "curve",
            "independent_quantity": "temperature",
            "independent_unit": "degC",
            "interpolation": "linear",
            "points": [{"x": 0.0, "y": 1.0}, {"x": 100.0, "y": 0.8}],
        },
        {"kind": "exact_decimal_string", "value": "-123.4500e+12"},
        {"kind": "unknown", "reason": "not supplied by the source"},
    ],
    ids=["scalar", "range", "enumeration", "curve", "exact-decimal", "unknown"],
)
def test_all_engineering_value_variants(payload):
    validated = TypeAdapter(EngineeringValueV2).validate_python(payload)
    assert validated.kind == payload["kind"]


def test_value_invariants_reject_empty_meaning_and_duplicate_numbers():
    with pytest.raises(ValidationError):
        TypeAdapter(EngineeringValueV2).validate_python(
            {"kind": "unknown", "reason": "   "}
        )
    with pytest.raises(ValidationError):
        TypeAdapter(EngineeringValueV2).validate_python(
            {"kind": "enumeration", "values": [1, 1.0]}
        )
    with pytest.raises(ValidationError):
        TypeAdapter(EngineeringValueV2).validate_python(
            {"kind": "range", "minimum": 2, "maximum": 1}
        )
    with pytest.raises(ValidationError):
        TypeAdapter(EngineeringValueV2).validate_python(
            {
                "kind": "curve",
                "independent_quantity": "time",
                "independent_unit": "s",
                "interpolation": "linear",
                "points": [{"x": 1, "y": 1}, {"x": 1, "y": 2}],
            }
        )


@pytest.mark.parametrize(
    "value",
    [True, "1.0", math.nan, math.inf, -0.0, 2**53, 1e20],
)
def test_engineering_numbers_reject_coercion_and_noncanonical_values(value):
    with pytest.raises(ValidationError):
        TypeAdapter(EngineeringValueV2).validate_python(
            {"kind": "scalar", "value": value}
        )


@pytest.mark.parametrize(
    "decimal",
    ["01", "+1", ".5", "1.", "NaN", "Infinity", " 1", "1 "],
)
def test_exact_decimal_string_uses_the_closed_grammar(decimal):
    with pytest.raises(ValidationError):
        TypeAdapter(EngineeringValueV2).validate_python(
            {"kind": "exact_decimal_string", "value": decimal}
        )


def test_known_and_unknown_claim_shapes_are_exclusive():
    adapter = TypeAdapter(CandidateClaimV2)
    known = adapter.validate_python(_known_claim())
    assert known.value_state == "known"

    unknown_payload = {
        "claim_id": CLAIM_ID,
        "revision_id": REVISION_ID,
        "slot_id": SLOT_ID,
        "value_state": "unknown",
        "value": {"kind": "unknown", "reason": "not stated"},
        "validity_conditions": ["synthetic conformance fixture only"],
        "provenance": "fixture_json_v2",
        "evidence_ids": [],
        "claim_fingerprint": HASH,
    }
    assert adapter.validate_python(unknown_payload).value_state == "unknown"

    with pytest.raises(ValidationError):
        adapter.validate_python({**unknown_payload, "unit": "h"})
    with pytest.raises(ValidationError):
        adapter.validate_python(
            {**_known_claim(), "value": {"kind": "unknown", "reason": "missing"}}
        )


@pytest.mark.parametrize(
    "payload",
    [
        {
            **_evidence_base("manufacturer_document", "manufacturer"),
            "artifact_hash": HASH,
            "document_identity": "ACME DS-100 rev C",
            "source_uri": "https://example.invalid/ds-100",
            "source_locator": "Table 4",
            "excerpt": "Rated voltage: 36 V",
        },
        {
            **_evidence_base("private_upload", "synthetic_fixture"),
            "artifact_hash": HASH,
            "local_provenance": "checked-in Program 01A fixture",
        },
        {
            **_evidence_base("user_measurement", "user"),
            "measurement_method": "four-wire resistance measurement",
            "measurement_unit": "ohm",
            "observed_at": "2026-08-11T12:00:00Z",
            "recorded_observation": "1.40 ohm at 23 degC",
        },
        {
            **_evidence_base("derived_claim", "prometheus_derivation"),
            "derivation_method": "ratio from reviewed tooth counts",
            "parent_claim_ids": [CLAIM_ID],
            "parent_evidence_ids": [],
        },
        {
            **_evidence_base("validation_observation", "validation_activity"),
            "physical_validation_status": "component_validated",
            "test_provenance": "bench inspection protocol P-01",
            "observed_at": "2026-08-11T12:00:00Z",
            "recorded_observation": "connector keying matched the model",
        },
    ],
    ids=[
        "manufacturer-document",
        "private-upload",
        "user-measurement",
        "derived-claim",
        "validation-observation",
    ],
)
def test_all_evidence_classes_and_conditional_content(payload):
    validated = TypeAdapter(EvidenceRecordV2).validate_python(payload)
    assert validated.evidence_class == payload["evidence_class"]


@pytest.mark.parametrize(
    ("payload", "missing"),
    [
        (
            {
                **_evidence_base("manufacturer_document", "manufacturer"),
                "artifact_hash": HASH,
                "document_identity": "DS-100",
            },
            "artifact_hash",
        ),
        (
            {
                **_evidence_base("private_upload", "synthetic_fixture"),
                "artifact_hash": HASH,
                "local_provenance": "fixture",
            },
            "local_provenance",
        ),
        (
            {
                **_evidence_base("user_measurement", "user"),
                "measurement_method": "meter",
                "measurement_unit": "V",
                "observed_at": "2026-08-11T12:00:00Z",
                "recorded_observation": "36 V",
            },
            "recorded_observation",
        ),
        (
            {
                **_evidence_base("derived_claim", "prometheus_derivation"),
                "derivation_method": "calculation",
                "parent_claim_ids": [CLAIM_ID],
                "parent_evidence_ids": [],
            },
            "parent_claim_ids",
        ),
        (
            {
                **_evidence_base("validation_observation", "validation_activity"),
                "test_provenance": "protocol",
                "observed_at": "2026-08-11T12:00:00Z",
                "recorded_observation": "observed",
            },
            "test_provenance",
        ),
    ],
)
def test_evidence_class_required_fields_are_not_interchangeable(payload, missing):
    payload.pop(missing)
    with pytest.raises(ValidationError):
        TypeAdapter(EvidenceRecordV2).validate_python(payload)


def test_measurement_requires_an_artifact_or_recorded_observation():
    payload = {
        **_evidence_base("user_measurement", "user"),
        "measurement_method": "meter",
        "measurement_unit": "V",
        "observed_at": "2026-08-11T12:00:00Z",
    }
    with pytest.raises(ValidationError):
        TypeAdapter(EvidenceRecordV2).validate_python(payload)


def test_document_locator_and_excerpt_are_a_pair():
    payload = {
        **_evidence_base("private_upload", "synthetic_fixture"),
        "artifact_hash": HASH,
        "local_provenance": "fixture",
        "source_locator": "parameters[0]",
    }
    with pytest.raises(ValidationError):
        TypeAdapter(EvidenceRecordV2).validate_python(payload)


def test_evidence_uri_validation_does_not_rewrite_hashed_text():
    payload = {
        **_evidence_base("manufacturer_document", "manufacturer"),
        "artifact_hash": HASH,
        "document_identity": "DS-100",
        "source_uri": "https://example.invalid",
    }
    validated = TypeAdapter(EvidenceRecordV2).validate_python(payload)
    assert validated.model_dump(mode="json")["source_uri"] == payload["source_uri"]


def test_review_request_trims_bounded_text_and_rejects_duplicates():
    request = ReviewRequestV2.model_validate(
        {
            "expected_draft_version": 0,
            "reviewed_by": "  local reviewer  ",
            "decisions": [
                {"claim_id": CLAIM_ID, "status": "accepted", "note": "  checked  "}
            ],
        }
    )
    assert request.reviewed_by == "local reviewer"
    assert request.decisions[0].note == "checked"

    with pytest.raises(ValidationError):
        ReviewRequestV2.model_validate(
            {
                "expected_draft_version": 0,
                "reviewed_by": "reviewer",
                "decisions": [
                    {"claim_id": CLAIM_ID, "status": "accepted", "note": "one"},
                    {"claim_id": CLAIM_ID, "status": "rejected", "note": "two"},
                ],
            }
        )


def test_reviewer_and_note_limits_are_measured_in_utf8_bytes():
    base = {
        "expected_draft_version": 0,
        "reviewed_by": "é" * 128,
        "decisions": [
            {"claim_id": CLAIM_ID, "status": "accepted", "note": "é" * 2048}
        ],
    }
    ReviewRequestV2.model_validate(base)

    with pytest.raises(ValidationError):
        ReviewRequestV2.model_validate({**base, "reviewed_by": "é" * 129})
    oversized_note = deepcopy(base)
    oversized_note["decisions"][0]["note"] = "é" * 2049
    with pytest.raises(ValidationError):
        ReviewRequestV2.model_validate(oversized_note)


@pytest.mark.parametrize(
    "claim_id",
    [
        "AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA",
        "aaaaaaaa-aaaa-1aaa-8aaa-aaaaaaaaaaaa",
        "{aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa}",
    ],
)
def test_uuid_inputs_require_lowercase_hyphenated_v4_spelling(claim_id):
    with pytest.raises(ValidationError):
        ReviewRequestV2.model_validate(
            {
                "expected_draft_version": 0,
                "reviewed_by": "reviewer",
                "decisions": [
                    {"claim_id": claim_id, "status": "accepted", "note": "checked"}
                ],
            }
        )


def test_request_numbers_are_strict_and_contracts_are_closed():
    valid = {
        "expected_draft_version": 0,
        "reviewed_by": "reviewer",
        "decisions": [
            {"claim_id": CLAIM_ID, "status": "accepted", "note": "checked"}
        ],
    }
    for invalid in (True, "0", 0.0):
        with pytest.raises(ValidationError):
            ReviewRequestV2.model_validate(
                {**valid, "expected_draft_version": invalid}
            )
    with pytest.raises(ValidationError):
        ReviewRequestV2.model_validate({**valid, "unexpected": "field"})


def test_publication_request_rejects_unsupported_contracts():
    PublicationRequestV2.model_validate(
        {
            "expected_draft_version": 1,
            "schema_id": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
        }
    )
    for field, value in (
        ("schema_id", "urn:prometheus:schema:execution-component:9.0.0"),
        ("schema_version", "9.0.0"),
    ):
        payload = {
            "expected_draft_version": 1,
            "schema_id": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
        }
        payload[field] = value
        with pytest.raises(ValidationError):
            PublicationRequestV2.model_validate(payload)


def test_execution_component_enforces_graph_and_contract_order():
    semantic = _load_fixture()
    package = ExecutionComponentV2.model_validate(semantic)
    assert package.execution_readiness == "blocked"
    assert len([gate for gate in package.gates if gate.phase == "publication"]) == 4
    assert all(
        gate.state == "satisfied"
        for gate in package.gates
        if gate.phase == "publication"
    )
    assert any(
        gate.phase == "execution" and gate.state == "blocked"
        for gate in package.gates
    )
    assert package.authority.authority_role == "input_only"

    reversed_slots = deepcopy(semantic)
    reversed_slots["parameter_slots"] = list(
        reversed(reversed_slots["parameter_slots"])
    )
    with pytest.raises(ValidationError):
        ExecutionComponentV2.model_validate(reversed_slots)

    absent_claim = deepcopy(semantic)
    absent_claim["claims"] = absent_claim["claims"][1:]
    with pytest.raises(ValidationError):
        ExecutionComponentV2.model_validate(absent_claim)

    blocked_publication = deepcopy(semantic)
    blocked_publication["gates"][0]["state"] = "blocked"
    with pytest.raises(ValidationError):
        ExecutionComponentV2.model_validate(blocked_publication)

    false_readiness = deepcopy(semantic)
    false_readiness["execution_readiness"] = "ready"
    with pytest.raises(ValidationError):
        ExecutionComponentV2.model_validate(false_readiness)


def test_execution_component_rejects_cross_revision_and_stale_review_links():
    semantic = _load_fixture()
    cross_revision = deepcopy(semantic)
    cross_revision["evidence"][0]["revision_id"] = (
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
    )
    with pytest.raises(ValidationError):
        ExecutionComponentV2.model_validate(cross_revision)

    stale_review = deepcopy(semantic)
    stale_review["claim_reviews"][0]["reviewed_claim_fingerprint"] = HASH
    with pytest.raises(ValidationError):
        ExecutionComponentV2.model_validate(stale_review)


def test_summary_keeps_verdict_coverage_and_execution_independent():
    summary = ProjectSummaryV2.model_validate(
        {
            "schema_version": "2.0.0",
            "verdict": "requirements_violated",
            "coverage": "insufficient",
            "execution_state": "completed_with_blocked_work",
            "counts": {
                "satisfied_within_scope": 8,
                "violated": 2,
                "indeterminate": 3,
                "not_applicable": 1,
                "not_evaluated": 5,
            },
            "obligation_total": 19,
            "assessment_scope_id": "sha256:" + "0" * 64,
            "decision_core": {"name": "prometheus_cpp", "version": "1.0.0"},
        }
    )
    assert summary.verdict == "requirements_violated"


def test_summary_rejects_unscoped_satisfaction():
    with pytest.raises(ValidationError):
        ProjectSummaryV2.model_validate(
            {
                "schema_version": "2.0.0",
                "verdict": "satisfied_within_scope",
                "coverage": "insufficient",
                "execution_state": "completed_with_blocked_work",
                "counts": {
                    "satisfied_within_scope": 1,
                    "violated": 0,
                    "indeterminate": 0,
                    "not_applicable": 0,
                    "not_evaluated": 1,
                },
                "obligation_total": 2,
                "assessment_scope_id": "sha256:" + "0" * 64,
                "decision_core": {"name": "prometheus_cpp", "version": "1.0.0"},
            }
        )


@pytest.mark.parametrize(
    "mutation",
    [
        {"verdict": "requirements_violated", "violated": 0},
        {"verdict": "indeterminate", "violated": 1},
        {"coverage": "sufficient", "not_evaluated": 1},
        {"coverage": "not_assessed", "satisfied_within_scope": 1},
    ],
)
def test_project_summary_consistency_rules(mutation):
    payload = {
        "schema_version": "2.0.0",
        "verdict": "satisfied_within_scope",
        "coverage": "sufficient",
        "execution_state": "completed",
        "counts": {
            "satisfied_within_scope": 1,
            "violated": 0,
            "indeterminate": 0,
            "not_applicable": 0,
            "not_evaluated": 0,
        },
        "obligation_total": 1,
        "assessment_scope_id": "sha256:" + "0" * 64,
        "decision_core": {"name": "prometheus_cpp", "version": "1.0.0"},
    }
    for field, value in mutation.items():
        if field in payload["counts"]:
            payload["counts"][field] = value
            payload["obligation_total"] = sum(payload["counts"].values())
        else:
            payload[field] = value
    with pytest.raises(ValidationError):
        ProjectSummaryV2.model_validate(payload)


def test_project_summary_rejects_count_sum_and_bad_scope_hash():
    payload = {
        "schema_version": "2.0.0",
        "verdict": "indeterminate",
        "coverage": "not_assessed",
        "execution_state": "not_started",
        "counts": {
            "satisfied_within_scope": 0,
            "violated": 0,
            "indeterminate": 0,
            "not_applicable": 0,
            "not_evaluated": 0,
        },
        "obligation_total": 1,
        "assessment_scope_id": "SHA256:" + "0" * 64,
        "decision_core": {"name": "prometheus_cpp", "version": "1.0.0"},
    }
    with pytest.raises(ValidationError):
        ProjectSummaryV2.model_validate(payload)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("verdict", "passed"),
        ("coverage", "complete"),
        ("execution_state", "success"),
    ],
)
def test_project_summary_vocabularies_are_closed(field, value):
    payload = {
        "schema_version": "2.0.0",
        "verdict": "indeterminate",
        "coverage": "not_assessed",
        "execution_state": "not_started",
        "counts": {
            "satisfied_within_scope": 0,
            "violated": 0,
            "indeterminate": 0,
            "not_applicable": 0,
            "not_evaluated": 0,
        },
        "obligation_total": 0,
        "assessment_scope_id": "sha256:" + "0" * 64,
        "decision_core": {"name": "prometheus_cpp", "version": "1.0.0"},
    }
    payload[field] = value
    with pytest.raises(ValidationError):
        ProjectSummaryV2.model_validate(payload)
