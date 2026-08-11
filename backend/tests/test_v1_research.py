from uuid import uuid4
from fastapi.testclient import TestClient
from app.main import app

def test_idempotent_research_review_publish_and_search():
    suffix=uuid4().hex[:8];manufacturer=f"Fixture Motors {suffix}";part=f"PM-{suffix}";key=f"research-{suffix}"
    with TestClient(app) as client:
        created=client.post("/v1/research-jobs",headers={"Idempotency-Key":key},json={"manufacturer":manufacturer,"part_number":part}).json()
        assert created["status"]=="ready_for_review" and len(created["events"])==8
        repeated=client.post("/v1/research-jobs",headers={"Idempotency-Key":key},json={"manufacturer":manufacturer,"part_number":part}).json()
        assert repeated["id"]==created["id"]
        search=client.get("/v1/components/search",params={"q":part,"page_size":5}).json()
        assert search["items"][0]["part_number"]==part and search["has_more"] is False
        component=client.get(f"/v1/components/{search['items'][0]['id']}").json();revision=component["revisions"][0]
        assert revision["status"]=="draft" and all(p["evidence"][0]["review_status"]=="pending" for p in revision["parameters"])
        blocked=client.post(f"/v1/research-jobs/{created['id']}/publish",headers={"Idempotency-Key":f"publish-{suffix}"})
        assert blocked.status_code==409
        decisions=[{"field_name":p["field_name"],"status":"accepted"} for p in revision["parameters"]]
        assert client.post(f"/v1/research-jobs/{created['id']}/review",json={"reviewed_by":"test-engineer","decisions":decisions}).status_code==200
        published=client.post(f"/v1/research-jobs/{created['id']}/publish",headers={"Idempotency-Key":f"publish-{suffix}"}).json()
        assert published["status"]=="published" and published["published_at"]
        assert client.post(f"/v1/research-jobs/{created['id']}/review",json={"reviewed_by":"test-engineer","decisions":decisions}).status_code==409
        cached=client.post("/v1/research-jobs",headers={"Idempotency-Key":f"research-cache-{suffix}"},json={"manufacturer":manufacturer,"part_number":part}).json()
        assert cached["status"]=="published" and cached["revision_id"]==published["id"]
        assert [event["stage"] for event in cached["events"]]==["cache_lookup","ready_for_review"]
