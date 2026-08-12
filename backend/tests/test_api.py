from uuid import uuid4

from fastapi.testclient import TestClient

from app.main import app


def assert_error(response, status_code: int, code: str, message_fragment: str) -> None:
    assert response.status_code == status_code
    detail = response.json()["detail"]
    assert detail["code"] == code
    assert message_fragment in detail["message"]


def test_unversioned_project_and_fixture_cad_routes_remain_prototype_utilities():
    with TestClient(app) as client:
        project = client.post(
            "/projects", json={"name": "Motor arm prototype viewer"}
        )
        assert project.status_code == 201
        project_id = project.json()["id"]

        imported = client.post(f"/projects/{project_id}/cad-imports?fixture=true")
        assert imported.status_code == 202
        assert imported.json()["status"] == "completed"
        assert imported.json()["limitations"]

        connection = client.post(
            f"/projects/{project_id}/connections",
            json={
                "source_part": "motor",
                "target_part": "arm",
                "connection_type": "revolute",
                "axis": [0, 0, 1],
                "limits_deg": [0, 90],
            },
        )
        assert connection.status_code == 201

        scenario = client.post(
            f"/projects/{project_id}/scenarios",
            json={
                "name": "Prototype scenario record",
                "natural_language_description": "Rotate an 8 kg payload",
                "definition": {
                    "payload_kg": 8,
                    "arm_length_m": 0.2,
                    "rotation_deg": 90,
                    "movement_s": 1.2,
                    "hold_s": 4,
                    "cycle_s": 10,
                    "ambient_c": 35,
                },
            },
        )
        assert scenario.status_code == 201


def test_legacy_evidence_routes_are_retired():
    with TestClient(app) as client:
        research = client.post(
            "/component-research",
            json={
                "manufacturer": "Prometheus Fixture Works",
                "part_number": "PM-36-GM",
            },
        )
        assert_error(
            research,
            410,
            "legacy_evidence_path_retired",
            "versioned research and explicit evidence review",
        )

        confirm = client.post(f"/component-packages/{uuid4()}/confirm")
        assert_error(
            confirm,
            410,
            "legacy_evidence_path_retired",
            "versioned research and explicit evidence review",
        )


def test_legacy_analysis_routes_fail_without_findings():
    with TestClient(app) as client:
        compile_response = client.post(f"/scenarios/{uuid4()}/compile")
        assert_error(
            compile_response,
            410,
            "legacy_analysis_path_retired",
            "legacy Python planning path",
        )

        run_response = client.post(f"/scenarios/{uuid4()}/runs")
        assert_error(
            run_response,
            501,
            "authoritative_execution_unavailable",
            "Program 01B",
        )
        assert "findings" not in run_response.json()
