# Program 01B: Package-Driven C++ Execution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Do not combine tasks or skip the red/green verification steps.

**Goal:** Replace the fixed motor-arm demonstrator with Prometheus's first exact-package-driven C++ execution path: users review and bind Motor A or Motor B, confirm one project scenario, obtain four traceable scoped findings, save both immutable runs, and reproduce their exact result bytes offline through the desktop or replay CLI.

**Architecture:** Python remains an evidence/review/package publisher and performs no production physics or finding decisions. A Qt-free `prometheus_execution` library independently verifies exact v2 package bytes, consumes typed claims, validates a reviewed scenario and request, calls the one authoritative `motor_arm_builtin_v1` backend, and compiles deterministic result and manifest objects. A Qt-free `prometheus_run_store` installs those objects under SHA-256 identities and atomically updates a strict version 2 project index. The Qt desktop and the read-only `prometheus_replay` CLI are two clients of the same execution entry point; neither owns equations, thresholds, or serializers.

**Tech Stack:** Python 3.11–3.14, FastAPI, Pydantic 2, SQLAlchemy 2, SQLite, PostgreSQL 17, RFC 8785, JSON Schema Draft 2020-12, pytest, C++20, nlohmann/json 3.12.0, Ryu, PicoSHA2 pinned at commit `161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29`, CMake/CTest, Qt 6/QML, native POSIX and Windows file locks/atomic file APIs, GitHub Actions.

---

**Approved design:** `docs/superpowers/specs/2026-08-12-program-01b-package-driven-execution-design.md`

## Fixed implementation boundaries

- The old `fixtures/contracts/execution-component-v2.pm-36-gm.*` bytes and hash remain unchanged and blocked.
- The new package capability is exactly `component_input.dc_gearmotor_v1`.
- The authoritative backend identity is exactly `motor_arm_builtin_v1` with contract version `1.0.0`.
- The package-consumer contract is exactly version `1.0.0`, media type `application/vnd.prometheus.package-consumer-contract+json;version=1.0.0`, and validation level `synthetic_conformance_only`.
- Scenario, request, result, and manifest use the schema IDs and media types approved in the design. Their JSON objects are closed and their canonical bytes are independently hashed.
- Scenario quantities are represented as closed `{ "value": number, "unit": enum }` objects. The seven quantity fields use field-specific units drawn from `kg`, `m`, `rad`, `s`, and `degC` as shown below.
- A request contains hashes and semantic bindings, not copied package/scenario values. A result exists only for `execution_disposition=completed`.
- Stored-object references always contain `object_hash`, `byte_length`, `media_type`, `schema_id`, and `schema_version`.
- Package-binding revisions and committed-run references are append-only. Supersession adds a binding record; replay adds only bounded non-authoritative observation metadata and never overwrites an immutable result.
- One object is at most 8 MiB. A project index is at most 8 MiB, contains at most 10,000 package-binding revisions, 10,000 committed runs, and 256 non-authoritative events.
- Writer lock acquisition waits at most five seconds. POSIX uses `flock`; Windows uses `LockFileEx`. Kernel release after process death is the only stale-lock recovery mechanism.
- The replay CLI accepts `prometheus_replay --project arm.prometheus --run sha256:` followed by exactly 64 lowercase hexadecimal characters and has no verification-bypass flags.
- Geometry and motor findings are separate collections and separate run identities. No code produces a global “project works” verdict in Program 01B.

## Contract field inventory

The implementation must use these shapes consistently in Python schemas, C++ parsers, checked-in vectors, persistence, desktop display, and replay.

| Object | Exact media type |
| --- | --- |
| Package consumer | `application/vnd.prometheus.package-consumer-contract+json;version=1.0.0` |
| Scenario | `application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0` |
| Request | `application/vnd.prometheus.analysis-request+json;version=1.0.0` |
| Result | `application/vnd.prometheus.analysis-result+json;version=1.0.0` |
| Run manifest | `application/vnd.prometheus.run-manifest+json;version=1.0.0` |

### Package consumer `1.0.0`

Its exact top-level members are:

```text
$schema, schema_version, contract_kind, backend, accepted_package,
required_slots, validation_slots, available_but_unused_slots,
supported_scenario, obligation_ids, applicability_ids, validation_level
```

`contract_kind` is `package_consumer`; `backend` contains `backend_id=motor_arm_builtin_v1` and `contract_version=1.0.0`; `accepted_package` fixes execution-component schema `2.0.0`, kind `component_execution_input`, and capability `component_input.dc_gearmotor_v1`. Required/validation entries name their exact slot, engineering quantity, dimension, value shape, and canonical unit. `supported_scenario` fixes the scenario schema/version and sole motion profile. All arrays use the order declared in this plan and the approved design.

### Scenario `1.0.0`

```json
{
  "$schema": "urn:prometheus:schema:motor-arm-scenario:1.0.0",
  "schema_version": "1.0.0",
  "scenario_kind": "motor_arm",
  "payload_mass": {"value": 8, "unit": "kg"},
  "arm_radius": {"value": 0.2, "unit": "m"},
  "rotation": {"value": 1.5707963267948966, "unit": "rad"},
  "move_duration": {"value": 1.2, "unit": "s"},
  "hold_duration": {"value": 4, "unit": "s"},
  "cycle_duration": {"value": 10, "unit": "s"},
  "ambient_temperature": {"value": 35, "unit": "degC"},
  "motion_profile": "symmetric_triangular_velocity",
  "review": {
    "confirmed_by_user": true,
    "intent": "Evaluate the bound motor for the reviewed motor-arm operating cycle."
  }
}
```

The C++ builder accepts UI degrees, converts once with the C++20 `std::numbers::pi_v<double>` constant, and returns this radians representation for confirmation. All values must be finite. Positive-domain and cycle-duration rules are those in the approved design.

### Request `1.0.0`

```json
{
  "$schema": "urn:prometheus:schema:analysis-request:1.0.0",
  "schema_version": "1.0.0",
  "request_kind": "motor_arm_analysis",
  "package_hash": "sha256:0000000000000000000000000000000000000000000000000000000000000000",
  "scenario_hash": "sha256:1111111111111111111111111111111111111111111111111111111111111111",
  "assembly_artifact_hash": "sha256:2222222222222222222222222222222222222222222222222222222222222222",
  "bound_cad_entity_id": "motor",
  "backend_id": "motor_arm_builtin_v1",
  "backend_contract_version": "1.0.0",
  "package_consumer_contract_hash": "sha256:3333333333333333333333333333333333333333333333333333333333333333",
  "obligation_ids": [
    "motor_arm.move_torque_speed",
    "motor_arm.hold_continuous_torque",
    "motor_arm.driver_current_limit",
    "motor_arm.thermal_peak"
  ]
}
```

The zero-like hashes above demonstrate field spelling only; checked-in vectors use real object hashes. Obligation order is contract order and any omission, addition, duplicate, or reordering is rejected.

### Result and manifest `1.0.0`

The result has these top-level members, in addition to `$schema` and `schema_version`:

```text
execution_disposition, request_hash, package_hash, backend,
calculations, consumed_inputs, sensitivities, obligation_outcomes,
missing_information, assumptions, limitations, applicability, coverage
```

`backend` contains `backend_id`, `contract_version`, and `numeric_profile`. `consumed_inputs` contains three contract-ordered arrays: `calculation_inputs`, `validation_inputs`, and `available_but_unused`. Each item binds slot and claim IDs and its use classification. Each obligation outcome contains a deterministic `finding_id`, exact obligation ID, `outcome`, severity, title, mechanism, calculated quantity, comparison quantity and operator, signed margin, package/request/scenario hashes, consumed claim IDs, assumptions, and limitations. The result contains no overall project verdict.

`coverage` separates the four requested obligations from broader known omissions. It records `requested_obligations=4`, `evaluated_obligations=4`, and pass/fail/indeterminate/not-evaluated counts whose sum is four. `known_uncovered_questions` contains `assembly.center_of_gravity` with the point-payload/no-part-mass reason; that question is not counted as a fifth requested obligation or emitted as a finding. Motor A counts are three pass and one fail. Motor B counts are four pass and zero fail.

The closed `numeric_profile` object is:

```json
{
  "operating_system": {"name": "macos", "release": "shape-only", "architecture": "arm64"},
  "compiler": {"id": "AppleClang", "version": "shape-only"},
  "standard_library": {"id": "libc++", "version": "shape-only"},
  "math_runtime": {"id": "apple-libSystem", "version": "shape-only"},
  "backend_build_fingerprint": "sha256:4444444444444444444444444444444444444444444444444444444444444444",
  "floating_point": {"contraction": "disabled", "fast_math": false, "rounding_mode": "to_nearest"},
  "numeric_serialization_version": "1.0.0"
}
```

Platform values above illustrate member spelling only. Production obtains all strings from the compiled/runtime identity described in Task 7; a missing value makes the profile unavailable.

`calculations` is a contract-ordered array of closed `{calculation_id, value, unit}` records for `holding_load_torque`, `acceleration_load_torque`, `required_hold_motor_torque`, `required_move_motor_torque`, `peak_motor_speed`, `available_move_torque`, `estimated_move_current`, and `estimated_peak_temperature`. Margins live on obligation outcomes so there is one classification source. `sensitivities` contains one closed efficiency-range record binding the range claim ID, minimum/maximum efficiency, hold margins at both bounds, and `crosses_zero`.

The manifest has these members:

```text
$schema, schema_version, manifest_kind, package, scenario, request,
result, assembly_artifact_hash, backend_id, backend_contract_version,
package_consumer_contract_hash, numeric_profile
```

`manifest_kind` is exactly `completed_analysis_run`; package, scenario, request, and result use the five-field stored-object reference. Replay status and wall-clock data are absent.

## Planned source layout

### Python publication side

- `backend/app/execution_contracts_v1.py`: closed consumer/scenario/request/result/manifest transport models; no equations.
- `backend/app/fixture_catalog_v2.py`: exact three-entry fixture catalog and consumer-artifact metadata.
- `backend/app/fixture_pipeline_v2.py`: catalog-driven draft construction.
- `backend/app/package_compiler_v2.py`: artifact-role derivation and ready-package compilation.
- `backend/app/api_v2.py`: fixed fixture IDs and unchanged exact export boundary.
- `backend/scripts/export_program_01b_fixtures.py`: deterministic Motor A/B and consumer vectors.
- `backend/scripts/export_contract_schemas.py`: checked-in Draft 2020-12 schemas.

### Qt-free native side

- `desktop/integrity/`: Qt-free canonical JSON plus vendored SHA-256.
- `desktop/core/include/prometheus/simulation/motor_arm_builtin_v1.hpp` and `desktop/core/src/motor_arm_builtin_v1.cpp`: typed numerical backend only.
- `desktop/execution/`: strict contracts, typed package consumer, numeric profile, finding compiler, and sole production execution entry point.
- `desktop/run_store/`: strict project v2 parsing, content-addressed object I/O, native locks, and atomic publication.
- `desktop/replay/`: shared Qt-free replay verification plus the read-only CLI argument adapter.

