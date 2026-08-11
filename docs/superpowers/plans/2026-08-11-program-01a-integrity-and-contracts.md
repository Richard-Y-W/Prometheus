# Program 01A: Integrity and Generic Contracts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the current Prometheus repository truthful and safe at its trust boundary: fixture data cannot impersonate arbitrary research, evidence cannot be accepted implicitly, published component inputs are typed, canonical, and hash-verifiable, and failed or unavailable analysis cannot be represented as engineering success.

**Architecture:** Keep Python on the evidence-acquisition and package-compilation side of a versioned JSON boundary. A single exact synthetic fixture is the only provider in 01A. Human review publishes an immutable execution-component package; Python does not make an engineering decision. Qt displays and submits explicit per-field decisions. C++ remains the only Prometheus decision authority, but the existing fixed motor-arm checker remains a clearly labeled conformance demonstrator until Program 01B makes it consume the published package.

**Tech Stack:** Python 3.12, FastAPI, Pydantic 2, SQLAlchemy 2, Alembic, SQLite, JSON Schema Draft 2020-12, pytest, C++20, Qt 6/QML, CMake/CTest, TypeScript/Vite for the archived reference UI, Git.

---

## Scope and hard gates

This plan implements only Program 01A from the approved general-engineering platform design. It does not implement live web research, PDF parsing, Motor A versus Motor B execution, arbitrary component recipes, project-wide artifact intake, solver adapters, or any new engineering physics.

The implementation is complete only when all of these statements are true:

- The fixture provider accepts exactly `Prometheus Fixture Works / PM-36-GM` and always returns its canonical synthetic identity.
- A caller-supplied URL can never be stored as the source of fixture values.
- Unknown manufacturer or part-number requests fail with a structured error and create no manufacturer, component, revision, source, or job rows.
- Every parameter receives one explicit human decision; omitted, duplicated, unknown, or invalid decisions fail closed.
- `rejected` and `ambiguous` decisions require a note and block publication.
- Draft revisions have no content hash and cannot be exported as execution inputs.
- A published execution-component package contains typed values, reviewed evidence, missing information, limitations, and the `prometheus_cpp` decision-authority declaration.
- Canonical JSON bytes reproduce the stored SHA-256 hash.
- A failed, cancelled, timed-out, unavailable, or nonconverged analysis cannot validate as `satisfied`.
- The Qt application contains no method or environment switch that constructs accepted decisions for the user.
- The legacy Python physics route cannot issue authoritative-looking engineering findings.
- Documentation calls the repository a fixture-backed vertical demonstrator and does not claim general-project verification.

Program 01B begins only after these gates pass and an independent review confirms them.

## Contract conventions fixed by this plan

- JSON Schema version: Draft 2020-12.
- Contract version: `1.0.0`.
- Hash spelling: `sha256:` followed by 64 lowercase hexadecimal characters.
- Hash input: UTF-8 canonical JSON with sorted object keys, compact separators, finite numbers only, and the top-level `content_hash` member omitted.
- Parameter ordering: ascending by parameter `name`.
- Evidence ordering: ascending by evidence `id`.
- Timestamps: timezone-aware UTC RFC 3339 strings.
- Authority declaration: `engineering_decision_authority` is exactly `prometheus_cpp`; an execution-component package is reviewed input, not a verdict.
- Obligation outcomes: `satisfied`, `violated`, `indeterminate`, `not_applicable`, `not_evaluated`.
- Project summaries: `requirements_violated`, `no_violations_detected_within_scope`, `insufficient_coverage`, `analysis_blocked`.
- Execution states: `not_started`, `running`, `completed`, `failed`, `cancelled`, `timed_out`, `backend_unavailable`, `nonconverged`.

## Task 1: Make backend tests deterministic before changing behavior

**Files:**

- Create: `backend/tests/conftest.py`
- Modify: `backend/tests/test_api.py`
- Verify: `backend/tests/`

- [ ] Run the untouched backend suite and record the baseline.

```bash
cd backend
uv run pytest -q
```

Expected before implementation: 10 tests pass. If the count differs, record the exact count in the task notes before continuing; do not hide an existing failure.

- [ ] Create `backend/tests/conftest.py` so the test database is selected before any application import, every test starts from an empty schema, and the process-specific temporary database is removed at session end.

```python
import os
import tempfile
from pathlib import Path

import pytest

TEST_DATABASE_PATH = Path(tempfile.gettempdir()) / f"prometheus-pytest-{os.getpid()}.db"
os.environ["PROMETHEUS_DATABASE_URL"] = f"sqlite:///{TEST_DATABASE_PATH}"

from app import models, models_v1  # noqa: E402,F401
from app.database import Base, engine  # noqa: E402


@pytest.fixture(autouse=True)
def isolated_database():
    Base.metadata.drop_all(engine)
    Base.metadata.create_all(engine)
    yield
    Base.metadata.drop_all(engine)


@pytest.fixture(scope="session", autouse=True)
def remove_test_database():
    yield
    engine.dispose()
    TEST_DATABASE_PATH.unlink(missing_ok=True)
```

- [ ] Remove the module-level `PROMETHEUS_DATABASE_URL` mutation from `backend/tests/test_api.py`; `conftest.py` is now the only test-database owner.

- [ ] Run the suite twice, with the second run using reverse file order, to prove order-independent cleanup without adding another pytest plugin.

```bash
uv run pytest -q
uv run pytest -q tests/test_v1_research.py tests/test_physics.py tests/test_contracts.py tests/test_api.py
```

Expected: both runs pass and no `test_prometheus.db` appears under `backend/`.

- [ ] Commit the test isolation change.

```bash
git add backend/tests/conftest.py backend/tests/test_api.py
git commit -m "test: isolate backend database state"
```

## Task 2: Define domain-neutral value, evidence, package, and failure contracts

**Files:**

- Create: `schemas/engineering-value.schema.json`
- Create: `schemas/evidence-record.schema.json`
- Create: `schemas/execution-component.schema.json`
- Modify: `schemas/finding.schema.json`
- Create: `fixtures/evidence/pm-36-gm.synthetic.json`
- Create: `fixtures/contracts/execution-component.pm-36-gm.json`
- Create: `fixtures/contracts/finding.backend-failed.json`
- Modify: `fixtures/contracts/finding.motor-torque-fail.json`
- Modify: `backend/tests/test_contracts.py`

