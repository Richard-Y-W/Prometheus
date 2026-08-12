import hashlib
import json
from pathlib import Path
from uuid import uuid4

import pytest
from fastapi.testclient import TestClient
from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource
from sqlalchemy import func, select

from app.database import SessionLocal
from app.fixture_catalog import get_fixture
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
ROOT = Path(__file__).parents[2]


def validate_execution_package_schema(package: dict) -> None:
    resources = []
    for path in sorted((ROOT / "schemas").glob("*.schema.json")):
        schema = json.loads(path.read_text(encoding="utf-8"))
        resources.append((schema["$id"], Resource.from_contents(schema)))
    schema = json.loads(
        (ROOT / "schemas" / "execution-component.schema.json").read_text(
            encoding="utf-8"
        )
    )
    Draft202012Validator(
        schema,
        registry=Registry().with_resources(resources),
        format_checker=FormatChecker(),
    ).validate(package)


def v1_record_counts() -> tuple[int, ...]:
    with SessionLocal() as db:
        return tuple(
            db.scalar(select(func.count()).select_from(model)) or 0
            for model in V1_MODELS
        )


def create_fixture_candidate(client: TestClient) -> tuple[dict, dict]:
    created = client.post(
        "/v1/research-jobs",
        headers={"Idempotency-Key": uuid4().hex},
        json={
            "manufacturer": "Prometheus Fixture Works",
            "part_number": "PM-36-GM",
        },
    ).json()
    return created, created["candidate"]


def accepted_decisions(candidate: dict) -> list[dict]:
    return [
        {"field_name": parameter["field_name"], "status": "accepted"}
        for parameter in candidate["parameters"]
    ]


def assert_all_evidence_pending(candidate: dict) -> None:
    assert all(
        evidence["review_status"] == "pending"
        for parameter in candidate["parameters"]
        for evidence in parameter["evidence"]
    )


def review_and_publish_fixture(client: TestClient) -> dict:
    created, candidate = create_fixture_candidate(client)
    review = client.post(
        f"/v1/research-jobs/{created['id']}/review",
        json={
            "reviewed_by": "execution-package-reviewer",
            "decisions": accepted_decisions(candidate),
        },
    )
    assert review.status_code == 200
    publish = client.post(
        f"/v1/research-jobs/{created['id']}/publish",
        headers={"Idempotency-Key": uuid4().hex},
    )
    assert publish.status_code == 200
    return publish.json()


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
        assert [event["sequence"] for event in created["events"]] == list(range(1, 9))
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
        assert revision["content_hash"] is None
        expected_names = {
            parameter.name for parameter in get_fixture(manufacturer, part, None).parameters
        }
        assert {parameter["field_name"] for parameter in revision["parameters"]} == expected_names
        assert all(
            {
                "value",
                "quantity",
                "dimension",
                "unit",
                "validity_conditions",
            }.issubset(parameter)
            for parameter in revision["parameters"]
        )
        assert all(
            "value_si" not in parameter and "unit_si" not in parameter
            for parameter in revision["parameters"]
        )
        assert all(
            parameter["evidence"][0]["review_status"] == "pending"
            for parameter in revision["parameters"]
        )
        assert {
            parameter["evidence"][0]["evidence_class"]
            for parameter in revision["parameters"]
        } == {"synthetic_fixture"}
        assert all(
            parameter["evidence"][0]["confidence"] is None
            for parameter in revision["parameters"]
        )
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
        immutable = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={"reviewed_by": "test-engineer", "decisions": decisions},
        )
        assert immutable.status_code == 409
        assert immutable.json()["detail"]["code"] == "published_revision_immutable"
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


def test_review_requires_one_decision_for_every_field_and_is_atomic():
    with TestClient(app) as client:
        created, candidate = create_fixture_candidate(client)
        decisions = accepted_decisions(candidate)

        empty = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={"reviewed_by": "reviewer", "decisions": []},
        )
        assert empty.status_code == 422
        assert empty.json()["detail"]["code"] == "review_decisions_incomplete"

        missing = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={"reviewed_by": "reviewer", "decisions": decisions[:-1]},
        )
        assert missing.status_code == 422
        assert missing.json()["detail"]["code"] == "review_decisions_incomplete"
        assert missing.json()["detail"]["missing_fields"] == [
            decisions[-1]["field_name"]
        ]

        duplicate = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={"reviewed_by": "reviewer", "decisions": decisions + [decisions[0]]},
        )
        assert duplicate.status_code == 422
        assert duplicate.json()["detail"]["code"] == "duplicate_review_decision"
        assert duplicate.json()["detail"]["duplicate_fields"] == [
            decisions[0]["field_name"]
        ]

        with_unknown = decisions[:-1] + [
            {"field_name": "not_a_parameter", "status": "accepted"}
        ]
        unknown = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={"reviewed_by": "reviewer", "decisions": with_unknown},
        )
        assert unknown.status_code == 422
        assert unknown.json()["detail"]["code"] == "unknown_review_field"
        assert unknown.json()["detail"]["unknown_fields"] == ["not_a_parameter"]

        current = client.get(f"/v1/research-jobs/{created['id']}").json()
        assert current["status"] == "ready_for_review"
        assert_all_evidence_pending(current["candidate"])