### Qt desktop side

- `desktop/app/exact_package_download.*`: bounded HTTP response acquisition.
- `desktop/app/project_controller.*`: Save As/open and one desktop-facing run-store owner.
- `desktop/app/execution_controller.*`: package binding, scenario confirmation, worker execution, history, and replay adapter.
- `desktop/app/engineering_controller.*`: joint and geometry adapter only.
- `desktop/ui/ComponentPackagePanel.qml`, `MotorScenarioDialog.qml`, `MotorRunPanel.qml`, and `RunHistoryPanel.qml`: explicit package/scenario/run views with no engineering calculation.

Before every task commit, run `git status --short` and `git diff --cached --check`, inspect the staged diff, and stage only files named by that task. If the worktree contains another person's change under a directory-form `git add` argument, replace that argument with the exact task-owned file paths; never absorb or revert unrelated work.

## Task 1: Define the closed Program 01B transport contracts

**Files:**

- Create: `backend/app/execution_contracts_v1.py`
- Create: `backend/tests/test_execution_contracts_v1.py`
- Modify: `backend/scripts/export_contract_schemas.py`
- Create: `schemas/package-consumer-contract-v1.schema.json`
- Create: `schemas/motor-arm-scenario-v1.schema.json`
- Create: `schemas/analysis-request-v1.schema.json`
- Create: `schemas/analysis-result-v1.schema.json`
- Create: `schemas/run-manifest-v1.schema.json`

- [ ] **Step 1: Write failing contract and schema-export tests**

Test exact schema IDs/media types, `extra="forbid"`, strict finite numbers, field-specific units, confirmation, fixed obligation order, hash spelling, stored-object five-field references, completed-only results, deterministic finding-ID spelling, bounded collections, and the absence of timestamps/random IDs/global verdicts. Add this production-authority assertion:

```python
def test_transport_models_do_not_contain_physics_or_finding_decisions():
    source = (APP_ROOT / "execution_contracts_v1.py").read_text(encoding="utf-8")
    forbidden = ("9.80665", "std::exp", "motor_torque(", "hold_margin >=", "random.")
    assert [token for token in forbidden if token in source] == []
```

Run:

```bash
cd backend
uv run --locked pytest -q tests/test_execution_contracts_v1.py tests/test_contracts_v2.py
```

Expected: FAIL because `execution_contracts_v1.py` and the five schemas do not exist.

- [ ] **Step 2: Implement strict Pydantic models**

Use `StrictStr`, `StrictBool`, uncoerced finite `StrictInt | StrictFloat`, the existing lowercase `HashId` policy, closed discriminated quantity objects, and model validators for ordered/unique arrays and graph consistency. Define these exact exported constants:

```python
SCENARIO_SCHEMA_ID = "urn:prometheus:schema:motor-arm-scenario:1.0.0"
REQUEST_SCHEMA_ID = "urn:prometheus:schema:analysis-request:1.0.0"
RESULT_SCHEMA_ID = "urn:prometheus:schema:analysis-result:1.0.0"
MANIFEST_SCHEMA_ID = "urn:prometheus:schema:run-manifest:1.0.0"
CONSUMER_SCHEMA_ID = "urn:prometheus:schema:package-consumer-contract:1.0.0"
BACKEND_ID = "motor_arm_builtin_v1"
BACKEND_CONTRACT_VERSION = "1.0.0"
OBLIGATION_IDS = (
    "motor_arm.move_torque_speed",
    "motor_arm.hold_continuous_torque",
    "motor_arm.driver_current_limit",
    "motor_arm.thermal_peak",
)
```

Models validate and serialize supplied values only. Do not add a function that derives a calculation, margin, outcome, severity, coverage, or finding.

- [ ] **Step 3: Export and check in all five schemas**

Extend `render_schemas()` with version-aware filenames rather than forcing the old v2 helper onto v1 IDs. Run:

```bash
cd backend
uv run --locked python scripts/export_contract_schemas.py
uv run --locked pytest -q tests/test_execution_contracts_v1.py tests/test_contracts_v2.py
```

Expected: PASS; a second schema export produces no diff.

- [ ] **Step 4: Commit the contract boundary**

```bash
git add backend/app/execution_contracts_v1.py \
  backend/scripts/export_contract_schemas.py \
  backend/tests/test_execution_contracts_v1.py \
  schemas/package-consumer-contract-v1.schema.json \
  schemas/motor-arm-scenario-v1.schema.json \
  schemas/analysis-request-v1.schema.json \
  schemas/analysis-result-v1.schema.json \
  schemas/run-manifest-v1.schema.json
git commit -m "feat: define Program 01B execution contracts"
```

## Task 2: Add the consumer contract and Motor A/B fixture catalog

**Files:**

- Create: `backend/app/fixture_catalog_v2.py`
- Modify: `backend/app/fixture_pipeline_v2.py`
- Create: `backend/scripts/export_program_01b_fixtures.py`
- Create: `backend/tests/test_program_01b_fixtures.py`
- Modify: `backend/tests/test_fixture_pipeline_v2.py`
- Create: `fixtures/evidence/motor-a.synthetic-v1.json`
- Create: `fixtures/evidence/motor-b.synthetic-v1.json`
- Create: `fixtures/contracts/package-consumer.motor-arm-builtin-v1.json`
- Create: `fixtures/contracts/package-consumer.motor-arm-builtin-v1.jcs`
- Create: `fixtures/contracts/package-consumer.motor-arm-builtin-v1.sha256`
- Create: `fixtures/contracts/execution-component-v2.motor-a.json`
- Create: `fixtures/contracts/execution-component-v2.motor-a.jcs`
- Create: `fixtures/contracts/execution-component-v2.motor-a.sha256`
- Create: `fixtures/contracts/execution-component-v2.motor-b.json`
- Create: `fixtures/contracts/execution-component-v2.motor-b.jcs`
- Create: `fixtures/contracts/execution-component-v2.motor-b.sha256`

- [ ] **Step 1: Write failing catalog and exact-vector tests**

Require exactly these fixture IDs:

```python
FIXTURE_IDS = (
    "prometheus.motor-a.fixture-1",
    "prometheus.motor-b.fixture-1",
    "prometheus.pm-36-gm.fixture-2",
)
```

Tests must prove:

- all source and contract hashes cover exact checked-in bytes;
- the old fixture still uses `component_input.pm_36_gm` and a blocked consumer gate;
- the new fixtures use `component_input.dc_gearmotor_v1` and one satisfied consumer gate referencing the consumer `.jcs` hash;
- the consumer artifact has role `supporting_input`, exact media type, backend/version, required slots, validation slots, scenario contract, four obligations, applicability, and `synthetic_conformance_only`;
- normalized Motor A/B parameter vectors differ only at `continuous_torque_nm` (`0.208` versus `0.320`);
- IDs, provenance, and hashes are excluded only by an explicit normalization helper in the test, never by the compiler; and
- unsupported IDs, Unicode lookalikes, caller paths, caller hashes, or caller capabilities create no rows.

Run:

```bash
cd backend
uv run --locked pytest -q tests/test_program_01b_fixtures.py tests/test_fixture_pipeline_v2.py
```

Expected: FAIL because the catalog, fixture files, and generator are absent.

- [ ] **Step 2: Create the two evidence files**

Copy the 17-parameter semantic shape of `pm-36-gm.synthetic-v2.json`, give each motor a distinct synthetic component identity, set capability-independent evidence metadata, and change only the normalized `continuous_torque_nm` value between A and B. Use:

```text
manufacturer = Prometheus Fixture Works
Motor A part_number = DC-GM-A
Motor B part_number = DC-GM-B
revision = fixture-1
component_class = dc_gearmotor
source_authority = synthetic_fixture
physical_validation_status = unvalidated
```

Outside those identity/provenance fields, use the same normalized parameter records except:

```text
Motor A continuous_torque_nm = 0.208 N*m
Motor B continuous_torque_nm = 0.320 N*m
gearbox_efficiency_range = [0.55, 0.82]
torque_speed_curve = [(0 rad/s, 1.92 N*m), (418.879 rad/s, 0 N*m)]
gearbox_lifetime = unknown and optional
```

Both files must state that they are synthetic conformance inputs and provide no physical validation.

- [ ] **Step 3: Define and render the consumer artifact**

The deterministic script constructs the `PackageConsumerContractV1` model, canonicalizes through the existing Python RFC 8785 implementation, and emits human JSON, `.jcs`, and `.sha256`. Required calculation slots and canonical units are the 12 approved in the design. Validation slots are exactly `gearbox_efficiency_range` and `torque_speed_curve`. Available-but-unused slots are exactly `nominal_voltage_v`, `supply_current_limit_a`, and `gearbox_lifetime`.

The applicability array is contract ordered and contains these exact IDs:

```text
point_payload_at_reviewed_radius
horizontal_gravity_loading
symmetric_triangular_velocity
linear_torque_speed_model
algebraic_current_estimate
one_node_periodic_rc_thermal_model
```

- [ ] **Step 4: Generalize fixture construction without changing old semantics**

Move identity/path/capability/execution-gate policy into immutable `FixtureDefinition` records. `create_fixture_draft()` resolves only a catalog ID, ingests the exact evidence artifact, and for Motor A/B also ingests the exact consumer `.jcs` as an artifact object. The old definition retains its old limitation and blocked execution gate byte-for-byte at export. New definitions use the satisfied consumer gate and replace the old “no solver” limitation with the synthetic-consumer limitation.

Each definition also supplies the exact `required_for_execution` slot-name set. Preserve the old fixture's former flags for byte identity. Motor A/B mark the 12 calculation slots plus `gearbox_efficiency_range` and `torque_speed_curve` required; `nominal_voltage_v`, `supply_current_limit_a`, and `gearbox_lifetime` are false because this backend does not consume them.

- [ ] **Step 5: Generate and verify deterministic package vectors**

Keep `backend/scripts/export_contract_fixture.py` responsible only for the old vector. The new exporter uses fixed UUIDv4 values isolated by motor namespace, validates each complete `ExecutionComponentV2`, and writes the six Motor A/B package files plus three consumer files.

All three deterministic vectors retain `package_compiler={"name":"prometheus_python","version":"0.2.0"}`. The historical exporter must reproduce its old bytes exactly; role-aware compilation must leave the live old fixture's source-only artifact role and blocked readiness unchanged.

```bash
cd backend
uv run --locked python scripts/export_contract_fixture.py
uv run --locked python scripts/export_program_01b_fixtures.py
uv run --locked pytest -q tests/test_program_01b_fixtures.py tests/test_fixture_pipeline_v2.py tests/test_contracts_v2.py
git diff --exit-code -- ../fixtures/contracts/execution-component-v2.pm-36-gm.json ../fixtures/contracts/execution-component-v2.pm-36-gm.jcs ../fixtures/contracts/execution-component-v2.pm-36-gm.sha256
```

Expected: all tests pass and the old three files have no diff.

- [ ] **Step 6: Commit the fixture portfolio**

