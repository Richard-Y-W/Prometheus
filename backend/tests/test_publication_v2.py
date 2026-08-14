from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from uuid import uuid4

import pytest
import sqlalchemy as sa

from app import publication_service_v2
from app.canonical_json import canonicalize_value, parse_strict_json
from app.contracts_v2 import (
    PACKAGE_MEDIA_TYPE,
    ClaimReviewDecisionV2,
    PublicationRequestV2,
    ReviewRequestV2,
    SCHEMA_ID,
    SCHEMA_VERSION,
)
from app.database import SessionLocal, engine
from app.db_types import utc_now
from app.fixture_pipeline_v2 import CAPABILITY_ID, FIXTURE_ID, create_fixture_draft
from app.models_v1 import ComponentRevision
from app.models_v2 import (
    CandidateClaimV2,
    CapabilityGateV2,
    ClaimSelectionV2,
    FixtureIngestionJobV2,
    PublicationRequestV2 as PublicationRequestRecord,
    PublishedObject,
)
from app.package_compiler_v2 import compile_execution_component
from app.publication_service_v2 import (
    PublicationInputError,
    publish_revision,
)
from app.review_service_v2 import review_claims


BACKEND_ROOT = Path(__file__).parents[1]
PROCESS_PROBE = BACKEND_ROOT / "tests/helpers/publication_process_probe.py"


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


def _create_draft(key: str, *, reviewed: bool) -> tuple[str, int]:
    with SessionLocal() as db:
        revision = create_fixture_draft(
            db, fixture_id=FIXTURE_ID, idempotency_key=key
        ).revision
        revision_id = revision.id
    if not reviewed:
        return revision_id, 0
    claims = _selected_claims(revision_id)
    result = review_claims(
        revision_id=revision_id,
        request=ReviewRequestV2(
            expected_draft_version=0,
            reviewed_by="publication-reviewer",
            decisions=[
                ClaimReviewDecisionV2(
                    claim_id=claim.id,
                    status="accepted",
                    note="Accepted for the synthetic publication test only.",
                )
                for claim in claims
            ],
        ),
        session_factory=SessionLocal,
    )
    return revision_id, result.draft_version


def _request(version: int) -> PublicationRequestV2:
    return PublicationRequestV2(
        expected_draft_version=version,
        schema_id=SCHEMA_ID,
        schema_version=SCHEMA_VERSION,
    )


def _error_code(response) -> str:
    value = parse_strict_json(response.body)
    return value["detail"]["code"]


def _error_detail(response) -> dict[str, object]:
    value = parse_strict_json(response.body)
    return value["detail"]


def _assert_exact_replay(first, second) -> None:
    assert second.status_code == first.status_code
    assert second.body == first.body
    assert second.headers == first.headers
    assert second.object_hash == first.object_hash


def _terminal_record(key: str) -> PublicationRequestRecord:
    with SessionLocal() as db:
        return db.scalar(
            sa.select(PublicationRequestRecord).where(
                PublicationRequestRecord.operation == "publication",
                PublicationRequestRecord.idempotency_key == key,
            )
        )


def test_success_seals_one_object_and_replays_exact_stored_response():
    revision_id, version = _create_draft(
        "fixture-publication-success-01", reviewed=True
    )
    request = _request(version)
    first = publish_revision(
        revision_id=revision_id,
        idempotency_key="publish-fixture-0001",
        request=request,
        session_factory=SessionLocal,
    )
    replayed = publish_revision(
        revision_id=revision_id,
        idempotency_key="publish-fixture-0001",
        request=request,
        session_factory=SessionLocal,
    )

    assert first.status_code == 201
    assert first.headers == {
        "Content-Type": "application/json",
        "ETag": f'"{first.object_hash}"',
        "Location": f"/api/v2/revisions/{revision_id}/execution-package",
    }
    assert first.body == canonicalize_value(json.loads(first.body))
    body = parse_strict_json(first.body)
    assert body == {
        "execution_readiness": "blocked",
        "media_type": PACKAGE_MEDIA_TYPE,
        "object_hash": first.object_hash,
        "publication_integrity": "sealed_v2",
        "revision_id": revision_id,
        "schema_id": SCHEMA_ID,
        "schema_version": SCHEMA_VERSION,
        "status": "published",
    }
    _assert_exact_replay(first, replayed)

    with SessionLocal() as db:
        revision = db.get(ComponentRevision, revision_id)
        record = _terminal_record("publish-fixture-0001")
        assert revision.status == "published"
        assert revision.publication_integrity == "sealed_v2"
        assert revision.content_hash == first.object_hash
        assert revision.published_object_hash == first.object_hash
        assert revision.published_at is not None
        assert revision.published_at.utcoffset().total_seconds() == 0
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublishedObject)
        ) == 1
        assert record.state == "succeeded"
        assert record.response_body == first.body
        assert record.response_headers == first.headers
        assert record.published_object_hash == first.object_hash
        package = db.get(PublishedObject, first.object_hash)
        package_value = parse_strict_json(package.payload_bytes)
        assert package_value["execution_readiness"] == "blocked"


