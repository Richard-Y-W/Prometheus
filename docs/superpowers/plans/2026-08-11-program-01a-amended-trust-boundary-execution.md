# Program 01A Amended Trust Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Execution mode is inline; do not dispatch subagents. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the former reconstructed v1 publication boundary with a v2 fixture-backed, claim-reviewed, immutable RFC 8785 package boundary that Python publishes and C++ independently verifies.

**Architecture:** Relational rows own mutable draft state. Publication locks one revision, compiles and validates a deterministic semantic value, stores its exact RFC 8785 bytes under their SHA-256 identity, and permanently binds the revision to that object. Export returns only verified stored bytes. The C++ integrity library independently parses, canonicalizes, and hashes the same package; the separate Qt-free C++ decision core owns project-summary derivation.

**Tech Stack:** Python 3.11–3.14, FastAPI, Pydantic 2, SQLAlchemy 2, Alembic, SQLite 3.35+, PostgreSQL 17, `rfc8785==0.1.4`, pytest, JSON Schema Draft 2020-12, C++20, nlohmann/json 3.12.0, Ryu, Qt 6, CMake/CTest, GitHub Actions.

**Normative product plan:** `docs/program/01-trust-kernel/01a-amended-implementation-plan.md`

**Approved design:** `docs/superpowers/specs/2026-08-11-program-01a-amended-trust-boundary-design.md`

---

### Task 1: Record that the former Program 01A gate is superseded

**Files:**

- Modify: `docs/superpowers/plans/2026-08-11-program-01a-integrity-and-contracts.md`
- Modify: `docs/program/01-trust-kernel/01a-integrity-and-contracts.md`
- Modify: `docs/program/00-master-roadmap.md`
- Modify: `docs/milestone-status.md`
- Modify: `README.md`
- Test: repository documentation searches

- [ ] **Step 1: Add a supersession banner to the old implementation plan**

Insert this immediately below its title:

```markdown
> **Historical plan — superseded 2026-08-11.** This plan was implemented under the former Program 01A gate. The approved [amended design](../specs/2026-08-11-program-01a-amended-trust-boundary-design.md) and [durable amended plan](../../program/01-trust-kernel/01a-amended-implementation-plan.md) now govern Program 01A. Keep this file as implementation history; do not execute it as the current plan.
```

- [ ] **Step 2: Mark the old completion record as historical without deleting its evidence**

Replace only its leading status block with:

```markdown
- Status: historical completion under the former gate; superseded and reopened 2026-08-11
- Originally closed: 2026-08-11
- Former validation level: `contract_tested`
- Verified former implementation commit: `a89ce37fb6e43f97d4df22ad6d1231f3a6bf20c7`
- Reopening authority: [Program 01A amended trust-boundary design](../../superpowers/specs/2026-08-11-program-01a-amended-trust-boundary-design.md)
- Current plan: [Program 01A amended implementation plan](01a-amended-implementation-plan.md)
```

Add this paragraph before `What this milestone establishes`:

```markdown
This document preserves what the repository implemented and tested at the time. Its former completion claim is superseded because publication reconstructed mutable rows, used a Python-specific canonical form, reviewed field labels rather than stable claims, and lacked durable concurrency/replay semantics. Those findings reopen Program 01A; they do not erase the recorded tests.
```

- [ ] **Step 3: Correct every current status page**

Use this exact current statement in `README.md`, `docs/program/00-master-roadmap.md`, and `docs/milestone-status.md`:

```text
Program 01A is reopened under the amended trust-boundary gate. The former fixture-backed v1 boundary remains historical evidence, but it is not sufficient for independently verifiable immutable publication. Program 01B has not started.
```

Retain all former milestone evidence and unsupported-behavior lists. Update links so the amended design and durable plan are adjacent to the historical record.

- [ ] **Step 4: Verify the truth boundary**

Run:

```bash
rg -n "Program 01A.*complete|01A creates a trustworthy input boundary|01B is the next gate" README.md docs
```

Expected: any remaining match is either inside a visibly labeled historical block or says that the former gate was complete at the time. No current-status paragraph calls amended Program 01A complete.

- [ ] **Step 5: Commit the coherent status correction**

```bash
git add README.md docs/program docs/milestone-status.md docs/superpowers/plans/2026-08-11-program-01a-integrity-and-contracts.md
git commit -m "docs: record amended Program 01A reopening"
```

Expected: the commit contains documentation only and leaves the worktree clean.

### Task 2: Align Python support and lock the v2 dependencies

**Files:**

- Modify: `backend/pyproject.toml`
- Modify: `backend/uv.lock`
- Create: `backend/tests/test_runtime_support.py`

- [ ] **Step 1: Write the runtime-metadata test**

Create `backend/tests/test_runtime_support.py`:

```python
from pathlib import Path

import tomllib


BACKEND_ROOT = Path(__file__).parents[1]


def test_declared_python_and_canonicalization_support():
    metadata = tomllib.loads(
        (BACKEND_ROOT / "pyproject.toml").read_text(encoding="utf-8")
    )["project"]
    assert metadata["requires-python"] == ">=3.11,<3.15"
    assert "rfc8785==0.1.4" in metadata["dependencies"]
    assert "psycopg[binary]>=3.2,<4" in metadata["dependencies"]
```

- [ ] **Step 2: Confirm the test fails on the former metadata**

Run:

```bash
cd backend
uv run pytest -q tests/test_runtime_support.py
```

Expected: FAIL because `requires-python` is only `>=3.11` and the two dependencies are absent.

- [ ] **Step 3: Change the declared range and dependencies**

Set this exact metadata in `backend/pyproject.toml`:

```toml
requires-python = ">=3.11,<3.15"
dependencies = [
  "fastapi>=0.115,<1",
  "uvicorn>=0.30,<1",
  "sqlalchemy>=2,<3",
  "alembic>=1.14,<2",
  "pydantic-settings>=2.5,<3",
  "python-multipart>=0.0.9,<1",
  "psycopg[binary]>=3.2,<4",
  "rfc8785==0.1.4",
]
```

Keep the existing dev dependencies. Regenerate the lock:

```bash
uv lock
```

Expected: `uv.lock` resolves all four supported Python minors and pins `rfc8785` 0.1.4.

- [ ] **Step 4: Run the metadata and former backend suites**

```bash
uv run pytest -q tests/test_runtime_support.py
uv run pytest -q
```

Expected: the metadata test passes; the former backend suite remains green before v2 behavior is introduced.

- [ ] **Step 5: Commit the runtime boundary**

```bash
git add backend/pyproject.toml backend/uv.lock backend/tests/test_runtime_support.py
git commit -m "build: define Program 01A runtime matrix"
```

### Task 3: Add the shared RFC 8785 corpus and strict Python canonicalizer

**Files:**

- Create: `backend/app/canonical_json.py`
- Create: `backend/tests/test_canonical_json.py`
- Create: `fixtures/conformance/rfc8785/manifest.json`
- Create: `fixtures/conformance/rfc8785/input/*.json`
- Create: `fixtures/conformance/rfc8785/canonical/*.jcs`
- Create: `fixtures/conformance/rfc8785/README.md`

- [ ] **Step 1: Check in the corpus manifest and exact case inventory**

Use this manifest structure. Ordinary cases name checked-in files; malformed-byte
and resource-limit cases use deterministic `hex` or `generated` input recipes
that both language harnesses implement identically:

```json
{
  "schema_version": "1.0.0",
  "canonicalization": "RFC8785+prometheus-negative-zero-rejection",
  "success_cases": [
    {"id": "empty-object", "input": "input/empty-object.json", "canonical": "canonical/empty-object.jcs", "sha256": "sha256:44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a"}
  ],
  "failure_cases": [
    {"id": "negative-zero", "input": "input/negative-zero.json", "error_code": "negative_zero"},
    {"id": "invalid-utf8", "input": {"kind": "hex", "data": "ff"}, "error_code": "invalid_utf8"},
    {"id": "depth-65", "input": {"kind": "generated", "generator": "nested_arrays", "depth": 65}, "error_code": "max_depth_exceeded"}
  ]
}
```

Keep the exact empty-object case and add the complete fixed inventory below. Compute each additional hash from its checked-in canonical bytes and require the test to recompute it.

```text
success:
  empty-object, rfc-literals, rfc-strings, rfc-numbers, utf16-property-order,
  unicode-values, control-escapes, nfc, nfd, nested-order,
  array-order, safe-integer-min, safe-integer-max,
  smallest-subnormal, largest-finite, fixed-threshold-low,
  fixed-threshold-high, scientific-positive, scientific-negative
failure:
  duplicate-key, duplicate-decoded-key, invalid-utf8, lone-high-surrogate,
  lone-low-surrogate, negative-zero, unsafe-integer-low,
  unsafe-integer-high, nan, positive-infinity, negative-infinity,
  overflow, nonzero-underflow, depth-65, nodes-100001,
  members-10001, array-10001, string-1048577, bytes-8388609
```

Also include `utf8-bom` and `unescaped-control-character` failure vectors. `README.md`
must identify which successful number cases came from RFC 8785 Appendix B and
identify all Prometheus-only failure policies. Generated inputs are corpus
contracts, not optional tests; the Python and C++ harnesses must construct the
same bytes and may not skip them.

- [ ] **Step 2: Write tests against the not-yet-existing API**

Create `backend/tests/test_canonical_json.py` with these public imports and assertions:

```python
import hashlib
import json
from pathlib import Path

import pytest

from app.canonical_json import (
    CanonicalJsonError,
    canonicalize_json_bytes,
    canonicalize_value,
    object_hash,
    verify_canonical_bytes,
)


ROOT = Path(__file__).parents[2]
CORPUS = ROOT / "fixtures" / "conformance" / "rfc8785"
MANIFEST = json.loads((CORPUS / "manifest.json").read_text(encoding="utf-8"))


@pytest.mark.parametrize("case", MANIFEST["success_cases"], ids=lambda case: case["id"])
def test_success_corpus(case):
    source = (CORPUS / case["input"]).read_bytes()
    expected = (CORPUS / case["canonical"]).read_bytes()
    assert canonicalize_json_bytes(source) == expected
    assert verify_canonical_bytes(expected) == expected
    assert object_hash(expected) == case["sha256"]
    assert case["sha256"] == f"sha256:{hashlib.sha256(expected).hexdigest()}"


@pytest.mark.parametrize("case", MANIFEST["failure_cases"], ids=lambda case: case["id"])
def test_failure_corpus(case):
    with pytest.raises(CanonicalJsonError) as caught:
        canonicalize_json_bytes((CORPUS / case["input"]).read_bytes())
    assert caught.value.code == case["error_code"]


def test_programmatic_values_use_the_same_preflight():
    with pytest.raises(CanonicalJsonError, match="negative zero"):
        canonicalize_value({"value": -0.0})


def test_verifier_rejects_valid_but_noncanonical_json():
    with pytest.raises(CanonicalJsonError) as caught:
        verify_canonical_bytes(b'{"b":2, "a":1}')
    assert caught.value.code == "noncanonical_bytes"
```

- [ ] **Step 3: Confirm the tests fail for the missing module**

