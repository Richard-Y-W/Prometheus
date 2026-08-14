from __future__ import annotations

import ast
from pathlib import Path
from uuid import uuid4

from fastapi.testclient import TestClient

from app.database import SessionLocal
from app.fixture_pipeline_v2 import FIXTURE_ID, create_fixture_draft
from app.main import app


RETIRED_DETAIL = {
    "code": "v1_trust_boundary_retired",
    "message": "The v1 review/publication boundary is retired.",
    "migration_guide": "/docs/migration/program-01a-v1-to-v2.md",
}
APP_ROOT = Path(__file__).parents[1] / "app"


def test_every_v1_trust_boundary_route_returns_the_same_migration_document():
    job_id = str(uuid4())
    revision_id = str(uuid4())
    with TestClient(app) as client:
        responses = [
            client.post(
                "/v1/research-jobs",
                headers={"Idempotency-Key": "retired-v1-create-0001"},
                json={
                    "manufacturer": "Prometheus Fixture Works",
                    "part_number": "PM-36-GM",
                },
            ),
            client.post(
                f"/v1/research-jobs/{job_id}/review",
                json={"reviewed_by": "historical", "decisions": []},
            ),
            client.post(
                f"/v1/research-jobs/{job_id}/publish",
                headers={"Idempotency-Key": "retired-v1-publish-0001"},
            ),
            client.get(
                f"/v1/component-revisions/{revision_id}/execution-package"
            ),
        ]
    for response in responses:
        assert response.status_code == 410
        assert response.json() == {"detail": RETIRED_DETAIL}


def test_historical_v1_component_reads_label_publication_integrity_only():
    with SessionLocal() as db:
        result = create_fixture_draft(
            db,
            fixture_id=FIXTURE_ID,
            idempotency_key="legacy-read-label-fixture-01",
        )
        component_id = result.revision.component_id
        revision_id = result.revision.id

    with TestClient(app) as client:
        component = client.get(f"/v1/components/{component_id}")
        revision = client.get(
            f"/v1/components/{component_id}/revisions/{revision_id}"
        )

    assert component.status_code == 200
    assert revision.status_code == 200
    assert all(
        item["publication_integrity"] is not None
        for item in component.json()["revisions"]
    )
    assert revision.json()["publication_integrity"] == "v2_draft"
    forbidden = {"payload_bytes", "execution_package", "package_bytes"}
    assert forbidden.isdisjoint(revision.json())


def test_unversioned_analysis_retirement_behavior_is_unchanged():
    with TestClient(app) as client:
        compilation = client.post(f"/scenarios/{uuid4()}/compile")
        execution = client.post(f"/scenarios/{uuid4()}/runs")
    assert compilation.status_code == 410
    assert compilation.json()["detail"]["code"] == "legacy_analysis_path_retired"
    assert execution.status_code == 501
    assert execution.json()["detail"]["code"] == "authoritative_execution_unavailable"


def test_production_v2_modules_never_import_historical_v1_contract_or_reconstruction():
    modules = sorted(APP_ROOT.glob("*v2.py")) + [APP_ROOT / "http_policy.py"]
    forbidden = {"contracts_v1", "execution_packages"}
    for module in modules:
        source = module.read_text(encoding="utf-8")
        imported_modules = set()
        for node in ast.walk(ast.parse(source, filename=str(module))):
            if isinstance(node, ast.Import):
                imported_modules.update(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom):
                if node.module is not None:
                    imported_modules.add(node.module)
                else:
                    imported_modules.update(alias.name for alias in node.names)
        assert not {
            imported.rsplit(".", maxsplit=1)[-1]
            for imported in imported_modules
        }.intersection(forbidden), module.name