def test_corruption_discovered_during_success_replay_fails_closed_without_overwrite():
    revision_id, version = _create_draft(
        "fixture-corrupt-success-replay-01", reviewed=True
    )
    key = "publish-corrupt-success-replay-01"
    request = _request(version)
    request_value = {
        "operation": "publication",
        "revision_id": revision_id,
        "expected_draft_version": version,
        "schema_id": SCHEMA_ID,
        "schema_version": SCHEMA_VERSION,
    }
    request_bytes = canonicalize_value(request_value)
    fingerprint = f"sha256:{hashlib.sha256(request_bytes).hexdigest()}"

    with SessionLocal.begin() as db:
        compiled = compile_execution_component(db, revision_id)
        corrupt_bytes = canonicalize_value({"corrupt": "canonical payload"})
        db.add(
            PublishedObject(
                object_hash=compiled.object_hash,
                payload_bytes=corrupt_bytes,
                byte_length=len(corrupt_bytes),
                media_type=PACKAGE_MEDIA_TYPE,
                schema_id=SCHEMA_ID,
                schema_version=SCHEMA_VERSION,
                canonicalization="RFC8785",
            )
        )
        db.flush()
        revision = db.get(ComponentRevision, revision_id)
        revision.status = "published"
        revision.publication_integrity = "sealed_v2"
        revision.content_hash = compiled.object_hash
        revision.published_object_hash = compiled.object_hash
        revision.published_at = utc_now()
        db.flush()
        original_body = canonicalize_value(
            {
                "execution_readiness": compiled.execution_readiness,
                "media_type": PACKAGE_MEDIA_TYPE,
                "object_hash": compiled.object_hash,
                "publication_integrity": "sealed_v2",
                "revision_id": revision_id,
                "schema_id": SCHEMA_ID,
                "schema_version": SCHEMA_VERSION,
                "status": "published",
            }
        )
        original_headers = {
            "Content-Type": "application/json",
            "ETag": f'"{compiled.object_hash}"',
            "Location": f"/api/v2/revisions/{revision_id}/execution-package",
        }
        db.add(
            PublicationRequestRecord(
                operation="publication",
                idempotency_key=key,
                revision_id=revision_id,
                request_fingerprint=fingerprint,
                state="succeeded",
                response_status=201,
                response_body=original_body,
                response_headers=original_headers,
                published_object_hash=compiled.object_hash,
            )
        )

    response = publish_revision(
        revision_id=revision_id,
        idempotency_key=key,
        request=request,
        session_factory=SessionLocal,
    )

    assert response.status_code == 409
    assert _error_code(response) == "published_object_integrity_error"
    record = _terminal_record(key)
    assert record.state == "succeeded"
    assert record.response_status == 201
    assert record.response_body == original_body
    assert record.response_headers == original_headers