```bash
git add backend/app/fixture_catalog_v2.py backend/app/fixture_pipeline_v2.py \
  backend/scripts/export_program_01b_fixtures.py \
  backend/tests/test_program_01b_fixtures.py \
  backend/tests/test_fixture_pipeline_v2.py \
  fixtures/evidence/motor-a.synthetic-v1.json \
  fixtures/evidence/motor-b.synthetic-v1.json \
  fixtures/contracts/package-consumer.motor-arm-builtin-v1.json \
  fixtures/contracts/package-consumer.motor-arm-builtin-v1.jcs \
  fixtures/contracts/package-consumer.motor-arm-builtin-v1.sha256 \
  fixtures/contracts/execution-component-v2.motor-a.json \
  fixtures/contracts/execution-component-v2.motor-a.jcs \
  fixtures/contracts/execution-component-v2.motor-a.sha256 \
  fixtures/contracts/execution-component-v2.motor-b.json \
  fixtures/contracts/execution-component-v2.motor-b.jcs \
  fixtures/contracts/execution-component-v2.motor-b.sha256
git commit -m "feat: add execution-ready Motor A and Motor B packages"
```

## Task 3: Compile supporting artifacts and expose all fixed fixtures

**Files:**

- Modify: `backend/app/package_compiler_v2.py`
- Modify: `backend/app/api_v2.py`
- Modify: `backend/tests/test_package_compiler_v2.py`
- Modify: `backend/tests/test_api_v2.py`
- Modify: `backend/tests/test_openapi.py`

- [ ] **Step 1: Write failing compiler role tests**

Add tests for one source artifact plus one package-consumer supporting artifact, ready-state derivation, missing consumer bytes, consumer hash corruption, a consumer gate referencing a non-artifact, the same artifact claimed as both roles, and an unrelated artifact. Require stable compiler errors and no publication mutation on every failure.

The compiler helper returns a role map, not a plain hash list:

```python
ArtifactRoles = dict[str, Literal["source_evidence", "supporting_input"]]
```

Run:

```bash
cd backend
uv run --locked pytest -q tests/test_package_compiler_v2.py -x
```

Expected: FAIL because package-consumer references are not loaded and all artifacts are emitted as `source_evidence`.

- [ ] **Step 2: Derive roles from reviewed graph edges**

Evidence and `source_artifact` gate hashes map to `source_evidence`. Hashes referenced by satisfied `package_consumer` execution gates map to `supporting_input`. Reject a role conflict, missing object, byte/hash mismatch, or unsupported media type. Sort output by artifact hash and leave readiness derived solely from execution gates.

Keep `PACKAGE_COMPILER_VERSION` at `0.2.0`; the role-aware behavior is additive for graphs that contain the new consumer gate, while the old live graph retains its source-only role and blocked readiness. Byte identity of the separate historical golden vector remains guarded by its unchanged exporter/tests.

- [ ] **Step 3: Write failing API portfolio tests**

Parameterize the complete ingest → explicit per-claim review → publication → exact export path over all three fixture IDs. Assert Motor A/B export as ready, old PM-36 as blocked, and every export has exactly one strong ETag and the exact v2 media type. Retain strict idempotency-conflict and unsupported-schema behavior.

```bash
cd backend
uv run --locked pytest -q tests/test_api_v2.py tests/test_openapi.py -x
```

Expected: FAIL because request/response models and the route still accept only the old fixture ID.

- [ ] **Step 4: Generalize the fixed API enum**

Use a shared `FixtureIdV2` literal/type from the catalog in request and response models. Validate membership before writing an ingestion job. Do not add a caller-provided path, URL, capability, artifact hash, or consumer hash to the HTTP request.

- [ ] **Step 5: Run both database dialect suites for the changed boundary**

```bash
cd backend
uv run --locked pytest -q tests/test_package_compiler_v2.py tests/test_api_v2.py tests/test_openapi.py
PROMETHEUS_TEST_POSTGRES_URL=postgresql+psycopg://127.0.0.1:55432/prometheus_program_01a_test uv run --locked pytest -q tests/test_package_compiler_v2.py tests/test_api_v2.py
```

Expected: PASS on SQLite and the already-configured local PostgreSQL 17 service. If the local service is not running, record that fact and defer only this PostgreSQL command to the final release gate; do not weaken the CI job.

- [ ] **Step 6: Commit publication support**

```bash
git add backend/app/package_compiler_v2.py backend/app/api_v2.py \
  backend/tests/test_package_compiler_v2.py backend/tests/test_api_v2.py \
  backend/tests/test_openapi.py
git commit -m "feat: publish execution-ready consumer packages"
```

## Task 4: Make canonical JSON and SHA-256 fully Qt-free

**Files:**

- Create: `third_party/picosha2/LICENSE`
- Create: `third_party/picosha2/picosha2.h`
- Modify: `third_party/manifest.json`
- Modify: `backend/tests/test_vendored_dependencies.py`
- Modify: `desktop/integrity/include/prometheus/integrity/canonical_json.hpp`
- Modify: `desktop/integrity/src/canonical_json.cpp`
- Modify: `desktop/integrity/tests/canonical_json_tests.cpp`
- Modify: `desktop/integrity/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`

- [ ] **Step 1: Add failing raw SHA-256 and link-boundary tests**

Expose tests for canonical object hashing plus raw byte/file hashing. Use these independent vectors:

```text
empty = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
abc = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
one million ASCII a = cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0
bytes 0x00 through 0xff = 40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880
```

Add a CMake test that fails if `prometheus_integrity` links any Qt target and a source scan that fails on `QCryptographicHash`, `QByteArray`, or `Qt6::Core` under `desktop/integrity`.

```bash
cmake --preset integrity-debug
cmake --build --preset integrity-debug
ctest --test-dir out/build/integrity-debug -R prometheus_integrity --output-on-failure
```

Expected: FAIL because the implementation and target still depend on Qt Core and raw byte/file hashing is not public.

- [ ] **Step 2: Vendor PicoSHA2 from one immutable upstream commit**

Acquire only `picosha2.h` and `LICENSE` from upstream commit `161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29`. The reviewed upstream is `https://github.com/okdshin/PicoSHA2`; both files are MIT licensed. Record each exact file hash, the full commit, upstream, license, and license path in `third_party/manifest.json`. Add this dependency to the closed `DEPENDENCIES` table in `test_vendored_dependencies.py`.

```bash
mkdir -p third_party/picosha2
curl --fail --location --proto '=https' --tlsv1.2 \
  https://raw.githubusercontent.com/okdshin/PicoSHA2/161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29/picosha2.h \
  --output third_party/picosha2/picosha2.h
curl --fail --location --proto '=https' --tlsv1.2 \
  https://raw.githubusercontent.com/okdshin/PicoSHA2/161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29/LICENSE \
  --output third_party/picosha2/LICENSE
shasum -a 256 third_party/picosha2/LICENSE third_party/picosha2/picosha2.h
```

Expected: two non-empty regular files. Put the two printed digests into the manifest with the `sha256:` prefix; the verifier independently recomputes them.

```bash
cd backend
uv run --locked python ../scripts/verify-vendored-dependencies.py
uv run --locked pytest -q tests/test_vendored_dependencies.py tests/test_vendored_dependency_verifier.py
```

Expected: every vendored file is accounted for and exact bytes match the manifest.

- [ ] **Step 3: Replace only the hash implementation**

Preserve canonicalization and object identity semantics. Add these public functions:

```cpp
[[nodiscard]] std::string sha256_bytes(std::string_view bytes);
[[nodiscard]] std::string sha256_file(const std::filesystem::path& path);
```

Both return `sha256:` plus 64 lowercase hex characters. `object_hash()` still first verifies canonical JSON and then calls `sha256_bytes()`. `sha256_file()` streams a regular file, rejects symbolic links and read errors, and does not impose the JSON 8 MiB limit on external CAD.

- [ ] **Step 4: Remove Qt from the target and prove corpus parity**

Add `third_party/picosha2` as a private include directory and remove the Qt link. Keep the complete shared RFC 8785 corpus and old PM-36 exact package tests unchanged. Configure integrity whenever integrity itself or execution is requested; make `headless-debug` build integrity without discovering Qt.

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug
cmake --preset integrity-debug
cmake --build --preset integrity-debug
ctest --preset integrity-debug
```

Expected: PASS and `cmake --preset integrity-debug` succeeds in an environment with no Qt package path.

- [ ] **Step 5: Commit the portable integrity boundary**

```bash
git add third_party/picosha2/LICENSE third_party/picosha2/picosha2.h \
  third_party/manifest.json backend/tests/test_vendored_dependencies.py \
  desktop/integrity/include/prometheus/integrity/canonical_json.hpp \
  desktop/integrity/src/canonical_json.cpp \
  desktop/integrity/tests/canonical_json_tests.cpp \
  desktop/integrity/CMakeLists.txt CMakeLists.txt CMakePresets.json
git commit -m "refactor: make native integrity verification Qt-free"
```

## Task 5: Add strict C++ scenario and request contracts

**Files:**

- Create: `desktop/execution/CMakeLists.txt`
- Create: `desktop/execution/include/prometheus/execution/contracts.hpp`
- Create: `desktop/execution/include/prometheus/execution/diagnostic.hpp`
- Create: `desktop/execution/src/contracts.cpp`
- Create: `desktop/execution/tests/test_support.hpp`
- Create: `desktop/execution/tests/contracts_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`

- [ ] **Step 1: Write failing strict-contract tests**

Tests call only public C++ APIs and cover:

- building the fixed scenario from 90 degrees and obtaining `1.5707963267948966` radians;
- showing the typed preview before confirmation;
- refusing serialization until `confirmed_by_user=true` and intent is non-empty;
- zero, negative, non-finite, wrong-unit, invalid-profile, and `cycle < move + hold` inputs;
- duplicate keys, unknown fields, invalid UTF-8, negative zero, unsafe integers, wrong versions, and over-limit JSON;
- fixed request obligation order, exact hash spelling, backend identity, consumer hash, and non-empty CAD entity ID;
- deterministic canonical bytes and hashes across two calls; and
- typed failures with a stable `stage` and `code`, never a partially populated object.

Use this API surface in the tests:

```cpp
namespace prometheus::execution {
struct CanonicalObject final {
  std::string bytes;
  std::string object_hash;
  std::string media_type;
  std::string schema_id;
  std::string schema_version;
};

struct ScenarioDraftDegrees final {
  double payload_mass_kg;
  double arm_radius_m;
  double rotation_degrees;
  double move_duration_s;
  double hold_duration_s;
  double cycle_duration_s;
  double ambient_temperature_c;
};

[[nodiscard]] Result<ScenarioPreview> preview_motor_arm_scenario(
    const ScenarioDraftDegrees& draft);
[[nodiscard]] Result<CanonicalObject> confirm_motor_arm_scenario(
    const ScenarioPreview& preview, std::string_view intent);
[[nodiscard]] Result<CanonicalObject> build_analysis_request(
    const AnalysisRequestDraft& draft);
}
```

Run:

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_execution_contracts --output-on-failure
```

Expected: FAIL because the execution target and APIs do not exist.

- [ ] **Step 2: Implement one strict parse/canonicalize path**

