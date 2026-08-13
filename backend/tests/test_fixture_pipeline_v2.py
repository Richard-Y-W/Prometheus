from __future__ import annotations

import hashlib
import json
from pathlib import Path
from uuid import UUID

import pytest
import sqlalchemy as sa

from app import fixture_pipeline_v2
from app.canonical_json import canonicalize_value, object_hash
from app.database import SessionLocal
from app.fixture_pipeline_v2 import (
    FIXTURE_ARTIFACT_HASH,
    FIXTURE_ID,
    FIXTURE_PATH,
    FixtureDraftError,
    create_fixture_draft,
)
from app.fixture_catalog_v2 import FIXTURE_IDS, get_fixture_definition
from app.models_v1 import Component, ComponentRevision, Manufacturer
from app.models_v2 import (
    ArtifactObjectV2,
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimEvidenceLinkV2,
    ClaimReviewEventV2,
    ClaimSelectionV2,
    EvidenceRecordV2,
    FixtureIngestionJobV2,
    ParameterSlotV2,
    PublishedObject,
)


REPOSITORY_ROOT = Path(__file__).parents[2]
LEGACY_FIXTURE_PATH = (
    REPOSITORY_ROOT / "fixtures" / "evidence" / "pm-36-gm.synthetic.json"
)


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


def test_exact_fixture_hash_and_numerical_meaning_are_checked_in():
    fixture_bytes = FIXTURE_PATH.read_bytes()
    assert FIXTURE_ARTIFACT_HASH == (
        f"sha256:{hashlib.sha256(fixture_bytes).hexdigest()}"
    )
    v2 = json.loads(fixture_bytes)
    legacy = json.loads(LEGACY_FIXTURE_PATH.read_bytes())

    assert set(v2) == {
        "fixture_id",
        "schema_version",
        "manufacturer",
        "part_number",
        "revision",
        "component_class",
        "source_authority",
        "physical_validation_status",
        "parameters",
        "limitations",
    }
    assert v2["fixture_id"] == FIXTURE_ID
    assert v2["schema_version"] == "2.0.0"
    assert len(v2["parameters"]) == len(legacy["parameters"]) == 17

    legacy_by_name = {item["name"]: item for item in legacy["parameters"]}
    for parameter in v2["parameters"]:
        old = legacy_by_name[parameter["name"]]
        assert parameter["quantity"] == old["quantity"]
        assert parameter["dimension"] == old["dimension"]
        assert parameter["validity_conditions"] == old["validity_conditions"]
        assert parameter["source_locator"] == f"parameters.{parameter['name']}"
        if old["value"]["kind"] == "unknown":
            assert parameter["unknown_reason"] == old["value"]["reason"]
            assert "value" not in parameter
            assert "unit" not in parameter
        else:
            assert parameter["value"] == old["value"]
            assert parameter["unit"] == old["unit"]
            assert parameter["original_value"] == old["original_value"]
            assert parameter["original_unit"] == old["original_unit"]

    forbidden_keys = {
        "claim_id",
        "database_id",
        "review",
        "review_decision",
        "reviewed_by",
        "verdict",
        "confidence",
    }
    assert not forbidden_keys.intersection(v2)
    assert all(not forbidden_keys.intersection(item) for item in v2["parameters"])


