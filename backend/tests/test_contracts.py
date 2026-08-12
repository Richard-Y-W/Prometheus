import copy
import hashlib
import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, FormatChecker, ValidationError
from referencing import Registry, Resource


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
    expected = package.pop("content_hash")
    canonical = json.dumps(
        package,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    assert expected == f"sha256:{hashlib.sha256(canonical).hexdigest()}"


def test_execution_fixture_evidence_hashes_exact_source_bytes():
    package = load_json(FIXTURE_DIR / "execution-component.pm-36-gm.json")
    source = ROOT / "fixtures" / "evidence" / "pm-36-gm.synthetic.json"
    expected = f"sha256:{hashlib.sha256(source.read_bytes()).hexdigest()}"
    assert {item["source_document_hash"] for item in package["evidence"]} == {
        expected
    }