Every parser first calls `verify_canonical_bytes()` for stored objects or `canonicalize_json_bytes()` for user-built objects, then parses the verified canonical bytes with nlohmann/json. Do not parse unverified source into an engineering type. Reject all unknown fields with an explicit key-set check and validate ordered arrays before constructing the typed value.

`Diagnostic` is closed and bounded:

```cpp
struct Diagnostic final {
  std::string stage;
  std::string code;
  std::string message;
  std::optional<std::string> object_hash;
  std::optional<std::string> field;
};
```

The generic `Result<T>` contains either one value or one diagnostic, never both. Noncompleted execution uses:

```cpp
enum class ExecutionDisposition { rejected_input, unsupported, failed, cancelled };
struct ExecutionFailure final {
  ExecutionDisposition disposition;
  std::vector<Diagnostic> diagnostics;
};
```

Diagnostics are non-empty and contract ordered. `ExecutionFailure` has no result or manifest member.

- [ ] **Step 3: Wire a Qt-free execution target**

Add `PROMETHEUS_BUILD_EXECUTION` defaulting to `ON`. `prometheus_execution` links only `prometheus_integrity` and `prometheus_core`, uses nlohmann privately, and exports C++20 headers. Headless presets explicitly enable execution; integrity-only presets disable it to retain the independent verifier boundary.

- [ ] **Step 4: Pass contract tests twice**

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_execution_contracts --repeat until-fail:2 --output-on-failure
```

Expected: PASS twice with identical scenario/request bytes and hashes.

- [ ] **Step 5: Commit the C++ contract layer**

```bash
git add desktop/execution CMakeLists.txt CMakePresets.json
git commit -m "feat: add strict C++ execution contracts"
```

## Task 6: Implement the typed v2 package consumer

**Files:**

- Create: `desktop/execution/include/prometheus/execution/package_consumer.hpp`
- Create: `desktop/execution/include/prometheus/execution/supported_consumer_contract.hpp.in`
- Create: `desktop/execution/src/package_consumer.cpp`
- Create: `desktop/execution/tests/package_consumer_tests.cpp`
- Modify: `desktop/execution/CMakeLists.txt`
- Modify: `desktop/integrity/include/prometheus/integrity/canonical_json.hpp`
- Modify: `desktop/integrity/src/canonical_json.cpp`
- Modify: `desktop/integrity/tests/canonical_json_tests.cpp`

- [ ] **Step 1: Write the complete failing mutation matrix**

Load both checked-in Motor packages and the old blocked package. Mutate one field at a time, re-canonicalize only where the test is exercising semantics rather than byte integrity, and assert stable rejection stage/code for:

```text
hash, canonical bytes, schema ID/version, package kind, capability,
authority triplet, execution readiness, gate phase/state/reference,
consumer artifact role/media type/hash/length, duplicate slot name,
missing slot, extra slot, selected-claim link, cross-revision claim,
review fingerprint, review decision, required unknown, scalar shape,
unit, efficiency range, torque-speed curve, non-finite path, and limits
```

Also assert that an optional unknown `gearbox_lifetime` is accepted and retained as available-but-unused. No failed case may return a `MotorComponentInput`.

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_package_consumer --output-on-failure
```

Expected: FAIL because there is no typed consumer.

- [ ] **Step 2: Compile the checked-in consumer identity into the target**

At configure time read and strip `fixtures/contracts/package-consumer.motor-arm-builtin-v1.sha256`, determine the exact canonical byte length, and configure `supported_consumer_contract.hpp` with both values. CMake must fail if the hash spelling is invalid or the `.jcs` bytes disagree. The runtime consumer requires a matching `supporting_input` artifact and satisfied gate; it does not load a mutable descriptor or accept an override.

- [ ] **Step 3: Strengthen independent package verification**

Extend `verify_execution_component()` to validate the whole package graph needed by the C++ consumer, not merely schema identity and hash. It must reject absent identities, duplicate selected links, stale reviews, unknown execution readiness, malformed authority, and unresolved gates before typed mapping.

- [ ] **Step 4: Map exact claims into typed input**

Define `MotorComponentInput` with the 12 calculation fields, the validated efficiency range and torque-speed endpoints, and provenance bindings for every consumed/validation/unused slot and claim. Require exact slot quantity, dimension, shape, and unit. Validate the range contains the nominal efficiency and the linear curve endpoints equal the selected stall torque and no-load speed claims. Additional valid package slots remain visible in available-but-unused coverage but cannot affect the backend.

Provide a read-only `inspect_execution_component()` path that performs byte, schema, graph, authority, gate, and metadata validation and returns identity/readiness/limitations without constructing `MotorComponentInput`. The desktop uses it to keep a valid blocked package visible. `consume_motor_component()` calls the same inspection path and additionally requires the supported capability, ready state, satisfied consumer gate, and typed inputs.

The supporting consumer artifact is verified from the package's metadata reference against the compiled supported hash, media type, and byte length. Its body is not a sixth sidecar object and is not fetched at execution or replay time; the checked-in `.jcs` bytes are the reviewed source used to compile that identity.

- [ ] **Step 5: Verify A/B normalization and fail-closed behavior**

```bash
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R "prometheus_(integrity|package_consumer)" --output-on-failure
```

Expected: both packages consume successfully; normalized engineering inputs differ only in continuous torque; the old package returns `rejected_input/package_not_ready`; every mutation returns the expected diagnostic.

- [ ] **Step 6: Commit the consumer**

```bash
git add desktop/execution desktop/integrity
git commit -m "feat: consume reviewed motor packages in C++"
```

## Task 7: Isolate the authoritative backend and finding compiler

**Files:**

- Create: `desktop/core/include/prometheus/simulation/motor_arm_builtin_v1.hpp`
- Create: `desktop/core/src/motor_arm_builtin_v1.cpp`
- Modify: `desktop/core/CMakeLists.txt`
- Modify: `desktop/core/tests/core_tests.cpp`
- Modify: `desktop/core/include/prometheus/simulation/motor_arm_analysis.hpp`
- Delete: `desktop/core/include/prometheus/simulation/motor_checker.hpp`
- Create: `desktop/execution/include/prometheus/execution/numeric_profile.hpp`
- Create: `desktop/execution/include/prometheus/execution/finding_compiler.hpp`
- Create: `desktop/execution/src/numeric_profile.cpp`
- Create: `desktop/execution/src/numeric_profile_platform.cpp`
- Create: `desktop/execution/src/finding_compiler.cpp`
- Create: `desktop/execution/tests/motor_backend_tests.cpp`
- Create: `desktop/execution/tests/finding_compiler_tests.cpp`
- Modify: `desktop/execution/CMakeLists.txt`

- [ ] **Step 1: Write independent backend tests before moving equations**

State expected values directly in tests; do not call a second implementation as the oracle. Cover the fixed scenario, invalid domains, each finite intermediate, equality boundaries, minimum/maximum efficiency sensitivity, and a non-finite rejection. The fixed nominal assertions include:

```cpp
require_near(output.holding_load_nm, 15.69064, 1e-10);
require_near(output.required_hold_motor_nm, 0.224152, 1e-10);
require_near(motor_a_hold_margin, -0.0720582461900853, 1e-12);
require_near(motor_b_hold_margin, 0.4276026981690996, 1e-12);
require_near(motor_b_min_efficiency_margin, 0.12168783427572118, 1e-12);
```

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R "prometheus_(motor_backend|finding_compiler)" --output-on-failure
```

Expected: FAIL because the compiled backend and finding compiler targets do not exist yet.

- [ ] **Step 2: Move the equations into a compiled core backend**

Split the current combined `MotorArmInput` into `MotorComponentInput`, `MotorArmScenario`, and `MotorArmCalculations`. The backend owns gravity, triangular motion, linear torque-speed, current approximation, and one-node periodic RC thermal equations. It accepts typed values and returns calculations/applicability diagnostics only—no JSON, strings for UI prose, findings, persistence, or Qt types.

Delete the separate inline `motor_checker.hpp`; its load-to-motor conversion and margin are part of this backend/finding path and must not remain as a second production formula.

For task-level build continuity, reduce `motor_arm_analysis.hpp` to a deprecated, equation-free forwarding adapter over `run_motor_arm_builtin_v1()`. The existing desktop may use that adapter only until Task 13 removes its fixed motor path and deletes the adapter. Do not keep two numerical implementations at any intermediate commit.

The final Program 01B state permits only `prometheus_execution` to call `run_motor_arm_builtin_v1()` from production code. Direct test calls are allowed; the temporary forwarding adapter described above is removed before the authority release gate.

- [ ] **Step 3: Define a concrete numeric execution identity**

Generate a backend build fingerprint from the backend sources, public headers, contract version, and floating-point policy at CMake configure time. Compile the backend with `-ffp-contract=off -fno-fast-math` on GCC/Clang and `/fp:strict` on MSVC. At runtime require `FE_TONEAREST`.

Record:

```text
operating system release, architecture, compiler ID/version,
standard-library ID/version, math-runtime ID/version,
backend build fingerprint, fp_contract=disabled,
fast_math=false, rounding_mode=to_nearest,
numeric_serialization_version=1.0.0
```

Use `uname` plus `gnu_get_libc_version()` on Linux, `uname` plus `apple-libSystem` identity on macOS, and the loaded `ucrtbase.dll` file version on Windows. An unavailable identity returns `unsupported_numeric_profile`; it does not invent `unknown` and continue.

- [ ] **Step 4: Write failing finding tests**

For all four obligations test inclusive pass/fail comparisons, deterministic `finding_id = sha256(canonical({request_hash, obligation_id}))`, exact provenance, relevant claim IDs, assumptions/limitations, and range sensitivity. A thermal pass may carry caution severity because the model is simplified, but its engineering outcome remains `pass`. Add an uncovered center-of-gravity entry to coverage; do not create a fifth finding.

Require A/B comparison to change only the holding calculation/margin/outcome plus package-bound provenance. Movement, current, thermal calculations, their outcomes, and geometry state remain equal.

The sensitivity record shows that Motor A's declared efficiency range crosses zero hold margin even though its reviewed nominal outcome is fail; Motor B remains positive at minimum efficiency and has `crosses_zero=false`. Sensitivity never silently replaces the reviewed nominal outcome.

- [ ] **Step 5: Implement finding compilation in execution only**

The compiler receives typed backend output and request/package/scenario provenance. It rejects non-finite values before comparisons. It emits one outcome for each fixed request obligation, carries synthetic validation limitations, and never emits a global pass or a “center of gravity checked” information finding.

Use `(available - required) / required` for movement torque, holding torque, and driver-current signed margins, and `(temperature_limit - peak_temperature) / temperature_limit` for the thermal signed margin. In every case a nonnegative margin passes, a negative margin fails, and exact equality is a zero-margin pass without an implied safety factor.

- [ ] **Step 6: Run backend and finding tests**

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R "prometheus_(core|motor_backend|finding_compiler)" --output-on-failure
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R prometheus_cad_controller_no_occt --output-on-failure
```

