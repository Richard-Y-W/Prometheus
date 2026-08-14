from __future__ import annotations

import json
from uuid import uuid4

from fastapi.testclient import TestClient
import pytest
import sqlalchemy as sa

from app.canonical_json import MAX_RAW_BYTES, canonicalize_value
from app.contracts_v2 import PACKAGE_MEDIA_TYPE, SCHEMA_ID, SCHEMA_VERSION
from app.database import SessionLocal
from app.db_types import utc_now
from app.fixture_pipeline_v2 import FIXTURE_ID
from app.fixture_catalog_v2 import FIXTURE_IDS, get_fixture_definition
from app.main import app
from app.models_v1 import ComponentRevision
from app.models_v2 import (
    FixtureIngestionJobV2,
    PublicationRequestV2 as PublicationRequestRecord,
    PublishedObject,
)
from app.package_compiler_v2 import compile_execution_component


CREATE_BODY = {"fixture_id": FIXTURE_ID, "schema_version": SCHEMA_VERSION}


def _create(
    client: TestClient,
    key: str = "api-v2-fixture-create-0001",
    *,
    fixture_id: str = FIXTURE_ID,
) -> dict:
    response = client.post(
        "/api/v2/fixture-ingestions",
        headers={"Idempotency-Key": key},
        json={"fixture_id": fixture_id, "schema_version": SCHEMA_VERSION},
    )
    assert response.status_code == 201, response.text
    return response.json()


def _accepted_decisions(revision: dict) -> list[dict[str, str]]:
    return [
        {
            "claim_id": parameter["selected_claim"]["claim_id"],
            "status": "accepted",
            "note": "Accepted as synthetic conformance input only.",
        }
        for parameter in revision["parameters"]
    ]


def _review_all(client: TestClient, revision: dict) -> dict:
    response = client.post(
        f"/api/v2/revisions/{revision['id']}/reviews",
        json={
            "expected_draft_version": revision["draft_version"],
            "reviewed_by": "api-v2-reviewer",
            "decisions": _accepted_decisions(revision),
        },
    )
    assert response.status_code == 200, response.text
    return response.json()


def _publish(
    client: TestClient,
    revision: dict,
    key: str = "api-v2-publication-0001",
):
    return client.post(
        f"/api/v2/revisions/{revision['id']}/publication",
        headers={"Idempotency-Key": key},
        json={
            "expected_draft_version": revision["draft_version"],
            "schema_id": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
        },
    )


def _error_code(response) -> str:
    return response.json()["detail"]["code"]


