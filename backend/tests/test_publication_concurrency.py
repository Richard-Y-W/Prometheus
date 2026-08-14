from __future__ import annotations

from queue import Queue
import threading

import sqlalchemy as sa

from app.canonical_json import parse_strict_json
from app.contracts_v2 import (
    ClaimReviewDecisionV2,
    PublicationRequestV2,
    ReviewRequestV2,
    SCHEMA_ID,
    SCHEMA_VERSION,
)
from app.database import SessionLocal
from app.fixture_pipeline_v2 import FIXTURE_ID, create_fixture_draft
from app.models_v1 import ComponentRevision
from app.models_v2 import (
    CandidateClaimV2,
    ClaimReviewEventV2,
    ClaimSelectionV2,
    PublicationRequestV2 as PublicationRequestRecord,
    PublishedObject,
)
from app.publication_service_v2 import PublicationStage, publish_revision
from app.review_service_v2 import ReviewServiceError, review_claims


def _create_reviewed(key: str) -> tuple[str, int, list[CandidateClaimV2]]:
    with SessionLocal() as db:
        revision_id = create_fixture_draft(
            db, fixture_id=FIXTURE_ID, idempotency_key=key
        ).revision.id
    with SessionLocal() as db:
        claims = list(
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
    result = review_claims(
        revision_id=revision_id,
        request=ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="concurrency-reviewer",
            decisions=[
                ClaimReviewDecisionV2(
                    claim_id=claim.id,
                    status="accepted",
                    note="Accepted for synchronized publication testing.",
                )
                for claim in claims
            ],
        ),
        session_factory=SessionLocal,
    )
    return revision_id, result.draft_version, claims


def _request(version: int) -> PublicationRequestV2:
    return PublicationRequestV2(
        expected_draft_version=version,
        schema_id=SCHEMA_ID,
        schema_version=SCHEMA_VERSION,
    )


def _run_concurrently(operations):
    start = threading.Barrier(len(operations) + 1)
    outcomes: Queue[object] = Queue()

    def invoke(operation) -> None:
        start.wait(timeout=10)
        try:
            outcomes.put(operation())
        except BaseException as exc:
            outcomes.put(exc)

    threads = [threading.Thread(target=invoke, args=(operation,)) for operation in operations]
    for thread in threads:
        thread.start()
    start.wait(timeout=10)
    for thread in threads:
        thread.join(timeout=15)
        assert not thread.is_alive()
    return [outcomes.get_nowait() for _ in operations]


def _error_code(response) -> str:
    return parse_strict_json(response.body)["detail"]["code"]


def test_same_key_concurrent_publications_converge_on_one_response_and_object():
    revision_id, version, _claims = _create_reviewed("fixture-same-key-race-01")
    request = _request(version)

    def publish():
        return publish_revision(
            revision_id=revision_id,
            idempotency_key="publish-same-key-race-01",
            request=request,
            session_factory=SessionLocal,
        )

    result_a, result_b = _run_concurrently([publish, publish])
    assert not isinstance(result_a, BaseException), result_a
    assert not isinstance(result_b, BaseException), result_b
    assert result_a.status_code == result_b.status_code == 201
    assert result_a.body == result_b.body
    assert result_a.headers == result_b.headers
    with SessionLocal() as db:
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 1
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(PublicationRequestRecord)
            .where(PublicationRequestRecord.state == "succeeded")
        ) == 1


def test_different_keys_concurrently_produce_one_success_and_one_terminal_conflict():
    revision_id, version, _claims = _create_reviewed(
        "fixture-different-key-race-01"
    )
    request = _request(version)

    def operation(key: str):
        return lambda: publish_revision(
            revision_id=revision_id,
            idempotency_key=key,
            request=request,
            session_factory=SessionLocal,
        )

    results = _run_concurrently(
        [
            operation("publish-different-key-race-a"),
            operation("publish-different-key-race-b"),
        ]
    )
    assert all(not isinstance(result, BaseException) for result in results), results
    assert sorted(result.status_code for result in results) == [201, 409]
    conflict = next(result for result in results if result.status_code == 409)
    assert _error_code(conflict) == "revision_already_published"
    success = next(result for result in results if result.status_code == 201)
    assert (
        parse_strict_json(conflict.body)["detail"]["published_object_hash"]
        == success.object_hash
    )
    with SessionLocal() as db:
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 1
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(PublicationRequestRecord)
            .where(PublicationRequestRecord.state == "succeeded")
        ) == 1
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(PublicationRequestRecord)
            .where(PublicationRequestRecord.state == "terminal_failure")
        ) == 1