Expected: headless tests pass with Motor A hold failure, Motor B hold pass, and all other normalized calculations equal; the existing desktop still builds through the equation-free compatibility adapter.

- [ ] **Step 7: Commit the authoritative calculation boundary**

```bash
git add desktop/core desktop/execution
git commit -m "feat: add authoritative motor-arm backend and findings"
```

## Task 8: Add the sole execution entry point and shared exact vectors

**Files:**

- Create: `desktop/execution/include/prometheus/execution/execute.hpp`
- Create: `desktop/execution/src/execute.cpp`
- Create: `desktop/execution/tests/execution_tests.cpp`
- Create: `desktop/execution/tools/export_program_01b_vectors.cpp`
- Modify: `desktop/execution/CMakeLists.txt`
- Modify: `backend/tests/test_execution_contracts_v1.py`
- Create: `fixtures/contracts/program-01b/motor-arm-scenario-v1.acceptance.json`
- Create: `fixtures/contracts/program-01b/motor-arm-scenario-v1.acceptance.jcs`
- Create: `fixtures/contracts/program-01b/motor-arm-scenario-v1.acceptance.sha256`
- Create: `fixtures/contracts/program-01b/analysis-request-v1.motor-a.json`
- Create: `fixtures/contracts/program-01b/analysis-request-v1.motor-a.jcs`
- Create: `fixtures/contracts/program-01b/analysis-request-v1.motor-a.sha256`
- Create: `fixtures/contracts/program-01b/analysis-result-v1.motor-a.json`
- Create: `fixtures/contracts/program-01b/analysis-result-v1.motor-a.jcs`
- Create: `fixtures/contracts/program-01b/analysis-result-v1.motor-a.sha256`
- Create: `fixtures/contracts/program-01b/run-manifest-v1.motor-a.json`
- Create: `fixtures/contracts/program-01b/run-manifest-v1.motor-a.jcs`
- Create: `fixtures/contracts/program-01b/run-manifest-v1.motor-a.sha256`
- Create: `fixtures/contracts/program-01b/analysis-request-v1.motor-b.json`
- Create: `fixtures/contracts/program-01b/analysis-request-v1.motor-b.jcs`
- Create: `fixtures/contracts/program-01b/analysis-request-v1.motor-b.sha256`
- Create: `fixtures/contracts/program-01b/analysis-result-v1.motor-b.json`
- Create: `fixtures/contracts/program-01b/analysis-result-v1.motor-b.jcs`
- Create: `fixtures/contracts/program-01b/analysis-result-v1.motor-b.sha256`
- Create: `fixtures/contracts/program-01b/run-manifest-v1.motor-b.json`
- Create: `fixtures/contracts/program-01b/run-manifest-v1.motor-b.jcs`
- Create: `fixtures/contracts/program-01b/run-manifest-v1.motor-b.sha256`

- [ ] **Step 1: Write failing end-to-end execution tests**

The public boundary is:

```cpp
struct ExecutionInput final {
  std::string package_bytes;
  std::string expected_package_hash;
  std::string scenario_bytes;
  std::string expected_scenario_hash;
  std::string request_bytes;
  std::string expected_request_hash;
};

struct CompletedExecution final {
  CanonicalObject result;
  CanonicalObject manifest;
};

using ExecutionOutcome = std::variant<CompletedExecution, ExecutionFailure>;

[[nodiscard]] ExecutionOutcome execute(const ExecutionInput& input);
```

Test exact re-verification at call time, request/object cross-reference agreement, numeric-profile availability, all rejection dispositions, result/manifest determinism, and the rule that noncompleted attempts have no result or manifest bytes. Run identical inputs twice in-process and through a helper process; require byte equality within the same numeric profile.

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_execution_end_to_end --output-on-failure
```

Expected: FAIL because the sole `execute()` entry point and result/manifest serializers do not exist.

- [ ] **Step 2: Implement the one production pipeline**

`execute()` performs these stages in order: integrity, package contract/graph, typed consumer, scenario, request, numeric profile, backend, finding compilation, result serialization, manifest serialization. It catches exceptions at the boundary and maps them to bounded stable diagnostics. It never turns a diagnostic into an engineering finding.

- [ ] **Step 3: Generate all shared vectors from C++**

The exporter consumes checked-in Motor package bytes, uses a fixed assembly hash/entity, builds the fixed scenario and both requests, invokes `execute()`, and writes human JSON, canonical bytes, and hash files. It refuses to emit a result/manifest if execution is not completed. Its only write target is the explicit `--output` directory.

```bash
cmake --build --preset headless-debug --target prometheus_export_program_01b_vectors
./out/build/headless-debug/desktop/execution/prometheus_export_program_01b_vectors --output fixtures/contracts/program-01b
```

Review the diff and confirm no wall-clock, random UUID, path, UI label, or network value entered the objects.

The checked-in result/manifest vectors record one explicitly named reference numeric profile. Other operating systems parse and schema-validate those bytes but do not claim they can reproduce that reference hash. Every native CI platform instead creates a local run and requires two in-process plus one fresh-process executions from its own build/profile to match exactly.

- [ ] **Step 4: Validate the same bytes independently in Python**

Python tests load every generated `.json`, `.jcs`, and `.sha256`, validate the human and canonical values through the Task 1 Pydantic models and Draft 2020-12 schemas, require Python RFC 8785 bytes to equal C++ `.jcs`, and require SHA-256 equality. They inspect supplied findings but do not derive them.

```bash
cd backend
uv run --locked pytest -q tests/test_execution_contracts_v1.py tests/test_contracts_v2.py
cd ..
ctest --test-dir out/build/headless-debug -R prometheus_execution --output-on-failure
```

Expected: all cross-language vector checks pass.

- [ ] **Step 5: Prove fresh-process repeatability**

```bash
vector_dir="$(mktemp -d)"
./out/build/headless-debug/desktop/execution/prometheus_export_program_01b_vectors --output "$vector_dir"
diff -ru fixtures/contracts/program-01b "$vector_dir"
```

Expected: no diff for the same build/profile.

- [ ] **Step 6: Commit execution and golden objects**

```bash
git add desktop/execution backend/tests/test_execution_contracts_v1.py fixtures/contracts/program-01b
git commit -m "feat: compile deterministic motor analysis runs"
```

## Task 9: Add the strict project v2 index and bounded object store

**Files:**

- Create: `schemas/project-v2.schema.json`
- Create: `backend/tests/test_project_v2_schema.py`
- Create: `desktop/run_store/CMakeLists.txt`
- Create: `desktop/run_store/include/prometheus/run_store/project_v2.hpp`
- Create: `desktop/run_store/include/prometheus/run_store/object_store.hpp`
- Create: `desktop/run_store/src/project_v2.cpp`
- Create: `desktop/run_store/src/object_store.cpp`
- Create: `desktop/run_store/tests/project_v2_tests.cpp`
- Create: `desktop/run_store/tests/object_store_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`

- [ ] **Step 1: Freeze the project-v2 JSON shape in failing tests**

The project retains the current CAD fields and adds this closed execution section:

```json
{
  "package_bindings": [
    {
      "binding_revision": 1,
      "supersedes_binding_revision": null,
      "cad_entity_id": "motor",
      "package": {
        "object_hash": "sha256:0000000000000000000000000000000000000000000000000000000000000000",
        "byte_length": 1,
        "media_type": "application/vnd.prometheus.execution-component+json;version=2.0.0",
        "schema_id": "urn:prometheus:schema:execution-component:2.0.0",
        "schema_version": "2.0.0"
      }
    }
  ],
  "current_scenario": null,
  "committed_runs": [],
  "events": []
}
```

Top-level members are exactly:

```text
$schema, schema_version, execution_store_version, name, cad_source,
assembly_artifact_hash, coordinate_system, length_unit, component_bindings,
placement_overrides, connections, interference_classifications,
engineering, legacy_v1_engineering_state, execution
```

`$schema` is `urn:prometheus:schema:project:2.0.0`, `schema_version` is `2.0.0`, and `execution_store_version` is `1.0.0`. `engineering` contains only joint plus geometry findings/status. `legacy_v1_engineering_state` is nullable, bounded, explicitly non-authoritative preservation from Save As, and never feeds execution.

Tests reject unknown fields, duplicate keys, invalid Unicode, unsafe numbers, bad hashes, bare hash references where five-field references are required, non-monotonic/duplicate binding revisions, cross-entity supersession, multiple unsuperseded bindings for one entity, over-limit arrays/events, invalid event sequence, and motor findings inserted into authoritative geometry state.

- [ ] **Step 2: Add failing path and object-install tests**

Test sibling derivation for `/work/arm.prometheus` → `/work/arm.prometheus.data`, digest fan-out, supported media/schema pairs, exact byte length, idempotent same-byte install, fatal different-byte destination collision, over-8-MiB input, temporary-file cleanup, and every symlink/traversal/absolute-reference case. A project path, sidecar root, `objects`, `sha256`, digest directory, object destination, or temporary file that is a symlink must reject.

Run:

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R "prometheus_run_store_(project|object)" --output-on-failure
```

Expected: FAIL because the run-store target does not exist.

- [ ] **Step 3: Implement strict project parsing and schema parity**

Use the integrity parser before nlohmann/json and explicit member-set validation in C++. Python only validates the checked-in schema against shared valid/invalid project fixtures; it does not mutate or interpret run state. C++ enforces the semantic binding/supersession and reference graph.

The mutable project writer emits deterministic UTF-8 JSON with sorted object keys, two-space indentation, and one final newline. The reader accepts noncanonical whitespace but applies the same duplicate-key, Unicode, number, depth, node, string, array, and byte limits before parsing. Project bytes are not assigned an object hash and are never cited as immutable execution evidence.

Use this core type:

```cpp
struct StoredObjectReference final {
  std::string object_hash;
  std::uint64_t byte_length;
  std::string media_type;
  std::string schema_id;
  std::string schema_version;
};
```

An event is bounded to `sequence`, `event_kind`, `status`, nullable `related_hash`, `occurred_at_utc`, and `diagnostic_code`. It is display metadata and may be trimmed from the front to 256 entries; no execution or replay decision reads it.

- [ ] **Step 4: Implement safe bounded object installation**

Object paths derive only from validated lowercase SHA-256. Create directories one component at a time while checking `symlink_status`; open temporary and destination files without following links. Verify input bytes against reference metadata before writing and destination bytes after writing. Restrict the object registry to execution package, scenario, request, result, and manifest media/schema pairs.

- [ ] **Step 5: Run native and schema tests**

```bash
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_run_store --output-on-failure
cd backend
uv run --locked pytest -q tests/test_project_v2_schema.py
```

Expected: PASS; the store does not depend on Qt, Python, network, or CAD.

- [ ] **Step 6: Commit the bounded storage substrate**

```bash
git add schemas/project-v2.schema.json backend/tests/test_project_v2_schema.py desktop/run_store CMakeLists.txt CMakePresets.json
git commit -m "feat: add bounded execution object store"
```

## Task 10: Make project updates transactional and crash-safe

**Files:**