- [ ] Replace `backend/tests/test_contracts.py` with tests that load all schemas into one reference registry, validate every checked-in contract fixture, exercise every engineering-value variant, and enforce the failure-state invariant.

```python
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
    assert {item["source_document_hash"] for item in package["evidence"]} == {expected}
```

- [ ] Run the tests and confirm they fail because the new schemas and fixtures do not exist.

```bash
cd backend
uv run pytest -q tests/test_contracts.py
```

Expected: file-not-found failures naming `engineering-value.schema.json`, `execution-component.schema.json`, and the new fixtures.

- [ ] Create `schemas/engineering-value.schema.json` with exactly five discriminated object shapes:

| `kind` | Required members | Validation rule |
| --- | --- | --- |
| `scalar` | `value: number` | finite JSON number |
| `range` | `minimum: number`, `maximum: number` | Pydantic additionally enforces `minimum <= maximum` |
| `enumeration` | `values: array` | at least one unique string, number, or boolean |
| `curve` | `independent_quantity`, `independent_unit`, `interpolation`, `points` | at least two `{x, y}` numerical points; interpolation is `linear`, `step`, or `cubic` |
| `unknown` | `reason` | non-empty string |

Every branch must set `additionalProperties` to `false`. The schema `$id` is `https://prometheus.local/schemas/engineering-value/v1`.

- [ ] Create `schemas/evidence-record.schema.json`. Require:

```text
schema_version, id, evidence_class, source_document_id,
source_document_hash, source_uri, source_locator, excerpt,
confidence, extraction_method, review
```

Use these evidence classes:

```text
physically_validated, system_validated, manufacturer_stated,
supplier_stated, user_measured, user_provided, derived,
llm_inferred, synthetic_fixture, unknown
```

`source_document_hash` uses the same `sha256:` spelling as package hashes. `confidence` is a number from 0 through 1 or null; use null for the synthetic fixture so extraction certainty is not mistaken for physical validation. `review.status` is `pending`, `accepted`, `rejected`, or `ambiguous`. For `pending`, reviewer and reviewed time are null. For `accepted`, both are populated. For `rejected` and `ambiguous`, reviewer, reviewed time, and a non-empty note are required. Set `additionalProperties` to `false` at every object boundary. The `$id` is `https://prometheus.local/schemas/evidence-record/v1`.

- [ ] Create `schemas/execution-component.schema.json`. It is a reviewed input package, not a component verdict. Require this top-level shape:

```json
{
  "schema_version": "1.0.0",
  "package_kind": "component_execution_input",
  "revision_id": "UUID",
  "content_hash": "sha256:64-lowercase-hex",
  "component": {
    "id": "UUID",
    "manufacturer": "non-empty string",
    "part_number": "non-empty string",
    "revision": "non-empty string",
    "component_class": "non-empty string"
  },
  "certification": {
    "tier": "provisional",
    "status": "published",
    "published_at": "UTC date-time"
  },
  "parameters": [],
  "evidence": [],
  "supported_recipes": [],
  "missing_information": [],
  "limitations": [],
  "authority": {
    "package_role": "reviewed_input",
    "engineering_decision_authority": "prometheus_cpp"
  }
}
```

Each parameter requires `name`, `quantity`, `dimension`, `value`, `unit`, `original_value`, `original_unit`, `validity_conditions`, and `evidence_ids`. Reference the engineering-value schema by its absolute `$id`. Reference the evidence schema for evidence items. Require at least one evidence ID for every non-unknown parameter. Require at least one limitation for a provisional package. Set `additionalProperties` to `false` recursively. Do not add gearmotor-specific properties to the schema.

- [ ] Change `schemas/finding.schema.json` from presentation statuses (`pass`, `warning`, `fail`) to the approved obligation and execution vocabulary. Require both `obligation_status` and `execution_status`. Add schema conditions with these exact consequences:

```text
execution_status == not_started       -> obligation_status == not_evaluated
execution_status == completed         -> any approved obligation outcome
execution_status == running           -> obligation_status == not_evaluated
execution_status in failed, cancelled, timed_out,
                    backend_unavailable, nonconverged
                                      -> obligation_status == indeterminate
```

Keep severity independent from disposition. A violated requirement may be low or critical; an indeterminate analysis may still be high severity if it blocks a critical claim.

- [ ] Update `finding.motor-torque-fail.json`: rename `status` to `obligation_status`, set it to `violated`, and add `execution_status: completed`.

- [ ] Create `finding.backend-failed.json` as a high-severity, `indeterminate` finding with `execution_status: backend_unavailable`, no calculated value, a non-empty `missing_information` entry naming the backend, and an action that tells the user to install or configure it. Do not use a pass-like title or summary.

- [ ] Create `fixtures/evidence/pm-36-gm.synthetic.json` as the immutable source artifact for the Task 3 parameter map. It contains the canonical fixture identity, revision, parameter values, validity conditions, recipes, missing information, and limitations. It contains no review decisions, database IDs, or engineering verdict. The fixture catalog in Task 3 must read this artifact rather than maintain a second parameter dictionary.

- [ ] Create `execution-component.pm-36-gm.json` with fixed UUIDs and the synthetic fixture metadata defined in Task 3. Include scalar, range, curve, and unknown values so the golden package exercises generic shapes. It must say `synthetic_fixture`, use only `fixture://prometheus/pm-36-gm/fixture-1` as its source URI, include the SHA-256 of the exact bytes of `fixtures/evidence/pm-36-gm.synthetic.json`, and include this limitation verbatim:

```text
Synthetic conformance data; not a real manufacturer's datasheet and not suitable for a physical design decision.
```

The checked-in `content_hash` must equal the Task 6 canonical hash of the same object with `content_hash` removed.

- [ ] Run the contract tests.

```bash
cd backend
uv run pytest -q tests/test_contracts.py
```

Expected: all contract tests pass, including rejection of `backend_unavailable + satisfied`.

- [ ] Commit the contracts.

```bash
git add schemas/engineering-value.schema.json schemas/evidence-record.schema.json
git add schemas/execution-component.schema.json schemas/finding.schema.json
git add fixtures/evidence/pm-36-gm.synthetic.json
git add fixtures/contracts/execution-component.pm-36-gm.json
git add fixtures/contracts/finding.backend-failed.json fixtures/contracts/finding.motor-torque-fail.json
git add backend/tests/test_contracts.py
git commit -m "feat: define trusted execution contracts"
```