```bash
cd backend
uv run pytest -q tests/test_canonical_json.py
```

Expected: collection fails with `ModuleNotFoundError: app.canonical_json`.

- [ ] **Step 4: Implement the strict parser and canonicalizer**

`backend/app/canonical_json.py` must export this exact interface:

```python
MAX_RAW_BYTES = 8 * 1024 * 1024
MAX_DEPTH = 64
MAX_NODES = 100_000
MAX_OBJECT_MEMBERS = 10_000
MAX_ARRAY_ELEMENTS = 10_000
MAX_STRING_BYTES = 1024 * 1024
SAFE_INTEGER_MIN = -(2**53) + 1
SAFE_INTEGER_MAX = 2**53 - 1


class CanonicalJsonError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
```

Export these exact signatures:

```text
parse_strict_json(source: bytes) -> object
canonicalize_value(value: object) -> bytes
canonicalize_json_bytes(source: bytes) -> bytes
verify_canonical_bytes(source: bytes) -> bytes
object_hash(source: bytes) -> str
```

The implementation rules are exact:

- reject a raw body above `MAX_RAW_BYTES` before decode;
- decode UTF-8 strictly and reject a BOM;
- use `json.JSONDecoder` callbacks that retain object pairs and reject both literal and decoded duplicate keys;
- parse number tokens through callbacks that detect non-finite conversion, nonzero underflow, negative zero, and unsafe integers before returning binary64-compatible values;
- walk programmatic and parsed values iteratively, counting the root as one node and enforcing depth, member, element, and UTF-8 string-byte limits;
- reject Python `Decimal`, arbitrary objects, surrogate-containing strings, and non-string object keys;
- call `rfc8785.dumps` only after preflight;
- require emitted canonical bytes to pass the same strict parser, so an
  exponent-form binary64 value cannot serialize into a disallowed unsafe-integer
  token;
- in `verify_canonical_bytes`, canonicalize the parsed bytes and require exact byte equality;
- hash only already-verified byte strings with `hashlib.sha256`.

Use explicit branches; do not normalize Unicode and do not convert negative zero to positive zero.

- [ ] **Step 5: Run the canonicalization suite**

```bash
uv run pytest -q tests/test_canonical_json.py
```

Expected: every success and failure case passes. No corpus case is skipped.

- [ ] **Step 6: Commit Python canonicalization and the shared corpus**

```bash
git add backend/app/canonical_json.py backend/tests/test_canonical_json.py fixtures/conformance/rfc8785
git commit -m "feat: define strict RFC 8785 package identity"
```

### Task 4: Define and generate the v2 contracts

**Files:**

- Create: `backend/app/contracts_v2.py`
- Create: `backend/scripts/export_contract_fixture.py`
- Create: `backend/scripts/export_contract_schemas.py`
- Create: `backend/tests/test_contracts_v2.py`
- Create: `schemas/engineering-value-v2.schema.json`
- Create: `schemas/evidence-record-v2.schema.json`
- Create: `schemas/review-request-v2.schema.json`
- Create: `schemas/publication-request-v2.schema.json`
- Create: `schemas/execution-component-v2.schema.json`
- Create: `schemas/project-summary-v2.schema.json`
- Create: `fixtures/contracts/execution-component-v2.pm-36-gm.json`
- Create: `fixtures/contracts/execution-component-v2.pm-36-gm.jcs`
- Create: `fixtures/contracts/execution-component-v2.pm-36-gm.sha256`

- [ ] **Step 1: Write the contract tests first**

Create `backend/tests/test_contracts_v2.py`. The core tests must use these assertions:

```python
import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator
from pydantic import ValidationError

from app.canonical_json import canonicalize_value, object_hash
from app.contracts_v2 import ExecutionComponentV2, ProjectSummaryV2


ROOT = Path(__file__).parents[2]


def test_checked_in_schemas_are_current(tmp_path):
    from scripts.export_contract_schemas import render_schemas

    rendered = render_schemas()
    for name, payload in rendered.items():
        checked_in = (ROOT / "schemas" / name).read_bytes()
        assert checked_in == payload
        Draft202012Validator.check_schema(json.loads(payload))


def test_complete_package_vector_is_exact():
    semantic = json.loads(
        (ROOT / "fixtures/contracts/execution-component-v2.pm-36-gm.json")
        .read_text(encoding="utf-8")
    )
    validated = ExecutionComponentV2.model_validate(semantic).model_dump(
        mode="json", by_alias=True
    )
    expected = (ROOT / "fixtures/contracts/execution-component-v2.pm-36-gm.jcs").read_bytes()
    expected_hash = (
        ROOT / "fixtures/contracts/execution-component-v2.pm-36-gm.sha256"
    ).read_text(encoding="ascii").strip()
    assert canonicalize_value(validated) == expected
    assert object_hash(expected) == expected_hash
    assert "content_hash" not in validated


def test_summary_keeps_verdict_coverage_and_execution_independent():
    summary = ProjectSummaryV2.model_validate(
        {
            "schema_version": "2.0.0",
            "verdict": "requirements_violated",
            "coverage": "insufficient",
            "execution_state": "completed_with_blocked_work",
            "counts": {
                "satisfied_within_scope": 8,
                "violated": 2,
                "indeterminate": 3,
                "not_applicable": 1,
                "not_evaluated": 5,
            },
            "obligation_total": 19,
            "assessment_scope_id": "sha256:" + "0" * 64,
            "decision_core": {"name": "prometheus_cpp", "version": "1.0.0"},
        }
    )
    assert summary.verdict == "requirements_violated"


def test_summary_rejects_unscoped_satisfaction():
    with pytest.raises(ValidationError):
        ProjectSummaryV2.model_validate(
            {
                "schema_version": "2.0.0",
                "verdict": "satisfied_within_scope",
                "coverage": "insufficient",
                "execution_state": "completed_with_blocked_work",
                "counts": {
                    "satisfied_within_scope": 1,
                    "violated": 0,
                    "indeterminate": 0,
                    "not_applicable": 0,
                    "not_evaluated": 1,
                },
                "obligation_total": 2,
                "assessment_scope_id": "sha256:" + "0" * 64,
                "decision_core": {"name": "prometheus_cpp", "version": "1.0.0"},
            }
        )
```

Add parametrized cases for all five evidence classes, all engineering-value variants, known/unknown claim exclusivity, UUIDv4 lowercase spelling, evidence-class conditional fields, array ordering, unsupported schema ID/version, reviewer/note byte limits, and the allowed closed vocabularies.

- [ ] **Step 2: Confirm the new tests fail at import**

```bash
cd backend
uv run pytest -q tests/test_contracts_v2.py
```

Expected: collection fails because `app.contracts_v2` does not exist.

- [ ] **Step 3: Implement the closed Pydantic model graph**

All v2 models inherit from:

```python
class ContractV2(BaseModel):
    model_config = ConfigDict(extra="forbid", populate_by_name=True)
```

Use these exact public request types and literals:

```python
SCHEMA_ID = "urn:prometheus:schema:execution-component:2.0.0"
SCHEMA_VERSION = "2.0.0"
PACKAGE_MEDIA_TYPE = (
    "application/vnd.prometheus.execution-component+json;version=2.0.0"
)

EvidenceClass = Literal[
    "manufacturer_document",
    "private_upload",
    "user_measurement",
    "derived_claim",
    "validation_observation",
]
ReviewDecision = Literal["accepted", "rejected", "ambiguous"]
GatePhase = Literal["publication", "execution"]
GateState = Literal["pending", "satisfied", "blocked"]


class ClaimReviewDecisionV2(ContractV2):
    claim_id: UUID
    status: ReviewDecision
    note: str


class ReviewRequestV2(ContractV2):
    expected_draft_version: int = Field(ge=0)
    reviewed_by: str
    decisions: list[ClaimReviewDecisionV2] = Field(min_length=1, max_length=1000)


class PublicationRequestV2(ContractV2):
    expected_draft_version: int = Field(ge=0)
    schema_id: Literal["urn:prometheus:schema:execution-component:2.0.0"]
    schema_version: Literal["2.0.0"]
```

Validate reviewer labels after trimming to 1–256 UTF-8 bytes and every note to 1–4096 UTF-8 bytes. Reject duplicate claim IDs in `ReviewRequestV2`. UUID serializers emit lowercase hyphenated text and validators reject non-v4 IDs.

Use strict scalar field types and before-validators at trust-sensitive members so booleans cannot become integers, numeric strings cannot become numbers, and arbitrary strings cannot become enums. Keep global coercion disabled selectively rather than using a model setting that prevents FastAPI JSON UUID strings from being validated.

Use discriminated models for scalar, range, enumeration, curve, exact-decimal-string, and unknown values. JSON numeric values must remain within the canonicalizer policy; exact decimal strings use the grammar `-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?` and are never silently converted to binary64.

Implement `EvidenceRecordV2` as a discriminated union with class-specific required fields. `private_upload` requires `artifact_hash`, `local_provenance`, `source_authority`, and `physical_validation_status`; it does not require URI, page, or excerpt. `extraction_confidence` is optional and constrained to `[0,1]`.

Implement `ExecutionComponentV2` with `schema_id: Literal["urn:prometheus:schema:execution-component:2.0.0"] = Field(alias="$schema")`, the exact top-level members from the durable plan, and no `content_hash`. Validators enforce stable ordering, one selected claim per emitted slot, same-package references, known/unknown shapes, effective accepted reviews, and publication/execution gate separation. Every package dump used for canonicalization passes `by_alias=True` so the stored member is `$schema`, never `schema_id`.

Implement `ProjectSummaryV2` as a validator only. It must check count sum, violation dominance, coverage consistency, and fail-closed satisfaction; it must not expose a function that derives verdict or coverage.

- [ ] **Step 4: Add deterministic schema export**

`backend/scripts/export_contract_schemas.py` must export:

```python
def render_schemas() -> dict[str, bytes]:
    """Return filename -> UTF-8 JSON Schema bytes with sorted keys and final newline."""


def main() -> int:
    for filename, payload in render_schemas().items():
        (ROOT / "schemas" / filename).write_bytes(payload)
    return 0
```

Patch the generated project-summary schema with Draft 2020-12 `allOf` conditions mirroring the Pydantic consistency rules. Use schema IDs `urn:prometheus:schema:<contract-name>:2.0.0`.

- [ ] **Step 5: Generate schemas and the exact complete-package vector**

Run:

```bash
uv run python scripts/export_contract_schemas.py
uv run python scripts/export_contract_fixture.py
uv run pytest -q tests/test_contracts_v2.py
```

Expected: schemas self-validate, regeneration is byte-stable, and the `.json`, `.jcs`, and `.sha256` fixture trio agrees. The fixture has fixed valid UUIDv4 values, synthetic-source limitations, four satisfied publication gates, at least one blocked execution gate, and `authority_role=input_only`.

- [ ] **Step 6: Commit the v2 contracts**

```bash
git add backend/app/contracts_v2.py backend/scripts/export_contract_fixture.py backend/scripts/export_contract_schemas.py backend/tests/test_contracts_v2.py
git add schemas/*-v2.schema.json fixtures/contracts/execution-component-v2.pm-36-gm.*
git commit -m "feat: define Program 01A v2 contracts"
```