- Create: `desktop/run_store/include/prometheus/run_store/run_store.hpp`
- Create: `desktop/run_store/src/run_store.cpp`
- Create: `desktop/run_store/src/platform_io.hpp`
- Create: `desktop/run_store/src/platform_io_posix.cpp`
- Create: `desktop/run_store/src/platform_io_windows.cpp`
- Create: `desktop/run_store/tests/run_store_transaction_tests.cpp`
- Create: `desktop/run_store/tests/run_store_contention_helper.cpp`
- Modify: `desktop/run_store/CMakeLists.txt`

- [ ] **Step 1: Write failing transaction and failure-injection tests**

Cover these operations independently:

```cpp
create_project_v2(project_path, cad_snapshot)
install_package_binding(project_path, entity_id, package_reference, package_bytes)
set_current_scenario(project_path, scenario_reference, scenario_bytes)
publish_completed_run(project_path, package, scenario, request, result, manifest)
open_read_only(project_path)
```

Inject failure before and after temporary create, write, flush, verification, object rename, project temporary write, project flush, and project replacement. Reopen after every boundary and assert either the old complete index or the new complete index—never a referenced partial run. Orphaned valid objects are allowed and remain unreferenced.

Also test two writers, a reader during a writer, five-second timeout → `store/project_busy`, process death releasing the kernel lock, and an old timestamp on an unlocked lock file not triggering deletion logic.

```bash
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_run_store_transaction --output-on-failure
```

Expected: FAIL because transactional publication and platform lock/I/O implementations do not exist.

- [ ] **Step 2: Implement native lock ownership**

Use the persistent `.writer.lock` inside the derived sibling sidecar—for example, `arm.prometheus.data/.writer.lock` for `arm.prometheus`. POSIX opens it with `O_NOFOLLOW | O_CLOEXEC` and uses `flock(LOCK_EX | LOCK_NB)` for writers and `LOCK_SH | LOCK_NB` for readers. Windows opens with `FILE_FLAG_OPEN_REPARSE_POINT`, validates it is not a reparse point, and uses `LockFileEx` exclusive/shared modes. Retry with bounded backoff until five seconds; never continue unlocked and never infer staleness from a timestamp.

In `desktop/run_store/CMakeLists.txt`, compile `platform_io_windows.cpp` only under `WIN32`; compile `platform_io_posix.cpp` only under `UNIX`. Unsupported platforms fail configuration rather than selecting a generic unlocked fallback.

- [ ] **Step 3: Implement durable same-directory replacement**

On POSIX anchor sidecar operations to directory descriptors opened with `O_DIRECTORY | O_NOFOLLOW`, use `openat`/`mkdirat`/`fstatat(AT_SYMLINK_NOFOLLOW)`/`renameat`, complete write loops, `fsync` on files, then `fsync` on the containing directory. On Windows open every directory/file with reparse-point checks, compare handle-resolved paths to the derived sibling root before and after mutation, use complete `WriteFile` loops and `FlushFileBuffers`, use `ReplaceFileW` when atomically replacing an existing project index, and use `MoveFileExW(MOVEFILE_WRITE_THROUGH)` when installing a new destination. Object installation never replaces an existing digest path until its existing bytes have been independently verified equal. Temporary names combine process ID and a process-local monotonic counter; they are never accepted from project content.

- [ ] **Step 4: Enforce publication order and idempotency**

For a completed run, install and reverify package, scenario, request, result, then manifest. Verify every manifest reference against the installed object before appending its full manifest reference to `committed_runs`. If that exact manifest is already committed, treat publication as idempotent and append only a bounded non-authoritative invocation event. A different reference at an existing digest path is fatal.

Cancellation before project replacement produces no committed run. Persistence failure returns execution disposition `failed` at the desktop boundary and never exposes the calculated result as recorded.

- [ ] **Step 5: Run repeated and subprocess contention tests**

```bash
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_run_store_transaction --repeat until-fail:5 --output-on-failure
```

Expected: PASS five times; no test relies on sleeps longer than the five-second contract timeout.

- [ ] **Step 6: Commit transactional publication**

```bash
git add desktop/run_store
git commit -m "feat: publish execution runs transactionally"
```

## Task 11: Add exact read-only replay CLI

**Files:**

- Create: `desktop/replay/CMakeLists.txt`
- Create: `desktop/replay/include/prometheus/replay/replay.hpp`
- Create: `desktop/replay/src/replay.cpp`
- Create: `desktop/replay/main.cpp`
- Create: `desktop/replay/tests/replay_cli_tests.cpp`
- Create: `desktop/replay/tests/create_replay_fixture.cpp`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`

- [ ] **Step 1: Write failing CLI acceptance tests**

Create a temporary v2 project with committed Motor A and B runs through production run-store APIs, then spawn the actual executable. Test:

- exact match and exit `0`;
- missing/extra/malformed arguments and exit `2`;
- project/store/reference/integrity failure and exit `3`;
- unavailable numeric identity or execution failure and exit `4`;
- canonical result-byte/hash mismatch and exit `5`;
- uncommitted-but-present manifest rejection;
- active writer rejection;
- missing sidecar/object, corrupt bytes, wrong length/media/schema/hash, and symlink substitution;
- missing, symlinked, or byte-changed external CAD relative to the recorded assembly hash;
- no project, sidecar, object, or event mutation after every invocation.

Exact-match stdout is one compact JSON object:

```json
{"manifest_hash":"sha256:0000000000000000000000000000000000000000000000000000000000000000","recorded_result_hash":"sha256:1111111111111111111111111111111111111111111111111111111111111111","replayed_result_hash":"sha256:1111111111111111111111111111111111111111111111111111111111111111","status":"exact_match"}
```

Failure stdout uses the same closed report with `status`, `stage`, and `code`; human detail goes to stderr. Tests validate hashes from the temporary run, not the shape-only values in the illustration above.

```bash
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_replay --output-on-failure
```

Expected: FAIL because replay support and the CLI executable do not exist.

- [ ] **Step 2: Implement shared read-only replay plus a minimal argument adapter**

Put project/object verification, same-library execution, and exact comparison in the Qt-free `prometheus_replay_support` library so the desktop and CLI cannot diverge. The CLI accepts only `--help` or the exact `--project`/`--run` pair and converts the shared typed report to stdout/exit status. Resolve neither path nor hash from environment variables. Open the store read-only, require the manifest in `committed_runs`, verify the manifest plus all referenced objects and current backend/profile, call `prometheus::execution::execute()`, and compare both bytes and hash. Do not add tolerance, substitution, update, output-file, or skip-verification options.

- [ ] **Step 3: Prove offline operation**

Add a link test showing the executable has no Qt Network, Python, HTTP, database, or backend-service dependency. Run it with deliberately invalid proxy/DNS environment values against local fixture objects and require exact match.

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_replay --output-on-failure
```

Expected: PASS for both Motor A and Motor B.

- [ ] **Step 4: Commit replay**

```bash
git add desktop/replay CMakeLists.txt CMakePresets.json
git commit -m "feat: add exact offline replay CLI"
```

## Task 12: Acquire exact package responses in the Qt adapter

**Files:**

- Create: `desktop/app/exact_package_download.hpp`
- Create: `desktop/app/exact_package_download.cpp`
- Create: `desktop/app/tests/exact_package_download_tests.cpp`
- Modify: `desktop/app/service_controller.hpp`
- Modify: `desktop/app/service_controller.cpp`
- Modify: `desktop/app/CMakeLists.txt`

- [ ] **Step 1: Write failing handcrafted HTTP tests**

Use a local `QTcpServer` to return raw responses so duplicate headers remain observable. Cover:

```text
one valid 200 response; missing/weak/unquoted/uppercase/malformed/duplicate ETag;
wrong/parameter-reordered/duplicate Content-Type; any Content-Encoding;
redirect status or redirect target; 206/Content-Range; body over 8 MiB;
declared Content-Length mismatch; duplicate Content-Length; truncated body;
network error; and ETag/body hash disagreement
```

The valid case emits exact bytes plus the lowercase expected hash only after `prometheus_integrity` verifies them. Every invalid case emits one bounded diagnostic and no package-ready signal.

```bash
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R prometheus_exact_package_download --output-on-failure
```

Expected: FAIL because the bounded exact-response adapter does not exist.

- [ ] **Step 2: Implement bounded streaming acquisition**

Set manual redirect policy. Inspect `rawHeaderPairs()` rather than normalized convenience accessors. Require status 200, exactly one exact content type, exactly one strong quoted ETag, no content encoding/range/redirect, and at most 8 MiB. If Content-Length is present it must be unique, canonical unsigned decimal, within the bound, and equal the final byte count. Abort on overflow while reading. Network completion and independent C++ verification are both required.

- [ ] **Step 3: Generalize service fixture selection and exact export**

Change `loadFixture()` to `loadFixture(const QString& fixtureId)` and expose the three fixed catalog choices to QML. Add `acquireExactPackage()` for the current published revision. Keep mutable revision JSON for review/display only. Emit:

```cpp
void exactPackageAcquired(QByteArray bytes, QString expectedObjectHash);
```

Never place package bytes in a QML property or reconstruct them from `candidate_`/`parameters_`.

- [ ] **Step 4: Run no-OCCT desktop tests**

```bash
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R "prometheus_(review_payload|exact_package_download|cad_controller_no_occt)" --output-on-failure
```

Expected: PASS without OCCT and without contacting the external network.

- [ ] **Step 5: Commit exact acquisition**

```bash
git add desktop/app
git commit -m "feat: acquire exact execution package bytes"
```

## Task 13: Move project ownership out of the CAD controller

**Files:**

- Create: `desktop/app/project_controller.hpp`
- Create: `desktop/app/project_controller.cpp`
- Create: `desktop/app/tests/project_controller_tests.cpp`
- Create: `desktop/run_store/include/prometheus/run_store/legacy_project_v1.hpp`
- Create: `desktop/run_store/src/legacy_project_v1.cpp`
- Create: `desktop/run_store/tests/legacy_project_v1_tests.cpp`
- Modify: `desktop/app/cad_controller.hpp`
- Modify: `desktop/app/cad_controller.cpp`
- Modify: `desktop/app/engineering_controller.hpp`
- Modify: `desktop/app/engineering_controller.cpp`
- Modify: `desktop/app/tests/project_tests.cpp`
- Modify: `desktop/app/main.cpp`
- Modify: `desktop/app/CMakeLists.txt`
- Modify: `desktop/ui/Main.qml`
- Delete: `desktop/core/include/prometheus/simulation/motor_arm_analysis.hpp`

- [ ] **Step 1: Write failing Save As and reopen tests**

Cover unsaved, v1, and v2 projects. Require:

- package install/bind/run reports `save_as_required` until a v2 path exists;
- Save As from v1 requires a different destination and leaves every source-v1 byte unchanged;
- CAD entity IDs, external CAD path, placement, connection, interference classification, joint, and geometry state survive conversion/reopen;
- old fixed motor scenario/findings survive only under nullable `legacy_v1_engineering_state` and never appear as a recorded run;
- the v2 file is still a regular file and the sidecar is its derived sibling;
- missing CAD reports `cad_missing` while project metadata/history still opens;
- missing sidecar reports `execution_store_missing` while CAD remains usable;
- CAD bytes changed after import report `assembly_artifact_changed` and block a new run without hiding recorded history; and
- no open operation contacts the backend service or starts replay.