## Task 3: Replace identity-spoofable mock research with one exact fixture catalog

**Files:**

- Create: `backend/app/fixture_catalog.py`
- Modify: `backend/app/research.py`
- Modify: `backend/app/api_v1.py`
- Modify: `backend/app/main.py`
- Modify: `backend/app/schemas.py`
- Verify: `fixtures/evidence/pm-36-gm.synthetic.json`
- Create: `backend/tests/test_fixture_catalog.py`
- Modify: `backend/tests/test_v1_research.py`
- Modify: `backend/tests/test_api.py`

- [ ] Write catalog unit tests before moving fixture values.

```python
import pytest

from app.fixture_catalog import FixtureRequestError, get_fixture


def test_exact_fixture_lookup_returns_canonical_identity():
    fixture = get_fixture(" prometheus fixture works ", "pm-36-gm", None)
    assert fixture.manufacturer == "Prometheus Fixture Works"
    assert fixture.part_number == "PM-36-GM"
    assert fixture.source.uri == "fixture://prometheus/pm-36-gm/fixture-1"
    assert all(item.evidence_class == "synthetic_fixture" for item in fixture.parameters)
    assert fixture.source.document_hash == fixture.source_file_hash()


@pytest.mark.parametrize(
    ("manufacturer", "part_number"),
    [
        ("Other Company", "PM-36-GM"),
        ("Prometheus Fixture Works", "UNKNOWN"),
        ("", "PM-36-GM"),
    ],
)
def test_unknown_fixture_identity_fails_closed(manufacturer: str, part_number: str):
    with pytest.raises(FixtureRequestError) as error:
        get_fixture(manufacturer, part_number, None)
    assert error.value.code == "fixture_identity_not_found"


def test_fixture_mode_rejects_caller_supplied_source_url():
    with pytest.raises(FixtureRequestError) as error:
        get_fixture(
            "Prometheus Fixture Works",
            "PM-36-GM",
            "https://example.com/unrelated.pdf",
        )
    assert error.value.code == "fixture_source_url_not_allowed"
```

- [ ] Add API tests for both the versioned and legacy entry points. Assert the exact fixture succeeds. Assert arbitrary identity and caller URL requests return `422` with `detail.code`. Query row counts before and after each rejected request and assert no records were created.

- [ ] Run the focused tests and verify that arbitrary identity and URL tests fail against the current `mock_research` behavior.

```bash
cd backend
uv run pytest -q tests/test_fixture_catalog.py tests/test_v1_research.py tests/test_api.py
```

Expected: fixture-catalog import fails first; after the test file can import, the arbitrary-identity cases expose the current bug.

- [ ] Implement `backend/app/fixture_catalog.py` with frozen records and one normalized lookup key. Use a deep copy or immutable tuples so a request cannot mutate global fixture state.

```python
import hashlib
from copy import deepcopy
from dataclasses import dataclass
from pathlib import Path
from typing import Any


def normalize_identity(value: str) -> str:
    return "".join(character.lower() for character in value if character.isalnum())


class FixtureRequestError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class FixtureSource:
    uri: str
    title: str
    revision: str
    content_type: str
    rights_status: str
    document_hash: str


@dataclass(frozen=True)
class FixtureParameter:
    name: str
    quantity: str
    dimension: str
    value: dict[str, Any]
    unit: str
    original_value: str
    original_unit: str
    validity_conditions: tuple[str, ...]
    evidence_class: str = "synthetic_fixture"


@dataclass(frozen=True)
class ComponentFixture:
    fixture_id: str
    source_path: Path
    manufacturer: str
    part_number: str
    revision: str
    family: str
    component_class: str
    source: FixtureSource
    parameters: tuple[FixtureParameter, ...]
    supported_recipes: tuple[str, ...]
    missing_information: tuple[dict[str, str], ...]
    limitations: tuple[str, ...]

    def source_file_hash(self) -> str:
        digest = hashlib.sha256(self.source_path.read_bytes()).hexdigest()
        return f"sha256:{digest}"


def get_fixture(
    manufacturer: str,
    part_number: str,
    source_url: str | None,
) -> ComponentFixture:
    if source_url is not None:
        raise FixtureRequestError(
            "fixture_source_url_not_allowed",
            "Fixture mode cannot attribute synthetic values to a caller-supplied URL.",
        )
    key = (normalize_identity(manufacturer), normalize_identity(part_number))
    fixture = FIXTURES.get(key)
    if fixture is None:
        raise FixtureRequestError(
            "fixture_identity_not_found",
            "The offline fixture provider supports only Prometheus Fixture Works / PM-36-GM.",
        )
    return deepcopy(fixture)
```

Build `FIXTURES` by loading `fixtures/evidence/pm-36-gm.synthetic.json` once and validating its exact structure. Assign `FixtureSource.document_hash` from `ComponentFixture.source_file_hash()` at load time. Recompute and compare the hash on lookup so an artifact changed after process start fails closed. Do not store a self-referential hash inside the source artifact, and do not duplicate the numerical parameter table in Python.

- [ ] Define the single fixture with this parameter map. Do not add Motor B in 01A.

| Name | Quantity | Dimension | Value | Unit |
| --- | --- | --- | --- | --- |
| `nominal_voltage_v` | `voltage` | `electric_potential` | scalar `36.0` | `V` |
| `continuous_torque_nm` | `torque` | `torque` | scalar `0.208` | `N*m` |
| `stall_torque_nm` | `torque` | `torque` | scalar `1.92` | `N*m` |
| `torque_constant_nm_a` | `torque_constant` | `torque/electric_current` | scalar `0.0749` | `N*m/A` |
| `no_load_speed_rad_s` | `angular_velocity` | `angle/time` | scalar `418.879` | `rad/s` |
| `no_load_current_a` | `electric_current` | `electric_current` | scalar `0.18` | `A` |
| `winding_resistance_ohm` | `electrical_resistance` | `electric_resistance` | scalar `1.4` | `ohm` |
| `thermal_resistance_k_w` | `thermal_resistance` | `temperature/power` | scalar `3.2` | `K/W` |
| `thermal_capacitance_j_k` | `heat_capacity` | `energy/temperature` | scalar `110.0` | `J/K` |
| `maximum_temperature_c` | `temperature_limit` | `temperature` | scalar `125.0` | `degC` |
| `gear_ratio` | `ratio` | `dimensionless` | scalar `100.0` | `1` |
| `gearbox_efficiency_nominal` | `efficiency` | `dimensionless` | scalar `0.70` | `1` |
| `gearbox_efficiency_range` | `efficiency` | `dimensionless` | range `0.55` through `0.82` | `1` |
| `driver_current_limit_a` | `electric_current_limit` | `electric_current` | scalar `4.0` | `A` |
| `supply_current_limit_a` | `electric_current_limit` | `electric_current` | scalar `5.0` | `A` |
| `torque_speed_curve` | `torque_by_angular_velocity` | `torque` | linear curve `(0.0, 1.92)` to `(418.879, 0.0)` with independent unit `rad/s` | `N*m` |
| `gearbox_lifetime` | `service_life` | `time` | unknown: `synthetic fixture does not define gearbox lifetime` | `h` |