@pytest.mark.parametrize(
    ("key", "setup", "expected_code"),
    [
        (
            "publish-stale-version-01",
            "stale",
            "stale_draft_version",
        ),
        (
            "publish-review-incomplete-01",
            "review_incomplete",
            "publication_review_incomplete",
        ),
        (
            "publish-gate-blocked-01",
            "gate_blocked",
            "publication_gate_blocked",
        ),
        (
            "publish-unsupported-schema-01",
            "unsupported_schema",
            "unsupported_schema",
        ),
        (
            "publish-invalid-package-01",
            "invalid_package",
            "execution_package_invalid",
        ),
        (
            "publish-object-integrity-01",
            "object_integrity",
            "published_object_integrity_error",
        ),
    ],
)
def test_deterministic_failures_are_stored_and_replayed(
    key: str, setup: str, expected_code: str
):
    reviewed = setup not in {"review_incomplete"}
    revision_id, version = _create_draft(
        f"fixture-{key}", reviewed=reviewed
    )
    request = _request(version)
    if setup == "stale":
        request = _request(version - 1)
    elif setup == "gate_blocked":
        with SessionLocal.begin() as db:
            gate = db.scalar(
                sa.select(CapabilityGateV2).where(
                    CapabilityGateV2.revision_id == revision_id,
                    CapabilityGateV2.capability_id == CAPABILITY_ID,
                    CapabilityGateV2.required_review_type == "source_artifact",
                )
            )
            gate.state = "blocked"
            gate.satisfying_references = []
            gate.reason = "Source-artifact gate deliberately blocked."
    elif setup == "unsupported_schema":
        request = PublicationRequestV2.model_construct(
            expected_draft_version=version,
            schema_id="urn:prometheus:schema:execution-component:9.0.0",
            schema_version="9.0.0",
        )
    elif setup == "invalid_package":
        with SessionLocal.begin() as db:
            revision = db.get(ComponentRevision, revision_id)
            revision.limitations = [{"not_a_limitation": True}]
    elif setup == "object_integrity":
        with SessionLocal.begin() as db:
            compiled = compile_execution_component(db, revision_id)
            corrupt_bytes = canonicalize_value({"corrupt": "collision"})
            db.add(
                PublishedObject(
                    object_hash=compiled.object_hash,
                    payload_bytes=corrupt_bytes,
                    byte_length=len(corrupt_bytes),
                    media_type=PACKAGE_MEDIA_TYPE,
                    schema_id=SCHEMA_ID,
                    schema_version=SCHEMA_VERSION,
                    canonicalization="RFC8785",
                )
            )

    first = publish_revision(
        revision_id=revision_id,
        idempotency_key=key,
        request=request,
        session_factory=SessionLocal,
    )
    replayed = publish_revision(
        revision_id=revision_id,
        idempotency_key=key,
        request=request,
        session_factory=SessionLocal,
    )

    assert first.status_code == 409
    assert _error_code(first) == expected_code
    if setup == "stale":
        assert _error_detail(first)["current_draft_version"] == version
    _assert_exact_replay(first, replayed)
    record = _terminal_record(key)
    assert record.state == "terminal_failure"
    assert record.response_status == 409
    assert record.response_body == first.body
    assert record.response_headers == first.headers
    assert record.published_object_hash is None
    with SessionLocal() as db:
        revision = db.get(ComponentRevision, revision_id)
        assert revision.status == "draft"
        assert revision.publication_integrity == "v2_draft"
        assert revision.published_object_hash is None


def test_different_key_after_success_stores_revision_already_published():
    revision_id, version = _create_draft(
        "fixture-already-published-01", reviewed=True
    )
    request = _request(version)
    success = publish_revision(
        revision_id=revision_id,
        idempotency_key="publish-original-success-01",
        request=request,
        session_factory=SessionLocal,
    )
    first = publish_revision(
        revision_id=revision_id,
        idempotency_key="publish-already-terminal-01",
        request=request,
        session_factory=SessionLocal,
    )
    replayed = publish_revision(
        revision_id=revision_id,
        idempotency_key="publish-already-terminal-01",
        request=request,
        session_factory=SessionLocal,
    )
    assert success.status_code == 201
    assert first.status_code == 409
    assert _error_code(first) == "revision_already_published"
    assert _error_detail(first)["published_object_hash"] == success.object_hash
    _assert_exact_replay(first, replayed)
    assert _terminal_record("publish-already-terminal-01").state == "terminal_failure"