def test_review_rejects_a_whitespace_only_reviewer_without_mutation():
    with TestClient(app) as client:
        created, candidate = create_fixture_candidate(client)

        response = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={
                "reviewed_by": "   ",
                "decisions": accepted_decisions(candidate),
            },
        )

        assert response.status_code == 422
        current = client.get(f"/v1/research-jobs/{created['id']}").json()
        assert current["status"] == "ready_for_review"
        assert_all_evidence_pending(current["candidate"])


@pytest.mark.parametrize(
    ("status", "note"),
    [
        ("unsupported", None),
        ("ambiguous", ""),
        ("rejected", None),
    ],
)
def test_review_rejects_invalid_status_or_missing_note(status: str, note: str | None):
    with TestClient(app) as client:
        created, candidate = create_fixture_candidate(client)
        decisions = accepted_decisions(candidate)
        decisions[0] = {
            "field_name": decisions[0]["field_name"],
            "status": status,
            "note": note,
        }
        response = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={"reviewed_by": "reviewer", "decisions": decisions},
        )
        assert response.status_code == 422
        current = client.get(f"/v1/research-jobs/{created['id']}").json()
        assert current["status"] == "ready_for_review"
        assert_all_evidence_pending(current["candidate"])


@pytest.mark.parametrize(
    ("decision_status", "job_status"),
    [("ambiguous", "review_ambiguous"), ("rejected", "review_rejected")],
)
def test_nonaccepted_review_blocks_publish_and_can_be_revised(
    decision_status: str, job_status: str
):
    with TestClient(app) as client:
        created, candidate = create_fixture_candidate(client)
        decisions = accepted_decisions(candidate)
        decisions[0] = {
            "field_name": decisions[0]["field_name"],
            "status": decision_status,
            "note": f"Engineer marked this field {decision_status}",
        }
        reviewed = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={"reviewed_by": "reviewer", "decisions": decisions},
        )
        assert reviewed.status_code == 200
        assert reviewed.json()["status"] == job_status
        reviewed_evidence = reviewed.json()["candidate"]["parameters"][0]["evidence"][0]
        assert reviewed_evidence["review_status"] == decision_status
        assert reviewed_evidence["review_note"] == decisions[0]["note"]

        blocked = client.post(
            f"/v1/research-jobs/{created['id']}/publish",
            headers={"Idempotency-Key": uuid4().hex},
        )
        assert blocked.status_code == 409
        assert blocked.json()["detail"]["code"] == "publication_review_incomplete"

        accepted = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={
                "reviewed_by": "reviewer",
                "decisions": accepted_decisions(candidate),
            },
        )
        assert accepted.status_code == 200
        assert accepted.json()["status"] == "reviewed"
        published = client.post(
            f"/v1/research-jobs/{created['id']}/publish",
            headers={"Idempotency-Key": uuid4().hex},
        )
        assert published.status_code == 200


def test_publication_rejects_a_parameter_with_no_evidence():
    with TestClient(app) as client:
        created, candidate = create_fixture_candidate(client)
        missing_evidence_id = candidate["parameters"][0]["evidence"][0]["id"]
        with SessionLocal() as db:
            db.delete(db.get(EvidenceRecord, missing_evidence_id))
            db.commit()

        reviewed = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={
                "reviewed_by": "reviewer",
                "decisions": accepted_decisions(candidate),
            },
        )
        assert reviewed.status_code == 200
        publish = client.post(
            f"/v1/research-jobs/{created['id']}/publish",
            headers={"Idempotency-Key": uuid4().hex},
        )
        assert publish.status_code == 409
        assert publish.json()["detail"]["code"] == "publication_review_incomplete"


def test_execution_package_requires_a_published_revision():
    with TestClient(app) as client:
        _, candidate = create_fixture_candidate(client)
        assert candidate["content_hash"] is None

        response = client.get(
            f"/v1/component-revisions/{candidate['id']}/execution-package"
        )

        assert response.status_code == 409
        assert response.json()["detail"]["code"] == "revision_not_published"

        missing = client.get(
            f"/v1/component-revisions/{uuid4()}/execution-package"
        )
        assert missing.status_code == 404