Every known parameter has `validity_conditions=("synthetic conformance fixture only",)`. Use the fixture URI and source title `Prometheus synthetic PM-36-GM fixture data`. Set rights status to `project_fixture`. Set supported recipes to `motor_torque_speed`, `motor_current`, `motor_continuous_hold`, and `motor_thermal_rc`. Add missing information for `gearbox_lifetime`. Use the exact synthetic-data limitation from Task 2.

- [ ] Move only `choose_claim` and `model_level` helpers to remain in `backend/app/research.py`. Delete `MOTOR_PARAMETERS`, `UNITS`, and `mock_research`. No production route may import `mock_research` after this task.

- [ ] In both research API entry points, call `get_fixture` before opening or mutating a database transaction. Convert `FixtureRequestError` to this exact error envelope:

```python
raise HTTPException(
    status_code=422,
    detail={
        "code": error.code,
        "message": str(error),
        "provider": "fixture",
        "supported_fixture_ids": ["prometheus-fixture-works/PM-36-GM"],
    },
)
```

- [ ] Always create `SourceDocument.source_url` from `fixture.source.uri`. Never read `body.source_url` after fixture validation. Set the document hash from the SHA-256 of the exact checked-in source-artifact bytes. A metadata-only or caller-identity hash is not acceptable.

- [ ] Run the focused tests and the full backend suite.

```bash
cd backend
uv run pytest -q tests/test_fixture_catalog.py tests/test_v1_research.py tests/test_api.py
uv run pytest -q
```

Expected: exact fixture succeeds; both spoofing paths fail with no database mutations; full suite passes.

- [ ] Commit the fixture boundary.

```bash
git add backend/app/fixture_catalog.py backend/app/research.py backend/app/api_v1.py
git add backend/app/main.py backend/app/schemas.py
git add backend/tests/test_fixture_catalog.py backend/tests/test_v1_research.py backend/tests/test_api.py
git commit -m "fix: make fixture provenance fail closed"
```

## Task 4: Migrate persistence from stringly values to typed reviewed records

**Files:**

- Create: `backend/migrations/versions/7b6d91e2a4f0_typed_execution_inputs.py`
- Modify: `backend/migrations/env.py`
- Modify: `backend/app/models.py`
- Modify: `backend/app/models_v1.py`
- Modify: `backend/app/database.py`
- Create: `backend/tests/test_migrations.py`
- Modify: `backend/tests/test_v1_research.py`

- [ ] Write a migration test that creates a temporary SQLite database, upgrades it to `cd418805b2c6`, inserts one old string-valued revision/parameter/evidence/event record, upgrades to `head`, and asserts:

```text
component_revisions.content_hash is nullable
component_revisions has supported_recipes_json, missing_information_json, limitations_json
component_parameters no longer has value_si
component_parameters has quantity, dimension, value_json, validity_conditions_json
value_json decodes to {"kind": "scalar", "value": 36.0}
evidence_records.confidence is numeric and equals 0.99
evidence_records has review_note
research_job_events.sequence is integer and equals 1
```

Use `Config.attributes["database_url"]` to give the Alembic environment the test URL. Do not mutate the process-wide application settings inside the migration test.

- [ ] Add a runtime database test that executes `PRAGMA foreign_keys` through the application engine and expects `1`, then attempts to insert a child with a missing parent and expects an integrity error.

- [ ] Run the tests and verify failure against the current schema.

```bash
cd backend
uv run pytest -q tests/test_migrations.py tests/test_v1_research.py
```

Expected: missing-column, wrong-type, and disabled-foreign-key failures.

- [ ] Make `backend/migrations/env.py` honor a programmatic migration URL while retaining application settings by default.

```python
database_url = config.attributes.get("database_url", settings.database_url)
config.set_main_option("sqlalchemy.url", database_url)
```

- [ ] Create revision `7b6d91e2a4f0`, with `down_revision = "cd418805b2c6"`. The upgrade must:

1. Add nullable JSON columns `supported_recipes_json`, `missing_information_json`, and `limitations_json` to `component_revisions`.
2. Add nullable `quantity`, `dimension`, `value_json`, and `validity_conditions_json` columns to `component_parameters`.
3. Read each legacy parameter row in Python, map known fixture fields using Task 3 metadata, parse the old scalar string with `decimal.Decimal`, and write canonical JSON. An unparseable legacy value becomes `{"kind":"unknown","reason":"legacy value is not numeric"}` and is never silently coerced to zero.
4. Backfill empty revision arrays as JSON arrays.
5. Make the new parameter columns non-null.
6. Drop `component_parameters.value_si` using Alembic batch mode.
7. Make `component_revisions.content_hash` nullable using Alembic batch mode.
8. Convert `evidence_records.confidence` from string to `Numeric(4, 3)` through a temporary column and add nullable `review_note`.
9. Convert `research_job_events.sequence` from string to integer through a temporary column.

The downgrade must restore the prior columns and serialize scalar values back to strings. It must reject downgrade with a clear exception if a row contains a range, enumeration, curve, or unknown value because those cannot be represented faithfully in the old schema.

- [ ] Update `backend/app/models_v1.py` to use SQLAlchemy `JSON` for structured fields, `Numeric(4, 3)` for confidence, and `Integer` for event order. Expose the database `value_json` member as the Python attribute `value`:

```python
value: Mapped[dict] = mapped_column("value_json", JSON)
validity_conditions: Mapped[list] = mapped_column(
    "validity_conditions_json",
    JSON,
    default=list,
)
```

Keep `original_value` and `original_unit` as source-preserving strings. Add `review_note` to `EvidenceRecord`. Make `ComponentRevision.content_hash` optional and add the three JSON metadata arrays.

