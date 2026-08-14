from __future__ import annotations

import json
from pathlib import Path

from jsonschema import Draft202012Validator


ROOT = Path(__file__).parents[2]
SCHEMA_PATH = ROOT / "schemas/project-v2.schema.json"
FIXTURE_ROOT = ROOT / "fixtures/contracts/project-v2"


def _load(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def test_project_v2_schema_is_checked_in_and_closed() -> None:
    schema = _load(SCHEMA_PATH)
    Draft202012Validator.check_schema(schema)
    assert schema["$id"] == "urn:prometheus:schema:project:2.0.0"
    assert schema["additionalProperties"] is False
    assert set(schema["required"]) == set(schema["properties"])
    assert schema["properties"]["execution_store_version"]["const"] == "1.0.0"


def test_shared_valid_project_fixture_matches_schema() -> None:
    validator = Draft202012Validator(_load(SCHEMA_PATH))
    errors = list(validator.iter_errors(_load(FIXTURE_ROOT / "valid.minimal.json")))
    assert errors == []


def test_shared_invalid_project_fixtures_are_rejected() -> None:
    validator = Draft202012Validator(_load(SCHEMA_PATH))
    invalid = sorted((FIXTURE_ROOT / "invalid").glob("*.json"))
    assert invalid
    for path in invalid:
        assert list(validator.iter_errors(_load(path))), path.name


def test_project_schema_keeps_motor_results_out_of_mutable_engineering_state() -> None:
    schema = _load(SCHEMA_PATH)
    finding = schema["$defs"]["GeometryFindingV2"]
    assert finding["properties"]["finding_kind"]["enum"] == [
        "static_interference",
        "sampled_joint_sweep",
    ]
    assert "overall_project_works" not in schema["properties"]


def test_execution_references_are_full_five_field_objects() -> None:
    schema = _load(SCHEMA_PATH)
    reference = schema["$defs"]["StoredObjectReferenceV1"]
    assert reference["additionalProperties"] is False
    assert reference["required"] == [
        "object_hash",
        "byte_length",
        "media_type",
        "schema_id",
        "schema_version",
    ]
    assert schema["$defs"]["ExecutionIndexV1"]["properties"]["events"][
        "maxItems"
    ] == 256