@pytest.mark.parametrize("change", ["revision", "version", "schema"])
def test_reusing_key_with_different_fingerprint_is_conflict_without_overwrite(
    change: str,
):
    revision_id, version = _create_draft(
        f"fixture-idempotency-conflict-{change}", reviewed=True
    )
    key = f"publish-idempotency-conflict-{change}"
    request = _request(version)
    success = publish_revision(
        revision_id=revision_id,
        idempotency_key=key,
        request=request,
        session_factory=SessionLocal,
    )
    conflicting_revision = revision_id
    conflicting_request = request
    if change == "revision":
        conflicting_revision = str(uuid4())
    elif change == "version":
        conflicting_request = _request(version + 1)
    else:
        conflicting_request = PublicationRequestV2.model_construct(
            expected_draft_version=version,
            schema_id="urn:prometheus:schema:execution-component:9.0.0",
            schema_version="9.0.0",
        )

    first = publish_revision(
        revision_id=conflicting_revision,
        idempotency_key=key,
        request=conflicting_request,
        session_factory=SessionLocal,
    )
    second = publish_revision(
        revision_id=conflicting_revision,
        idempotency_key=key,
        request=conflicting_request,
        session_factory=SessionLocal,
    )

    assert first.status_code == 409
    assert _error_code(first) == "idempotency_conflict"
    _assert_exact_replay(first, second)
    record = _terminal_record(key)
    assert record.state == "succeeded"
    assert record.response_body == success.body
    assert record.response_headers == success.headers


def test_invalid_idempotency_key_never_creates_a_request_record():
    revision_id, version = _create_draft("fixture-invalid-publish-key-01", reviewed=True)
    with pytest.raises(PublicationInputError) as captured:
        publish_revision(
            revision_id=revision_id,
            idempotency_key="short",
            request=_request(version),
            session_factory=SessionLocal,
        )
    assert captured.value.code == "invalid_idempotency_key"
    with SessionLocal() as db:
        assert db.scalar(
            sa.select(sa.func.count()).select_from(PublicationRequestRecord)
        ) == 0


def test_lost_response_after_commit_replays_without_recompilation(monkeypatch):
    revision_id, version = _create_draft("fixture-lost-response-01", reviewed=True)
    key = "publish-lost-response-0001"
    request = _request(version)
    lost = False

    def lose_first_response(_response) -> None:
        nonlocal lost
        if not lost:
            lost = True
            raise ConnectionError("simulated response transport loss")

    monkeypatch.setattr(publication_service_v2, "_after_commit", lose_first_response)
    with pytest.raises(ConnectionError, match="transport loss"):
        publish_revision(
            revision_id=revision_id,
            idempotency_key=key,
            request=request,
            session_factory=SessionLocal,
        )
    monkeypatch.setattr(publication_service_v2, "_after_commit", lambda _response: None)
    monkeypatch.setattr(
        publication_service_v2,
        "compile_execution_component",
        lambda *_args, **_kwargs: pytest.fail("replay must not recompile"),
    )
    replayed = publish_revision(
        revision_id=revision_id,
        idempotency_key=key,
        request=request,
        session_factory=SessionLocal,
    )
    assert replayed.status_code == 201
    assert _terminal_record(key).state == "succeeded"


def test_process_restart_replays_status_body_and_headers_byte_for_byte():
    revision_id, version = _create_draft("fixture-process-replay-01", reviewed=True)
    key = "publish-process-replay-0001"
    request = _request(version)
    original = publish_revision(
        revision_id=revision_id,
        idempotency_key=key,
        request=request,
        session_factory=SessionLocal,
    )
    engine.dispose()
    completed = subprocess.run(
        [
            sys.executable,
            str(PROCESS_PROBE),
            "--database-url",
            engine.url.render_as_string(hide_password=False),
            "--revision-id",
            revision_id,
            "--idempotency-key",
            key,
            "--expected-draft-version",
            str(version),
        ],
        cwd=BACKEND_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stderr
    replay = json.loads(completed.stdout)
    assert replay["status_code"] == original.status_code
    assert base64.b64decode(replay["body_base64"]) == original.body
    assert replay["headers"] == original.headers
    assert replay["object_hash"] == original.object_hash