- [ ] Update `Record.updated_at` in `backend/app/models.py` so mutations advance the timestamp.

```python
updated_at: Mapped[str] = mapped_column(String, default=now, onupdate=now)
```

- [ ] Enable SQLite foreign-key enforcement in `backend/app/database.py` with an engine connect event. Apply the listener only to SQLite connections.

```python
from sqlalchemy import create_engine, event


if settings.database_url.startswith("sqlite"):
    @event.listens_for(engine, "connect")
    def enable_sqlite_foreign_keys(dbapi_connection, _connection_record):
        cursor = dbapi_connection.cursor()
        cursor.execute("PRAGMA foreign_keys=ON")
        cursor.close()
```

- [ ] Update revision responses and tests to expose `value`, `quantity`, `dimension`, `unit`, and `validity_conditions`. Remove `value_si` and `unit_si` from the versioned API contract; `unit` is canonical for the value and need not pretend every offset unit is raw SI.

- [ ] Run migration round-trip and backend tests.

```bash
cd backend
uv run pytest -q tests/test_migrations.py
uv run pytest -q
```

Expected: migration upgrade test, FK test, and all backend tests pass.

- [ ] Commit the typed persistence change.

```bash
git add backend/migrations/versions/7b6d91e2a4f0_typed_execution_inputs.py
git add backend/migrations/env.py backend/app/models.py backend/app/models_v1.py backend/app/database.py
git add backend/tests/test_migrations.py backend/tests/test_v1_research.py
git commit -m "feat: persist typed reviewed component values"
```

## Task 5: Require a complete explicit review set

**Files:**

- Create: `backend/app/contracts_v1.py`
- Modify: `backend/app/api_v1.py`
- Modify: `backend/tests/test_v1_research.py`

- [ ] Add API tests for all review states before changing the endpoint:

```text
empty decisions                         -> 422 review_decisions_incomplete
one missing field                       -> 422 review_decisions_incomplete
duplicate field                         -> 422 duplicate_review_decision
unknown field                           -> 422 unknown_review_field
unsupported status                      -> Pydantic 422
ambiguous without note                  -> Pydantic 422
rejected without note                   -> Pydantic 422
all fields accepted                     -> reviewed; publication allowed
one field ambiguous with note           -> review_ambiguous; publication 409
one field rejected with note            -> review_rejected; publication 409
attempt to review published revision    -> 409 published_revision_immutable
```

Assert that a failed request does not partially update any evidence row.

- [ ] Run the focused test and observe the current partial-review and automatic-acceptance weaknesses.

```bash
cd backend
uv run pytest -q tests/test_v1_research.py
```

- [ ] Move all versioned request/response Pydantic models out of `api_v1.py` into `contracts_v1.py`. Define review decisions with a discriminated validator:

```python
from typing import Literal

from pydantic import BaseModel, Field, model_validator


class ResearchCreate(BaseModel):
    manufacturer: str = Field(min_length=1)
    part_number: str = Field(min_length=1)
    source_url: str | None = None


class ReviewDecision(BaseModel):
    field_name: str = Field(min_length=1)
    status: Literal["accepted", "rejected", "ambiguous"]
    note: str | None = None

    @model_validator(mode="after")
    def require_note_for_non_acceptance(self):
        if self.status in {"rejected", "ambiguous"} and not (self.note or "").strip():
            raise ValueError("rejected and ambiguous decisions require a note")
        return self


class ReviewRequest(BaseModel):
    reviewed_by: str = Field(min_length=1)
    decisions: list[ReviewDecision]
```

- [ ] Validate the complete decision set before changing any row. Compare lists and sets so duplicates cannot be hidden by dictionary construction. Use structured `HTTPException.detail` objects containing `code`, `message`, and the relevant `missing_fields`, `duplicate_fields`, or `unknown_fields` arrays.

- [ ] Apply review changes only after validation succeeds. Set `reviewed_by`, one captured UTC timestamp, status, and note on every evidence row for the field. Set job status according to the whole decision set:

```text
all accepted      -> reviewed
any rejected      -> review_rejected
otherwise         -> review_ambiguous
```

Allow a human to revise rejected or ambiguous decisions until publication. A published revision remains immutable.

- [ ] Keep publication fail-closed. A missing evidence row, pending decision, rejected decision, or ambiguous decision returns `409` with code `publication_review_incomplete`. Do not translate any of these states into accepted.

- [ ] Run review and full backend tests.

```bash
cd backend
uv run pytest -q tests/test_v1_research.py
uv run pytest -q
```

Expected: all review-state tests pass and no existing test regresses.

- [ ] Commit explicit review semantics.

```bash
git add backend/app/contracts_v1.py backend/app/api_v1.py backend/tests/test_v1_research.py
git commit -m "feat: require explicit complete evidence review"
```

## Task 6: Publish canonical execution-component packages

**Files:**

- Create: `backend/app/execution_packages.py`
- Modify: `backend/app/api_v1.py`
- Modify: `backend/app/contracts_v1.py`
- Modify: `backend/tests/test_v1_research.py`
- Modify: `backend/tests/test_contracts.py`
- Modify: `fixtures/contracts/execution-component.pm-36-gm.json`

- [ ] Add endpoint tests before implementing the builder:

```text
GET package for draft revision             -> 409 revision_not_published
publish before all accepted                -> 409 publication_review_incomplete
publish after all accepted                 -> 200 and non-null sha256 hash
GET published package twice                -> byte-equivalent canonical payloads
recompute hash without content_hash        -> equals stored content_hash
validate returned package against schema   -> passes
mutate persisted published parameter       -> 409 execution_package_hash_mismatch
```

Also assert that `content_hash` is null while the revision is draft and is assigned only in the publication transaction.

- [ ] Run the focused tests and confirm the endpoint is missing and draft hashes are currently premature.

```bash
cd backend
uv run pytest -q tests/test_v1_research.py tests/test_contracts.py
```

- [ ] Add Pydantic models mirroring the three new JSON Schemas. Use a discriminated union for engineering values and `FiniteFloat` for every numeric field. Add model validators for ordered ranges and strictly increasing curve x-coordinates. Reject duplicate parameter names, duplicate evidence IDs, and references to absent evidence. Model the unhashed form as `ExecutionComponentPayload`; model the exported form as `ExecutionComponentPackage`, which adds required `content_hash`. This avoids inventing a dummy hash during publication.

