from __future__ import annotations

import pytest
import sqlalchemy as sa

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
    ClaimSelectionV2,
    FixtureIngestionJobV2,
    PublicationRequestV2 as PublicationRequestRecord,
    PublishedObject,
)
from app.publication_service_v2 import PublicationStage, publish_revision
from app.review_service_v2 import review_claims


PUBLICATION_STAGES = [
    "after_idempotency_resolution",
    "after_draft_validation",
    "after_package_compilation",
    "after_schema_validation",
    "after_canonicalization",
    "after_hash_computation",
    "after_byte_verification",
    "after_object_insertion",
    "after_revision_binding",
    "after_response_storage",
    "before_commit",
]


class InjectedInfrastructureFailure(RuntimeError):
    pass


def _create_reviewed(stage: str) -> tuple[str, int, str]:
    with SessionLocal() as db:
        result = create_fixture_draft(
            db,
            fixture_id=FIXTURE_ID,
            idempotency_key=f"fixture-failure-{stage}",
        )
        revision_id = result.revision.id
        job_id = result.ingestion_job.id
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
    reviewed = review_claims(
        revision_id=revision_id,
        request=ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="failure-injection-reviewer",
            decisions=[
                ClaimReviewDecisionV2(
                    claim_id=claim.id,
                    status="accepted",
                    note="Accepted solely for publication rollback testing.",
                )
                for claim in claims
            ],
        ),
        session_factory=SessionLocal,
    )
    return revision_id, reviewed.draft_version, job_id


@pytest.mark.parametrize("stage", PUBLICATION_STAGES)
def test_infrastructure_failure_at_every_stage_rolls_back_then_retries(stage: str):
    revision_id, version, job_id = _create_reviewed(stage)
    key = f"publish-failure-{stage}"
    request = PublicationRequestV2(
        expected_draft_version=version,
        schema_id=SCHEMA_ID,
        schema_version=SCHEMA_VERSION,
    )

    def inject(current: PublicationStage) -> None:
        if current.value == stage:
            raise InjectedInfrastructureFailure(stage)

    with pytest.raises(InjectedInfrastructureFailure, match=stage):
        publish_revision(
            revision_id=revision_id,
            idempotency_key=key,
            request=request,
            session_factory=SessionLocal,
            stage_callback=inject,
        )

    with SessionLocal() as db:
        revision = db.get(ComponentRevision, revision_id)
        job = db.get(FixtureIngestionJobV2, job_id)
        assert revision.status == "draft"
        assert revision.publication_integrity == "v2_draft"
        assert revision.content_hash is None
        assert revision.published_object_hash is None
        assert revision.published_at is None
        assert revision.draft_version == version
        assert job.status == "succeeded"
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 0
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(PublicationRequestRecord)
            .where(PublicationRequestRecord.idempotency_key == key)
        ) == 0

    retried = publish_revision(
        revision_id=revision_id,
        idempotency_key=key,
        request=request,
        session_factory=SessionLocal,
    )
    assert retried.status_code == 201
    with SessionLocal() as db:
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 1
        assert db.scalar(
            sa.select(sa.func.count())
            .select_from(PublicationRequestRecord)
            .where(
                PublicationRequestRecord.idempotency_key == key,
                PublicationRequestRecord.state == "succeeded",
            )
        ) == 1