def test_fixture_builds_one_complete_unreviewed_v2_draft_graph(db):
    result = create_fixture_draft(
        db,
        fixture_id=FIXTURE_ID,
        idempotency_key="fixture-create-0001",
    )

    assert result.ingestion_job.status == "succeeded"
    assert result.ingestion_job.revision_id == result.revision.id
    assert result.ingestion_job.artifact_hash == FIXTURE_ARTIFACT_HASH
    assert result.revision.contract_schema_version == "2.0.0"
    assert result.revision.draft_version == 0
    assert result.revision.status == "draft"
    assert result.revision.publication_integrity == "v2_draft"
    assert result.revision.published_object_hash is None
    assert result.revision.content_hash is None
    assert result.revision.published_at is None
    assert result.revision.supported_recipes == [
        fixture_pipeline_v2.CAPABILITY_ID
    ]
    assert len(result.revision.missing_information) == 1
    assert len(result.revision.limitations) == 2
    for record in (
        *result.revision.missing_information,
        *result.revision.limitations,
    ):
        identity = record.get("missing_information_id") or record.get(
            "limitation_id"
        )
        _assert_uuid4(identity)

    assert len(result.slots) == len(result.claims) == len(result.selections) == 17
    assert [slot.name for slot in result.slots] == sorted(
        slot.name for slot in result.slots
    )
    assert all(slot.claims for slot in result.slots)
    assert all(selection.claim_id for selection in result.selections)
    assert all(claim.finalized and claim.fingerprint for claim in result.claims)
    assert len({claim.slot_id for claim in result.claims}) == 17
    for domain_id in [
        result.revision.id,
        result.ingestion_job.id,
        *(slot.id for slot in result.slots),
        *(claim.id for claim in result.claims),
        *(selection.id for selection in result.selections),
        *(gate.id for gate in result.gates),
    ]:
        _assert_uuid4(domain_id)

    for claim in result.claims:
        assert claim.fingerprint == object_hash(
            canonicalize_value(_claim_semantic_value(db, claim))
        )

    unknown_claims = [claim for claim in result.claims if claim.value_state == "unknown"]
    assert len(unknown_claims) == 1
    assert unknown_claims[0].reason == (
        "synthetic fixture does not define gearbox lifetime"
    )
    assert not db.scalars(
        sa.select(ClaimEvidenceLinkV2).where(
            ClaimEvidenceLinkV2.claim_id == unknown_claims[0].id
        )
    ).all()
    assert _row_count(db, EvidenceRecordV2) == 16
    assert _row_count(db, ClaimEvidenceLinkV2) == 16
    assert all(
        evidence.evidence_class == "private_upload"
        and evidence.source_authority == "synthetic_fixture"
        and evidence.physical_validation_status == "unvalidated"
        and evidence.artifact_hash == FIXTURE_ARTIFACT_HASH
        for evidence in db.scalars(sa.select(EvidenceRecordV2))
    )

    assert {gate.phase for gate in result.gates} == {"publication", "execution"}
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
    assert publication_gates["claim_review"].satisfying_references == []
    assert all(
        gate.state == "satisfied"
        for name, gate in publication_gates.items()
        if name != "claim_review"
    )
    execution_gate = next(
        gate for gate in result.gates if gate.phase == "execution"
    )
    assert execution_gate.required_review_type == "package_consumer"
    assert execution_gate.state == "blocked"

    stored = db.get(ArtifactObjectV2, FIXTURE_ARTIFACT_HASH)
    assert stored.payload_bytes == FIXTURE_PATH.read_bytes()
    assert _row_count(db, PublishedObject) == 0
    assert _row_count(db, ClaimReviewEventV2) == 0


def test_fixture_allows_multiple_candidates_but_keeps_one_selection(db):
    result = create_fixture_draft(
        db,
        fixture_id=FIXTURE_ID,
        idempotency_key="fixture-create-0002",
    )
    slot = result.slots[0]
    conflicting = CandidateClaimV2(
        revision_id=result.revision.id,
        slot_id=slot.id,
        value_state="known",
        value={"kind": "scalar", "value": 999.0},
        unit="1",
        original_value="999.0",
        original_unit="1",
        reason=None,
        validity_conditions=["deliberately conflicting candidate"],
        provenance="direct_test_candidate",
        finalized=False,
        fingerprint=None,
    )
    db.add(conflicting)
    db.flush()

    assert db.scalar(
        sa.select(sa.func.count())
        .select_from(CandidateClaimV2)
        .where(CandidateClaimV2.slot_id == slot.id)
    ) == 2
    assert db.scalar(
        sa.select(sa.func.count())
        .select_from(ClaimSelectionV2)
        .where(ClaimSelectionV2.slot_id == slot.id)
    ) == 1


def test_same_fixture_idempotency_key_replays_the_same_graph(db):
    first = create_fixture_draft(
        db,
        fixture_id=FIXTURE_ID,
        idempotency_key="fixture-create-replay-01",
    )
    second = create_fixture_draft(
        db,
        fixture_id=FIXTURE_ID,
        idempotency_key="fixture-create-replay-01",
    )

    assert second.ingestion_job.id == first.ingestion_job.id
    assert second.revision.id == first.revision.id
    assert [slot.id for slot in second.slots] == [slot.id for slot in first.slots]
    assert _row_count(db, FixtureIngestionJobV2) == 1
    assert _row_count(db, ComponentRevision) == 1
    assert _row_count(db, ArtifactObjectV2) == 1


