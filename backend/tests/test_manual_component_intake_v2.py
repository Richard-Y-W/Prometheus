from __future__ import annotations

from uuid import UUID

from fastapi.testclient import TestClient
import pytest
import sqlalchemy as sa

from app import manual_component_intake_v2
from app.canonical_json import canonicalize_value, object_hash
from app.contracts_v2 import SCHEMA_ID, SCHEMA_VERSION
from app.database import SessionLocal
from app.main import app
from app.manual_component_intake_v2 import (
    ManualComponentDraftError,
    ManualComponentDraftRequestV2,
    create_manual_component_draft,
)
from app.models_v1 import Component, ComponentRevision, Manufacturer
from app.models_v2 import (
    ArtifactObjectV2,
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimEvidenceLinkV2,
    ClaimReviewEventV2,
    ClaimSelectionV2,
    EvidenceRecordV2,
    ManualComponentDraftJobV2,
    ParameterSlotV2,
    PublishedObject,
)
from app.package_compiler_v2 import compile_execution_component


@pytest.fixture
def db():
    with SessionLocal() as session:
        yield session
        session.rollback()


def _row_count(db, model) -> int:
    return db.scalar(sa.select(sa.func.count()).select_from(model))


def _assert_uuid4(value: str) -> None:
    parsed = UUID(value)
    assert parsed.version == 4
    assert str(parsed) == value


def _sample_body(**overrides: object) -> dict[str, object]:
    body: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "manufacturer": "Acme Bearings Inc.",
        "part_number": "AB-6205-2RS",
        "component_class": "ball_bearing",
        "revision": "nameplate-1",
        "capability_id": "component_input.manual_entry_v1",
        "limitations": ["No independent validation has been performed."],
        "parameters": [
            {
                "name": "bore_diameter",
                "quantity": "length",
                "dimension": "length",
                "required_for_execution": True,
                "value": {"kind": "scalar", "value": 0.025},
                "unit": "m",
                "original_value": "25",
                "original_unit": "mm",
                "validity_conditions": ["room temperature"],
                "measurement_method": "read from the physical nameplate",
                "observed_at": "2026-08-15T00:00:00Z",
            },
            {
                "name": "dynamic_load_rating",
                "quantity": "force",
                "dimension": "force",
                "required_for_execution": False,
                "value": {"kind": "scalar", "value": 14000.0},
                "unit": "N",
                "original_value": "14000",
                "original_unit": "N",
                "validity_conditions": [],
                "measurement_method": "transcribed from a supplier catalog page",
                "observed_at": "2026-08-15T00:05:00Z",
            },
            {
                "name": "fatigue_life",
                "quantity": "service_life",
                "dimension": "time",
                "required_for_execution": False,
                "unknown_reason": "no fatigue data was available at entry time",
                "validity_conditions": [],
            },
        ],
    }
    body.update(overrides)
    return body


def _sample_request(**overrides: object) -> ManualComponentDraftRequestV2:
    return ManualComponentDraftRequestV2.model_validate(_sample_body(**overrides))


def _claim_semantic_value(db, claim: CandidateClaimV2) -> dict[str, object]:
    evidence_ids = list(
        db.scalars(
            sa.select(ClaimEvidenceLinkV2.evidence_id)
            .where(
                ClaimEvidenceLinkV2.revision_id == claim.revision_id,
                ClaimEvidenceLinkV2.claim_id == claim.id,
            )
            .order_by(ClaimEvidenceLinkV2.evidence_id)
        )
    )
    value = (
        claim.value
        if claim.value_state == "known"
        else {"kind": "unknown", "reason": claim.reason}
    )
    semantic: dict[str, object] = {
        "revision_id": claim.revision_id,
        "slot_id": claim.slot_id,
        "value_state": claim.value_state,
        "value": value,
        "provenance": claim.provenance,
        "evidence_ids": evidence_ids,
        "validity_conditions": claim.validity_conditions,
    }
    if claim.value_state == "known":
        semantic.update(
            {
                "unit": claim.unit,
                "original_value": claim.original_value,
                "original_unit": claim.original_unit,
            }
        )
    return semantic