### Task 5: Implement the Qt-free C++ project-summary decision core

**Files:**

- Create: `desktop/core/include/prometheus/decision/project_summary.hpp`
- Create: `desktop/core/src/project_summary.cpp`
- Create: `desktop/core/tests/project_summary_tests.cpp`
- Modify: `desktop/core/CMakeLists.txt`

- [ ] **Step 1: Write the summary truth-table test**

The test executable must cover these exact rows:

```cpp
struct Case {
  Counts counts;
  ExecutionState execution;
  Verdict verdict;
  Coverage coverage;
};

const std::vector<Case> cases{
  {{8,2,3,1,5}, ExecutionState::completed_with_blocked_work,
   Verdict::requirements_violated, Coverage::insufficient},
  {{3,0,0,0,0}, ExecutionState::completed,
   Verdict::satisfied_within_scope, Coverage::sufficient},
  {{2,0,1,0,0}, ExecutionState::completed,
   Verdict::indeterminate, Coverage::insufficient},
  {{0,0,0,4,0}, ExecutionState::not_started,
   Verdict::indeterminate, Coverage::not_assessed},
  {{0,0,0,0,2}, ExecutionState::failed,
   Verdict::indeterminate, Coverage::insufficient},
};
```

Also test count overflow, sum mismatch, invalid hash spelling, and any attempt to produce satisfaction in `blocked`, `failed`, `cancelled`, or `completed_with_blocked_work` state.

- [ ] **Step 2: Confirm the target fails before implementation**

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
```

Expected after registering the new test target: compilation fails because `prometheus/decision/project_summary.hpp` is missing.

- [ ] **Step 3: Add the public decision-core interface**

Use this exact namespace and API:

```cpp
namespace prometheus::decision {

enum class Verdict { satisfied_within_scope, requirements_violated, indeterminate };
enum class Coverage { sufficient, insufficient, not_assessed };
enum class ExecutionState {
  not_started, ready, running, blocked, completed,
  completed_with_blocked_work, failed, cancelled
};

struct Counts final {
  std::uint64_t satisfied_within_scope{};
  std::uint64_t violated{};
  std::uint64_t indeterminate{};
  std::uint64_t not_applicable{};
  std::uint64_t not_evaluated{};
};

struct ProjectSummary final {
  Verdict verdict;
  Coverage coverage;
  ExecutionState execution_state;
  Counts counts;
  std::uint64_t obligation_total;
  std::string assessment_scope_id;
  std::string decision_core_name{"prometheus_cpp"};
  std::string decision_core_version{"1.0.0"};
};

[[nodiscard]] ProjectSummary summarize(
    Counts counts,
    ExecutionState execution_state,
    std::uint64_t obligation_total,
    std::string assessment_scope_id);

} // namespace prometheus::decision
```

The implementation checks addition without overflow and requires the sum to equal `obligation_total`. Violations dominate verdict. Zero applicable obligations yields `not_assessed` and `indeterminate`. Any applicable indeterminate/not-evaluated count yields insufficient coverage and an indeterminate verdict absent violations. Satisfaction requires `completed`, at least one applicable obligation, and only satisfied/not-applicable counts.

- [ ] **Step 4: Convert `prometheus_core` from header-only interface to a static library**

Update `desktop/core/CMakeLists.txt` so `src/project_summary.cpp` is compiled, public headers stay visible, C++20 remains required, and both the former core tests and new `prometheus_project_summary_tests` link without Qt.

- [ ] **Step 5: Run the headless suite**

```bash
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
```

Expected: former core tests and every project-summary test pass; the target has no Qt link dependency.

- [ ] **Step 6: Commit the decision core**

```bash
git add desktop/core
git commit -m "feat: add multidimensional C++ project summary"
```

### Task 6: Add the v2 persistence graph and migration invariants

**Files:**

- Create: `backend/app/db_types.py`
- Create: `backend/app/models_v2.py`
- Create: `backend/migrations/versions/a41f0c93e2d7_amended_trust_boundary_v2.py`
- Create: `backend/tests/test_migrations_v2.py`
- Modify: `backend/app/models_v1.py`
- Modify: `backend/migrations/env.py`
- Modify: `backend/tests/conftest.py`

- [ ] **Step 1: Write fresh-install and upgrade tests**

`backend/tests/test_migrations_v2.py` must create independent SQLite databases through Alembic and, when `PROMETHEUS_TEST_POSTGRES_URL` is set, repeat the semantic cases against PostgreSQL 17. Start with these assertions:

```python
HEAD_REVISION = "a41f0c93e2d7"


def test_upgrade_preserves_and_classifies_legacy_publication(migrated_legacy_db):
    row = migrated_legacy_db.execute(
        sa.text(
            "SELECT content_hash, published_object_hash, publication_integrity "
            "FROM component_revisions WHERE id='revision-1'"
        )
    ).mappings().one()
    assert row["content_hash"] == "legacy-hash"
    assert row["published_object_hash"] is None
    assert row["publication_integrity"] == "legacy_unsealed"


def test_direct_invalid_confidence_is_rejected(v2_connection):
    with pytest.raises(sa.exc.IntegrityError):
        v2_connection.execute(
            sa.text(
                "INSERT INTO evidence_records_v2 "
                "(id, revision_id, evidence_class, extraction_confidence, created_at) "
                "VALUES (:id, :revision, 'private_upload', 1.01, :created_at)"
            ),
            valid_minimum_evidence_row(),
        )
```

Add direct-write cases for every closed state, known/unknown shape, duplicate slot, duplicate current selection, cross-revision slot/claim/evidence/review references, link insertion after claim finalization, selection/review of an unfinalized claim, a second finalization transition, draft publication fields, sealed field presence/equality, UTC timestamp shape, and immutable update/delete/repoint operations.

- [ ] **Step 2: Confirm the migration tests fail at the old head**

```bash
cd backend
uv run pytest -q tests/test_migrations_v2.py
```

Expected: FAIL because revision `a41f0c93e2d7` and v2 tables do not exist.

- [ ] **Step 3: Define v2 model tables with exact names**

`backend/app/models_v2.py` defines:

```text
artifact_objects_v2
parameter_slots_v2
candidate_claims_v2
evidence_records_v2
claim_evidence_links_v2
evidence_parent_claims_v2
evidence_parent_evidence_v2
claim_selections_v2
claim_review_events_v2
capability_gates_v2
published_objects
fixture_ingestion_jobs_v2
publication_requests
```

Use lowercase UUIDv4 string primary keys for domain records and `sha256:` strings for object primary keys. Add composite unique keys `(revision_id,id)` to slots, claims, evidence, reviews, and gates so composite foreign keys enforce same-revision relationships. Candidate claims also have unique `(revision_id,slot_id,id)` for the selection foreign key. `claim_selections_v2` has unique `(revision_id,slot_id)` and a composite foreign key `(revision_id,slot_id,claim_id)` to the candidate. `publication_requests` has unique `(operation,idempotency_key)`.

Candidate claims begin with `finalized=false`, a null fingerprint, and immutable semantic content. Evidence links may be inserted only while false. One allowed update sets the computed fingerprint and `finalized=true`; every later semantic/link update or deletion fails. Selection and review triggers require a finalized claim. This construction phase prevents a link added after fingerprinting from changing the reviewed assertion.

Add these nullable columns to `ComponentRevision` in `models_v1.py`:

```python
draft_version: Mapped[int | None]
contract_schema_id: Mapped[str | None]
contract_schema_version: Mapped[str | None]
publication_integrity: Mapped[str | None]
published_object_hash: Mapped[str | None]
```

V1 rows have null contract identity. Historical published rows are `legacy_unsealed`. V2 rows use the exact schema ID/version and may be draft with no publication fields or published with `sealed_v2`, equal content/object hashes, and a UTC publication time.

Change `ComponentRevision.published_at` to the dialect-aware `UtcTimestamp` type during migration. Preserve parseable legacy values as aware UTC instants and abort the migration on an invalid or naive legacy publication timestamp rather than guessing a timezone.

- [ ] **Step 4: Implement dialect-correct UTC storage**

`backend/app/db_types.py` exposes `UtcTimestamp`, a SQLAlchemy `TypeDecorator` that selects `TIMESTAMP(timezone=True)` on PostgreSQL and validated RFC 3339 text ending in `Z` on SQLite. It rejects naive datetimes on bind and returns aware UTC datetimes on read. The migration installs SQLite timestamp checks/triggers and relies on PostgreSQL `TIMESTAMPTZ` type enforcement.

- [ ] **Step 5: Implement the migration and immutable triggers**

The upgrade order is:

```text
1. create artifact_objects_v2 and published_objects
2. add conditional v2 columns to component_revisions
3. classify former published rows legacy_unsealed
4. create slots, claims, evidence, dependency links, selections, reviews, gates
5. create fixture_ingestion_jobs_v2 and publication_requests
6. add closed-state and same-revision constraints
7. add SQLite and PostgreSQL immutable update/delete/repoint triggers
```

Triggers reject semantic update/deletion of artifacts, candidate claims, claim-evidence links, review events, and published objects. They reject any change to a sealed revision's contract identity, object reference, content hash, publication classification, or publication time. The downgrade raises a clear `RuntimeError` if sealed v2 rows exist; it may downgrade an unused schema and must preserve former v1 rows.

- [ ] **Step 6: Make tests use migrated schemas**

Refactor `backend/tests/conftest.py` so trust-boundary tests run against `alembic upgrade head`, not `Base.metadata.create_all`. SQLite gets a process-specific temporary file; PostgreSQL uses the explicit test URL. Cleanup deletes rows or drops the isolated test schema without dropping a user database.

- [ ] **Step 7: Run migration and former backend tests**

```bash
uv run pytest -q tests/test_migrations.py tests/test_migrations_v2.py
uv run pytest -q
```

Expected: fresh install, former upgrade/downgrade tests, v2 direct-write constraints, and all former behavior pass. No PostgreSQL case is silently skipped when `PROMETHEUS_TEST_POSTGRES_URL` is present.

- [ ] **Step 8: Commit the v2 database boundary**

```bash
git add backend/app/db_types.py backend/app/models_v1.py backend/app/models_v2.py
git add backend/migrations backend/tests/conftest.py backend/tests/test_migrations_v2.py
git commit -m "feat: enforce v2 trust invariants in storage"
```

### Task 7: Ingest the exact fixture artifact and build the v2 draft graph

**Files:**

- Create: `backend/app/artifact_store.py`
- Create: `backend/app/fixture_pipeline_v2.py`
- Create: `backend/tests/test_artifact_store.py`
- Create: `backend/tests/test_fixture_pipeline_v2.py`
- Create: `fixtures/evidence/pm-36-gm.synthetic-v2.json`
- Modify: `backend/app/config.py`

- [ ] **Step 1: Write artifact lifecycle and attack tests**

Use this public service interface in `backend/tests/test_artifact_store.py`:

```python
from app.artifact_store import ArtifactIngestionError, ingest_local_artifact


