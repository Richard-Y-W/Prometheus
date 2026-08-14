from __future__ import annotations

from datetime import datetime, timezone
import threading
from uuid import uuid4

from pydantic import ValidationError
import pytest
import sqlalchemy as sa

from app.claim_identity_v2 import compute_claim_fingerprint
from app.contracts_v2 import (
    ClaimReviewDecisionV2,
    ReviewRequestV2,
    SCHEMA_ID,
    SCHEMA_VERSION,
)
from app.database import SessionLocal
from app.fixture_pipeline_v2 import FIXTURE_ID, create_fixture_draft
from app.models_v1 import Component, ComponentRevision, Manufacturer
from app.models_v2 import (
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimReviewEventV2,
    ClaimSelectionV2,
    ParameterSlotV2,
    PublishedObject,
)
from app.review_service_v2 import ReviewServiceError, review_claims


HASH_A = "sha256:" + "a" * 64
HASH_B = "sha256:" + "b" * 64


class StageBarrier:
    def __init__(self, parties: int):
        self._barrier = threading.Barrier(parties)

    def __call__(self, stage: str) -> None:
        if stage == "revision_locked":
            self._barrier.wait(timeout=10)


def _create_draft(key: str = "fixture-review-0001"):
    with SessionLocal() as db:
        result = create_fixture_draft(
            db,
            fixture_id=FIXTURE_ID,
            idempotency_key=key,
        )
        return result.revision.id


def _selected_claims(revision_id: str) -> list[CandidateClaimV2]:
    with SessionLocal() as db:
        return list(
            db.scalars(
                sa.select(CandidateClaimV2)
                .join(
                    ClaimSelectionV2,
                    sa.and_(
                        ClaimSelectionV2.revision_id
                        == CandidateClaimV2.revision_id,
                        ClaimSelectionV2.claim_id == CandidateClaimV2.id,
                    ),
                )
                .where(CandidateClaimV2.revision_id == revision_id)
                .order_by(CandidateClaimV2.id)
            )
        )


def _request(
    claims: list[CandidateClaimV2],
    *,
    version: int = 0,
    statuses: dict[str, str] | None = None,
    reviewer: str = "fixture-reviewer",
) -> ReviewRequestV2:
    statuses = statuses or {}
    return ReviewRequestV2(
        expected_draft_version=version,
        reviewed_by=reviewer,
        decisions=[
            ClaimReviewDecisionV2(
                claim_id=claim.id,
                status=statuses.get(claim.id, "accepted"),
                note="Accepted as synthetic conformance input only.",
            )
            for claim in claims
        ],
    )


def _review_gate(db, revision_id: str) -> CapabilityGateV2:
    return db.scalar(
        sa.select(CapabilityGateV2).where(
            CapabilityGateV2.revision_id == revision_id,
            CapabilityGateV2.phase == "publication",
            CapabilityGateV2.required_review_type == "claim_review",
        )
    )


def _add_unknown_candidate(
    db, revision_id: str, slot_id: str, *, fingerprint: str | None = None
) -> CandidateClaimV2:
    claim = CandidateClaimV2(
        revision_id=revision_id,
        slot_id=slot_id,
        value_state="unknown",
        value=None,
        unit=None,
        original_value=None,
        original_unit=None,
        reason="alternative claim intentionally has no numerical value",
        validity_conditions=["review service test"],
        provenance="review_service_test",
        finalized=False,
        fingerprint=None,
    )
    db.add(claim)
    db.flush()
    claim.fingerprint = fingerprint or compute_claim_fingerprint(db, claim)
    claim.finalized = True
    db.flush()
    return claim


def _add_second_revision_claim(db) -> tuple[ComponentRevision, CandidateClaimV2]:
    manufacturer = Manufacturer(
        name="Independent Fixture Works",
        normalized_name="independentfixtureworks",
    )
    db.add(manufacturer)
    db.flush()
    component = Component(
        manufacturer_id=manufacturer.id,
        part_number="SECOND-1",
        normalized_part_number="second1",
        family="test",
        model_class="test_component",
    )
    db.add(component)
    db.flush()
    revision = ComponentRevision(
        component_id=component.id,
        revision="r2",
        certification_tier="provisional",
        status="draft",
        published_at=None,
        content_hash=None,
        draft_version=0,
        contract_schema_id=SCHEMA_ID,
        contract_schema_version=SCHEMA_VERSION,
        publication_integrity="v2_draft",
        published_object_hash=None,
        supported_recipes=[],
        missing_information=[],
        limitations=[],
    )
    db.add(revision)
    db.flush()
    slot = ParameterSlotV2(
        revision_id=revision.id,
        name="second_parameter",
        quantity="ratio",
        dimension="dimensionless",
        required_for_execution=False,
    )
    db.add(slot)
    db.flush()
    claim = _add_unknown_candidate(db, revision.id, slot.id)
    return revision, claim