def test_review_committing_first_makes_publication_stale_without_partial_object():
    revision_id, version, claims = _create_reviewed("fixture-review-first-race-01")
    lock_reached = threading.Barrier(2)
    release_review = threading.Barrier(2)
    publication_starting = threading.Barrier(2)
    outcomes: Queue[tuple[str, object]] = Queue()

    def review_stage(stage: str) -> None:
        if stage == "revision_locked":
            lock_reached.wait(timeout=10)
            release_review.wait(timeout=10)

    def run_review() -> None:
        try:
            outcomes.put(
                (
                    "review",
                    review_claims(
                        revision_id=revision_id,
                        request=ReviewRequestV2(
                            expected_draft_version=version,
                            reviewed_by="review-first-racer",
                            decisions=[
                                ClaimReviewDecisionV2(
                                    claim_id=claims[0].id,
                                    status="accepted",
                                    note="Review wins this synchronized schedule.",
                                )
                            ],
                        ),
                        session_factory=SessionLocal,
                        stage_callback=review_stage,
                    ),
                )
            )
        except BaseException as exc:
            outcomes.put(("review", exc))

    def run_publication() -> None:
        publication_starting.wait(timeout=10)
        try:
            outcomes.put(
                (
                    "publication",
                    publish_revision(
                        revision_id=revision_id,
                        idempotency_key="publish-review-first-race-01",
                        request=_request(version),
                        session_factory=SessionLocal,
                    ),
                )
            )
        except BaseException as exc:
            outcomes.put(("publication", exc))

    review_thread = threading.Thread(target=run_review)
    review_thread.start()
    lock_reached.wait(timeout=10)
    publication_thread = threading.Thread(target=run_publication)
    publication_thread.start()
    publication_starting.wait(timeout=10)
    release_review.wait(timeout=10)
    review_thread.join(timeout=15)
    publication_thread.join(timeout=15)
    assert not review_thread.is_alive() and not publication_thread.is_alive()
    result = dict(outcomes.get_nowait() for _ in range(2))
    assert not isinstance(result["review"], BaseException), result["review"]
    assert result["review"].draft_version == version + 1
    assert not isinstance(result["publication"], BaseException), result["publication"]
    assert result["publication"].status_code == 409
    assert _error_code(result["publication"]) == "stale_draft_version"
    assert (
        parse_strict_json(result["publication"].body)["detail"][
            "current_draft_version"
        ]
        == version + 1
    )
    with SessionLocal() as db:
        revision = db.get(ComponentRevision, revision_id)
        assert revision.status == "draft"
        assert revision.draft_version == version + 1
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 0
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(ClaimReviewEventV2)
            .where(ClaimReviewEventV2.revision_id == revision_id)
        ) == len(claims) + 1


def test_publication_committing_first_makes_review_fail_without_partial_event():
    revision_id, version, claims = _create_reviewed(
        "fixture-publication-first-race-01"
    )
    publication_locked = threading.Barrier(2)
    release_publication = threading.Barrier(2)
    review_starting = threading.Barrier(2)
    outcomes: Queue[tuple[str, object]] = Queue()

    def publication_stage(stage: PublicationStage) -> None:
        if stage is PublicationStage.AFTER_IDEMPOTENCY_RESOLUTION:
            publication_locked.wait(timeout=10)
            release_publication.wait(timeout=10)

    def run_publication() -> None:
        try:
            outcomes.put(
                (
                    "publication",
                    publish_revision(
                        revision_id=revision_id,
                        idempotency_key="publish-publication-first-race-01",
                        request=_request(version),
                        session_factory=SessionLocal,
                        stage_callback=publication_stage,
                    ),
                )
            )
        except BaseException as exc:
            outcomes.put(("publication", exc))

    def run_review() -> None:
        review_starting.wait(timeout=10)
        try:
            outcomes.put(
                (
                    "review",
                    review_claims(
                        revision_id=revision_id,
                        request=ReviewRequestV2(
                            expected_draft_version=version,
                            reviewed_by="publication-first-racer",
                            decisions=[
                                ClaimReviewDecisionV2(
                                    claim_id=claims[0].id,
                                    status="accepted",
                                    note="Publication wins this synchronized schedule.",
                                )
                            ],
                        ),
                        session_factory=SessionLocal,
                    ),
                )
            )
        except BaseException as exc:
            outcomes.put(("review", exc))

    publication_thread = threading.Thread(target=run_publication)
    publication_thread.start()
    publication_locked.wait(timeout=10)
    review_thread = threading.Thread(target=run_review)
    review_thread.start()
    review_starting.wait(timeout=10)
    release_publication.wait(timeout=10)
    publication_thread.join(timeout=15)
    review_thread.join(timeout=15)
    assert not publication_thread.is_alive() and not review_thread.is_alive()
    result = dict(outcomes.get_nowait() for _ in range(2))
    assert not isinstance(result["publication"], BaseException), result["publication"]
    assert result["publication"].status_code == 201
    assert isinstance(result["review"], ReviewServiceError)
    assert result["review"].code == "revision_not_draft"
    with SessionLocal() as db:
        revision = db.get(ComponentRevision, revision_id)
        assert revision.status == "published"
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 1
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(ClaimReviewEventV2)
            .where(ClaimReviewEventV2.revision_id == revision_id)
        ) == len(claims)
