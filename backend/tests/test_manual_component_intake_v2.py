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


def _gearmotor_parameter(name, quantity, dimension, required, value, unit,
                         original_value, original_unit) -> dict[str, object]:
    return {
        "name": name,
        "quantity": quantity,
        "dimension": dimension,
        "required_for_execution": required,
        "value": value,
        "unit": unit,
        "original_value": original_value,
        "original_unit": original_unit,
        "validity_conditions": [],
        "measurement_method": "read from the manufacturer nameplate",
        "observed_at": "2026-08-17T00:00:00Z",
    }


def _sample_gearmotor_body(**overrides: object) -> dict[str, object]:
    """A manually entered component declaring the shared DC-gearmotor
    capability with the full parameter set `consume_motor_component`
    requires -- proves a real analysis can consume manually entered values
    (Phase 5 checkpoint 4), reusing the same 17-slot contract shape as the
    Motor A/B fixtures."""
    body: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "manufacturer": "Northline Motion Co.",
        "part_number": "NM-42-GM",
        "component_class": "dc_gearmotor",
        "revision": "nameplate-1",
        "capability_id": "component_input.dc_gearmotor_v1",
        "limitations": ["manually entered, not independently verified"],
        "parameters": [
            _gearmotor_parameter(
                "continuous_torque_nm", "torque", "torque", True,
                {"kind": "scalar", "value": 0.31}, "N*m", "0.31", "N*m"),
            _gearmotor_parameter(
                "driver_current_limit_a", "electric_current_limit",
                "electric_current", True, {"kind": "scalar", "value": 6.0},
                "A", "6.0", "A"),
            _gearmotor_parameter(
                "gear_ratio", "ratio", "dimensionless", True,
                {"kind": "scalar", "value": 64.0}, "1", "64:1", "1"),
            _gearmotor_parameter(
                "gearbox_efficiency_nominal", "efficiency", "dimensionless",
                True, {"kind": "scalar", "value": 0.72}, "1", "72", "%"),
            _gearmotor_parameter(
                "gearbox_efficiency_range", "efficiency", "dimensionless",
                True, {"kind": "range", "minimum": 0.60, "maximum": 0.85},
                "1", "60-85", "%"),
            {
                "name": "gearbox_lifetime",
                "quantity": "service_life",
                "dimension": "time",
                "required_for_execution": False,
                "unknown_reason": "no gearbox lifetime data was available at entry time",
                "validity_conditions": [],
            },
            _gearmotor_parameter(
                "maximum_temperature_c", "temperature_limit", "temperature",
                True, {"kind": "scalar", "value": 130.0}, "degC", "130", "degC"),
            _gearmotor_parameter(
                "no_load_current_a", "electric_current", "electric_current",
                True, {"kind": "scalar", "value": 0.22}, "A", "0.22", "A"),
            _gearmotor_parameter(
                "no_load_speed_rad_s", "angular_velocity", "angle/time",
                True, {"kind": "scalar", "value": 345.6}, "rad/s", "3300", "rpm"),
            _gearmotor_parameter(
                "nominal_voltage_v", "voltage", "electric_potential", False,
                {"kind": "scalar", "value": 24.0}, "V", "24", "V"),
            _gearmotor_parameter(
                "stall_torque_nm", "torque", "torque", True,
                {"kind": "scalar", "value": 2.85}, "N*m", "2.85", "N*m"),
            _gearmotor_parameter(
                "supply_current_limit_a", "electric_current_limit",
                "electric_current", False, {"kind": "scalar", "value": 8.0},
                "A", "8.0", "A"),
            _gearmotor_parameter(
                "thermal_capacitance_j_k", "heat_capacity",
                "energy/temperature", True, {"kind": "scalar", "value": 95.0},
                "J/K", "95", "J/K"),
            _gearmotor_parameter(
                "thermal_resistance_k_w", "thermal_resistance",
                "temperature/power", True, {"kind": "scalar", "value": 3.8},
                "K/W", "3.8", "K/W"),
            _gearmotor_parameter(
                "torque_constant_nm_a", "torque_constant",
                "torque/electric_current", True,
                {"kind": "scalar", "value": 0.0781}, "N*m/A", "0.0781", "N*m/A"),
            {
                "name": "torque_speed_curve",
                "quantity": "torque_by_angular_velocity",
                "dimension": "torque",
                "required_for_execution": True,
                "value": {
                    "kind": "curve",
                    "independent_quantity": "angular_velocity",
                    "independent_unit": "rad/s",
                    "interpolation": "linear",
                    "points": [{"x": 0.0, "y": 2.85}, {"x": 345.6, "y": 0.0}],
                },
                "unit": "N*m",
                "original_value": "(0,2.85);(345.6,0)",
                "original_unit": "rad/s,N*m",
                "validity_conditions": [],
                "measurement_method": "read from the manufacturer torque-speed chart",
                "observed_at": "2026-08-17T00:00:00Z",
            },
            _gearmotor_parameter(
                "winding_resistance_ohm", "electrical_resistance",
                "electric_resistance", True, {"kind": "scalar", "value": 1.1},
                "ohm", "1.1", "ohm"),
        ],
    }
    body.update(overrides)
    return body


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