- [ ] Implement `backend/app/execution_packages.py` with one canonical serializer and hash function.

```python
import hashlib
import json
from typing import Any


def canonical_json_bytes(value: dict[str, Any]) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def content_hash(value: dict[str, Any]) -> str:
    payload = dict(value)
    payload.pop("content_hash", None)
    digest = hashlib.sha256(canonical_json_bytes(payload)).hexdigest()
    return f"sha256:{digest}"
```

- [ ] Implement `build_execution_component_payload(revision, db)` and `finalize_execution_component(payload, expected_hash=None)` with these rules:

1. Load component, manufacturer, parameters, source documents, and evidence in bounded queries.
2. Permit payload construction only inside the publication transaction after status and publication time are set, or for an already published revision.
3. Reject missing evidence or any evidence not explicitly accepted.
4. Sort parameters by name and evidence by ID.
5. Preserve typed value shapes and validity conditions without numerical conversion.
6. Include revision-level recipes, missing information, and limitations from persistence.
7. Declare the package role and `prometheus_cpp` authority exactly as the schema requires.
8. Validate the unhashed dictionary with `ExecutionComponentPayload`.
9. In `finalize_execution_component`, compute the hash from the payload, add it, and validate with `ExecutionComponentPackage`.
10. When `expected_hash` is provided, compare before returning and raise `execution_package_hash_mismatch` on disagreement.

- [ ] Change publication to this transaction order:

```text
validate all evidence accepted
set revision.status = published
set revision.published_at = captured UTC timestamp
flush, but do not commit
build canonical package payload without content_hash
finalize package and set revision.content_hash to its hash
flush and rebuild the payload from persisted rows
finalize again with expected_hash=revision.content_hash
set job.status = published
commit once
```

If package validation or hashing fails, roll back the entire transaction. A draft must not be left published without a valid hash.

- [ ] Add `GET /v1/component-revisions/{revision_id}/execution-package`. Return `404` for an unknown revision, structured `409` for a draft, and structured `409` for a hash mismatch. Do not auto-publish from the GET endpoint.

- [ ] Generate `fixtures/contracts/execution-component.pm-36-gm.json` from fixed test IDs through the same Pydantic model and canonical hash function. Do not hand-maintain a second hash algorithm.

- [ ] Run focused and full tests.

```bash
cd backend
uv run pytest -q tests/test_v1_research.py tests/test_contracts.py
uv run pytest -q
```

Expected: draft export is blocked, published export is stable and schema-valid, and tampering is detected.

- [ ] Commit canonical package publication.

```bash
git add backend/app/execution_packages.py backend/app/api_v1.py backend/app/contracts_v1.py
git add backend/tests/test_v1_research.py backend/tests/test_contracts.py
git add fixtures/contracts/execution-component.pm-36-gm.json
git commit -m "feat: publish canonical execution inputs"
```

## Task 7: Remove automatic acceptance from the Qt workflow

**Files:**

- Create: `desktop/app/review_payload.hpp`
- Create: `desktop/app/review_payload.cpp`
- Create: `desktop/app/tests/review_payload_tests.cpp`
- Modify: `desktop/app/service_controller.hpp`
- Modify: `desktop/app/service_controller.cpp`
- Modify: `desktop/app/main.cpp`
- Modify: `desktop/ui/Main.qml`
- Modify: `desktop/app/CMakeLists.txt`

- [ ] Write a Qt Core unit test for a pure review-payload builder. Cover:

```text
one explicit decision for every parameter -> valid payload
missing decision                          -> invalid
duplicate decision                        -> invalid
unknown field                             -> invalid
empty status                              -> invalid
ambiguous or rejected without note        -> invalid
accepted decisions generated from only a parameter list -> impossible API
```

The helper accepts the displayed parameter list, a user-produced decision list, and reviewer string. It never supplies a default status.

- [ ] Add `prometheus_review_payload_tests` in `desktop/app/CMakeLists.txt`. Link it to Qt Core and the review helper only. Gate it on `BUILD_TESTING`, not on `TARGET prometheus_cad`, so it runs anywhere the Qt application can build.

- [ ] Build the test and verify it fails because the helper does not exist.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --target prometheus_review_payload_tests
ctest --preset windows-debug -R prometheus_review_payload_tests
```

- [ ] Implement the pure helper. Return a result containing `ok`, `error`, and `QJsonObject payload`. Validate exact field-set equality and note rules before producing JSON.

- [ ] Replace `ServiceController::acceptAndPublish` with two separate invokable methods:

```cpp
Q_INVOKABLE void submitReview(
    const QVariantList& decisions,
    const QString& reviewer);
Q_INVOKABLE void publish();
```

`submitReview` calls the pure validator, posts only the supplied decisions, and updates state from the review response. `publish` is disabled by state unless the server reports `reviewed`; it makes only the publish request. Neither method loops over parameters to create decisions.

- [ ] Parse structured server error bodies in `ServiceController::fail` so the UI shows `detail.message` and retains `detail.code` for diagnostics instead of discarding the response body in favor of a generic network string.

- [ ] Remove all use of `PROMETHEUS_DEMO_AUTO_PUBLISH` and the `demoAutoPublish` QML context property from `desktop/app/main.cpp`. Keep `PROMETHEUS_DEMO_RESEARCH` only to open the exact fixture for a manual review demonstration.

- [ ] Replace the component evidence list controls in `Main.qml` with explicit per-field decisions:

```text
Review choice: Select decision / Accept / Ambiguous / Reject
Review note: required and visible for Ambiguous or Reject
Reviewer: editable non-empty field
Submit review: enabled only when every field has a valid explicit choice
Publish revision: separate button, enabled only when server status is reviewed
```

Store decisions in a QML `ListModel` initialized with empty status values when a new candidate arrives. Do not initialize any row to accepted. After a rejected or ambiguous review, allow edits and resubmission. After publication, make the decision controls read-only.

- [ ] Update value rendering for the Task 4 typed API:

```text
scalar       -> value plus unit
range        -> minimum–maximum plus unit
enumeration  -> joined values
curve        -> point count and independent unit
unknown      -> Unknown: reason
```

Do not coerce unknown, range, or curve values through `Number()`.

- [ ] Keep the fixed C++ motor-arm demo visibly labeled until 01B. Add this exact message anywhere the user can launch those checks:

```text
Conformance demo: the current C++ motor-arm checks still use fixed fixture inputs and do not yet consume this published package.
```

- [ ] Verify source removal mechanically.

```bash
rg -n "acceptAndPublish|PROMETHEUS_DEMO_AUTO_PUBLISH|demoAutoPublish" desktop
```

Expected: no matches.

- [ ] Run Qt tests and manually verify the state machine on Windows:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
$env:PROMETHEUS_DEMO_RESEARCH = '1'
.\out\build\windows-debug\desktop\app\prometheus_desktop.exe
```

