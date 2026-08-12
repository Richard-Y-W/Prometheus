"""Atomic append-only claim review for Program 01A v2 drafts."""

from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass

import sqlalchemy as sa
from sqlalchemy.orm import Session, sessionmaker

from .claim_identity_v2 import compute_claim_fingerprint
from .contracts_v2 import ReviewRequestV2, SCHEMA_ID, SCHEMA_VERSION
from .db_types import utc_now
from .models_v1 import ComponentRevision
from .models_v2 import (
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimReviewEventV2,
    ClaimSelectionV2,
)
from .transaction import (
    MAX_POSTGRES_WRITE_ATTEMPTS,
    RetryableWriteError,
    RevisionLockError,
    locked_revision_transaction,
)


class ReviewServiceError(ValueError):
    def __init__(
        self,
        code: str,
        message: str,
        *,
        current_draft_version: int | None = None,
    ):
        super().__init__(message)
        self.code = code
        self.current_draft_version = current_draft_version


@dataclass(frozen=True)
class ReviewResultV2:
    revision_id: str
    draft_version: int
    events: Sequence[ClaimReviewEventV2]


def _require_reviewable_revision(
    revision: ComponentRevision, request: ReviewRequestV2
) -> int:
    if (
        revision.contract_schema_id != SCHEMA_ID
        or revision.contract_schema_version != SCHEMA_VERSION
        or revision.publication_integrity != "v2_draft"
        or revision.status != "draft"
    ):
        raise ReviewServiceError(
            "revision_not_draft",
            "claim review requires an unpublished v2 draft revision",
            current_draft_version=revision.draft_version,
        )
    if revision.draft_version is None or revision.draft_version < 0:
        raise ReviewServiceError(
            "revision_integrity_failure",
            "v2 draft revision has no valid concurrency version",
        )
    if request.expected_draft_version != revision.draft_version:
        raise ReviewServiceError(
            "stale_draft_version",
            "review expected a different draft version",
            current_draft_version=revision.draft_version,
        )
    return revision.draft_version


def _load_and_validate_claims(
    db: Session,
    *,
    revision_id: str,
    request: ReviewRequestV2,
) -> list[CandidateClaimV2]:
    decision_ids = [str(decision.claim_id) for decision in request.decisions]
    if len(decision_ids) != len(set(decision_ids)):
        raise ReviewServiceError(
            "duplicate_claim_id", "a review batch may name each claim only once"
        )
    stored = list(
        db.scalars(
            sa.select(CandidateClaimV2).where(
                CandidateClaimV2.id.in_(decision_ids)
            )
        )
    )
    by_id = {claim.id: claim for claim in stored}
    for claim_id in decision_ids:
        claim = by_id.get(claim_id)
        if claim is None:
            raise ReviewServiceError(
                "claim_not_found", f"claim {claim_id!r} does not exist"
            )
        if claim.revision_id != revision_id:
            raise ReviewServiceError(
                "cross_revision_claim",
                f"claim {claim_id!r} does not belong to the reviewed revision",
            )
        if not claim.finalized or claim.fingerprint is None:
            raise ReviewServiceError(
                "claim_not_finalized",
                f"claim {claim_id!r} has not completed immutable construction",
            )
        try:
            recomputed = compute_claim_fingerprint(db, claim)
        except ValueError as exc:
            raise ReviewServiceError(
                "claim_integrity_failure",
                f"claim {claim_id!r} cannot be fingerprinted",
            ) from exc
        if recomputed != claim.fingerprint:
            raise ReviewServiceError(
                "claim_fingerprint_mismatch",
                f"claim {claim_id!r} does not match its stored fingerprint",
            )
    return [by_id[claim_id] for claim_id in decision_ids]


def _effective_selected_reviews(
    db: Session, revision_id: str
) -> tuple[list[str], dict[str, ClaimReviewEventV2]]:
    selected_claim_ids = list(
        db.scalars(
            sa.select(ClaimSelectionV2.claim_id)
            .where(ClaimSelectionV2.revision_id == revision_id)
            .order_by(ClaimSelectionV2.claim_id)
        )
    )
    if not selected_claim_ids:
        return [], {}
    events = list(
        db.scalars(
            sa.select(ClaimReviewEventV2)
            .where(
                ClaimReviewEventV2.revision_id == revision_id,
                ClaimReviewEventV2.claim_id.in_(selected_claim_ids),
            )
            .order_by(
                ClaimReviewEventV2.applied_draft_version,
                ClaimReviewEventV2.id,
            )
        )
    )
    effective: dict[str, ClaimReviewEventV2] = {}
    for event in events:
        effective[event.claim_id] = event
    return selected_claim_ids, effective


