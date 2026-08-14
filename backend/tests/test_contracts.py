import copy
import hashlib
import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, FormatChecker, ValidationError
from pydantic import ValidationError as PydanticValidationError
from referencing import Registry, Resource

from app.contracts_v1 import ExecutionComponentPackage
from app.execution_packages import finalize_execution_component
from app.schemas import ScenarioDefinition


ROOT = Path(__file__).parents[2]
SCHEMA_DIR = ROOT / "schemas"
FIXTURE_DIR = ROOT / "fixtures" / "contracts"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def schema_registry() -> Registry:
    resources = []
    for path in sorted(SCHEMA_DIR.glob("*.schema.json")):
        schema = load_json(path)
        resources.append((schema["$id"], Resource.from_contents(schema)))
    return Registry().with_resources(resources)


def validate(schema_name: str, instance: dict) -> None:
    schema = load_json(SCHEMA_DIR / schema_name)
    Draft202012Validator(
        schema,
        registry=schema_registry(),
        format_checker=FormatChecker(),
    ).validate(instance)


def test_all_schemas_are_valid_draft_2020_12():
    for path in sorted(SCHEMA_DIR.glob("*.schema.json")):
        Draft202012Validator.check_schema(load_json(path))


@pytest.mark.parametrize(
    ("schema_name", "fixture_name"),
    [
        ("execution-component.schema.json", "execution-component.pm-36-gm.json"),
        ("finding.schema.json", "finding.motor-torque-fail.json"),
        ("finding.schema.json", "finding.backend-failed.json"),
    ],
)
def test_golden_contracts(schema_name: str, fixture_name: str):
    validate(schema_name, load_json(FIXTURE_DIR / fixture_name))


@pytest.mark.parametrize(
    "value",
    [
        {"kind": "scalar", "value": 36.0},
        {"kind": "range", "minimum": 0.55, "maximum": 0.82},
        {"kind": "enumeration", "values": ["air", "water"]},
        {
            "kind": "curve",
            "independent_quantity": "angular_velocity",
            "independent_unit": "rad/s",
            "interpolation": "linear",
            "points": [{"x": 0.0, "y": 1.92}, {"x": 418.879, "y": 0.0}],
        },
        {"kind": "unknown", "reason": "manufacturer did not state a value"},
    ],
)
def test_engineering_value_variants(value: dict):
    validate("engineering-value.schema.json", value)


def test_failed_execution_cannot_satisfy_an_obligation():
    finding = load_json(FIXTURE_DIR / "finding.backend-failed.json")
    invalid = copy.deepcopy(finding)
    invalid["obligation_status"] = "satisfied"
    with pytest.raises(ValidationError):
        validate("finding.schema.json", invalid)


def test_execution_fixture_content_hash_is_canonical():
    package = load_json(FIXTURE_DIR / "execution-component.pm-36-gm.json")
    validated = ExecutionComponentPackage.model_validate(package).model_dump(mode="json")
    payload = dict(package)
    expected = payload.pop("content_hash")

    assert finalize_execution_component(payload) == validated
    assert validated["content_hash"] == expected


@pytest.mark.parametrize(
    "mutate",
    [
        lambda package: package["parameters"].append(
            copy.deepcopy(package["parameters"][0])
        ),
        lambda package: package["evidence"].append(
            copy.deepcopy(package["evidence"][0])
        ),
        lambda package: package["parameters"][0].update(
            {"evidence_ids": ["55555555-5555-4555-8555-555555555555"]}
        ),
        lambda package: package["parameters"][4].update(
            {"value": {"kind": "range", "minimum": 0.9, "maximum": 0.2}}
        ),
        lambda package: package["parameters"][15]["value"].update(
            {"points": [{"x": 1.0, "y": 1.0}, {"x": 1.0, "y": 0.0}]}
        ),
        lambda package: package["parameters"][0].update(
            {"value": {"kind": "scalar", "value": float("nan")}}
        ),
        lambda package: package["evidence"][0].update(
            {"source_uri": "not a URI"}
        ),
        lambda package: package["evidence"][0]["review"].update(
            {"reviewed_by": "   "}
        ),
        lambda package: package["parameters"][0].update(
            {"validity_conditions": [""]}
        ),
    ],
)
def test_execution_model_rejects_noncanonical_or_unsafe_inputs(mutate):
    package = load_json(FIXTURE_DIR / "execution-component.pm-36-gm.json")
    mutate(package)

    with pytest.raises(PydanticValidationError):
        ExecutionComponentPackage.model_validate(package)


def test_execution_schema_rejects_a_whitespace_only_reviewer():
    package = load_json(FIXTURE_DIR / "execution-component.pm-36-gm.json")
    package["evidence"][0]["review"]["reviewed_by"] = "   "

    with pytest.raises(ValidationError):
        validate("execution-component.schema.json", package)


def test_execution_fixture_evidence_hashes_exact_source_bytes():
    package = load_json(FIXTURE_DIR / "execution-component.pm-36-gm.json")
    source = ROOT / "fixtures" / "evidence" / "pm-36-gm.synthetic.json"
    expected = f"sha256:{hashlib.sha256(source.read_bytes()).hexdigest()}"
    assert {item["source_document_hash"] for item in package["evidence"]} == {
        expected
    }


def test_scenario_rejects_a_cycle_shorter_than_move_and_hold():
    with pytest.raises(PydanticValidationError):
        ScenarioDefinition(
            payload_kg=8,
            arm_length_m=0.2,
            rotation_deg=90,
            movement_s=1.2,
            hold_s=4,
            cycle_s=4,
            ambient_c=35,
        )