def test_manual_draft_builds_one_complete_unreviewed_v2_draft_graph(db):
    result = create_manual_component_draft(
        db,
        request=_sample_request(),
        idempotency_key="manual-create-0001",
    )

    assert result.job.status == "succeeded"
    assert result.job.revision_id == result.revision.id
    assert result.job.artifact_hash is not None
    assert result.revision.contract_schema_version == "2.0.0"
    assert result.revision.draft_version == 0
    assert result.revision.status == "draft"
    assert result.revision.publication_integrity == "v2_draft"
    assert result.revision.supported_recipes == ["component_input.manual_entry_v1"]
    assert len(result.revision.missing_information) == 1
    assert len(result.revision.limitations) == 2
    for record in (*result.revision.missing_information, *result.revision.limitations):
        identity = record.get("missing_information_id") or record.get("limitation_id")
        _assert_uuid4(identity)

    assert len(result.slots) == len(result.claims) == len(result.selections) == 3
    assert [slot.name for slot in result.slots] == sorted(
        slot.name for slot in result.slots
    )
    assert all(claim.finalized and claim.fingerprint for claim in result.claims)
    for claim in result.claims:
        assert claim.fingerprint == object_hash(
            canonicalize_value(_claim_semantic_value(db, claim))
        )

    unknown_claims = [c for c in result.claims if c.value_state == "unknown"]
    assert len(unknown_claims) == 1
    assert unknown_claims[0].reason == "no fatigue data was available at entry time"
    assert not db.scalars(
        sa.select(ClaimEvidenceLinkV2).where(
            ClaimEvidenceLinkV2.claim_id == unknown_claims[0].id
        )
    ).all()

    assert _row_count(db, EvidenceRecordV2) == 2
    assert all(
        evidence.evidence_class == "user_measurement"
        and evidence.source_authority == "user"
        and evidence.physical_validation_status == "unvalidated"
        and evidence.artifact_hash == result.job.artifact_hash
        for evidence in db.scalars(sa.select(EvidenceRecordV2))
    )

    publication_gates = {
        gate.required_review_type: gate
        for gate in result.gates
        if gate.phase == "publication"
    }
    assert set(publication_gates) == {
        "component_identity",
        "source_artifact",
        "claim_selection",
        "claim_review",
    }
    assert publication_gates["claim_review"].state == "pending"
    assert all(
        gate.state == "satisfied"
        for name, gate in publication_gates.items()
        if name != "claim_review"
    )
    execution_gate = next(gate for gate in result.gates if gate.phase == "execution")
    assert execution_gate.required_review_type == "package_consumer"
    assert execution_gate.state == "blocked"
    assert execution_gate.reason == manual_component_intake_v2.NO_CONSUMER_REASON

    artifact = db.get(ArtifactObjectV2, result.job.artifact_hash)
    assert artifact is not None
    assert (
        artifact.media_type
        == manual_component_intake_v2.MANUAL_ENTRY_RECORD_MEDIA_TYPE
    )
    assert _row_count(db, PublishedObject) == 0
    assert _row_count(db, ClaimReviewEventV2) == 0


def test_manual_draft_idempotency_key_replays_the_same_graph(db):
    first = create_manual_component_draft(
        db, request=_sample_request(), idempotency_key="manual-replay-0001"
    )
    second = create_manual_component_draft(
        db, request=_sample_request(), idempotency_key="manual-replay-0001"
    )

    assert second.job.id == first.job.id
    assert second.revision.id == first.revision.id
    assert [slot.id for slot in second.slots] == [slot.id for slot in first.slots]
    assert _row_count(db, ManualComponentDraftJobV2) == 1
    assert _row_count(db, ComponentRevision) == 1
    assert _row_count(db, ArtifactObjectV2) == 1


def test_manual_draft_idempotency_key_conflicts_on_a_different_request(db):
    create_manual_component_draft(
        db, request=_sample_request(), idempotency_key="manual-conflict-0001"
    )
    db.rollback()

    with pytest.raises(ManualComponentDraftError) as captured:
        create_manual_component_draft(
            db,
            request=_sample_request(part_number="DIFFERENT-PART"),
            idempotency_key="manual-conflict-0001",
        )
    assert captured.value.code == "manual_draft_idempotency_conflict"


def test_manual_draft_rejects_a_duplicate_component_revision(db):
    create_manual_component_draft(
        db, request=_sample_request(), idempotency_key="manual-duplicate-0001"
    )
    db.rollback()

    with pytest.raises(ManualComponentDraftError) as captured:
        create_manual_component_draft(
            db, request=_sample_request(), idempotency_key="manual-duplicate-0002"
        )
    assert captured.value.code == "manual_draft_revision_exists"


def test_manual_draft_rejects_invalid_idempotency_key(db):
    with pytest.raises(ManualComponentDraftError) as captured:
        create_manual_component_draft(
            db, request=_sample_request(), idempotency_key="too-short"
        )
    assert captured.value.code == "invalid_idempotency_key"
    for model in (ManualComponentDraftJobV2, ArtifactObjectV2, ComponentRevision):
        assert _row_count(db, model) == 0


