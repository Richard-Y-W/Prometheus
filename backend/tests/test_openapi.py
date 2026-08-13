import json
from pathlib import Path

from app.main import app


ROOT = Path(__file__).parents[2]


def test_checked_in_openapi_matches_application():
    checked_in = json.loads(
        (ROOT / "docs" / "openapi-v2.json").read_text(encoding="utf-8")
    )
    assert checked_in == app.openapi()


def test_openapi_exposes_complete_v2_boundary_and_v1_retirement():
    document = app.openapi()
    paths = document["paths"]
    assert {
        "/api/v2/fixture-ingestions",
        "/api/v2/fixture-ingestions/{ingestion_id}",
        "/api/v2/revisions/{revision_id}",
        "/api/v2/revisions/{revision_id}/reviews",
        "/api/v2/revisions/{revision_id}/publication",
        "/api/v2/revisions/{revision_id}/execution-package",
    }.issubset(paths)
    fixture_parameters = paths["/api/v2/fixture-ingestions"]["post"][
        "parameters"
    ]
    assert any(
        parameter["name"] == "Idempotency-Key" and parameter["required"]
        for parameter in fixture_parameters
    )
    fixture_schema = document["components"]["schemas"][
        "FixtureIngestionRequestV2"
    ]["properties"]["fixture_id"]
    assert fixture_schema["enum"] == [
        "prometheus.motor-a.fixture-1",
        "prometheus.motor-b.fixture-1",
        "prometheus.pm-36-gm.fixture-2",
    ]
    publication_parameters = paths[
        "/api/v2/revisions/{revision_id}/publication"
    ]["post"]["parameters"]
    assert any(
        parameter["name"] == "Idempotency-Key" and parameter["required"]
        for parameter in publication_parameters
    )
    package_content = paths[
        "/api/v2/revisions/{revision_id}/execution-package"
    ]["get"]["responses"]["200"]["content"]
    assert (
        "application/vnd.prometheus.execution-component+json;version=2.0.0"
        in package_content
    )

    retired = {
        ("/v1/research-jobs", "post"),
        ("/v1/research-jobs/{job_id}/review", "post"),
        ("/v1/research-jobs/{job_id}/publish", "post"),
        ("/v1/component-revisions/{revision_id}/execution-package", "get"),
    }
    for path, method in retired:
        assert "410" in paths[path][method]["responses"]