```bash
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R "prometheus_(project_controller|legacy_project)" --output-on-failure
```

Expected: FAIL because the v2 project owner and strict legacy reader do not exist.

- [ ] **Step 2: Add a strict read-only legacy parser**

Use the Program 01A JSON safety policy for v1 bytes, accept only schema `1.0.0`, and map the exact old fields. It may read a v1 project and provide a conversion snapshot; it never overwrites or adds execution claims to that source file. Reject duplicate keys, malformed data, symlinks, and a Save As destination equal to the source path.

- [ ] **Step 3: Make `CadController` a CAD-state adapter**

Replace `saveProject()`/`openProject()` with bounded `snapshotCadState()` and `restoreCadState()` operations. Preserve import/geometry behavior. Compute and expose the external CAD SHA-256 at successful import and recheck it before a new request. Keep missing-path state visible instead of aborting the entire project open.

At the same time, make `EngineeringController` geometry-only: retain the reviewed joint plus static/swept collision findings under `runGeometryChecks()`, and remove the fixed motor scenario/check path. Delete its include of the temporary `motor_arm_analysis.hpp`, then delete that compatibility header from `desktop/core`. Update the existing QML call sites and project test expectations so this intermediate commit remains buildable and truthful while Task 14 adds the package-driven motor surface.

- [ ] **Step 4: Implement one desktop project owner**

`ProjectController` owns the current project path, schema version, Save As requirement, CAD availability, execution-store availability, and calls `prometheus_run_store`. It atomically creates/saves v2 indexes and coordinates CAD snapshot restoration. It exposes no equation, threshold, or finding classification.

Use these QML-facing operations:

```cpp
Q_INVOKABLE void openProject(const QUrl& path);
Q_INVOKABLE void saveAsVersion2(const QUrl& destination);
Q_INVOKABLE void saveCurrentProject();
```

Saving an already-open v2 project preserves the complete execution section loaded under the writer lock; it cannot overwrite newer committed runs with a stale CAD-only snapshot.

- [ ] **Step 5: Run no-OCCT plus OCCT project tests**

```bash
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R "prometheus_(project_controller|legacy_project)" --output-on-failure
cmake --build out/build/macos-occt-debug
ctest --test-dir out/build/macos-occt-debug -R prometheus_project_tests --output-on-failure
```

Expected: no-OCCT persistence tests pass; the existing motor-arm STEP round-trip also passes in the configured macOS OCCT build.

- [ ] **Step 6: Commit project ownership**

```bash
git add desktop/app desktop/run_store desktop/ui/Main.qml \
  desktop/core/include/prometheus/simulation/motor_arm_analysis.hpp
git commit -m "refactor: centralize versioned desktop project ownership"
```

## Task 14: Wire package binding, reviewed scenarios, execution, history, and replay

**Files:**

- Create: `desktop/app/execution_controller.hpp`
- Create: `desktop/app/execution_controller.cpp`
- Create: `desktop/app/tests/execution_controller_tests.cpp`
- Modify: `desktop/app/tests/project_tests.cpp`
- Modify: `desktop/app/main.cpp`
- Modify: `desktop/app/CMakeLists.txt`

- [ ] **Step 1: Write failing controller workflow tests**

Drive controllers without QML through this sequence: create/save v2 project, accept exact Motor A bytes, bind to stable entity `motor`, preview degrees, inspect exact radians, explicitly confirm, run, commit, bind Motor B, reuse the unchanged confirmed scenario, run, close, reopen offline, display both recorded runs, replay both, and obtain exact match.

Also cover:

```text
no package; blocked package; wrong entity; unsaved/v1 project; stale CAD hash;
unconfirmed/edited/invalid scenario; package switch; scenario switch;
double run click; cancellation before commit; stale async completion;
calculation failure; project_busy; object/project write failure;
recorded-result display without replay; numeric profile unavailable;
replay mismatch; missing object; and service/network absence
```

Assert every noncompleted attempt has a typed status and no new committed-run reference. Geometry findings and prior runs remain unchanged after motor input changes.

```bash
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R prometheus_execution_controller --output-on-failure
```

Expected: FAIL because the Qt execution adapter does not exist.

- [ ] **Step 2: Preserve the completed geometry/motor split**

Confirm Task 13 left only static/swept collision findings in `EngineeringController`. It contains no `MotorArmInput`, PM-36 value, motor formula, motor threshold, motor finding prose/classification, scenario gearbox assumption, or center-of-gravity pseudo-finding.

`ExecutionController` is the Qt adapter over `prometheus_execution`, `prometheus_run_store`, and `prometheus_replay_support`. It converts typed immutable results to `QVariant` for display only after run-store commit.

- [ ] **Step 3: Install and bind exact packages**

Connect `ServiceController::exactPackageAcquired` directly to a C++ slot. The execution controller independently inspects the bytes/hash, asks `ProjectController` to install them, and appends a package-binding revision for the pending stable CAD entity. A new binding supersedes the prior active binding for that entity but retains the prior binding and runs. A valid blocked package may be inspected/bound and shows its reason, but `canRun` remains false.

- [ ] **Step 4: Preview and explicitly confirm scenario bytes**

Expose editable draft values separately from the scenario preview (`scenarioPreview`). `previewScenarioDegrees()` calls the shared C++ builder; QML displays its exact typed radians and units. `confirmScenario(intent)` sets confirmation, canonicalizes, installs the scenario object, and updates the project reference. Editing any field clears current confirmation/result selection but does not alter old scenario/run objects or geometry.

- [ ] **Step 5: Execute and publish on a worker**

Snapshot the active binding, exact assembly hash/entity, confirmed scenario, fixed backend/consumer/obligations, and a monotonically increasing UI generation. Reload package/scenario bytes from the store, build the request, and call `execute()` via `QtConcurrent`. On success, publish all objects transactionally and only then expose recorded findings/history. Cancellation or a newer generation discards calculated bytes before publication.

- [ ] **Step 6: Display recorded results and invoke shared replay**

On open, load and verify committed manifests/results without rerunning. Mark them `Recorded`. An explicit replay action calls `prometheus_replay_support` on a worker and stores only a bounded non-authoritative observation after completion; it never edits the immutable manifest/result. UI states are exactly `Recorded`, `Exact match`, `Reproduction failed`, and `Backend identity unavailable`.

- [ ] **Step 7: Run controller and OCCT vertical tests**

```bash
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R "prometheus_(execution_controller|project_controller|exact_package_download)" --output-on-failure
cmake --build out/build/macos-occt-debug
ctest --test-dir out/build/macos-occt-debug -R prometheus_project_tests --output-on-failure
```

Expected: both Motor runs persist and replay; no-OCCT motor replay works; OCCT geometry remains separate and intact.

- [ ] **Step 8: Commit desktop orchestration**

```bash
git add desktop/app
git commit -m "feat: wire package-driven execution into desktop controllers"
```

## Task 15: Replace the fixed-demo QML with the reviewed run workflow

**Files:**

- Create: `desktop/ui/ComponentPackagePanel.qml`
- Create: `desktop/ui/MotorScenarioDialog.qml`
- Create: `desktop/ui/MotorRunPanel.qml`
- Create: `desktop/ui/RunHistoryPanel.qml`
- Modify: `desktop/ui/Main.qml`
- Modify: `desktop/app/CMakeLists.txt`
- Create: `desktop/app/tests/qml_authority_tests.cpp`

- [ ] **Step 1: Add failing QML authority and state tests**

Scan production QML for component constants, radians conversion, formulas, outcome thresholds, auto-review decisions, and direct package-byte handling. Instantiate the QML module offscreen and assert:

- Run motor analysis is disabled with no ready binding or no confirmed scenario;
- blocked packages remain visible with the backend reason;
- Save As is required before the first execution mutation;
- preview shows C++-returned radians and exact typed units before confirmation;
- component properties are read-only and scenario fields editable;
- Motor A/B history remains after switching packages;
- geometry and motor sections have independent status/findings; and
- no label says “Project works.”

```bash
cmake --build --preset desktop-no-occt-debug
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/desktop-no-occt-debug -R prometheus_qml_authority --output-on-failure
```

Expected: FAIL because the package/scenario/run components and their states are not implemented.

- [ ] **Step 2: Build the package and binding panel**

Show identity, revision, source authority/class, physical validation status, capability, readiness, limitations, abbreviated hash, and selected entity before binding. Include Motor A, Motor B, and the old blocked conformance package. Review and publication remain explicit per-claim actions through `ServiceController`; binding uses exact acquired bytes only.

- [ ] **Step 3: Build the scenario review dialog**

Prefill the fixed acceptance values as editable, unconfirmed project conditions. “Review typed values” calls C++, then a separate “Confirm scenario” action requires non-empty intent. Display rotation in both entered degrees and exact stored radians, while all other fields show stored unit/value. Do not place efficiency or another component property in this dialog.

- [ ] **Step 4: Build findings, coverage, and history views**

For each motor finding show outcome, calculated/comparison values, inclusive operator, signed margin, package/request/scenario hashes, consumed claims, backend/profile, assumptions, and limitations. Show validation-only and available-but-unused claims and the uncovered center-of-gravity question. Keep recorded versus replay status separate. Project header shows scoped counts/coverage, not one combined verdict.

- [ ] **Step 5: Replace file actions with project-controller actions**

Open/Save/Save As use `ProjectController`. If binding or scenario confirmation needs v2 storage, preserve the pending action and resume it only after successful explicit Save As. A cancelled/failed Save As leaves the action unperformed.

- [ ] **Step 6: Run the offscreen desktop suite**

```bash
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/desktop-no-occt-debug -R "prometheus_(qml_authority|execution_controller|review_payload)" --output-on-failure
```

Expected: PASS with no QML engineering authority.

- [ ] **Step 7: Commit the product workflow**

```bash
git add desktop/ui desktop/app/CMakeLists.txt desktop/app/tests/qml_authority_tests.cpp
git commit -m "feat: add reviewed motor execution workflow"
```

## Task 16: Remove duplicate authorities and enforce repository boundaries

**Files:**

- Create: `fixtures/conformance/program-01b/motor-parity-reference.json`
- Create: `backend/tests/test_authority_boundaries.py`
- Create: `backend/tests/test_research.py`
- Modify: `backend/tests/test_contracts.py`
- Delete: `backend/app/physics.py`
- Delete: `backend/tests/test_physics.py`
- Modify: `desktop/execution/tests/motor_backend_tests.cpp`

- [ ] **Step 1: Record parity before deleting the historical module**

While `backend/app/physics.py` still exists, add an assertion to its existing test that the checked-in parity fixture contains these independently recorded outputs for the fixed scenario:

