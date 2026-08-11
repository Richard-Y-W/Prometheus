import os
os.environ["PROMETHEUS_DATABASE_URL"]="sqlite:///./test_prometheus.db"
from fastapi.testclient import TestClient
from app.main import app

def test_complete_workflow():
    with TestClient(app) as c:
        project=c.post("/projects",json={"name":"Motor arm"}).json()
        imported=c.post(f"/projects/{project['id']}/cad-imports?fixture=true").json()
        assert imported["status"] == "completed"
        research=c.post("/component-research",json={"manufacturer":"Fixture Motor Co","part_number":"PM-36-GM"}).json()
        package=research["component_package"]
        assert package["model_level"] == "behavioral" and package["evidence"]
        assert c.post(f"/component-packages/{package['id']}/confirm").json()["validation_status"] == "confirmed"
        assert c.post(f"/projects/{project['id']}/connections",json={"source_part":"motor","target_part":"arm"}).status_code == 201
        scenario=c.post(f"/projects/{project['id']}/scenarios",json={"natural_language_description":"Rotate 8 kg payload", "definition":{"payload_kg":8,"arm_length_m":.2,"rotation_deg":90,"movement_s":1.2,"hold_s":4,"cycle_s":10,"ambient_c":35}}).json()
        plan=c.post(f"/scenarios/{scenario['id']}/compile").json()
        assert "torque_speed" in plan["planned_checks"] and plan["omitted_checks"]
        run=c.post(f"/scenarios/{scenario['id']}/runs").json()
        summaries=[f["data"].get("summary") for f in run["findings"]]
        assert "Short movement feasible" in summaries
        assert "Continuous horizontal holding questionable" in summaries
        assert "Intermittent thermal behavior depends on duty cycle" in summaries
        assert run["manifest"]["claim_ids"]