Manual acceptance:

1. Research opens the exact synthetic fixture.
2. Publish is unavailable before review.
3. Submit review is unavailable with one undecided field.
4. Ambiguous without a note is unavailable.
5. An ambiguous decision with a note submits but publication remains blocked.
6. Changing every field to Accept and submitting enables Publish.
7. Publishing binds the revision only after the separate Publish action.

- [ ] Commit the explicit Qt review flow.

```bash
git add desktop/app/review_payload.hpp desktop/app/review_payload.cpp
git add desktop/app/tests/review_payload_tests.cpp desktop/app/service_controller.hpp
git add desktop/app/service_controller.cpp desktop/app/main.cpp desktop/app/CMakeLists.txt
git add desktop/ui/Main.qml
git commit -m "fix: require human evidence decisions in Qt"
```

## Task 8: Retire the legacy Python decision path

**Files:**

- Modify: `backend/app/main.py`
- Modify: `backend/app/physics.py`
- Modify: `backend/tests/test_api.py`
- Modify: `backend/tests/test_physics.py`
- Modify: `frontend/src/App.tsx`
- Modify: `frontend/src/api.ts`
- Modify: `frontend/src/types.ts`
- Modify: `frontend/src/logic.ts`
- Modify: `frontend/src/logic.test.ts`

- [ ] Rewrite `backend/tests/test_api.py` so the remaining unversioned project and fixture-CAD endpoints are tested as prototype utilities, while these legacy trust-sensitive routes fail explicitly:

```text
POST /component-research                  -> 410 legacy_evidence_path_retired
POST /component-packages/{id}/confirm     -> 410 legacy_evidence_path_retired
POST /scenarios/{id}/compile              -> 410 legacy_analysis_path_retired
POST /scenarios/{id}/runs                 -> 501 authoritative_execution_unavailable
```

The `501` detail must say that reviewed package-to-C++ execution is Program 01B work. It must not return findings, margins, or a pass-like status.

- [ ] Run `test_api.py` and confirm the current legacy endpoints still perform unreviewed confirmation and Python calculations.

```bash
cd backend
uv run pytest -q tests/test_api.py
```

- [ ] Replace the four route bodies with structured retirement errors. Do not silently redirect the old `ComponentPackage` table into the trusted versioned store; the models have different review semantics.

- [ ] Remove `analyze_motor_arm`, `center_of_gravity`, and `severity` imports from `backend/app/main.py`. Add a module docstring to `backend/app/physics.py` stating that it is retained only as a non-authoritative historical reference until deletion after 01B parity review. No production route may import it.

- [ ] Keep the mathematical unit tests temporarily, but rename their test descriptions to `reference_only` and assert no application module other than `test_physics.py` imports `app.physics`.

- [ ] Change the React reference UI into an explicitly archived viewer for this transition:

1. Add a persistent banner: `Archived rough-V1 interface — engineering execution is disabled while the reviewed C++ path is rebuilt.`
2. Remove calls to legacy research, confirm, compile, and run routes.
3. Disable the corresponding buttons with readable explanations.
4. Do not fabricate local findings to preserve the old screenshots.

- [ ] Add a TypeScript unit test that asserts the disabled-state explanation is returned for research and run actions. Keep `findingsBySeverity` and `shouldHighlight` only for rendering stored historical results.

- [ ] Run backend and frontend verification.

```bash
cd backend
uv run pytest -q
cd ../frontend
npm test
npm run build
```

Expected: legacy routes fail closed, the frontend builds, and no Python route emits an engineering finding.

- [ ] Verify the language boundary mechanically.

```bash
rg -n "from \.physics|import app\.physics|analyze_motor_arm\(" backend/app
```

Expected: matches may exist inside `backend/app/physics.py` definitions but not in `backend/app/main.py`, `api_v1.py`, or any route module.

- [ ] Commit legacy path retirement.

```bash
git add backend/app/main.py backend/app/physics.py backend/tests/test_api.py backend/tests/test_physics.py
git add frontend/src/App.tsx frontend/src/api.ts frontend/src/types.ts
git add frontend/src/logic.ts frontend/src/logic.test.ts
git commit -m "fix: retire non-authoritative Python analysis path"
```

## Task 9: Lock OpenAPI and make repository claims truthful

**Files:**

- Create: `backend/scripts/export_openapi.py`
- Create: `backend/tests/test_openapi.py`
- Modify: `docs/openapi-v1.json`
- Create: `docs/adr/0006-authoritative-analysis-backends.md`
- Create: `docs/program/00-master-roadmap.md`
- Create: `docs/program/01-trust-kernel/01a-integrity-and-contracts.md`
- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/product-scope.md`
- Modify: `docs/rough-v1-release-status.md`
- Modify: `docs/milestone-status.md`
- Modify: `docs/validation-policy.md`

- [ ] Add an OpenAPI exact-match test first.

```python
import json
from pathlib import Path

from app.main import app

ROOT = Path(__file__).parents[2]


def test_checked_in_openapi_matches_application():
    checked_in = json.loads((ROOT / "docs" / "openapi-v1.json").read_text(encoding="utf-8"))
    assert checked_in == app.openapi()
```

- [ ] Run it and confirm the checked-in document is stale after the API work.

```bash
cd backend
uv run pytest -q tests/test_openapi.py
```

- [ ] Create `backend/scripts/export_openapi.py` as the only generator.

```python
import json
from pathlib import Path

from app.main import app

