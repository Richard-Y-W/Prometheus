from __future__ import annotations

import pytest
from fastapi.testclient import TestClient
import sqlalchemy as sa

from app import component_acquisition_v2
from app.component_acquisition_v2 import (
    ComponentAcquisitionError,
    ComponentAcquisitionRequestV2,
    create_component_acquisition,
)
from app.contracts_v2 import SCHEMA_VERSION
from app.database import SessionLocal
from app.main import app
from app.models_v2 import ArtifactObjectV2, ComponentAcquisitionJobV2
from app.outbound_fetch import FetchResult, OutboundFetchError


@pytest.fixture
def db():
    with SessionLocal() as session:
        yield session
        session.rollback()


def _row_count(db, model) -> int:
    return db.scalar(sa.select(sa.func.count()).select_from(model))


_PRODUCT_HTML = (
    b'<html><head><script type="application/ld+json">'
    b'{"@type":"Product","name":"Widget","brand":{"name":"Acme"},"mpn":"AB-1"}'
    b"</script></head><body></body></html>"
)
_PLAIN_HTML = b"<html><body>no structured data here</body></html>"


def _fake_fetch(body: bytes = _PRODUCT_HTML):
    def fetch(url: str):
        return FetchResult(bytes=body, content_type="text/html", final_url=url)
    return fetch


def _sample_request(**overrides) -> ComponentAcquisitionRequestV2:
    body = {
        "schema_version": SCHEMA_VERSION,
        "url": "https://example.com/products/widget",
    }
    body.update(overrides)
    return ComponentAcquisitionRequestV2.model_validate(body)


def test_acquisition_retains_the_artifact_and_extracts_identity(db, monkeypatch):
    monkeypatch.setattr(component_acquisition_v2, "fetch_url_safely", _fake_fetch())
    job = create_component_acquisition(
        db, request=_sample_request(), idempotency_key="acquire-0001-0001-0001"
    )
    assert job.status == "succeeded"
    assert job.extraction_method == "jsonld"
    assert job.extracted_manufacturer == "Acme"
    assert job.extracted_part_number == "AB-1"
    assert job.source_artifact_hash is not None

    artifact = db.get(ArtifactObjectV2, job.source_artifact_hash)
    assert artifact is not None
    assert bytes(artifact.payload_bytes) == _PRODUCT_HTML
    assert artifact.media_type == "text/html"


def test_acquisition_without_extractable_identity_still_succeeds(db, monkeypatch):
    monkeypatch.setattr(
        component_acquisition_v2, "fetch_url_safely", _fake_fetch(_PLAIN_HTML)
    )
    job = create_component_acquisition(
        db, request=_sample_request(), idempotency_key="acquire-0002-0002-0002"
    )
    assert job.status == "succeeded"
    assert job.extraction_method == "none"
    assert job.extracted_manufacturer is None
    assert job.extracted_part_number is None
    assert job.source_artifact_hash is not None


def test_acquisition_idempotency_key_replays_the_same_job(db, monkeypatch):
    monkeypatch.setattr(component_acquisition_v2, "fetch_url_safely", _fake_fetch())
    first = create_component_acquisition(
        db, request=_sample_request(), idempotency_key="acquire-replay-0001"
    )
    second = create_component_acquisition(
        db, request=_sample_request(), idempotency_key="acquire-replay-0001"
    )
    assert first.id == second.id
    assert _row_count(db, ComponentAcquisitionJobV2) == 1


def test_acquisition_idempotency_key_conflicts_on_a_different_request(db, monkeypatch):
    monkeypatch.setattr(component_acquisition_v2, "fetch_url_safely", _fake_fetch())
    create_component_acquisition(
        db, request=_sample_request(), idempotency_key="acquire-conflict-0001"
    )
    with pytest.raises(ComponentAcquisitionError) as captured:
        create_component_acquisition(
            db,
            request=_sample_request(url="https://example.com/products/other"),
            idempotency_key="acquire-conflict-0001",
        )
    assert captured.value.code == "acquisition_idempotency_conflict"


def test_acquisition_rejects_invalid_idempotency_key(db, monkeypatch):
    monkeypatch.setattr(component_acquisition_v2, "fetch_url_safely", _fake_fetch())
    with pytest.raises(ComponentAcquisitionError) as captured:
        create_component_acquisition(
            db, request=_sample_request(), idempotency_key="too-short"
        )
    assert captured.value.code == "invalid_idempotency_key"


def test_acquisition_rejects_unsupported_schema_version(db, monkeypatch):
    monkeypatch.setattr(component_acquisition_v2, "fetch_url_safely", _fake_fetch())
    with pytest.raises(ComponentAcquisitionError) as captured:
        create_component_acquisition(
            db,
            request=_sample_request(schema_version="9.9.9"),
            idempotency_key="acquire-badschema-0001",
        )
    assert captured.value.code == "unsupported_schema"


def test_acquisition_fetch_failure_leaves_no_job_row(db, monkeypatch):
    def failing_fetch(url: str):
        raise OutboundFetchError("address_not_public", "blocked")

    monkeypatch.setattr(component_acquisition_v2, "fetch_url_safely", failing_fetch)
    before = _row_count(db, ComponentAcquisitionJobV2)
    db.rollback()
    with pytest.raises(ComponentAcquisitionError) as captured:
        create_component_acquisition(
            db, request=_sample_request(), idempotency_key="acquire-fail-0001"
        )
    assert captured.value.code == "address_not_public"
    db.rollback()
    assert _row_count(db, ComponentAcquisitionJobV2) == before


def test_acquisition_http_create_and_get_round_trip(monkeypatch):
    monkeypatch.setattr(component_acquisition_v2, "fetch_url_safely", _fake_fetch())
    with TestClient(app) as client:
        create_response = client.post(
            "/api/v2/component-acquisitions",
            headers={"Idempotency-Key": "acquire-http-0001-0001"},
            json={
                "schema_version": SCHEMA_VERSION,
                "url": "https://example.com/products/widget",
            },
        )
        assert create_response.status_code == 201, create_response.text
        created = create_response.json()
        assert created["extraction_method"] == "jsonld"
        assert created["extracted_manufacturer"] == "Acme"
        assert created["extracted_part_number"] == "AB-1"

        get_response = client.get(
            f"/api/v2/component-acquisitions/{created['id']}"
        )
        assert get_response.status_code == 200
        assert get_response.json() == created


def test_acquisition_http_rejects_unreachable_target_without_mocking():
    with TestClient(app) as client:
        response = client.post(
            "/api/v2/component-acquisitions",
            headers={"Idempotency-Key": "acquire-http-real-0001"},
            json={
                "schema_version": SCHEMA_VERSION,
                "url": "http://127.0.0.1:1/nope",
            },
        )
        assert response.status_code == 422
        assert response.json()["detail"]["code"] == "address_not_public"


def test_acquisition_not_found_returns_404():
    with TestClient(app) as client:
        response = client.get("/api/v2/component-acquisitions/does-not-exist")
        assert response.status_code == 404
        assert response.json()["detail"]["code"] == "component_acquisition_not_found"