def test_published_execution_package_is_canonical_and_hash_verifiable():
    with TestClient(app) as client:
        published = review_and_publish_fixture(client)
        assert published["content_hash"].startswith("sha256:")

        endpoint = f"/v1/component-revisions/{published['id']}/execution-package"
        first = client.get(endpoint)
        second = client.get(endpoint)

        assert first.status_code == 200
        assert first.content == second.content
        package = first.json()
        validate_execution_package_schema(package)
        assert first.content == json.dumps(
            package,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        unhashed = dict(package)
        expected_hash = unhashed.pop("content_hash")
        canonical = json.dumps(
            unhashed,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        assert expected_hash == f"sha256:{hashlib.sha256(canonical).hexdigest()}"
        assert expected_hash == published["content_hash"]


def test_execution_package_detects_persisted_parameter_tampering():
    with TestClient(app) as client:
        published = review_and_publish_fixture(client)
        with SessionLocal() as db:
            parameter = db.scalar(
                select(ComponentParameter).where(
                    ComponentParameter.revision_id == published["id"],
                    ComponentParameter.field_name == "nominal_voltage_v",
                )
            )
            parameter.value = {"kind": "scalar", "value": 48.0}
            db.commit()

        response = client.get(
            f"/v1/component-revisions/{published['id']}/execution-package"
        )

        assert response.status_code == 409
        assert response.json()["detail"]["code"] == "execution_package_hash_mismatch"


def test_repeated_publish_detects_persisted_parameter_tampering():
    with TestClient(app) as client:
        created, candidate = create_fixture_candidate(client)
        review = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={
                "reviewed_by": "repeat-publish-reviewer",
                "decisions": accepted_decisions(candidate),
            },
        )
        assert review.status_code == 200
        first_publish = client.post(
            f"/v1/research-jobs/{created['id']}/publish",
            headers={"Idempotency-Key": uuid4().hex},
        )
        assert first_publish.status_code == 200

        with SessionLocal() as db:
            parameter = db.scalar(
                select(ComponentParameter).where(
                    ComponentParameter.revision_id == candidate["id"],
                    ComponentParameter.field_name == "nominal_voltage_v",
                )
            )
            parameter.value = {"kind": "scalar", "value": 48.0}
            db.commit()

        repeated = client.post(
            f"/v1/research-jobs/{created['id']}/publish",
            headers={"Idempotency-Key": uuid4().hex},
        )

        assert repeated.status_code == 409
        assert repeated.json()["detail"]["code"] == "execution_package_hash_mismatch"

        job_detail_response = client.get(f"/v1/research-jobs/{created['id']}")
        assert job_detail_response.status_code == 409
        assert (
            job_detail_response.json()["detail"]["code"]
            == "execution_package_hash_mismatch"
        )

        before_cached_request = v1_record_counts()
        cached = client.post(
            "/v1/research-jobs",
            headers={"Idempotency-Key": uuid4().hex},
            json={
                "manufacturer": "Prometheus Fixture Works",
                "part_number": "PM-36-GM",
            },
        )
        assert cached.status_code == 409
        assert cached.json()["detail"]["code"] == "execution_package_hash_mismatch"
        assert v1_record_counts() == before_cached_request


def test_invalid_execution_package_rolls_back_publication_state():
    with TestClient(app) as client:
        created, candidate = create_fixture_candidate(client)
        review = client.post(
            f"/v1/research-jobs/{created['id']}/review",
            json={
                "reviewed_by": "rollback-reviewer",
                "decisions": accepted_decisions(candidate),
            },
        )
        assert review.status_code == 200
        with SessionLocal() as db:
            parameter = db.scalar(
                select(ComponentParameter).where(
                    ComponentParameter.revision_id == candidate["id"],
                    ComponentParameter.field_name == "gearbox_efficiency_range",
                )
            )
            parameter.value = {"kind": "range", "minimum": 0.9, "maximum": 0.2}
            db.commit()

        publish = client.post(
            f"/v1/research-jobs/{created['id']}/publish",
            headers={"Idempotency-Key": uuid4().hex},
        )

        assert publish.status_code == 409
        assert publish.json()["detail"]["code"] == "execution_package_invalid"
        current_job = client.get(f"/v1/research-jobs/{created['id']}").json()
        assert current_job["status"] == "reviewed"
        assert current_job["candidate"]["status"] == "draft"
        assert current_job["candidate"]["published_at"] is None
        assert current_job["candidate"]["content_hash"] is None