```text
peak arm speed = 2.6179938779914944 rad/s
angular acceleration = 4.363323129985824 rad/s^2
holding load = 15.69064 N*m
required nominal hold torque = 0.224152 N*m
required nominal move torque = 0.24409862002279234 N*m
available movement torque = 0.7199999413330177 N*m
motor speed = 261.79938779914943 rad/s
move current = 3.4389935917595778 A
hold current = 3.172683578104139 A
historical iterative thermal peak = 58.423729595943975 degC
```

Check in the reviewed JSON with `source="historical_python_reference"` and `authority="non_authoritative_parity_only"`. Name the one intentional difference: the authoritative existing C++ model applies movement current over the whole active interval and produces approximately `62.73923846782907 degC`, while the historical Python reference used separate movement/hold currents and time stepping. Historical random efficiency percentiles are explicitly excluded rather than reproduced. The C++ backend test reads the fixture, requires shared quantities within versioned tolerances, and requires the documented thermal-model difference instead of silently treating it as parity.

```bash
cd backend
uv run --locked pytest -q tests/test_physics.py -k parity
cd ..
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R prometheus_motor_backend --output-on-failure
```

Expected: the historical fixture assertion and the C++ comparison both pass before the Python module is removed.

- [ ] **Step 2: Preserve unrelated test coverage in proper modules**

Move `choose_claim`/`model_level` assertions to research tests and legacy `ScenarioDefinition` validation to contract tests. Do not preserve center-of-gravity, tipping, random sampling, severity, or motor calculation helpers as production Python utilities.

- [ ] **Step 3: Write the failing authority scan, then delete Python physics**

Add a production source scan that rejects motor formulas, margin/outcome decisions, and finding construction in `backend/app`, then run it while the historical module still exists:

```bash
cd backend
uv run --locked pytest -q tests/test_authority_boundaries.py -x
```

Expected: FAIL naming `app/physics.py`. After that red result—and only after the parity checks in Step 1 passed—delete `backend/app/physics.py` and `backend/tests/test_physics.py`.

- [ ] **Step 4: Enforce native authority and adapter cleanliness**

The authority test permits motor constants only in checked-in fixtures, tests, and the backend's model constant definitions. It requires:

```text
no PM-36 component value in EngineeringController, ExecutionController, QML, replay, or store;
no production caller of run_motor_arm_builtin_v1 except desktop/execution/src/execute.cpp;
no second load/ratio/efficiency, torque-speed, current, thermal, or outcome implementation;
desktop and CLI link the same prometheus_execution and prometheus_replay_support targets;
no test-only fallback or verification bypass in a production target
```

- [ ] **Step 5: Run all authority checks**

```bash
cd backend
uv run --locked pytest -q tests/test_authority_boundaries.py
cd ..
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug -R "prometheus_(motor_backend|execution|replay)" --output-on-failure
rg -n "0\.208|0\.320|1\.92|418\.879|0\.0749|hold_margin *[<>]=?" backend/app desktop/app desktop/replay desktop/run_store
```

Expected: tests pass and the final `rg` has no production authority match.

- [ ] **Step 6: Commit authority cleanup**

```bash
git add fixtures/conformance/program-01b/motor-parity-reference.json \
  backend/app/physics.py backend/tests/test_physics.py \
  backend/tests/test_authority_boundaries.py backend/tests/test_research.py \
  backend/tests/test_contracts.py \
  desktop/execution/tests/motor_backend_tests.cpp
git commit -m "refactor: remove duplicate motor analysis authorities"
```

## Task 17: Extend CI and run the release gate

**Files:**

- Modify: `.github/workflows/verify.yml`
- Modify: `CMakePresets.json`
- Modify: `scripts/verify.ps1`
- Modify: `scripts/bootstrap.sh`
- Modify: `scripts/bootstrap.ps1`

- [ ] **Step 1: Make every required native job build the new boundary**

Headless builds must produce and test integrity, core, execution, run store, replay support, and replay CLI. Integrity-only remains Qt-free. Desktop-no-OCCT must build/test all headless targets plus desktop controllers/QML. Linux, macOS, and Windows each run local exact-repeatability tests; they compare classifications/tolerances across platforms but never require hashes to match different numeric profiles.

Keep Windows pinned to `windows-2022` with Visual Studio 17 2022 and Qt's MSVC 2022 ABI. Keep all third-party Actions SHA-pinned. Required desktop configuration remains fail-closed on missing Qt; do not make a skipped desktop target count as success.

- [ ] **Step 2: Run formatting, generated-byte, vendored-byte, and repository checks**

```bash
git diff --check
cd backend
uv run --locked python ../scripts/verify-vendored-dependencies.py
uv run --locked python scripts/export_contract_schemas.py
uv run --locked python scripts/export_contract_fixture.py
uv run --locked python scripts/export_program_01b_fixtures.py
git diff --exit-code -- ../schemas ../fixtures/contracts
uv run --locked pytest -q
cd ..
```

Expected: no whitespace error, no generated-file drift, and the complete SQLite suite passes.

- [ ] **Step 3: Run PostgreSQL and frontend regression suites**

```bash
cd backend
PROMETHEUS_TEST_POSTGRES_URL=postgresql+psycopg://127.0.0.1:55432/prometheus_program_01a_test uv run --locked pytest -q
cd ../frontend
npm ci
npm test
npm run build
npm audit --audit-level=high
cd ..
```

Expected: complete PostgreSQL suite, frontend tests/build, and high-severity audit pass.

- [ ] **Step 4: Run all local native presets**

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug
cmake --preset integrity-debug
cmake --build --preset integrity-debug
ctest --preset integrity-debug
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --preset desktop-no-occt-debug
```

Expected: all required targets and tests pass; target-presence assertions prove nothing was silently omitted.

- [ ] **Step 5: Reconfigure and run the macOS OCCT acceptance build**

```bash
cmake -S . -B out/build/macos-occt-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPROMETHEUS_BUILD_DESKTOP=ON \
  -DPROMETHEUS_BUILD_INTEGRITY=ON \
  -DPROMETHEUS_BUILD_EXECUTION=ON \
  -DPROMETHEUS_ENABLE_OCCT=ON \
  -DOpenCASCADE_DIR=/opt/homebrew/opt/opencascade/lib/cmake/opencascade \
  -DQt6_DIR=/opt/homebrew/opt/qt/lib/cmake/Qt6
cmake --build out/build/macos-occt-debug
ctest --test-dir out/build/macos-occt-debug --output-on-failure
```

Expected: full OCCT-enabled local suite passes.

- [ ] **Step 6: Perform the approved manual desktop workflow**

Open the built macOS app and perform all 12 release-demonstration steps from the design: assembly, Motor A fail, Motor B pass with unchanged scenario, unchanged non-hold calculations and geometry, Save As v2, close, stop service/network, reopen, recorded display, desktop replay, and CLI replay. Retain exact project path, both package/request/result/manifest hashes, numeric profile, CLI stdout/exit status, screenshots, and command outputs for Task 18. Do not embed machine-local absolute paths in immutable objects.

Start the reviewed local backend in one terminal:

```bash
cd backend
uv run --locked uvicorn app.main:app --host 127.0.0.1 --port 8000
```

Then launch the exact OCCT build from another terminal:

```bash
open out/build/macos-occt-debug/desktop/app/prometheus_desktop
```

After both runs are committed and the app is closed, terminate the backend normally, explicitly disable external network access, reopen the same executable and saved project, and perform desktop/CLI replay. Use the exact project path and full manifest hashes copied from the UI; verify each CLI exits zero and emits `status=exact_match`.

- [ ] **Step 7: Commit the verified CI and support changes**

```bash
git add .github/workflows/verify.yml CMakePresets.json \
  scripts/verify.ps1 scripts/bootstrap.sh scripts/bootstrap.ps1
git commit -m "ci: verify Program 01B execution matrix"
```

Expected: this commit contains only CI/preset/bootstrap changes whose required local commands already passed.

- [ ] **Step 8: Push and verify the complete GitHub Actions matrix**

Push only after local gates pass. Record every required job name, run URL, conclusion, and commit. A failed/cancelled/skipped required job keeps Program 01B open.

## Task 18: Record bounded Program 01B completion

**Files:**

- Modify: `README.md`
- Modify: `docs/milestone-status.md`
- Modify: `docs/program/00-master-roadmap.md`
- Create: `docs/program/01-trust-kernel/01b-package-driven-execution-completion.md`

- [ ] **Step 1: Write the completion record from verified evidence**


Only after Task 17's exact commit has a complete green required matrix, record exact implementation commits, dependency identities, local commands/outputs, CI run URLs/conclusions, Motor A/B package/request/result/manifest hashes, numeric profile, offline desktop/CLI evidence, screenshots, failure-injection coverage, known limitations, and exclusions. Do not summarize a command as passing unless its retained output says it passed.

- [ ] **Step 2: Advance only the next bounded gate**

The completion document records exact commits/dependencies/commands/results, Motor A/B values and hashes, offline evidence, failure-injection coverage, known limitations, and exclusions. Update current status to say Program 01B established one synthetic package-driven built-in backend—not arbitrary project verification, not physical validation, not universal intake, and not external solver execution. Mark Program 01C safe evidence acquisition as next; do not claim it started.

- [ ] **Step 3: Verify documentation claims and links before committing**

```bash
rg -n "arbitrary project|general engineering|physical validation|Program 01C|Program 01B" \
  README.md docs/milestone-status.md docs/program/00-master-roadmap.md \
  docs/program/01-trust-kernel/01b-package-driven-execution-completion.md
git diff --check
```

Expected: every broad term is inside an explicit limitation/non-claim, Program 01C is next but unstarted, recorded hashes/commits/run URLs are complete, links resolve, and no whitespace error is reported.

- [ ] **Step 4: Commit release evidence**

```bash
git add README.md \
  docs/milestone-status.md docs/program/00-master-roadmap.md \
  docs/program/01-trust-kernel/01b-package-driven-execution-completion.md
git commit -m "chore: close Program 01B package execution gate"
```

Expected: the commit contains truthful release documentation only; implementation and CI commits remain separately reviewable.

- [ ] **Step 5: Push and verify the final commit**

Push the documentation commit and require the same GitHub Actions matrix to finish successfully on that exact final SHA. The completion record continues to cite Task 17's implementation/CI SHA and evidence; report the final documentation SHA and its green run in the handoff without creating an endless evidence-only commit cycle.

## Final independent review checklist

Before declaring the branch ready, review the complete diff against every invariant and release-gate item in the approved design. Specifically inspect:

- every exact-byte acquisition and re-verification call site;
- every path from a package claim to a backend field and finding;
- every scenario field to prove component/scenario ownership is not mixed;
- every noncompleted disposition to prove no result/manifest/pass is emitted;
- every project/object write and failure-injection boundary;
- desktop versus CLI linkage to the same execution/replay implementations;
- history/replay behavior with the service absent;
- numeric-profile comparison and unsafe floating-point compiler flags;
- old PM-36 byte identity and blocked readiness;
- Python, QML, controller, store, and CLI authority scans; and
- status/documentation language for unsupported general-engineering claims.

Run one final clean-tree verification after review:

```bash
git status --short
git log --oneline --decorate -20
```

Expected: no uncommitted files and a task-ordered commit series ending in the Program 01B completion record.
