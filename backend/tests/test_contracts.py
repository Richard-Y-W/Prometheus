import json
from pathlib import Path
from jsonschema import Draft202012Validator, FormatChecker

ROOT=Path(__file__).parents[2]

def test_golden_finding_matches_canonical_contract():
    schema=json.loads((ROOT/"schemas/finding.schema.json").read_text())
    fixture=json.loads((ROOT/"fixtures/contracts/finding.motor-torque-fail.json").read_text())
    Draft202012Validator(schema,format_checker=FormatChecker()).validate(fixture)

def test_all_schemas_are_valid_draft_2020_12():
    for path in (ROOT/"schemas").glob("*.schema.json"):
        Draft202012Validator.check_schema(json.loads(path.read_text()))