def _update_review_gates(db: Session, revision_id: str) -> None:
    selected_claim_ids, effective = _effective_selected_reviews(db, revision_id)
    if not selected_claim_ids:
        state = "pending"
        references: list[str] = []
        reason = "No selected claims are available for review."
    elif any(claim_id not in effective for claim_id in selected_claim_ids):
        state = "pending"
        references = []
        reason = "Every selected claim requires an effective review."
    elif any(
        effective[claim_id].decision != "accepted"
        for claim_id in selected_claim_ids
    ):
        state = "blocked"
        references = []
        reason = "A selected claim has an effective rejected or ambiguous review."
    else:
        state = "satisfied"
        references = sorted(
            effective[claim_id].id for claim_id in selected_claim_ids
        )
        reason = None

    gates = list(
        db.scalars(
            sa.select(CapabilityGateV2).where(
                CapabilityGateV2.revision_id == revision_id,
                CapabilityGateV2.phase == "publication",
                CapabilityGateV2.required_review_type == "claim_review",
            )
        )
    )
    if not gates:
        raise ReviewServiceError(
            "claim_review_gate_missing",
            "the v2 draft has no declared publication claim-review gate",
        )
    for gate in gates:
        gate.state = state
        gate.satisfying_references = list(references)
        gate.reason = reason


def _review_once(
    *,
    revision_id: str,
    request: ReviewRequestV2,
    session_factory: sessionmaker,
    stage_callback: Callable[[str], None] | None,
) -> ReviewResultV2:
    with locked_revision_transaction(
        session_factory,
        revision_id,
        operation="review",
        stage_callback=stage_callback,
    ) as (db, revision):
        current_version = _require_reviewable_revision(revision, request)
        claims = _load_and_validate_claims(
            db, revision_id=revision_id, request=request
        )
        if stage_callback is not None:
            stage_callback("claims_validated")

        reviewed_at = utc_now()
        new_version = current_version + 1
        events = [
            ClaimReviewEventV2(
                revision_id=revision_id,
                claim_id=claim.id,
                claim_fingerprint=claim.fingerprint,
                decision=decision.status,
                reviewed_by=request.reviewed_by,
                note=decision.note,
                reviewed_at=reviewed_at,
                applied_draft_version=new_version,
            )
            for decision, claim in zip(request.decisions, claims, strict=True)
        ]
        db.add_all(events)
        db.flush()
        if stage_callback is not None:
            stage_callback("events_appended")

        _update_review_gates(db, revision_id)
        revision.draft_version = new_version
        db.flush()
        if stage_callback is not None:
            stage_callback("gates_updated")
            stage_callback("before_commit")
        result = ReviewResultV2(
            revision_id=revision_id,
            draft_version=new_version,
            events=tuple(events),
        )
    return result


def review_claims(
    *,
    revision_id: str,
    request: ReviewRequestV2,
    session_factory: sessionmaker,
    stage_callback: Callable[[str], None] | None = None,
) -> ReviewResultV2:
    """Append a complete review batch under one revision transaction."""

    bind = session_factory.kw.get("bind")
    dialect = bind.dialect.name if bind is not None else None
    maximum_attempts = (
        MAX_POSTGRES_WRITE_ATTEMPTS if dialect == "postgresql" else 1
    )
    last_retryable: RetryableWriteError | None = None
    for _attempt in range(maximum_attempts):
        try:
            return _review_once(
                revision_id=revision_id,
                request=request,
                session_factory=session_factory,
                stage_callback=stage_callback,
            )
        except RevisionLockError as exc:
            raise ReviewServiceError(exc.code, str(exc)) from exc
        except RetryableWriteError as exc:
            last_retryable = exc
    assert last_retryable is not None
    raise last_retryable


__all__ = [
    "ReviewResultV2",
    "ReviewServiceError",
    "review_claims",
]