def test_external_deletion_does_not_change_ingested_bytes(db, source_file, expected_hash):
    stored = ingest_local_artifact(
        db,
        source_path=source_file,
        allowed_root=source_file.parent,
        expected_hash=expected_hash,
        media_type="application/json",
    )
    source_file.unlink()
    assert stored.payload_bytes == FIXTURE_BYTES
    assert stored.object_hash == expected_hash
```

Add explicit tests for missing/unreadable files, expected-hash mismatch, source deletion before open, mutation during read through an injected chunk callback, before/after descriptor identity mismatch, traversal, a symlink inside the root pointing outside, symlink substitution after validation, NFC/NFD path names, Unicode lookalike fixture IDs, over-8-MiB input, immutable direct update/delete, and same-hash/different-bytes collision through an injected test hasher.

- [ ] **Step 2: Write fixture graph tests**

`backend/tests/test_fixture_pipeline_v2.py` calls:

```python
result = create_fixture_draft(
    db,
    fixture_id="prometheus.pm-36-gm.fixture-2",
    idempotency_key="fixture-create-0001",
)
assert result.revision.contract_schema_version == "2.0.0"
assert result.revision.draft_version == 0
assert result.revision.status == "draft"
assert result.revision.published_object_hash is None
assert all(slot.claims for slot in result.slots)
assert all(selection.claim_id for selection in result.selections)
assert {gate.phase for gate in result.gates} == {"publication", "execution"}
```

Assert one candidate per fixture slot while inserting a second conflicting candidate directly remains valid. Assert all publication gates exist but claim-review gates remain pending before review. Unsupported fixture IDs and caller-supplied manufacturer/part/source substitutions fail before any row is created.

- [ ] **Step 3: Confirm both suites fail before implementation**

```bash
cd backend
uv run pytest -q tests/test_artifact_store.py tests/test_fixture_pipeline_v2.py
```

Expected: import failures for `artifact_store` and `fixture_pipeline_v2`.

- [ ] **Step 4: Add exact fixture metadata**

Create `fixtures/evidence/pm-36-gm.synthetic-v2.json` with:

```json
{
  "fixture_id": "prometheus.pm-36-gm.fixture-2",
  "schema_version": "2.0.0",
  "manufacturer": "Prometheus Fixture Works",
  "part_number": "PM-36-GM",
  "revision": "fixture-2",
  "component_class": "gearmotor",
  "source_authority": "synthetic_fixture",
  "physical_validation_status": "unvalidated",
  "parameters": [],
  "limitations": [
    "Synthetic conformance data; not a real manufacturer's datasheet and not suitable for a physical design decision."
  ]
}
```

Populate `parameters` from the former fixture without changing its numerical meaning. Each item includes name, quantity, dimension, typed value or unknown reason, unit, original spelling, validity conditions, and source locator. The file contains no review decisions, database IDs, verdict, or generic confidence.

- [ ] **Step 5: Implement descriptor-based bounded ingestion**

`ingest_local_artifact` must:

```text
resolve and validate allowed_root -> lstat and reject symlink -> open descriptor
with no-follow where supported -> fstat before -> bounded chunk copy -> fstat after
-> compare device/inode/size/mtime -> hash copied bytes -> compare expected hash
-> insert immutable object or require an exact existing match -> flush
```

The origin path and filename are provenance only. The database BLOB is authoritative after ingestion. Define stable error codes: `artifact_missing`, `artifact_unreadable`, `artifact_path_escape`, `artifact_symlink`, `artifact_changed_during_ingestion`, `artifact_too_large`, `artifact_hash_mismatch`, and `artifact_hash_collision`.

- [ ] **Step 6: Implement the exact fixture pipeline in one transaction**

`fixture_pipeline_v2.py` exports:

```python
FIXTURE_ID = "prometheus.pm-36-gm.fixture-2"


@dataclass(frozen=True)
class FixtureDraftResult:
    ingestion_job: FixtureIngestionJobV2
    revision: ComponentRevision
    slots: Sequence[ParameterSlotV2]
    claims: Sequence[CandidateClaimV2]
    selections: Sequence[ClaimSelectionV2]
    gates: Sequence[CapabilityGateV2]
```

Export this exact signature:

```text
create_fixture_draft(
    db: Session, *, fixture_id: str, idempotency_key: str
) -> FixtureDraftResult
```

After the fixture file is complete, compute its SHA-256 once, store that exact lowercase `sha256:` value as `FIXTURE_ARTIFACT_HASH`, and add a test that recomputes the constant from checked-in bytes. Generate server UUIDv4 IDs, create claim fingerprints with `canonicalize_value`, use `private_upload` evidence plus explicit synthetic authority/unvalidated status, link every known claim to the stored fixture artifact, allow an unknown claim with a reason and no fake numerical evidence, finalize each claim only after its link set is complete, select the one fixture candidate per slot, and create four publication-gate declarations plus the truthful blocked `package_consumer_available` execution gate. Any failure rolls back job, artifact, component, revision, claim, and gate rows together.

- [ ] **Step 7: Run fixture and artifact suites**

```bash
uv run pytest -q tests/test_artifact_store.py tests/test_fixture_pipeline_v2.py
```

Expected: every lifecycle/attack case passes and the exact fixture creates one complete draft graph with no publication object.

- [ ] **Step 8: Commit the fixture boundary**

```bash
git add backend/app/artifact_store.py backend/app/fixture_pipeline_v2.py backend/app/config.py
git add backend/tests/test_artifact_store.py backend/tests/test_fixture_pipeline_v2.py fixtures/evidence/pm-36-gm.synthetic-v2.json
git commit -m "feat: ingest exact fixture into v2 claims"
```

### Task 8: Implement atomic append-only claim review

**Files:**

- Create: `backend/app/transaction.py`
- Create: `backend/app/review_service_v2.py`
- Create: `backend/tests/test_review_v2.py`
- Modify: `backend/app/database.py`

- [ ] **Step 1: Write review identity, version, and history tests**

Use this public API in `backend/tests/test_review_v2.py`:

```python
result = review_claims(
    revision_id=revision.id,
    request=ReviewRequestV2(
        expected_draft_version=0,
        reviewed_by="fixture-reviewer",
        decisions=[
            ClaimReviewDecisionV2(
                claim_id=claim.id,
                status="accepted",
                note="Accepted as synthetic conformance input only.",
            )
            for claim in selected_claims
        ],
    ),
    session_factory=SessionLocal,
)
assert result.draft_version == 1
assert {event.applied_draft_version for event in result.events} == {1}
```

Add tests for multiple candidates in one slot, reviewing an unselected candidate, duplicate IDs, cross-revision claim IDs, nonexistent IDs, stale version, published revision, empty/whitespace reviewer, empty/whitespace notes for every status, UTF-8 byte limits, 1001 decisions, partial invalid batch, database uniqueness, and re-review from rejected/ambiguous to accepted while preserving all events.

- [ ] **Step 2: Add synchronized review/publication race scaffolding**

Create a reusable barrier callback in the test file:

```python
class StageBarrier:
    def __init__(self, parties: int):
        self._barrier = threading.Barrier(parties)

    def __call__(self, stage: str) -> None:
        if stage == "revision_locked":
            self._barrier.wait(timeout=10)
```

Do not add timing sleeps. The full race assertion is completed after publication exists in Task 10.

- [ ] **Step 3: Confirm the review tests fail before implementation**

```bash
cd backend
uv run pytest -q tests/test_review_v2.py
```

Expected: import failure for `review_service_v2`.

- [ ] **Step 4: Configure database write semantics**

In `backend/app/database.py`, enable SQLite foreign keys and `PRAGMA busy_timeout=5000` for every connection. Reject SQLite versions below 3.35 at startup. Keep PostgreSQL at `READ COMMITTED`.

`backend/app/transaction.py` exposes:

```python
class RetryableWriteError(RuntimeError):
    code: str
```

```text
locked_revision_transaction(
    session_factory: sessionmaker,
    revision_id: str,
    *,
    operation: Literal["review", "publication"],
    stage_callback: Callable[[str], None] | None = None,
) -> Iterator[tuple[Session, ComponentRevision]]
```

SQLite obtains `BEGIN IMMEDIATE`; lock failure maps to `review_busy` or `publication_busy`. PostgreSQL selects the revision `FOR UPDATE`, uses lock order revision then operation-specific records, and retries deadlock/serialization errors at most three times without changing the logical request.

- [ ] **Step 5: Implement append-only reviews**

`review_service_v2.py` exports:

```python
@dataclass(frozen=True)
class ReviewResultV2:
    revision_id: str
    draft_version: int
    events: Sequence[ClaimReviewEventV2]
```

Export this exact signature:

```text
review_claims(
    *,
    revision_id: str,
    request: ReviewRequestV2,
    session_factory: sessionmaker,
    stage_callback: Callable[[str], None] | None = None,
) -> ReviewResultV2
```

Under the revision lock: require v2 draft state, compare expected version, load all decisions by composite same-revision identity, reject duplicates before write, recompute and compare every claim fingerprint, append all events at `n+1`, update affected review gates, increment `draft_version` exactly once, and commit. An exception before commit leaves no events or gate/version mutation.

The effective event is the greatest `applied_draft_version`, with event ID as deterministic tie-breaker that should never be needed because `(claim_id,applied_draft_version)` is unique.

- [ ] **Step 6: Run the review and migration suites**

```bash
uv run pytest -q tests/test_review_v2.py tests/test_migrations_v2.py
```

Expected: every review event is append-only, all stale/cross-revision cases fail closed, and direct database bypass remains constrained.

- [ ] **Step 7: Commit claim review**

```bash
git add backend/app/database.py backend/app/transaction.py backend/app/review_service_v2.py backend/tests/test_review_v2.py
git commit -m "feat: review immutable claims by revision version"
```

### Task 9: Compile deterministic packages and store immutable objects

**Files:**

- Create: `backend/app/package_compiler_v2.py`
- Create: `backend/app/object_store.py`
- Create: `backend/tests/test_package_compiler_v2.py`
- Create: `backend/tests/test_object_store.py`

- [ ] **Step 1: Write package compilation tests**

`backend/tests/test_package_compiler_v2.py` must establish these outcomes:

```python
compiled = compile_execution_component(db, reviewed_revision.id)
assert compiled.value["$schema"] == SCHEMA_ID
assert compiled.value["schema_version"] == "2.0.0"
assert compiled.value["authority"] == {
    "engineering_decision_authority": "prometheus_cpp",
    "authority_role": "input_only",
}
assert compiled.value["execution_readiness"] == "blocked"
assert compiled.canonical_bytes == canonicalize_value(compiled.value)
assert compiled.object_hash == object_hash(compiled.canonical_bytes)
assert "content_hash" not in compiled.value
```

Add failure cases for no slots, missing selection, selected claim without accepted effective review, accepted event with a mismatched fingerprint, cross-revision evidence, missing stored artifact, unresolved publication gate, unsupported schema ID/version, invalid contract ordering, oversized canonical bytes, and a noncanonical compiler value. Add the capability-isolation case: a blocked gate declared only for capability B must not block publication for capability A.

- [ ] **Step 2: Write immutable object-store tests**

Use this interface in `backend/tests/test_object_store.py`:

```python
stored = put_published_object(
    db,
    canonical_bytes=compiled.canonical_bytes,
    expected_object_hash=compiled.object_hash,
    media_type=PACKAGE_MEDIA_TYPE,
    schema_id=SCHEMA_ID,
    schema_version=SCHEMA_VERSION,
    canonicalization="RFC8785",
)
assert load_verified_published_object(db, stored.object_hash).payload_bytes \
    == compiled.canonical_bytes