ROOT = Path(__file__).parents[2]
OUTPUT = ROOT / "docs" / "openapi-v1.json"
OUTPUT.write_text(
    json.dumps(app.openapi(), indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
print(OUTPUT)
```

- [ ] Generate and verify OpenAPI.

```bash
cd backend
uv run python scripts/export_openapi.py
uv run pytest -q tests/test_openapi.py
```

- [ ] Write ADR-0006 with the approved authority boundary:

```text
Status: accepted
Prometheus C++ owns engineering decisions, review gates, applicability,
result validation, coverage, and findings. Each numerical analysis has one
declared versioned authoritative backend. External solvers return numerical
results and diagnostics; Python may acquire and package evidence but may not
issue an independent production verdict. Missing, failed, or nonconverged
backends fail to indeterminate or not evaluated, never satisfied.
```

- [ ] Create the program documents. `00-master-roadmap.md` links the approved design and all Programs 01 through 10. `01a-integrity-and-contracts.md` records the exact scope, gates, tests, limitations, and current commit. Mark 01B as the next gate; do not mark 01A complete until Task 10 verification succeeds.

- [ ] Rewrite the README opening and repository-status section around two separate statements:

```text
Product goal: a local project compiler and solver-orchestration environment
for heterogeneous engineering projects.

Current repository: a fixture-backed electromechanical vertical demonstrator
plus the Program 01A trust contracts. It cannot determine whether an arbitrary
engineering project works.
```

- [ ] Correct the other status documents. They must state all of the following:

1. Real STEP/XDE import exists and is distinct from the synthetic motor fixture.
2. Research is exact synthetic-fixture lookup, not public research or datasheet parsing.
3. Publication now requires explicit per-field review.
4. The execution-component package is not consumed by the C++ checker until 01B.
5. The fixed C++ motor-arm path is a conformance demonstrator, not arbitrary engineering support.
6. Python analysis endpoints are retired and Python is not an engineering decision authority.
7. No external solver adapter exists yet.
8. No claim of certification or project-wide correctness is made.

- [ ] Search for stale or dangerous claims and edit each hit in context.

```bash
rg -n "complete Prometheus thesis|researches and publishes|mock/offline research|Accept evidence and publish|works without an API key|Python.*engineering calculations|universal|arbitrary.*works" README.md docs backend frontend desktop
```

Expected: no misleading claim remains. Legitimate negated or historical references may remain only when clearly labeled.

- [ ] Commit API and documentation truthfulness.

```bash
git add backend/scripts/export_openapi.py backend/tests/test_openapi.py docs/openapi-v1.json
git add docs/adr/0006-authoritative-analysis-backends.md docs/program/00-master-roadmap.md
git add docs/program/01-trust-kernel/01a-integrity-and-contracts.md
git add README.md docs/architecture.md docs/product-scope.md docs/rough-v1-release-status.md
git add docs/milestone-status.md docs/validation-policy.md
git commit -m "docs: align repository claims with trust kernel"
```

## Task 10: Complete verification, independent review, and milestone record

**Files:**

- Modify: `docs/program/01-trust-kernel/01a-integrity-and-contracts.md`
- Modify only if verification finds defects: files changed in Tasks 1 through 9

- [ ] Run the complete backend suite from a clean test database.

```bash
cd backend
uv sync --extra dev
uv run pytest -q
```

Expected: every unit, API, migration, schema, hash, OpenAPI, and fail-closed test passes.

- [ ] Exercise a real Alembic upgrade on a temporary database outside the repository.

```bash
cd backend
PROMETHEUS_DATABASE_URL=sqlite:////tmp/prometheus-01a-verification.db uv run alembic upgrade head
PROMETHEUS_DATABASE_URL=sqlite:////tmp/prometheus-01a-verification.db uv run alembic current
```

Expected: current revision is `7b6d91e2a4f0`. Remove only `/tmp/prometheus-01a-verification.db` after recording the result.

- [ ] Run reference frontend verification.

```bash
cd frontend
npm ci
npm test
npm run build
npm audit --audit-level=high
```

Expected: tests and build pass; no high-severity audit finding.

- [ ] Run headless C++ verification.

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
```

Expected: core tests pass. Record that headless tests do not exercise Qt review UI or OCCT.

- [ ] Run native Windows verification on the documented Qt/Open Cascade toolchain.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

Expected: review-payload, core, CAD, and project tests pass.

- [ ] Run the source-integrity searches.

```bash
rg -n "acceptAndPublish|PROMETHEUS_DEMO_AUTO_PUBLISH|demoAutoPublish|mock_research" backend/app desktop frontend
rg -n "source_url\s*=\s*body\.source_url|body\.source_url\s+or\s+\"fixture:" backend/app
rg -n "from \.physics|analyze_motor_arm\(" backend/app/main.py backend/app/api_v1.py
```

Expected: no matches.

- [ ] Manually inspect one published package:

1. Create the exact fixture research job.
2. Confirm draft export returns `409`.
3. Submit one explicit decision per parameter.
4. Publish in a separate request.
5. Export the package twice.
6. Confirm identical canonical JSON and hash.
7. Confirm the source URI is the fixture URI and no caller URL appears.
8. Confirm the authority says `prometheus_cpp` and contains no verdict.

- [ ] Perform an independent code review focused on trust failures, not style. Review transaction atomicity, partial review mutation, hash exclusions, JSON reference validation, source attribution, legacy route reachability, Qt default state, and error-to-success conversions. Fix every critical or high-severity finding and rerun the affected verification commands.

- [ ] Update `docs/program/01-trust-kernel/01a-integrity-and-contracts.md` with:

```text
implemented behavior
verification commands and exact results
unsupported behavior
security implications
licensing implications
validation level: contract_tested
known false-positive and false-negative risks
exact final commit
reproduction instructions
Program 01B entry criteria
```

- [ ] Commit only verified corrections and the milestone record.

```bash
git add docs/program/01-trust-kernel/01a-integrity-and-contracts.md
git commit -m "chore: close Program 01A trust kernel"
```

If independent review required a correction, stage that correction by its exact path and commit it before the milestone record. Never use a repository-wide `git add` in a worktree that may contain user changes.

- [ ] Confirm final repository state.

```bash
git status --short
git log --oneline --decorate -12
```

Expected: clean worktree and a visible commit sequence for test isolation, contracts, fixture safety, typed persistence, explicit review, package publication, Qt review, legacy retirement, documentation, and milestone closure.

## Program 01A handoff to Program 01B

Do not treat 01A as evidence that Prometheus can evaluate an arbitrary project. Its output is a trustworthy input boundary.

Program 01B must begin with two immutable synthetic packages, Motor A and Motor B, that differ in a decision-relevant reviewed parameter. It must remove production PM-36 constants from `EngineeringController`, fetch or load the bound execution package, parse it in C++, prove the two packages produce different expected outcomes, persist the package and manifest for offline reopen, and demonstrate that Python never reproduces the calculation. That work requires its own approved implementation plan and acceptance gate.
