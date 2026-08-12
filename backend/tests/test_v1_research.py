from uuid import uuid4

import pytest
from fastapi.testclient import TestClient
from sqlalchemy import func, select

from app.database import SessionLocal
from app.main import app
from app.models_v1 import (
    Component,
    ComponentParameter,
    ComponentRevision,
    EvidenceRecord,
    Manufacturer,
    ResearchJob,
    ResearchJobEvent,
    SourceDocument,
)


V1_MODELS = (
    Manufacturer,
    Component,
    ComponentRevision,
    ComponentParameter,
    SourceDocument,
    EvidenceRecord,
    ResearchJob,
    ResearchJobEvent,
)


def v1_record_counts() -> tuple[int, ...]:
    with SessionLocal() as db:
        return tuple(
            db.scalar(select(func.count()).select_from(model)) or 0
            for model in V1_MODELS
        )


def test_idempotent_fixture_research_review_publish_and_search():
    suffix = uuid4().hex[:8]
    manufacturer = "Prometheus Fixture Works"
    part = "PM-36-GM"
    key = f"research-{suffix}"
    with TestClient(app) as client:
        created = client.post(
            "/v1/research-jobs",
            headers={"Idempotency-Key": key},
            json={"manufacturer": manufacturer, "part_number": part},
        ).json()
        assert created["status"] == "ready_for_review"
        assert len(created["events"]) == 8
        repeated = client.post(
            "/v1/research-jobs",
            headers={"Idempotency-Key": key},
            json={"manufacturer": manufacturer, "part_number": part},
        ).json()
        assert repeated["id"] == created["id"]
        search = client.get(
            "/v1/components/search", params={"q": part, "page_size": 5}
        ).json()
        assert search["items"][0]["part_number"] == part
        assert search["has_more"] is False
        component = client.get(f"/v1/components/{search['items'][0]['id']}").json()
        revision = component["revisions"][0]
        assert revision["manufacturer"] == manufacturer
        assert revision["status"] == "draft"
        assert all(
            parameter["evidence"][0]["review_status"] == "pending"
            for parameter in revision["parameters"]
        )
        assert {
            parameter["evidence"][0]["evidence_class"]
            for parameter in revision["parameters"]
        } == {"synthetic_fixture"}
        blocked = client.post(
            f"/v1/research-jobs/{created['id']}/publish",
            headers={"Idempotency-Key": f"publish-{suffix}"},
        )
        assert blocked.status_code == 409
        decisions = [
            {"field_name": parameter["field_name"], "status": "accepted"}
            for parameter in revision["parameters"]
        ]
        assert (
            client.post(
                f"/v1/research-jobs/{created['id']}/review",
                json={"reviewed_by": "test-engineer", "decisions": decisions},
            ).status_code
            == 200
        )
        published = client.post(
            f"/v1/research-jobs/{created['id']}/publish",
            headers={"Idempotency-Key": f"publish-{suffix}"},
        ).json()
        assert published["status"] == "published"
        assert published["published_at"]
        assert (
            client.post(
                f"/v1/research-jobs/{created['id']}/review",
                json={"reviewed_by": "test-engineer", "decisions": decisions},
            ).status_code
            == 409
        )
        cached = client.post(
            "/v1/research-jobs",
            headers={"Idempotency-Key": f"research-cache-{suffix}"},
            json={"manufacturer": manufacturer, "part_number": part},
        ).json()
        assert cached["status"] == "published"
        assert cached["revision_id"] == published["id"]
        assert [event["stage"] for event in cached["events"]] == [
            "cache_lookup",
            "ready_for_review",
        ]


@pytest.mark.parametrize(
    ("body", "code"),
    [
        (
            {"manufacturer": "Other Company", "part_number": "PM-36-GM"},
            "fixture_identity_not_found",
        ),
        (
            {"manufacturer": "Prometheus Fixture Works", "part_number": "UNKNOWN"},
            "fixture_identity_not_found",
        ),
        (
            {
                "manufacturer": "Prometheus Fixture Works",
                "part_number": "PM-36-GM",
                "source_url": "https://example.com/unrelated.pdf",
            },
            "fixture_source_url_not_allowed",
        ),
    ],
)
def test_versioned_fixture_rejection_creates_no_records(body: dict, code: str):
    before = v1_record_counts()
    with TestClient(app) as client:
        response = client.post(
            "/v1/research-jobs",
            headers={"Idempotency-Key": uuid4().hex},
            json=body,
        )
    assert response.status_code == 422
    detail = response.json()["detail"]
    assert detail["code"] == code
    assert detail["provider"] == "fixture"
    assert detail["supported_fixture_ids"] == [
        "prometheus-fixture-works/PM-36-GM"
    ]
    assert v1_record_counts() == before