@pytest.mark.parametrize(
    ("fixture_id", "expected_readiness"),
    [
        (FIXTURE_IDS[0], "ready"),
        (FIXTURE_IDS[1], "ready"),
        (FIXTURE_IDS[2], "blocked"),
    ],
)
def test_complete_v2_fixture_review_publication_and_exact_export_path(
    fixture_id, expected_readiness
):
    definition = get_fixture_definition(fixture_id)
    key_suffix = fixture_id.removeprefix("prometheus.").replace(".", "-")
    with TestClient(app) as client:
        creation = _create(
            client,
            f"api-v2-fixture-create-{key_suffix}",
            fixture_id=fixture_id,
        )
        assert set(creation) == {"id", "state", "fixture_id", "revision"}
        assert creation["state"] == "succeeded"
        assert creation["fixture_id"] == fixture_id

        revision = creation["revision"]
        assert set(revision) == {
            "id",
            "status",
            "draft_version",
            "contract",
            "component",
            "capability_id",
            "evidence",
            "limitations",
            "parameters",
            "capability_gates",
            "publication_integrity",
            "object_hash",
            "published_at",
        }
        assert revision["status"] == "draft"
        assert revision["draft_version"] == 0
        assert revision["contract"] == {
            "schema_id": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
        }
        assert revision["component"] == {
            "component_id": revision["component"]["component_id"],
            "manufacturer": definition.manufacturer,
            "part_number": definition.part_number,
            "revision": definition.revision,
            "component_class": definition.component_class,
        }
        assert revision["publication_integrity"] == "v2_draft"
        assert revision["capability_id"] == definition.capability_id
        assert revision["evidence"]
        assert {
            item["evidence_class"] for item in revision["evidence"]
        } == {"private_upload"}
        assert {
            item["source_authority"] for item in revision["evidence"]
        } == {"synthetic_fixture"}
        assert {
            item["physical_validation_status"]
            for item in revision["evidence"]
        } == {"unvalidated"}
        assert all(item["limitations"] for item in revision["evidence"])
        source_limitations = json.loads(definition.source_path.read_text())[
            "limitations"
        ]
        assert {item["statement"] for item in revision["limitations"]} == {
            *source_limitations,
            definition.execution_limitation,
        }
        assert revision["object_hash"] is None
        assert revision["published_at"] is None
        assert len(revision["parameters"]) == 17
        assert [item["name"] for item in revision["parameters"]] == sorted(
            item["name"] for item in revision["parameters"]
        )
        for parameter in revision["parameters"]:
            assert set(parameter) == {
                "slot_id",
                "name",
                "quantity",
                "dimension",
                "required_for_execution",
                "selected_claim",
            }
            claim = parameter["selected_claim"]
            assert set(claim) == {
                "claim_id",
                "claim_fingerprint",
                "value_state",
                "value",
                "unit",
                "original_value",
                "original_unit",
                "validity_conditions",
                "provenance",
                "evidence_ids",
            }
            assert claim["claim_id"]
            assert claim["claim_fingerprint"].startswith("sha256:")
        assert {gate["phase"] for gate in revision["capability_gates"]} == {
            "publication",
            "execution",
        }

        fetched_job = client.get(
            f"/api/v2/fixture-ingestions/{creation['id']}",
            headers={"Content-Type": "application/json"},
        )
        assert fetched_job.status_code == 200
        assert fetched_job.json() == creation
        fetched_revision = client.get(f"/api/v2/revisions/{revision['id']}")
        assert fetched_revision.status_code == 200
        assert fetched_revision.json() == revision

        reviewed = _review_all(client, revision)
        assert reviewed["id"] == revision["id"]
        assert reviewed["draft_version"] == 1
        review_gate = next(
            gate
            for gate in reviewed["capability_gates"]
            if gate["required_review_type"] == "claim_review"
        )
        assert review_gate["state"] == "satisfied"

        published = _publish(
            client,
            reviewed,
            f"api-v2-publication-{key_suffix}",
        )
        assert published.status_code == 201, published.text
        publication = published.json()
        assert publication == {
            "execution_readiness": expected_readiness,
            "media_type": PACKAGE_MEDIA_TYPE,
            "object_hash": publication["object_hash"],
            "publication_integrity": "sealed_v2",
            "revision_id": revision["id"],
            "schema_id": SCHEMA_ID,
            "schema_version": SCHEMA_VERSION,
            "status": "published",
        }
        assert published.headers.get_list("etag") == [
            f'"{publication["object_hash"]}"'
        ]

        exported = client.get(
            f"/api/v2/revisions/{revision['id']}/execution-package"
        )
        assert exported.status_code == 200, exported.text
        assert exported.headers["content-type"] == PACKAGE_MEDIA_TYPE
        assert exported.headers.get_list("etag") == [
            f'"{publication["object_hash"]}"'
        ]
        with SessionLocal() as db:
            stored = db.get(PublishedObject, publication["object_hash"])
            assert exported.content == stored.payload_bytes
        assert exported.json()["execution_readiness"] == expected_readiness


def test_fixture_creation_replays_and_changed_request_conflicts_without_overwrite():
    key = "api-v2-fixture-replay-0001"
    with TestClient(app) as client:
        first = _create(client, key)
        replay = _create(client, key)
        assert replay == first

        conflict = client.post(
            "/api/v2/fixture-ingestions",
            headers={"Idempotency-Key": key},
            json={"fixture_id": FIXTURE_ID, "schema_version": "9.0.0"},
        )
        assert conflict.status_code == 409
        assert _error_code(conflict) == "idempotency_conflict"

        different_fixture = client.post(
            "/api/v2/fixture-ingestions",
            headers={"Idempotency-Key": key},
            json={
                "fixture_id": FIXTURE_IDS[0],
                "schema_version": SCHEMA_VERSION,
            },
        )
        assert different_fixture.status_code == 409
        assert _error_code(different_fixture) == "idempotency_conflict"
    with SessionLocal() as db:
        assert db.scalar(
            sa.select(sa.func.count()).select_from(FixtureIngestionJobV2)
        ) == 1