@pytest.mark.parametrize(
    "fixture_id",
    [
        "prometheus.pm-36-gm.fixture-1",
        "prometheus.pm-36-gm.fixture-\uff12",
        "prometheus.pm-36-g\u043c.fixture-2",
    ],
)
def test_unsupported_and_unicode_lookalike_fixture_ids_create_nothing(
    db, fixture_id: str
):
    with pytest.raises(FixtureDraftError) as captured:
        create_fixture_draft(
            db,
            fixture_id=fixture_id,
            idempotency_key="fixture-invalid-0001",
        )
    assert captured.value.code == "unsupported_fixture_id"
    for model in (
        FixtureIngestionJobV2,
        ArtifactObjectV2,
        Manufacturer,
        Component,
        ComponentRevision,
        ParameterSlotV2,
        CandidateClaimV2,
        EvidenceRecordV2,
        ClaimSelectionV2,
        CapabilityGateV2,
    ):
        assert _row_count(db, model) == 0


@pytest.mark.parametrize(
    "substitution",
    [
        {"manufacturer": "A Real Manufacturer"},
        {"part_number": "SUBSTITUTED"},
        {"source_path": "/tmp/untrusted.json"},
        {"source_hash": "sha256:" + "0" * 64},
        {"consumer_hash": "sha256:" + "1" * 64},
        {"capability_id": "caller.selected"},
    ],
)
def test_caller_cannot_substitute_fixture_identity_or_source(db, substitution):
    with pytest.raises(TypeError):
        create_fixture_draft(
            db,
            fixture_id=FIXTURE_ID,
            idempotency_key="fixture-substitution-01",
            **substitution,
        )
    assert _row_count(db, FixtureIngestionJobV2) == 0
    assert _row_count(db, ArtifactObjectV2) == 0


@pytest.mark.parametrize("fixture_id", FIXTURE_IDS[:2])
def test_motor_fixtures_ingest_source_and_consumer_with_role_policy(db, fixture_id):
    definition = get_fixture_definition(fixture_id)
    result = create_fixture_draft(
        db,
        fixture_id=fixture_id,
        idempotency_key=f"fixture-motor-{fixture_id[-10:]}-01",
    )

    assert result.ingestion_job.artifact_hash == definition.source_hash
    assert result.revision.supported_recipes == [definition.capability_id]
    assert _row_count(db, ArtifactObjectV2) == 2
    source = db.get(ArtifactObjectV2, definition.source_hash)
    consumer = db.get(ArtifactObjectV2, definition.consumer_hash)
    assert source.payload_bytes == definition.source_path.read_bytes()
    assert source.media_type == "application/json"
    assert consumer.payload_bytes == definition.consumer_path.read_bytes()
    assert consumer.media_type == definition.consumer_media_type

    required_names = {
        slot.name for slot in result.slots if slot.required_for_execution
    }
    assert required_names == set(definition.required_for_execution)
    assert {
        slot.name for slot in result.slots if not slot.required_for_execution
    } == {
        "nominal_voltage_v",
        "supply_current_limit_a",
        "gearbox_lifetime",
    }
    execution_gate = next(
        gate for gate in result.gates if gate.phase == "execution"
    )
    assert execution_gate.required_review_type == "package_consumer"
    assert execution_gate.state == "satisfied"
    assert execution_gate.satisfying_references == [definition.consumer_hash]
    assert execution_gate.reason is None
    assert any(
        "synthetic" in item["statement"].lower()
        and "physical" in item["statement"].lower()
        for item in result.revision.limitations
    )


def test_fixture_idempotency_key_cannot_switch_catalog_identity(db):
    create_fixture_draft(
        db,
        fixture_id=FIXTURE_IDS[0],
        idempotency_key="fixture-cross-catalog-0001",
    )
    before = {
        model: _row_count(db, model)
        for model in (
            FixtureIngestionJobV2,
            ArtifactObjectV2,
            ComponentRevision,
            ParameterSlotV2,
            CandidateClaimV2,
        )
    }
    db.rollback()
    with pytest.raises(FixtureDraftError) as captured:
        create_fixture_draft(
            db,
            fixture_id=FIXTURE_IDS[1],
            idempotency_key="fixture-cross-catalog-0001",
        )
    assert captured.value.code == "fixture_idempotency_conflict"
    assert before == {model: _row_count(db, model) for model in before}


def test_pipeline_failure_rolls_back_every_draft_row(db, monkeypatch):
    def injected_failure(**_semantic):
        raise RuntimeError("injected claim fingerprint failure")

    monkeypatch.setattr(fixture_pipeline_v2, "_claim_fingerprint", injected_failure)
    with pytest.raises(RuntimeError, match="injected claim fingerprint failure"):
        create_fixture_draft(
            db,
            fixture_id=FIXTURE_ID,
            idempotency_key="fixture-rollback-0001",
        )

    for model in (
        FixtureIngestionJobV2,
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