def _assert_unchanged(revision_id: str, *, version: int, event_count: int) -> None:
    with SessionLocal() as db:
        revision = db.get(ComponentRevision, revision_id)
        assert revision.draft_version == version
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(ClaimReviewEventV2)
            .where(ClaimReviewEventV2.revision_id == revision_id)
        ) == event_count


def test_review_selected_claims_appends_one_versioned_batch_and_satisfies_gate():
    revision_id = _create_draft()
    selected = _selected_claims(revision_id)
    stages: list[str] = []

    result = review_claims(
        revision_id=revision_id,
        request=_request(selected),
        session_factory=SessionLocal,
        stage_callback=stages.append,
    )

    assert result.revision_id == revision_id
    assert result.draft_version == 1
    assert len(result.events) == len(selected) == 17
    assert {event.applied_draft_version for event in result.events} == {1}
    assert {event.reviewed_at for event in result.events}.__len__() == 1
    assert stages == [
        "revision_locked",
        "claims_validated",
        "events_appended",
        "gates_updated",
        "before_commit",
    ]
    with SessionLocal() as db:
        revision = db.get(ComponentRevision, revision_id)
        gate = _review_gate(db, revision_id)
        assert revision.draft_version == 1
        assert gate.state == "satisfied"
        assert gate.reason is None
        assert gate.satisfying_references == sorted(
            event.id for event in result.events
        )


def test_multiple_candidates_are_valid_and_unselected_review_does_not_satisfy_gate():
    revision_id = _create_draft()
    with SessionLocal.begin() as db:
        selected = db.scalar(
            sa.select(ClaimSelectionV2).where(
                ClaimSelectionV2.revision_id == revision_id
            )
        )
        alternative = _add_unknown_candidate(db, revision_id, selected.slot_id)
        alternative_id = alternative.id

    result = review_claims(
        revision_id=revision_id,
        request=_request([alternative]),
        session_factory=SessionLocal,
    )

    assert len(result.events) == 1
    assert result.events[0].claim_id == alternative_id
    with SessionLocal() as db:
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(CandidateClaimV2)
            .where(CandidateClaimV2.slot_id == selected.slot_id)
        ) == 2
        assert db.scalar(
            sa.select(ClaimSelectionV2.claim_id).where(
                ClaimSelectionV2.slot_id == selected.slot_id
            )
        ) != alternative_id
        gate = _review_gate(db, revision_id)
        assert gate.state == "pending"
        assert gate.satisfying_references == []


def test_cross_revision_claim_is_distinct_from_nonexistent_claim():
    revision_id = _create_draft()
    with SessionLocal.begin() as db:
        _other_revision, other_claim = _add_second_revision_claim(db)
        other_claim_id = other_claim.id

    cross_revision_request = ReviewRequestV2(
        expected_draft_version=0,
        reviewed_by="fixture-reviewer",
        decisions=[
            ClaimReviewDecisionV2(
                claim_id=other_claim_id,
                status="accepted",
                note="This claim belongs to another revision.",
            )
        ],
    )
    with pytest.raises(ReviewServiceError) as cross_revision:
        review_claims(
            revision_id=revision_id,
            request=cross_revision_request,
            session_factory=SessionLocal,
        )
    assert cross_revision.value.code == "cross_revision_claim"

    nonexistent_request = ReviewRequestV2(
        expected_draft_version=0,
        reviewed_by="fixture-reviewer",
        decisions=[
            ClaimReviewDecisionV2(
                claim_id=uuid4(),
                status="accepted",
                note="This claim does not exist.",
            )
        ],
    )
    with pytest.raises(ReviewServiceError) as nonexistent:
        review_claims(
            revision_id=revision_id,
            request=nonexistent_request,
            session_factory=SessionLocal,
        )
    assert nonexistent.value.code == "claim_not_found"
    _assert_unchanged(revision_id, version=0, event_count=0)