```

Test exact-match reuse, metadata mismatch, forced same-hash/different-bytes collision through an injected test hash function, noncanonical input, unsupported schema, incorrect length, direct update, direct delete, corrupt prepared row on export, and missing referenced object.

- [ ] **Step 3: Confirm both suites fail before implementation**

```bash
cd backend
uv run pytest -q tests/test_package_compiler_v2.py tests/test_object_store.py
```

Expected: import failures for both new modules.

- [ ] **Step 4: Implement the package compiler as a pure draft reader**

`backend/app/package_compiler_v2.py` exports:

```python
@dataclass(frozen=True)
class CompiledExecutionComponentV2:
    value: dict[str, object]
    canonical_bytes: bytes
    object_hash: str
    execution_readiness: Literal["ready", "blocked"]
```

Export this exact signature:

```text
compile_execution_component(
    db: Session,
    revision_id: str,
    *,
    schema_id: str = SCHEMA_ID,
    schema_version: str = SCHEMA_VERSION,
    stage_callback: Callable[[str], None] | None = None,
) -> CompiledExecutionComponentV2
```

The function performs reads and validation only. It does not set status, timestamp, hash, job state, or object rows. It loads selected claims and their effective reviews using same-revision joins, verifies stored source artifacts, derives publication readiness from only declared publication gates, records blocked execution gates without treating them as publication failure, constructs arrays in contract order, validates through `ExecutionComponentV2`, canonicalizes once, enforces the 8-MiB package limit, computes the object hash, and calls `verify_canonical_bytes` before returning. Its callback emits `after_package_compilation`, `after_schema_validation`, `after_canonicalization`, `after_hash_computation`, and `after_byte_verification` immediately after those actual operations and in that order.

- [ ] **Step 5: Implement exact-match immutable object operations**

`backend/app/object_store.py` exports:

```text
PublishedObjectIntegrityError(code: str, message: str)

put_published_object(
    db: Session,
    *,
    canonical_bytes: bytes,
    expected_object_hash: str,
    media_type: str,
    schema_id: str,
    schema_version: str,
    canonicalization: str,
    hash_function: Callable[[bytes], str] = object_hash,
) -> PublishedObject

load_verified_published_object(
    db: Session, object_hash: str
) -> PublishedObject
```

Insertion verifies canonical bytes, recomputed hash, byte length, and exact metadata. A primary-key conflict reuses the row only if every byte and metadata field matches. Loading repeats all checks and returns the stored BLOB unchanged. Stable errors are `published_object_missing`, `published_object_noncanonical`, `published_object_hash_mismatch`, `published_object_metadata_mismatch`, and `published_object_hash_collision`.

- [ ] **Step 6: Run compiler and object-store tests**

```bash
uv run pytest -q tests/test_package_compiler_v2.py tests/test_object_store.py
```

Expected: all deterministic compilation, gate, collision, and corruption cases pass.

- [ ] **Step 7: Commit package compilation and immutable storage**

```bash
git add backend/app/package_compiler_v2.py backend/app/object_store.py
git add backend/tests/test_package_compiler_v2.py backend/tests/test_object_store.py
git commit -m "feat: compile and store immutable v2 packages"
```

### Task 10: Implement idempotent transactional publication

**Files:**

- Create: `backend/app/publication_service_v2.py`
- Create: `backend/tests/test_publication_v2.py`
- Create: `backend/tests/test_publication_concurrency.py`
- Create: `backend/tests/test_publication_failures.py`
- Create: `backend/tests/helpers/publication_process_probe.py`
- Modify: `backend/app/transaction.py`

- [ ] **Step 1: Write the successful publication and replay tests**

Use this exact service API:

```python
request = PublicationRequestV2(
    expected_draft_version=reviewed_revision.draft_version,
    schema_id=SCHEMA_ID,
    schema_version=SCHEMA_VERSION,
)
response = publish_revision(
    revision_id=reviewed_revision.id,
    idempotency_key="publish-fixture-0001",
    request=request,
    session_factory=SessionLocal,
)
assert response.status_code == 201
assert response.headers["ETag"] == f'"{response.object_hash}"'
assert response.body == canonicalize_value(json.loads(response.body))

replayed = publish_revision(
    revision_id=reviewed_revision.id,
    idempotency_key="publish-fixture-0001",
    request=request,
    session_factory=SessionLocal,
)
assert replayed.status_code == response.status_code
assert replayed.body == response.body
assert replayed.headers == response.headers
```

After success, assert exactly one `published_objects` row, one sealed revision binding, one succeeded request, equal revision content/object hashes, and a UTC publication time. Assert blocked execution readiness remains present in the response/package but does not change the publication success.

- [ ] **Step 2: Write deterministic terminal-failure tests**

Cover these exact errors and persisted replay behavior:

```text
stale_draft_version
revision_already_published
publication_review_incomplete
publication_gate_blocked
unsupported_schema
idempotency_conflict
execution_package_invalid
published_object_integrity_error
```

For each error, replaying the same operation/key/fingerprint returns identical stored status, body bytes, and application headers. Reusing the same key with a different revision, version, or schema returns `409 idempotency_conflict` and does not overwrite the stored record.

- [ ] **Step 3: Write synchronized concurrency tests**

`backend/tests/test_publication_concurrency.py` must use two independent `SessionLocal` connections and `threading.Barrier`, never sleeps. Add these assertions:

```python
assert same_key_result_a.body == same_key_result_b.body
assert count_rows("published_objects") == 1
assert count_rows("publication_requests", state="succeeded") == 1

assert sorted(result.status_code for result in different_key_results) == [201, 409]
assert error_code(different_key_results[1]) == "revision_already_published"
assert count_rows("published_objects") == 1
```

Complete the review-versus-publication race begun in Task 8. If review commits first, publication sees a stale expected version. If publication commits first, review sees immutable published state. Neither schedule may create partial review events or a second object.

- [ ] **Step 4: Write failure-injection rollback tests**

Parametrize exactly these stages in `backend/tests/test_publication_failures.py`:

```python
PUBLICATION_STAGES = [
    "after_idempotency_resolution",
    "after_draft_validation",
    "after_package_compilation",
    "after_schema_validation",
    "after_canonicalization",
    "after_hash_computation",
    "after_byte_verification",
    "after_object_insertion",
    "after_revision_binding",
    "after_response_storage",
    "before_commit",
]
```

At each stage raise `InjectedInfrastructureFailure`. After the call, a fresh connection must observe draft status, null publication fields, no object/binding, no succeeded or terminal-failure request, and unchanged job state. Retrying the same key without injection must then succeed.

- [ ] **Step 5: Write process-restart and lost-response tests**

`backend/tests/helpers/publication_process_probe.py` accepts command-line arguments `database-url`, `revision-id`, `idempotency-key`, `expected-draft-version`, writes a base64-encoded status/body/header record to stdout, and never mutates test source files. The parent test publishes, disposes the original engine, invokes the helper with `sys.executable`, and requires byte-identical replay.

For lost response, inject only after the database commit returns but before the caller observes the response. A second call with the same key must load and verify the object, then replay the stored response without recompilation.

- [ ] **Step 6: Confirm publication suites fail before implementation**

```bash
cd backend
uv run pytest -q tests/test_publication_v2.py tests/test_publication_concurrency.py tests/test_publication_failures.py
```

Expected: collection fails because `publication_service_v2` does not exist.

- [ ] **Step 7: Implement the publication response and stage seam**

Use these exact types:

```python
class PublicationStage(StrEnum):
    AFTER_IDEMPOTENCY_RESOLUTION = "after_idempotency_resolution"
    AFTER_DRAFT_VALIDATION = "after_draft_validation"
    AFTER_PACKAGE_COMPILATION = "after_package_compilation"
    AFTER_SCHEMA_VALIDATION = "after_schema_validation"
    AFTER_CANONICALIZATION = "after_canonicalization"
    AFTER_HASH_COMPUTATION = "after_hash_computation"
    AFTER_BYTE_VERIFICATION = "after_byte_verification"
    AFTER_OBJECT_INSERTION = "after_object_insertion"
    AFTER_REVISION_BINDING = "after_revision_binding"
    AFTER_RESPONSE_STORAGE = "after_response_storage"
    BEFORE_COMMIT = "before_commit"


@dataclass(frozen=True)
class StoredApplicationResponse:
    status_code: int
    body: bytes
    headers: dict[str, str]
    object_hash: str | None