def _publish_manual_draft(
    client: TestClient, body: dict[str, object], create_key: str, publish_key: str
) -> dict[str, object]:
    create_response = client.post(
        "/api/v2/component-drafts",
        headers={"Idempotency-Key": create_key},
        json=body,
    )
    assert create_response.status_code == 201, create_response.text
    revision = create_response.json()["revision"]
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
    publish_response = client.post(
        f"/api/v2/revisions/{revision['id']}/publication",
        headers={"Idempotency-Key": publish_key},
        json={
            "expected_draft_version": reviewed["draft_version"],
            "schema_id": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
        },
    )
    assert publish_response.status_code == 201, publish_response.text
    return publish_response.json()


def test_get_revision_reports_a_newer_published_sibling_as_superseding():
    with TestClient(app) as client:
        older = _publish_manual_draft(
            client,
            _sample_body(revision="nameplate-1"),
            create_key="manual-supersede-older-0001",
            publish_key="manual-supersede-older-publish-0001",
        )
        newer = _publish_manual_draft(
            client,
            _sample_body(revision="nameplate-2"),
            create_key="manual-supersede-newer-0001",
            publish_key="manual-supersede-newer-publish-0001",
        )

        older_get = client.get(f"/api/v2/revisions/{older['revision_id']}")
        assert older_get.status_code == 200
        older_value = older_get.json()
        assert older_value["superseded_by"] is not None
        assert older_value["superseded_by"]["revision_id"] == newer["revision_id"]
        assert older_value["superseded_by"]["revision"] == "nameplate-2"
        assert older_value["superseded_by"]["object_hash"] == newer["object_hash"]

        newer_get = client.get(f"/api/v2/revisions/{newer['revision_id']}")
        assert newer_get.status_code == 200
        assert newer_get.json()["superseded_by"] is None


def test_manual_draft_declaring_the_shared_gearmotor_capability_is_execution_ready():
    """Phase 5 checkpoint 4: a manually entered component that declares the
    same capability Motor A/B already run against gets a real, satisfied
    package_consumer gate and compiles to a `ready` package -- not just a
    gate flip, proven end-to-end against the unmodified C++ consumer in
    desktop/execution/tests/package_consumer_tests.cpp
    (test_manual_motor_consumption)."""
    with TestClient(app) as client:
        create_response = client.post(
            "/api/v2/component-drafts",
            headers={"Idempotency-Key": "manual-gearmotor-0001"},
            json=_sample_gearmotor_body(),
        )
        assert create_response.status_code == 201, create_response.text
        revision = create_response.json()["revision"]
        consumer_gate = next(
            gate
            for gate in revision["capability_gates"]
            if gate["required_review_type"] == "package_consumer"
        )
        assert consumer_gate["state"] == "satisfied"
        assert consumer_gate["reason"] is None
        assert consumer_gate["satisfying_reference_ids"] == [
            "sha256:e185ee08c987b30cf20d69af06a8754224f25068e499b052a49566c137bd0155"
        ]

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
        publish_response = client.post(
            f"/api/v2/revisions/{revision['id']}/publication",
            headers={"Idempotency-Key": "manual-gearmotor-publish-0001"},
            json={
                "expected_draft_version": reviewed["draft_version"],
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
            },
        )
        assert publish_response.status_code == 201, publish_response.text
        assert publish_response.json()["execution_readiness"] == "ready"


def test_manual_draft_with_an_unconsumable_capability_stays_blocked():
    with TestClient(app) as client:
        create_response = client.post(
            "/api/v2/component-drafts",
            headers={"Idempotency-Key": "manual-unconsumable-0001"},
            json=_sample_body(),
        )
        assert create_response.status_code == 201, create_response.text
        consumer_gate = next(
            gate
            for gate in create_response.json()["revision"]["capability_gates"]
            if gate["required_review_type"] == "package_consumer"
        )
        assert consumer_gate["state"] == "blocked"
        assert consumer_gate["satisfying_reference_ids"] == []