def test_stale_review_fails_without_appending_a_second_batch():
    revision_id = _create_draft()
    claim = _selected_claims(revision_id)[0]
    review_claims(
        revision_id=revision_id,
        request=_request([claim]),
        session_factory=SessionLocal,
    )

    with pytest.raises(ReviewServiceError) as captured:
        review_claims(
            revision_id=revision_id,
            request=_request([claim], version=0),
            session_factory=SessionLocal,
        )
    assert captured.value.code == "stale_draft_version"
    assert captured.value.current_draft_version == 1
    _assert_unchanged(revision_id, version=1, event_count=1)


def test_published_revision_cannot_be_reviewed():
    revision_id = _create_draft()
    claim = _selected_claims(revision_id)[0]
    with SessionLocal.begin() as db:
        db.add(
            PublishedObject(
                object_hash=HASH_A,
                payload_bytes=b"{}",
                byte_length=2,
                media_type="application/json",
                schema_id=SCHEMA_ID,
                schema_version=SCHEMA_VERSION,
                canonicalization="RFC8785",
            )
        )
        revision = db.get(ComponentRevision, revision_id)
        revision.status = "published"
        revision.publication_integrity = "sealed_v2"
        revision.content_hash = HASH_A
        revision.published_object_hash = HASH_A
        revision.published_at = datetime.now(timezone.utc)

    with pytest.raises(ReviewServiceError) as captured:
        review_claims(
            revision_id=revision_id,
            request=_request([claim]),
            session_factory=SessionLocal,
        )
    assert captured.value.code == "revision_not_draft"
    _assert_unchanged(revision_id, version=0, event_count=0)


@pytest.mark.parametrize("reviewed_by", ["", " \t\n"])
def test_empty_or_whitespace_reviewer_is_rejected(reviewed_by: str):
    with pytest.raises(ValidationError):
        ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by=reviewed_by,
            decisions=[
                ClaimReviewDecisionV2(
                    claim_id=uuid4(), status="accepted", note="valid note"
                )
            ],
        )


@pytest.mark.parametrize("status", ["accepted", "rejected", "ambiguous"])
@pytest.mark.parametrize("note", ["", " \t\n"])
def test_every_review_status_requires_a_nonblank_note(status: str, note: str):
    with pytest.raises(ValidationError):
        ClaimReviewDecisionV2(claim_id=uuid4(), status=status, note=note)


def test_reviewer_and_note_limits_are_utf8_byte_limits():
    with pytest.raises(ValidationError):
        ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="é" * 129,
            decisions=[
                ClaimReviewDecisionV2(
                    claim_id=uuid4(), status="accepted", note="valid note"
                )
            ],
        )
    with pytest.raises(ValidationError):
        ClaimReviewDecisionV2(
            claim_id=uuid4(), status="accepted", note="é" * 2049
        )


def test_duplicate_ids_and_1001_decisions_are_rejected_by_request_contract():
    claim_id = uuid4()
    duplicate = ClaimReviewDecisionV2(
        claim_id=claim_id, status="accepted", note="duplicate"
    )
    with pytest.raises(ValidationError):
        ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="fixture-reviewer",
            decisions=[duplicate, duplicate],
        )
    with pytest.raises(ValidationError):
        ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="fixture-reviewer",
            decisions=[
                ClaimReviewDecisionV2(
                    claim_id=uuid4(), status="accepted", note="bounded batch"
                )
                for _ in range(1001)
            ],
        )


def test_partial_invalid_batch_writes_no_event_or_version_change():
    revision_id = _create_draft()
    selected = _selected_claims(revision_id)
    request = ReviewRequestV2(
        expected_draft_version=0,
        reviewed_by="fixture-reviewer",
        decisions=[
            ClaimReviewDecisionV2(
                claim_id=selected[0].id,
                status="accepted",
                note="This decision is valid but the batch is not.",
            ),
            ClaimReviewDecisionV2(
                claim_id=uuid4(),
                status="accepted",
                note="This decision names no stored claim.",
            ),
        ],
    )

    with pytest.raises(ReviewServiceError) as captured:
        review_claims(
            revision_id=revision_id,
            request=request,
            session_factory=SessionLocal,
        )
    assert captured.value.code == "claim_not_found"
    _assert_unchanged(revision_id, version=0, event_count=0)