```

Export this exact signature:

```text
publish_revision(
    *,
    revision_id: str,
    idempotency_key: str,
    request: PublicationRequestV2,
    session_factory: sessionmaker,
    stage_callback: Callable[[PublicationStage], None] | None = None,
) -> StoredApplicationResponse
```

Validate idempotency keys as 16–128 characters from `[A-Za-z0-9._:-]`. Compute the request fingerprint over RFC 8785 bytes containing operation, revision ID, expected version, schema ID, and schema version.

- [ ] **Step 8: Implement the one-commit state machine**

Inside `locked_revision_transaction`, perform the approved eleven publication stages in order. Persist `in_progress` only inside the uncommitted transaction. A matching succeeded request verifies its object before replay; a matching terminal application failure replays directly. Infrastructure failures roll back and remain retryable. Deterministic failures are caught inside the transaction, serialized canonically to the standard error body, stored as `terminal_failure`, and committed without sealing the revision. Forward the stage callback into the package compiler so schema, canonicalization, hash, and byte-verification injections occur at their real boundaries. Corruption discovered while replaying an already-stored success fails closed without replacing that succeeded request with a terminal-failure record.

The success response value has exactly:

```json
{
  "execution_readiness": "blocked",
  "media_type": "application/vnd.prometheus.execution-component+json;version=2.0.0",
  "object_hash": "sha256:0000000000000000000000000000000000000000000000000000000000000000",
  "publication_integrity": "sealed_v2",
  "revision_id": "00000000-0000-4000-8000-000000000000",
  "schema_id": "urn:prometheus:schema:execution-component:2.0.0",
  "schema_version": "2.0.0",
  "status": "published"
}
```

The zeros above show the contract shape; runtime values are the actual object and revision IDs. Store canonical response bytes plus only `Content-Type`, `ETag`, and `Location`. Never store transport-generated headers.

- [ ] **Step 9: Run all publication suites on SQLite**

```bash
uv run pytest -q tests/test_publication_v2.py tests/test_publication_concurrency.py tests/test_publication_failures.py
```

Expected: exact replay, every race, every rollback stage, and restart/lost-response behavior pass.

- [ ] **Step 10: Run the same semantic suites on PostgreSQL 17**

With the test service at the runbook DSN:

```bash
PROMETHEUS_TEST_POSTGRES_URL=postgresql+psycopg://prometheus:prometheus@127.0.0.1:55432/prometheus_test \
uv run pytest -q tests/test_publication_v2.py tests/test_publication_concurrency.py tests/test_publication_failures.py tests/test_migrations_v2.py
```

Expected: all cases pass without skips. Lock/deadlock errors map to the documented retry behavior and never to success.

- [ ] **Step 11: Commit transactional publication**

```bash
git add backend/app/publication_service_v2.py backend/app/transaction.py
git add backend/tests/test_publication_v2.py backend/tests/test_publication_concurrency.py backend/tests/test_publication_failures.py backend/tests/helpers/publication_process_probe.py
git commit -m "feat: seal v2 publication under durable replay"
```

### Task 11: Expose `/api/v2` and retire v1 trust-sensitive mutations

**Files:**

- Create: `backend/app/http_policy.py`
- Create: `backend/app/api_v2.py`
- Create: `backend/tests/test_api_v2.py`
- Create: `backend/tests/test_legacy_api.py`
- Delete after replacement coverage is green: `backend/tests/test_v1_research.py`
- Create: `docs/openapi-v2.json`
- Modify: `backend/app/main.py`
- Modify: `backend/app/api_v1.py`
- Modify: `backend/scripts/export_openapi.py`
- Modify: `backend/tests/test_openapi.py`

- [ ] **Step 1: Write the v2 HTTP acceptance path**

`backend/tests/test_api_v2.py` must execute this sequence through `TestClient`:

```text
POST /api/v2/fixture-ingestions
GET  /api/v2/fixture-ingestions/{ingestion_id}
GET  /api/v2/revisions/{revision_id}
POST /api/v2/revisions/{revision_id}/reviews
POST /api/v2/revisions/{revision_id}/publication
GET  /api/v2/revisions/{revision_id}/execution-package
```

The creation body is exactly:

```json
{
  "fixture_id": "prometheus.pm-36-gm.fixture-2",
  "schema_version": "2.0.0"
}
```

The successful creation response is `201` and has exactly `id`, `state="succeeded"`, `fixture_id`, and `revision`. `revision` contains its ID, status, draft version, contract identity, component identity, ordered parameters with `slot_id` and `selected_claim`, capability gates, publication integrity, object hash, and publication time. Draft publication fields are null. `GET` returns the same semantic response.

Require `Idempotency-Key` for fixture creation and publication. Replaying a fixture key returns the original ingestion/revision; using the key with a different request returns `409 idempotency_conflict`. Review uses claim IDs and `expected_draft_version`. Export must equal the database BLOB byte-for-byte, use the versioned media type, and carry a strong quoted `ETag` equal to the object hash.

- [ ] **Step 2: Write HTTP failure and body-policy tests**

Cover missing/malformed keys, unsupported schema, duplicate JSON keys, decoded duplicate keys, invalid UTF-8, body over 8 MiB with and without `Content-Length`, 1001 review decisions, oversized reviewer/note, stale version, legacy field-name decisions, corrupt object export, and a valid publication with blocked execution readiness.

The legacy field-name review body must return `422 claim_id_required`; do not infer a claim ID from `field_name`.

- [ ] **Step 3: Write retired-v1 endpoint tests**

`backend/tests/test_legacy_api.py` asserts `410 Gone` plus the same machine-readable migration guide for:

```text
POST /v1/research-jobs
POST /v1/research-jobs/{job_id}/review
POST /v1/research-jobs/{job_id}/publish
GET  /v1/component-revisions/{revision_id}/execution-package
```

Historical component/revision reads may remain `200` only if every revision includes `publication_integrity` and never returns reconstructed package bytes. Unversioned Python analysis routes retain their former `410`/`501` behavior.

- [ ] **Step 4: Confirm API tests fail before registration**

```bash
cd backend
uv run pytest -q tests/test_api_v2.py tests/test_legacy_api.py
```

Expected: v2 routes are `404` and former v1 mutation tests do not yet return the new migration document.

- [ ] **Step 5: Implement strict v2 body middleware**

`backend/app/http_policy.py` implements a plain ASGI middleware, not `BaseHTTPMiddleware`. It buffers only `/api/v2` JSON request bodies up to 8 MiB, rejects an oversized declared length before read, rejects streamed overflow while reading, calls `parse_strict_json` to detect duplicate keys and encoding/numeric policy failures, then replays the exact original bytes to FastAPI. Error responses use:

```json
{"detail":{"code":"request_json_invalid","message":"The v2 JSON request is invalid."}}
```

Use `413 request_too_large` for byte overflow and include no partial parsed content in logs or errors.

- [ ] **Step 6: Implement v2 handlers as thin service adapters**

`api_v2.py` uses `APIRouter(prefix="/api/v2")`. Handlers validate Pydantic requests, call the fixture/review/publication services, and convert their stable errors without changing status or body semantics. Publication sends stored response bytes and stored application headers. Export calls `load_verified_published_object` and returns `payload_bytes` unchanged; it never imports `execution_packages.py`.

- [ ] **Step 7: Reduce v1 to history and deterministic retirement**

Remove mutation/reconstruction logic from reachable v1 handlers. Use one helper:

```python
def retired_v1_trust_boundary() -> NoReturn:
    raise HTTPException(
        status_code=410,
        detail={
            "code": "v1_trust_boundary_retired",
            "message": "The v1 review/publication boundary is retired.",
            "migration_guide": "/docs/migration/program-01a-v1-to-v2.md",
        },
    )
```

Label `contracts_v1.py` and `execution_packages.py` as historical v1 modules. Verify no production v2 module imports them.

After `test_api_v2.py` covers the former fixture, review, publication, and integrity cases and `test_legacy_api.py` covers every retired route, delete `test_v1_research.py`; its former happy-path expectations would otherwise contradict the intentional v1 breaking change.

- [ ] **Step 8: Generate and test the new OpenAPI snapshot**

Change `backend/scripts/export_openapi.py` to write `docs/openapi-v2.json`. Keep `docs/openapi-v1.json` unchanged as history. Run:

```bash
uv run python scripts/export_openapi.py
uv run pytest -q tests/test_api_v2.py tests/test_legacy_api.py tests/test_openapi.py
```

Expected: the generated snapshot equals `app.openapi()` exactly, v2 is complete, and retired v1 responses are documented as `410`.

- [ ] **Step 9: Run the full backend suite**

```bash
uv run pytest -q
```

Expected: all v1 historical, v2, canonicalization, migration, concurrency, and failure tests pass together.

- [ ] **Step 10: Commit the HTTP boundary**

```bash
git add backend/app/http_policy.py backend/app/api_v2.py backend/app/api_v1.py backend/app/main.py
git add backend/scripts/export_openapi.py backend/tests/test_api_v2.py backend/tests/test_legacy_api.py backend/tests/test_openapi.py docs/openapi-v2.json
git commit -m "feat: expose v2 publication and retire v1 mutation"
```

### Task 12: Vendor and independently implement C++ RFC 8785 verification

**Files:**

- Create: `third_party/manifest.json`
- Create: `third_party/nlohmann-json/LICENSE.MIT`
- Create: `third_party/nlohmann-json/include/nlohmann/json.hpp`
- Create: `third_party/ryu/LICENSE-Apache2`
- Create: `third_party/ryu/LICENSE-Boost`
- Create: `third_party/ryu/ryu/common.h`
- Create: `third_party/ryu/ryu/d2s.c`
- Create: `third_party/ryu/ryu/d2s_full_table.h`
- Create: `third_party/ryu/ryu/d2s_intrinsics.h`
- Create: `third_party/ryu/ryu/digit_table.h`
- Create: `third_party/ryu/ryu/ryu.h`
- Create: `desktop/integrity/CMakeLists.txt`
- Create: `desktop/integrity/include/prometheus/integrity/canonical_json.hpp`
- Create: `desktop/integrity/src/canonical_json.cpp`
- Create: `desktop/integrity/src/ecmascript_number.cpp`
- Create: `desktop/integrity/src/ecmascript_number.hpp`
- Create: `desktop/integrity/tests/canonical_json_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`

- [ ] **Step 1: Vendor only the pinned source bytes**

Fetch nlohmann/json 3.12.0 at commit `55f93686c01528224f448c19128836e7df245f72` and the listed Ryu subset at commit `3377662b1958dbdefb679e2c110368512cccf4f6`. The selected `d2s.c` path uses `d2s_full_table.h`; do not define the size-optimized alternate-table mode. Preserve upstream license texts. Do not add build-time downloads, Git submodules, tests, benchmarks, or unrelated Ryu converters.

`third_party/manifest.json` has one object per file with exactly `path`, `upstream`, `commit`, `license`, `license_path`, and `sha256`. Generate each lowercase `sha256:` value from the exact committed bytes after fetching the pinned commit, then make the manifest-check test recompute every value. The nlohmann entry records the fixed commit above and `MIT`; every Ryu entry records its fixed commit and the applicable upstream `Apache-2.0 OR Boost-1.0` notice.

- [ ] **Step 2: Write the C++ corpus test before implementation**

`desktop/integrity/tests/canonical_json_tests.cpp` loads the repository-root manifest, runs every success and failure case, and reads the same complete-package `.jcs` and `.sha256` files as Python. It must assert:

```cpp
const auto canonical = canonicalize_json_bytes(input);
require(canonical == expected_bytes, case_id + " canonical bytes");
require(verify_canonical_bytes(expected_bytes) == expected_bytes,
        case_id + " stored canonical verification");
require(object_hash(expected_bytes) == expected_hash,
        case_id + " SHA-256 identity");