@pytest.mark.parametrize("route", ["fixture", "publication"])
@pytest.mark.parametrize("header", [None, "short"])
def test_idempotency_key_is_required_and_strict(route: str, header: str | None):
    headers = {} if header is None else {"Idempotency-Key": header}
    with TestClient(app) as client:
        if route == "fixture":
            response = client.post(
                "/api/v2/fixture-ingestions", headers=headers, json=CREATE_BODY
            )
        else:
            response = client.post(
                f"/api/v2/revisions/{uuid4()}/publication",
                headers=headers,
                json={
                    "expected_draft_version": 0,
                    "schema_id": SCHEMA_ID,
                    "schema_version": SCHEMA_VERSION,
                },
            )
    assert response.status_code == 422
    expected = "idempotency_key_required" if header is None else "invalid_idempotency_key"
    assert _error_code(response) == expected


def test_unsupported_schema_is_explicit_for_fixture_and_publication():
    with TestClient(app) as client:
        fixture = client.post(
            "/api/v2/fixture-ingestions",
            headers={"Idempotency-Key": "api-v2-unsupported-fixture-01"},
            json={"fixture_id": FIXTURE_ID, "schema_version": "9.0.0"},
        )
        assert fixture.status_code == 422
        assert _error_code(fixture) == "unsupported_schema"

        creation = _create(client, "api-v2-unsupported-publish-create")
        revision = creation["revision"]
        publication = client.post(
            f"/api/v2/revisions/{revision['id']}/publication",
            headers={"Idempotency-Key": "api-v2-unsupported-publish-01"},
            json={
                "expected_draft_version": revision["draft_version"],
                "schema_id": "urn:prometheus:schema:execution-component:9.0.0",
                "schema_version": "9.0.0",
            },
        )
        assert publication.status_code == 409
        assert _error_code(publication) == "unsupported_schema"
    with SessionLocal() as db:
        record = db.scalar(
            sa.select(PublicationRequestRecord).where(
                PublicationRequestRecord.idempotency_key
                == "api-v2-unsupported-publish-01"
            )
        )
        assert record.state == "terminal_failure"


@pytest.mark.parametrize(
    "source",
    [
        b'{"fixture_id":"one","fixture_id":"two","schema_version":"2.0.0"}',
        b'{"fixture_id":"one","fixture\\u005fid":"two","schema_version":"2.0.0"}',
        b'{"fixture_id":"\xff","schema_version":"2.0.0"}',
    ],
)
def test_strict_json_policy_rejects_duplicate_decoded_keys_and_invalid_utf8(
    source: bytes,
):
    with TestClient(app) as client:
        response = client.post(
            "/api/v2/fixture-ingestions",
            headers={
                "Content-Type": "application/json",
                "Idempotency-Key": "api-v2-invalid-json-policy-01",
            },
            content=source,
        )
    assert response.status_code == 400
    assert response.content == canonicalize_value(
        {
            "detail": {
                "code": "request_json_invalid",
                "message": "The v2 JSON request is invalid.",
            }
        }
    )


def test_declared_and_streamed_body_overflow_are_413():
    expected = {
        "detail": {
            "code": "request_too_large",
            "message": "The v2 JSON request exceeds the 8 MiB limit.",
        }
    }
    with TestClient(app) as client:
        declared = client.post(
            "/api/v2/fixture-ingestions",
            headers={
                "Content-Type": "application/json",
                "Content-Length": str(MAX_RAW_BYTES + 1),
                "Idempotency-Key": "api-v2-declared-overflow-01",
            },
            content=b"{}",
        )

        def streamed_body():
            yield b'{"padding":"'
            yield b"a" * MAX_RAW_BYTES
            yield b'"}'

        streamed = client.post(
            "/api/v2/fixture-ingestions",
            headers={
                "Content-Type": "application/json",
                "Idempotency-Key": "api-v2-streamed-overflow-01",
            },
            content=streamed_body(),
        )
    assert declared.status_code == 413
    assert declared.json() == expected
    assert streamed.status_code == 413
    assert streamed.json() == expected