def test_fingerprint_mismatch_fails_before_any_review_event():
    revision_id = _create_draft()
    with SessionLocal.begin() as db:
        slot = db.scalar(
            sa.select(ParameterSlotV2).where(
                ParameterSlotV2.revision_id == revision_id
            )
        )
        invalid_claim = _add_unknown_candidate(
            db, revision_id, slot.id, fingerprint=HASH_B
        )
        invalid_claim_id = invalid_claim.id

    with pytest.raises(ReviewServiceError) as captured:
        review_claims(
            revision_id=revision_id,
            request=ReviewRequestV2(
                expected_draft_version=0,
                reviewed_by="fixture-reviewer",
                decisions=[
                    ClaimReviewDecisionV2(
                        claim_id=invalid_claim_id,
                        status="accepted",
                        note="Stored fingerprint is deliberately invalid.",
                    )
                ],
            ),
            session_factory=SessionLocal,
        )
    assert captured.value.code == "claim_fingerprint_mismatch"
    _assert_unchanged(revision_id, version=0, event_count=0)


def test_rejected_and_ambiguous_reviews_can_be_superseded_without_history_loss():
    revision_id = _create_draft()
    selected = _selected_claims(revision_id)
    statuses = {selected[0].id: "rejected", selected[1].id: "ambiguous"}
    first = review_claims(
        revision_id=revision_id,
        request=_request(selected, statuses=statuses),
        session_factory=SessionLocal,
    )
    assert first.draft_version == 1
    with SessionLocal() as db:
        gate = _review_gate(db, revision_id)
        assert gate.state == "blocked"
        assert gate.satisfying_references == []

    second = review_claims(
        revision_id=revision_id,
        request=_request(selected[:2], version=1),
        session_factory=SessionLocal,
    )
    assert second.draft_version == 2
    with SessionLocal() as db:
        events = list(
            db.scalars(
                sa.select(ClaimReviewEventV2)
                .where(ClaimReviewEventV2.revision_id == revision_id)
                .order_by(
                    ClaimReviewEventV2.applied_draft_version,
                    ClaimReviewEventV2.id,
                )
            )
        )
        assert len(events) == len(selected) + 2
        assert {event.applied_draft_version for event in events} == {1, 2}
        assert any(event.decision == "rejected" for event in events)
        assert any(event.decision == "ambiguous" for event in events)
        gate = _review_gate(db, revision_id)
        assert gate.state == "satisfied"
        assert len(gate.satisfying_references) == len(selected)


def test_database_rejects_two_events_for_one_claim_at_one_draft_version():
    revision_id = _create_draft()
    claim = _selected_claims(revision_id)[0]
    result = review_claims(
        revision_id=revision_id,
        request=_request([claim]),
        session_factory=SessionLocal,
    )
    event = result.events[0]
    with SessionLocal.begin() as db:
        db.add(
            ClaimReviewEventV2(
                revision_id=revision_id,
                claim_id=claim.id,
                claim_fingerprint=claim.fingerprint,
                decision="accepted",
                reviewed_by="second-reviewer",
                note="Duplicate version should fail.",
                reviewed_at=datetime.now(timezone.utc),
                applied_draft_version=event.applied_draft_version,
            )
        )
        with pytest.raises(sa.exc.IntegrityError):
            db.flush()


def test_injected_failure_after_event_append_rolls_back_entire_batch():
    revision_id = _create_draft()
    selected = _selected_claims(revision_id)

    def fail_after_events(stage: str) -> None:
        if stage == "events_appended":
            raise RuntimeError("injected review failure")

    with pytest.raises(RuntimeError, match="injected review failure"):
        review_claims(
            revision_id=revision_id,
            request=_request(selected),
            session_factory=SessionLocal,
            stage_callback=fail_after_events,
        )
    _assert_unchanged(revision_id, version=0, event_count=0)
    with SessionLocal() as db:
        gate = _review_gate(db, revision_id)
        assert gate.state == "pending"
        assert gate.satisfying_references == []


def test_stage_barrier_scaffold_releases_at_revision_lock():
    StageBarrier(1)("revision_locked")