```

For failures, require the exact manifest `error_code`. Add a byte-flip package case and unsupported schema ID/version cases.

- [ ] **Step 3: Register the integrity target and confirm red state**

Add `PROMETHEUS_BUILD_INTEGRITY` and an `integrity-debug` preset. Enable C alongside C++ in the top-level CMake project so the unmodified vendored `d2s.c` compiles as C. Find Qt 6 Core for SHA-256 without requiring Quick/Quick3D. Register `desktop/integrity` when enabled.

Run:

```bash
cmake --preset integrity-debug
cmake --build --preset integrity-debug
```

Expected: compilation fails because the integrity implementation is absent.

- [ ] **Step 4: Implement the bounded SAX value builder**

Use this public API:

```cpp
namespace prometheus::integrity {

struct Limits final {
  std::size_t raw_bytes{8U * 1024U * 1024U};
  std::size_t depth{64};
  std::size_t nodes{100000};
  std::size_t object_members{10000};
  std::size_t array_elements{10000};
  std::size_t string_bytes{1024U * 1024U};
};

class CanonicalJsonError final : public std::runtime_error {
public:
  CanonicalJsonError(std::string code, std::string message);
  [[nodiscard]] const std::string& code() const noexcept;
};

[[nodiscard]] std::string canonicalize_json_bytes(
    std::string_view source, Limits limits = {});
[[nodiscard]] std::string verify_canonical_bytes(
    std::string_view source, Limits limits = {});
[[nodiscard]] std::string object_hash(std::string_view canonical_bytes);
[[nodiscard]] std::string verify_execution_component(
    std::string_view stored_bytes,
    std::string_view expected_object_hash,
    Limits limits = {});

} // namespace prometheus::integrity
```

Build a custom nlohmann SAX handler that maintains per-object decoded-key sets and a bounded value tree. Reject duplicates before insertion, invalid UTF-8/surrogates, unsafe integer callbacks, non-finite floats, overflow, nonzero underflow, and a raw negative numeric token whose parsed value is zero. Count nodes/depth/members/elements/UTF-8 bytes during SAX events.

- [ ] **Step 5: Implement RFC strings and UTF-16 key comparison**

Convert valid UTF-8 keys to UTF-16 code-unit sequences for lexicographic comparison without normalization. Emit quote and reverse-solidus escapes, the five short control escapes, lowercase `\\u00xx` for remaining controls, and raw UTF-8 for other valid characters. Never escape `/` and never normalize NFC/NFD.

- [ ] **Step 6: Implement ECMAScript binary64 formatting with Ryu**

`ecmascript_number.cpp` uses Ryu shortest-round-trip digits and then applies ECMAScript/JCS spelling: lowercase `e`, explicit `+` for positive scientific exponents, no redundant leading exponent zeroes, fixed form for magnitudes in the ECMAScript fixed interval, and `0` only for positive zero. Prometheus negative zero is rejected before this function. Integers remain constrained to the safe interval.

- [ ] **Step 7: Verify stored bytes, SHA-256, and schema identity**

`verify_canonical_bytes` parses and re-emits, then requires byte equality. `object_hash` uses `QCryptographicHash::Sha256` and emits the lowercase `sha256:` form. `verify_execution_component` performs byte verification, hash comparison, then requires the exact `$schema` and `schema_version` values; it returns the object hash and issues no engineering verdict.

- [ ] **Step 8: Run the independent corpus and package test**

```bash
cmake --build --preset integrity-debug
ctest --test-dir out/build/integrity-debug --output-on-failure
```

Expected: every shared corpus case and the full package vector pass. The C++ test does not invoke Python or compare against Python output generated during the test.

- [ ] **Step 9: Verify the vendored manifest and offline configure**

Add a stdlib-only checksum/license test to `backend/tests/test_vendored_dependencies.py`, then run:

```bash
cd backend
uv run pytest -q tests/test_vendored_dependencies.py
cd ..
cmake --preset integrity-debug
```

Expected: every vendored byte and license is accounted for, and configure performs no network access.

- [ ] **Step 10: Commit the independent verifier**

```bash
git add third_party desktop/integrity CMakeLists.txt CMakePresets.json backend/tests/test_vendored_dependencies.py
git commit -m "feat: verify RFC 8785 packages independently in C++"
```

### Task 13: Move the Qt review and publication flow to v2

**Files:**

- Modify: `desktop/app/review_payload.hpp`
- Modify: `desktop/app/review_payload.cpp`
- Modify: `desktop/app/service_controller.hpp`
- Modify: `desktop/app/service_controller.cpp`
- Modify: `desktop/app/tests/review_payload_tests.cpp`
- Modify: `desktop/ui/Main.qml`

- [ ] **Step 1: Rewrite the pure review-payload tests for claim identity**

The public function becomes:

```cpp
[[nodiscard]] ReviewPayloadResult buildReviewPayload(
    const QVariantList& parameters,
    const QVariantList& decisions,
    const QString& reviewer,
    qint64 expectedDraftVersion);
```

Each displayed parameter contains `selected_claim.claim_id`; each decision contains `claim_id`, `status`, and `note`. Add test assertions for this exact result:

```cpp
const auto result = buildReviewPayload(
    parameters(), acceptedDecisions(), "fixture-reviewer", 4);
require(result.ok, "claim review payload should be valid");
require(result.payload.value("expected_draft_version").toInteger() == 4,
        "draft version must be preserved");
const auto decisions = result.payload.value("decisions").toArray();
require(decisions.at(0).toObject().contains("claim_id"),
        "review must identify a claim");
require(!decisions.at(0).toObject().contains("field_name"),
        "field labels are not review identity");
```

Test negative draft versions, missing/duplicate/unknown claim IDs, empty status, empty or whitespace note for every decision status, empty reviewer, 257-byte reviewer, 4097-byte note, 1001 decisions, duplicate displayed claims, and empty parameter list.

- [ ] **Step 2: Run the Qt contract test red**

Configure the existing Qt build and run:

```bash
cmake --build out/build/macos-qt-debug --target prometheus_review_payload_tests
ctest --test-dir out/build/macos-qt-debug -R prometheus_review_payload_tests --output-on-failure
```

Expected: compilation fails because `buildReviewPayload` still uses field names and has no version argument.

- [ ] **Step 3: Implement claim-based payload normalization**

Normalize the reviewer and notes by trimming. Enforce UTF-8 byte counts, not UTF-16 `QString::size()`. Preserve the displayed parameter order in the emitted decision array. Require exactly one decision for each displayed selected claim. Emit only:

```json
{
  "expected_draft_version": 4,
  "reviewed_by": "fixture-reviewer",
  "decisions": [
    {
      "claim_id": "00000000-0000-4000-8000-000000000000",
      "status": "accepted",
      "note": "Accepted as synthetic conformance input only."
    }
  ]
}
```

- [ ] **Step 4: Add v2 state to `ServiceController`**

Add properties for `draftVersion`, `executionReadiness`, `objectHash`, and `publicationIntegrity`. Replace the old request paths with:

```text
/api/v2/fixture-ingestions
/api/v2/revisions/{revision_id}/reviews
/api/v2/revisions/{revision_id}/publication
```

Replace `research(manufacturer, partNumber)` with `loadFixture()`. It sends only `fixture_id=prometheus.pm-36-gm.fixture-2` and schema version `2.0.0`. Replace free-form manufacturer/part controls in this workflow with a fixed conformance-fixture label. No Qt v2 method accepts or transmits a source URL.

- [ ] **Step 5: Persist one publication key per logical request**

Add `QString publication_idempotency_key_`. Generate it only when `publish()` begins and the field is empty. Keep it across network errors and retries. Clear it when a newly loaded revision has a different ID, when a successful review advances `draftVersion`, or when reset starts a new fixture flow. Do not generate a fresh key on each click.

The publication body carries the current draft version and exact schema ID/version. A terminal stale-version response remains visible and cannot be silently retried against newer state with the same key.

- [ ] **Step 6: Update QML review and state presentation**

`Main.qml` stores decisions by `claim_id`, shows the selected claim fingerprint and current draft version, and shows a note field for every decision because v2 notes are always required. Separate labels show:

```text
Publication: draft | published
Execution readiness: ready | blocked
```

A successful publication with blocked execution must not use pass-like color or language. Remove old `reviewedRevisionId` logic that keys only on revision without version. Retain empty initial decisions and no accept-all action.

- [ ] **Step 7: Run Qt contract and static forbidden-path checks**

```bash
cmake --build out/build/macos-qt-debug --target prometheus_review_payload_tests
ctest --test-dir out/build/macos-qt-debug -R prometheus_review_payload_tests --output-on-failure
rg -n "field_name.*status|acceptAll|autoAccept|autoPublish|/v1/research-jobs|QUuid::createUuid.*publish" desktop/app desktop/ui
```

Expected: Qt tests pass. The search returns no review identity by field, no accept-all/auto-publish path, no v1 trust mutation URL, and no publish-click key regeneration.

- [ ] **Step 8: Commit the Qt v2 workflow**

```bash
git add desktop/app/review_payload.* desktop/app/service_controller.* desktop/app/tests/review_payload_tests.cpp desktop/ui/Main.qml
git commit -m "feat: review and publish v2 claims in Qt"
```

### Task 14: Repair the OCCT-disabled desktop seam

**Files:**

- Create: `desktop/core/include/prometheus/cad/types.hpp`
- Create: `desktop/app/tests/cad_controller_no_occt_tests.cpp`
- Modify: `desktop/cad/include/prometheus/cad/step_importer.hpp`
- Modify: `desktop/app/cad_controller.hpp`
- Modify: `desktop/app/cad_controller.cpp`
- Modify: `desktop/app/CMakeLists.txt`
- Modify: `CMakePresets.json`

- [ ] **Step 1: Add a full disabled-adapter controller test target**

When `prometheus_cad` is absent, register `prometheus_cad_controller_no_occt_tests`, link it to `Qt6::Gui`, `Qt6::Test`, and `prometheus_desktop_support`, and set `QT_QPA_PLATFORM=offscreen`. The test creates a `QGuiApplication`, constructs `CadController`, calls both `importStep` and `importStepAsync` with a nonexistent STEP path, drains the event loop, and asserts:

```cpp
require(!controller.importStep("missing.step"),
        "sync import must fail when OCCT is disabled");
require(controller.error() == "Open Cascade adapter is not enabled",
        "disabled adapter must be explicit");
require(controller.parts().isEmpty(),
        "disabled adapter must not synthesize geometry");
require(!controller.busy(),
        "disabled async import must terminate");
```

Use `QSignalSpy` on `importFinished`, call `importStepAsync`, and require one `false` completion plus `busy=false` without constructing a `StepImporter`. Compilation of the support library proves the sweep/interference methods have no unresolved optional-adapter symbols.

- [ ] **Step 2: Reproduce the existing full-build failure**

Add a portable `desktop-no-occt-debug` preset with desktop on and OCCT off, then run:

```bash
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
```

Expected before the fix: compilation fails at the unconditional `prometheus/cad/step_importer.hpp` include or an unguarded `StepImporter` call.

- [ ] **Step 3: Extract only plain CAD value types**

Move `BoundingBox`, `DisplayMesh`, `AssemblyNode`, `StaticInterference`, `SweepInterference`, `PartPlacement`, and `StepImportResult` unchanged into `desktop/core/include/prometheus/cad/types.hpp`. Make `step_importer.hpp` include that file and contain only `StepImporter`. Make `cad_controller.hpp` include the plain types, never the optional importer.

- [ ] **Step 4: Guard every optional adapter call**

In `cad_controller.cpp`, retain the importer include only inside `#ifdef PROMETHEUS_HAS_OCCT`. Put every `StepImporter` construction in the same compile-time branch, including synchronous import, asynchronous import, joint sweep, and refreshed interference. The disabled branches set the exact error, clear busy flags, emit completion signals, and never return fabricated success.

- [ ] **Step 5: Build and test the complete application with OCCT off**

```bash
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure
```

Expected: the full `prometheus_desktop`, review payload, integrity, summary, and disabled-CAD tests build; all registered tests pass.

- [ ] **Step 6: Commit the optional-adapter repair**

```bash
git add desktop/core/include/prometheus/cad/types.hpp desktop/cad/include/prometheus/cad/step_importer.hpp
git add desktop/app/cad_controller.* desktop/app/CMakeLists.txt desktop/app/tests/cad_controller_no_occt_tests.cpp CMakePresets.json
git commit -m "fix: compile the desktop without Open Cascade"
```

### Task 15: Add the supported CI and database matrix

**Files:**

- Modify: `.github/workflows/verify.yml`
- Create: `scripts/verify-vendored-dependencies.py`
- Modify: `scripts/verify.ps1`
- Modify: `CMakePresets.json`