def test_review_transport_limits_stale_version_and_legacy_identity_are_explicit():
    with TestClient(app) as client:
        revision = _create(client, "api-v2-review-policy-create-01")["revision"]
        claim = revision["parameters"][0]["selected_claim"]
        base_decision = {
            "claim_id": claim["claim_id"],
            "status": "accepted",
            "note": "bounded review note",
        }
        oversized_batch = client.post(
            f"/api/v2/revisions/{revision['id']}/reviews",
            json={
                "expected_draft_version": 0,
                "reviewed_by": "reviewer",
                "decisions": [
                    {
                        "claim_id": str(uuid4()),
                        "status": "accepted",
                        "note": "bounded review note",
                    }
                    for _ in range(1001)
                ],
            },
        )
        assert oversized_batch.status_code == 422
        assert _error_code(oversized_batch) == "request_validation_error"

        oversized_reviewer = client.post(
            f"/api/v2/revisions/{revision['id']}/reviews",
            json={
                "expected_draft_version": 0,
                "reviewed_by": "r" * 257,
                "decisions": [base_decision],
            },
        )
        assert oversized_reviewer.status_code == 422
        assert _error_code(oversized_reviewer) == "request_validation_error"

        oversized_note = client.post(
            f"/api/v2/revisions/{revision['id']}/reviews",
            json={
                "expected_draft_version": 0,
                "reviewed_by": "reviewer",
                "decisions": [{**base_decision, "note": "n" * 4097}],
            },
        )
        assert oversized_note.status_code == 422
        assert _error_code(oversized_note) == "request_validation_error"

        legacy = client.post(
            f"/api/v2/revisions/{revision['id']}/reviews",
            json={
                "expected_draft_version": 0,
                "reviewed_by": "reviewer",
                "decisions": [
                    {
                        "field_name": revision["parameters"][0]["name"],
                        "status": "accepted",
                        "note": "legacy identity must not be inferred",
                    }
                ],
            },
        )
        assert legacy.status_code == 422
        assert _error_code(legacy) == "claim_id_required"

        accepted = client.post(
            f"/api/v2/revisions/{revision['id']}/reviews",
            json={
                "expected_draft_version": 0,
                "reviewed_by": "reviewer",
                "decisions": [base_decision],
            },
        )
        assert accepted.status_code == 200
        stale = client.post(
            f"/api/v2/revisions/{revision['id']}/reviews",
            json={
                "expected_draft_version": 0,
                "reviewed_by": "reviewer",
                "decisions": [base_decision],
            },
        )
        assert stale.status_code == 409
        assert _error_code(stale) == "stale_draft_version"
        assert stale.json()["detail"]["current_draft_version"] == 1


def test_export_fails_closed_when_bound_object_bytes_are_corrupt():
    with TestClient(app) as client:
        revision = _create(client, "api-v2-corrupt-export-create-01")["revision"]
        reviewed = _review_all(client, revision)
        with SessionLocal.begin() as db:
            compiled = compile_execution_component(db, revision["id"])
            corrupt_bytes = canonicalize_value({"corrupt": "canonical object"})
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
            stored_revision = db.get(ComponentRevision, revision["id"])
            stored_revision.status = "published"
            stored_revision.publication_integrity = "sealed_v2"
            stored_revision.content_hash = compiled.object_hash
            stored_revision.published_object_hash = compiled.object_hash
            stored_revision.published_at = utc_now()

        response = client.get(
            f"/api/v2/revisions/{reviewed['id']}/execution-package"
        )
    assert response.status_code == 409
    assert _error_code(response) == "published_object_integrity_error"


def test_json_policy_leaves_non_v2_routes_unchanged():
    with TestClient(app) as client:
        response = client.post(
            "/projects",
            content=json.dumps({"name": "Prototype utility"}).encode("utf-8"),
            headers={"Content-Type": "application/json"},
        )
        lookalike_prefix = client.post(
            "/api/v20/not-a-v2-route",
            content=b'{"duplicate":1,"duplicate":2}',
            headers={"Content-Type": "application/json"},
        )
    assert response.status_code == 201
    assert lookalike_prefix.status_code == 404