def test_manual_draft_rejects_unsupported_schema_version(db):
    with pytest.raises(ManualComponentDraftError) as captured:
        create_manual_component_draft(
            db,
            request=_sample_request(schema_version="1.9.9"),
            idempotency_key="manual-schema-0001",
        )
    assert captured.value.code == "unsupported_schema"


def test_manual_draft_pipeline_failure_rolls_back_every_draft_row(db, monkeypatch):
    def injected_failure(**_semantic):
        raise RuntimeError("injected claim fingerprint failure")

    monkeypatch.setattr(
        manual_component_intake_v2, "_claim_fingerprint", injected_failure
    )
    with pytest.raises(RuntimeError, match="injected claim fingerprint failure"):
        create_manual_component_draft(
            db, request=_sample_request(), idempotency_key="manual-rollback-0001"
        )

    for model in (
        ManualComponentDraftJobV2,
        ArtifactObjectV2,
        Manufacturer,
        Component,
        ComponentRevision,
        ParameterSlotV2,
        CandidateClaimV2,
        EvidenceRecordV2,
        ClaimEvidenceLinkV2,
        ClaimSelectionV2,
        CapabilityGateV2,
    ):
        assert _row_count(db, model) == 0


def test_manual_draft_compiles_a_blocked_package_after_review(db):
    """The service-layer draft graph must be consumable by the unmodified
    review and package-compilation pipeline without any manual-intake-aware
    changes to that shared code."""

    result = create_manual_component_draft(
        db, request=_sample_request(), idempotency_key="manual-compile-0001"
    )
    from app.contracts_v2 import ClaimReviewDecisionV2, ReviewRequestV2
    from app.review_service_v2 import review_claims

    decisions = [
        ClaimReviewDecisionV2(
            claim_id=claim.id, status="accepted", note="manually entered value"
        )
        for claim in result.claims
    ]
    review_claims(
        revision_id=result.revision.id,
        request=ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="test-reviewer",
            decisions=decisions,
        ),
        session_factory=SessionLocal,
    )

    with SessionLocal() as fresh_db:
        compiled = compile_execution_component(fresh_db, result.revision.id)
    assert compiled.execution_readiness == "blocked"
    assert compiled.value["capability_id"] == "component_input.manual_entry_v1"
    assert len(compiled.value["claims"]) == 3
    consumer_gate = next(
        gate
        for gate in compiled.value["gates"]
        if gate["required_review_type"] == "package_consumer"
    )
    assert consumer_gate["state"] == "blocked"


def test_manual_draft_http_create_review_publish_and_export_round_trip():
    with TestClient(app) as client:
        create_response = client.post(
            "/api/v2/component-drafts",
            headers={"Idempotency-Key": "manual-http-create-0001"},
            json=_sample_body(),
        )
        assert create_response.status_code == 201, create_response.text
        created = create_response.json()
        revision = created["revision"]
        assert revision["publication_integrity"] == "v2_draft"

        get_response = client.get(f"/api/v2/component-drafts/{created['id']}")
        assert get_response.status_code == 200
        assert get_response.json() == created

        decisions = [
            {
                "claim_id": parameter["selected_claim"]["claim_id"],
                "status": "accepted",
                "note": "manually entered value",
            }
            for parameter in revision["parameters"]
        ]
        review_response = client.post(
            f"/api/v2/revisions/{revision['id']}/reviews",
            json={
                "expected_draft_version": revision["draft_version"],
                "reviewed_by": "http-reviewer",
                "decisions": decisions,
            },
        )
        assert review_response.status_code == 200, review_response.text
        reviewed = review_response.json()
        claim_review_gate = next(
            gate
            for gate in reviewed["capability_gates"]
            if gate["required_review_type"] == "claim_review"
        )
        assert claim_review_gate["state"] == "satisfied"

        publish_response = client.post(
            f"/api/v2/revisions/{revision['id']}/publication",
            headers={"Idempotency-Key": "manual-http-publish-0001"},
            json={
                "expected_draft_version": reviewed["draft_version"],
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
            },
        )
        assert publish_response.status_code == 201, publish_response.text
        published = publish_response.json()
        assert published["execution_readiness"] == "blocked"
        assert published["publication_integrity"] == "sealed_v2"

        export_response = client.get(
            f"/api/v2/revisions/{revision['id']}/execution-package"
        )
        assert export_response.status_code == 200
        assert export_response.headers["ETag"] == f'"{published["object_hash"]}"'


def test_manual_draft_not_found_returns_404():
    with TestClient(app) as client:
        response = client.get("/api/v2/component-drafts/does-not-exist")
        assert response.status_code == 404
        assert response.json()["detail"]["code"] == "manual_component_draft_not_found"