- [ ] **Step 1: Split backend verification by supported responsibility**

Define these jobs in `.github/workflows/verify.yml`:

```text
backend-sqlite: Python 3.11, 3.12, 3.13, 3.14 matrix; full backend suite
backend-postgres: Python 3.12 + postgres:17 service; full semantic/migration suite
frontend: Node 20; npm ci, test, build, high-severity audit
native: ubuntu-latest, macos-latest, windows-latest matrix; headless + integrity + desktop-no-occt
```

The PostgreSQL service uses database/user/password `prometheus_test`/`prometheus`/`prometheus`, exposes port 5432 inside the job, and supplies:

```text
postgresql+psycopg://prometheus:prometheus@127.0.0.1:5432/prometheus_test
```

Do not conditionally skip the PostgreSQL tests in that job.

- [ ] **Step 2: Install the declared native dependencies without build-time fetches**

Each native job installs Qt 6 Core, Quick, QuickControls2, Quick3D, Concurrent, and Network before configuring. The build consumes only checked-in nlohmann/Ryu files. Linux and macOS use Ninja; Windows may use Ninja with the configured MSVC or UCRT compiler, but the selected compiler and Qt ABI must match.

- [ ] **Step 3: Run all three CMake configurations on every native platform**

The job executes the equivalent of:

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
cmake --preset integrity-debug
cmake --build --preset integrity-debug
ctest --test-dir out/build/integrity-debug --output-on-failure
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure
```

Use platform-specific presets only where generator syntax requires it; do not weaken the target/test set.

- [ ] **Step 4: Add deterministic vendored dependency verification**

`scripts/verify-vendored-dependencies.py` uses only the Python standard library. It loads `third_party/manifest.json`, rejects duplicate or unlisted files, verifies every SHA-256, requires every recorded commit to be a 40-character lowercase hexadecimal ID, and requires the named license file to exist. Both backend tests and CI run it.

- [ ] **Step 5: Update the Windows verification script**

`scripts/verify.ps1` runs backend tests, frontend checks, vendored verification, headless C++, integrity C++, and the OCCT-disabled desktop. Keep the existing OCCT-enabled Windows preset as an additional adapter check where its separately installed dependencies exist; do not let an absent optional OCCT installation substitute for the required OCCT-disabled build.

- [ ] **Step 6: Validate workflow syntax and local equivalents**

Run available local jobs:

```bash
cd backend
uv run pytest -q
cd ../frontend
npm ci
npm test
npm run build
npm audit --audit-level=high
cd ..
python3 scripts/verify-vendored-dependencies.py
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
cmake --preset integrity-debug
cmake --build --preset integrity-debug
ctest --test-dir out/build/integrity-debug --output-on-failure
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure
```

Expected: every locally available required suite passes. Record unavailable platform checks as unavailable until CI runs; do not infer success.

- [ ] **Step 7: Commit the matrix**

```bash
git add .github/workflows/verify.yml scripts/verify-vendored-dependencies.py scripts/verify.ps1 CMakePresets.json
git commit -m "ci: verify the amended Program 01A matrix"
```

### Task 16: Document v1-to-v2 migration, validation, and threats

**Files:**

- Create: `docs/migration/program-01a-v1-to-v2.md`
- Create: `docs/program/01-trust-kernel/01a-amended-completion.md`
- Create: `CHANGELOG.md`
- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/component-model.md`
- Modify: `docs/validation-plan.md`
- Modify: `docs/validation-policy.md`
- Modify: `docs/threat-model.md`
- Modify: `docs/program/00-master-roadmap.md`
- Modify: `docs/milestone-status.md`
- Modify: `docs/product-scope.md`

- [ ] **Step 1: Write the breaking migration guide from tested behavior**

The guide must tabulate:

```text
v1 field-name review -> v2 claim_id + expected_draft_version
v1 mutable evidence review columns -> v2 append-only review events
v1 top-level content_hash -> external object identity/ETag
v1 reconstructed export -> v2 verified stored-byte export
v1 generic confidence -> v2 extraction_confidence only
v1 mutation/export endpoints -> 410 with v2 replacements
former published row -> legacy_unsealed historical metadata
new publication -> sealed_v2 immutable object
```

Include exact endpoint bodies, status/error codes, schema/media identifiers, idempotency retry semantics, and the fact that legacy rows are not silently upgraded.

Create `CHANGELOG.md` with an `Unreleased` entry that records the Program 01A reopening, the planned v1 breaking retirement, and links to the migration guide. Move those bullets to a dated release section only when Task 17 closes the amended gate.

- [ ] **Step 2: Update architecture and component-model documents**

State the draft-versus-published authority switch, stable claim graph, evidence-class rules, capability-specific gates, Python compiler role, independent C++ verifier, and C++ summary authority. Put the package non-claim beside every package-integrity claim.

- [ ] **Step 3: Update validation and threat records**

The validation plan names every SQLite/PostgreSQL, Python-minor, C++ platform, canonicalization, concurrency, restart, failure-injection, artifact, Qt, and OCCT-disabled test. The threat model covers duplicate-key smuggling, Unicode identity substitution, source races, stale review, idempotency conflict, stored-byte corruption, same-hash defense, direct DB mutation, local unauthenticated reviewer labels, and the inability to defend against an attacker who can replace both application and database controls.

- [ ] **Step 4: Keep the amended completion record explicitly pending**

Create `01a-amended-completion.md` with:

```markdown
# Program 01A amended completion record

- Status: pending verification
- Completion commit: not recorded
- Validation level: not assigned

This record may change to complete only after every gate in the approved amended design passes. Until then, the repository remains a fixture-backed vertical demonstrator with Program 01A in progress.
```

Do not add passing counts or versions until Task 17 observes them.

- [ ] **Step 5: Run documentation truth searches**

```bash
rg -n "arbitrary.*(works|verified|validated)|all engineering|general engineering.*implemented|Program 01A.*complete|Python 3\.12 only|content_hash.*package" README.md docs
rg -n "live research|PDF parsing|solver adapter|package.*drives.*C\+\+" README.md docs
```

Expected: strong claims are absent or immediately scoped as future/unsupported. The completion record remains pending. V2 documentation does not put a hash inside package bytes.

- [ ] **Step 6: Commit truthful documentation before final closure**

```bash
git add README.md docs
git commit -m "docs: define the v2 trust boundary and migration"
```

### Task 17: Run the amended release gate and close only from evidence

**Files:**

- Modify after observed verification: `docs/program/01-trust-kernel/01a-amended-completion.md`
- Modify after observed verification: `docs/program/01-trust-kernel/01a-integrity-and-contracts.md`
- Modify after observed verification: `docs/program/00-master-roadmap.md`
- Modify after observed verification: `docs/milestone-status.md`
- Modify after observed verification: `README.md`
- Copy after approval: `/Users/byungkim/Desktop/Prometheus_Program_01A_Amended_Trust_Boundary_Design.md`
- Copy after approval: `/Users/byungkim/Desktop/Prometheus_Program_01A_Amended_Implementation_Plan.md`

- [ ] **Step 1: Verify the worktree and migration head**

```bash
git status --short
cd backend
uv run alembic heads
uv run alembic current
```

Expected: worktree changes are understood; Alembic reports one head, `a41f0c93e2d7`. Use a fresh temporary verification database for `current`; do not point destructive test setup at a user database.

- [ ] **Step 2: Run the full SQLite/Python verification**

```bash
uv run python -m pip check
uv run pytest -q
uv run python scripts/export_contract_schemas.py
uv run python scripts/export_openapi.py
git diff --exit-code -- ../schemas ../docs/openapi-v2.json
```

Expected: dependency check and every backend test pass; regeneration leaves no diff. Record the exact interpreter and pass count from output.

- [ ] **Step 3: Run PostgreSQL 17 verification**

```bash
PROMETHEUS_TEST_POSTGRES_URL=postgresql+psycopg://prometheus:prometheus@127.0.0.1:55432/prometheus_test \
uv run pytest -q tests/test_migrations_v2.py tests/test_review_v2.py tests/test_publication_v2.py tests/test_publication_concurrency.py tests/test_publication_failures.py tests/test_api_v2.py
```

Expected: all required semantic cases pass with no skips. Record PostgreSQL server version and exact pass count.

- [ ] **Step 4: Run frontend and vendored checks**

```bash
cd ../frontend
npm ci
npm test
npm run build
npm audit --audit-level=high
cd ..
python3 scripts/verify-vendored-dependencies.py
```

Expected: tests/build pass, high-severity audit reports zero findings, and all vendored files/licenses/hashes verify.

- [ ] **Step 5: Run all local C++/Qt configurations**

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
cmake --preset integrity-debug
cmake --build --preset integrity-debug
ctest --test-dir out/build/integrity-debug --output-on-failure
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure
```

Expected: decision core, shared-corpus verifier, complete package hash, Qt review payload, full desktop, and disabled-CAD tests all pass. Record exact compiler, CMake, Qt, platform, and CTest counts.

- [ ] **Step 6: Confirm GitHub matrix evidence before closure**

Require passing `backend-sqlite` on Python 3.11–3.14, `backend-postgres` on PostgreSQL 17, `frontend`, and native macOS/Linux/Windows jobs. If any required job is missing, skipped, unavailable, or failing, leave the completion record pending and list the exact blocker.

- [ ] **Step 7: Perform the manual exact-byte audit**

On a fresh database, load the exact fixture, submit explicit claim decisions, publish, export twice, restart the application process, replay publication, and export again. Record:

```text
revision ID
reviewed draft version
object hash
stored byte length
three export SHA-256 values
publication replay response SHA-256 values
media type and ETag
execution readiness and blocked gate
```

Expected: all export hashes equal the object ID, all response replay hashes match, stored bytes never contain a top-level content hash, and the package contains no verdict.

- [ ] **Step 8: Update the completion record only if all evidence exists**

Replace the pending fields with the verified implementation commit, test commands/counts, exact dependency/platform versions, manual object identity, known unavailable optional checks, residual risks, and the bounded release claim from the durable plan. Update current status pages to `complete under the amended contract-tested gate` while retaining links to both historical completion records.

If one required gate fails, do not perform this step.

- [ ] **Step 9: Copy the approved design and durable plan to Desktop**

Use exact source/destination pairs:

```text
docs/superpowers/specs/2026-08-11-program-01a-amended-trust-boundary-design.md
  -> /Users/byungkim/Desktop/Prometheus_Program_01A_Amended_Trust_Boundary_Design.md
docs/program/01-trust-kernel/01a-amended-implementation-plan.md
  -> /Users/byungkim/Desktop/Prometheus_Program_01A_Amended_Implementation_Plan.md
```

After copying, compare SHA-256 for each pair and require equality. These are documentation copies only; the repository files remain authoritative.

- [ ] **Step 10: Commit the evidence-bound closure**

```bash
git add README.md docs/program/01-trust-kernel docs/program/00-master-roadmap.md docs/milestone-status.md
git commit -m "chore: close amended Program 01A trust boundary"
```

Expected: make this commit only when every required gate is green. Do not push or merge without the user's separate approval.
