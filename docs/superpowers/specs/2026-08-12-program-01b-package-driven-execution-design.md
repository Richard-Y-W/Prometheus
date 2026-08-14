# Program 01B Package-Driven C++ Execution Design

- Status: approved design; implementation pending
- Decision date: 2026-08-12
- Applies to: Program 01B, Package-Driven Execution
- Depends on: Program 01A closure commit `ae0cf2d2cba8716e78554a113f5429b5454b06b3`
- Preserves: the sealed Program 01A execution-component package and all Program 01A
  trust invariants
- Next document: a task-level implementation plan written after review of this specification

## Decision

Program 01B establishes the first package-driven C++ background execution kernel. A user binds
an exact, reviewed component-package hash to an assembly entity, reviews a motor-arm scenario,
and asks one bounded backend to evaluate four motor obligations. The same C++ library verifies
the package bytes, maps reviewed claims into typed inputs, runs `motor_arm_builtin_v1`, compiles
the findings, and emits deterministic request, result, and manifest objects.

The desktop application is the product workflow. A small replay CLI is an independent client of
the same library; it does not contain physics. Its purpose is to reopen a saved run without
Python or networking, recompute it with the recorded execution identity, and compare the exact
canonical result hash.

This program closes one evidence-to-execution loop. It does not implement a general physics
engine, arbitrary-file understanding, a requirement compiler, a capability planner, or external
solver isolation. Those later systems must reuse the execution boundary proven here rather than
generalize the motor model by renaming it.

## Current baseline and concrete gap

Program 01A publishes exact RFC 8785 execution-component v2 bytes under an external SHA-256
identity. The backend stores those bytes immutably and exports them unchanged. The independent
C++ integrity code verifies canonical bytes, the object hash, and the package schema identity.

The engineering path does not consume that object. `EngineeringController::runChecks` currently
constructs `MotorArmInput` with fixed PM-36 values, and the same controller mixes CAD collision
findings with motor findings. Saving a project serializes mutable Qt maps and rendered findings;
it does not preserve an exact package, request, backend identity, result, or replay manifest.
Consequently, the current application cannot show that a reviewed package changed an
authoritative calculation, nor can it reproduce that calculation from saved evidence.

Program 01B removes that gap without expanding the claimed physics. The existing equations in
`desktop/core/include/prometheus/simulation/motor_arm_analysis.hpp` remain the bounded numerical
backend. The work changes where their component inputs originate, how execution is authorized,
how findings are scoped, and how the run is preserved.

## Claim and non-claim

When the Program 01B release gate passes, Prometheus may claim:

> The reviewed component package and reviewed motor-arm scenario drove the recorded C++ result,
> and that result reproduced exactly offline with the recorded execution identity.

For a passing set of motor obligations, the strongest engineering summary is:

> Motor-arm checks passed within the reviewed scenario and the declared applicability of
> `motor_arm_builtin_v1`.

Program 01B does not establish that the whole assembly works. It does not validate the synthetic
component data against a manufacturer, certify the one-node thermal model, prove continuous
collision clearance, or cover structural stress, fatigue, fluids, circuits, controls, or
manufacturing. Package integrity shows which reviewed bytes were used; it does not show that the
source assertion is physically true.

## Scope

### Included

- Two new immutable synthetic component packages, Motor A and Motor B, whose normalized
  engineering-input vectors differ only in `continuous_torque_nm`.
- An execution-ready package-consumer gate tied to the exact `motor_arm_builtin_v1` consumer
  contract.
- A Qt-free C++ integrity and execution path.
- Typed package, scenario, request, result, finding, and manifest models.
- One shared `prometheus_execution` library used by the desktop and replay CLI.
- One authoritative built-in backend: `motor_arm_builtin_v1`.
- Project-local content-addressed storage and transactional run publication.
- Desktop package selection, entity binding, scenario review, execution, provenance inspection,
  history, reopen, and replay.
- A headless offline replay executable with no calculation code of its own.
- Separate geometry and motor capability runs.
- Cross-platform, adversarial, failure-injection, and manual workflow verification.
- Removal of the historical Python motor calculation after parity evidence has been recorded.

### Excluded

- Arbitrary engineering-file ingestion, OCR, evidence extraction, or semantic graph inference.
- General requirement compilation, proof-obligation planning, or backend selection.
- FEA, multibody dynamics, CFD, circuits, controls, or external solvers.
- Process-isolated solver execution, which remains Program 05 work.
- Automatic component selection, scenario synthesis, or automatic reruns after an edit.
- Multi-component propagation or assembly-wide project verdicts.
- Probabilistic uncertainty propagation or interval-certified numerics.
- Distribution of old backend binaries with a project.
- The general portable project bundle and universal artifact index assigned to Programs 01D/02.
- A claim that different platforms or math libraries produce identical final floating-point bits.
- Mutation of the sealed Program 01A package.

## Trust and authority invariants

The implementation must preserve these invariants across the service, desktop, bounded run store, and
replay client:

1. The exact stored package bytes and their expected SHA-256 object ID are the component input.
   Relational rows, UI maps, revision labels, and reconstructed JSON are not substitutes.
2. C++ re-verifies canonical bytes, hash, schema, package graph, review state, authority,
   capability, gate readiness, selected claims, types, units, and applicability at execution
   time. A prior UI verification badge is not authorization.
3. Component properties originate only in the verified package. Scenario conditions originate
   only in the reviewed scenario. Neither side silently supplies a value owned by the other.
4. `motor_arm_builtin_v1` is the sole authoritative numerical backend for this analysis. Python,
   QML, the controller, the replay CLI, and persistence code do not reproduce or alter its
   equations.
5. C++ alone compiles Prometheus obligation outcomes and findings from the backend result.
6. Every completed finding binds the exact package, consumed claim and slot IDs, scenario,
   request, backend contract, assumptions, limitations, and numeric execution profile.
7. Invalid input, a blocked gate, missing reviewed data, unsupported scope, a calculation error,
   failed persistence, or a replay mismatch cannot become a completed pass.
8. Geometry and motor analyses retain independent requests, status, provenance, and coverage.
   The UI may place them in one project view but cannot merge them into an unqualified verdict.
9. Published inputs and committed run objects are append-only. A package change, scenario edit,
   or backend change creates new objects and preserves prior runs.
10. The original recorded result is never overwritten by replay. Replay reports whether a new
    computation matched it.
11. Identical requests may deduplicate to the same content objects, but invocation timestamps and
    UI events remain non-authoritative metadata outside the deterministic hashes.
12. Unsupported and missing material stays visible with a reason. It is never dropped from
    coverage because a parser or backend cannot use it.

## Architecture

    Exact sealed package bytes + expected object hash
                         |
                         v
               prometheus_integrity
        canonical bytes, SHA-256, schema identity
                         |
                         v
               prometheus_execution
        typed package consumer + scenario validation
                         |
                         v
               motor_arm_builtin_v1
             bounded C++ numerical backend
                         |
                         v
        C++ finding and run-object compiler
                         |
              +----------+----------+
              |                     |
              v                     v
        Qt desktop adapter     prometheus_replay
              |                independent client
              +----------+----------+
                         |
                         v
        project file + bounded execution-object sidecar

The desktop and CLI call the same public execution entry point. The CLI has no alternate parser,
formula, threshold, finding builder, or serializer. The backend service ends at exact-byte
delivery and is not present during offline replay.

### Selected approach

Program 01B uses a shared C++ execution library plus replay CLI.

Two alternatives were considered and rejected for this program:

- A desktop-only execution path would make the UI the only way to validate persistence and would
  couple replay to Qt state.
- A process-isolated solver worker would anticipate the Program 05 runtime before this single,
  fast built-in calculation has proven the package and result contracts.

The selected library boundary is the future process boundary. Program 05 may place the same
versioned request and result contracts behind an isolated process without moving physics into
the desktop or Python.

### Technology boundary

- `prometheus_integrity`: standard C++20 plus checked-in JSON, number-formatting, and SHA-256
  dependencies; no Qt, Python, networking, or solver code.
- `prometheus_core`: Qt-free engineering types and the bounded motor-arm equations.
- `prometheus_execution`: Qt-free typed package consumption, request validation, backend dispatch
  for the one supported backend, finding compilation, and deterministic serialization.
- `prometheus_run_store`: bounded content-addressed execution objects, exclusive project-write
  locking, and atomic project-manifest updates. It stores bytes and references; it never
  interprets engineering values.
- Qt desktop: service access, user review, entity binding, run invocation, history, and result
  presentation.
- `prometheus_replay`: headless argument handling, project/sidecar loading, same-library
  invocation, and exact comparison.
- Python backend: synthetic evidence preparation, human-review workflow, package compilation,
  immutable publication, and exact export only.

The current integrity target links Qt Core only for `QCryptographicHash`. Program 01B removes
that dependency from the execution path. The replacement SHA-256 implementation must be
repository-vendored, license-recorded in `third_party/manifest.json`, and verified against the
existing shared corpus and standard SHA-256 vectors. This is a dependency-boundary correction,
not a second hash algorithm or a change to object identity.

## Component boundaries

### Exact package acquisition

The service exports the stored RFC 8785 bytes with the versioned media type and strong ETag. The
desktop treats the ETag as the expected object ID, passes both bytes and ID to C++, and installs
the bytes in the bounded run store only after verification succeeds. A mutable API response
that describes a revision cannot be executed.

Acquisition accepts one successful exact-export response, one strong quoted ETag containing the
lowercase `sha256:` identity, the exact v2 media type, no content encoding, and bytes within the
Program 01A package limit. A weak, absent, duplicated, malformed, redirected, partially delivered,
or metadata-inconsistent response fails before installation.

Once installed, the package is available offline. Execution always reloads the stored bytes and
re-verifies them rather than trusting an in-memory parsed object.

### Typed package consumer

The package consumer accepts only:

- schema `urn:prometheus:schema:execution-component:2.0.0`;
- package kind `component_execution_input`;
- capability `component_input.dc_gearmotor_v1`;
- authority `package_role=reviewed_input`,
  `engineering_decision_authority=prometheus_cpp`, and `authority_role=input_only`;
- `execution_readiness=ready` with every execution gate satisfied; and
- the exact supported slot, claim, review, evidence, and gate relationships.

The consumer does not implement a generic JSON-to-map conversion. It resolves every required
slot by its versioned ASCII name, follows its selected claim ID, checks the same-revision graph,
requires an effective accepted review over the current claim fingerprint, validates the declared
quantity and unit, and maps the known value into `MotorComponentInput`.

The backend's calculation inputs are:

| Package slot | Required shape | Canonical unit | C++ field |
| --- | --- | --- | --- |
| `gear_ratio` | scalar | `1` | `gear_ratio` |
| `gearbox_efficiency_nominal` | scalar | `1` | `efficiency` |
| `continuous_torque_nm` | scalar | `N*m` | `continuous_torque_nm` |
| `stall_torque_nm` | scalar | `N*m` | `stall_torque_nm` |
| `no_load_speed_rad_s` | scalar | `rad/s` | `no_load_speed_rad_s` |
| `no_load_current_a` | scalar | `A` | `no_load_current_a` |
| `torque_constant_nm_a` | scalar | `N*m/A` | `torque_constant_nm_a` |
| `driver_current_limit_a` | scalar | `A` | `driver_current_limit_a` |
| `winding_resistance_ohm` | scalar | `ohm` | `winding_resistance_ohm` |
| `thermal_resistance_k_w` | scalar | `K/W` | `thermal_resistance_k_w` |
| `thermal_capacitance_j_k` | scalar | `J/K` | `thermal_capacitance_j_k` |
| `maximum_temperature_c` | scalar | `degC` | `maximum_temperature_c` |

Program 01B accepts these exact canonical units. It does not add a general unit-conversion engine.
The UI may display typographic equivalents such as N·m, but the stored contract spelling remains
unchanged.

The consumer also validates the declared efficiency range and linear torque-speed curve against
their selected scalar claims. It records those claim IDs as validation inputs. Nominal voltage,
supply-current limit, and optional gearbox lifetime remain available-but-unused claims for this
backend and appear in coverage. An unknown optional lifetime does not block execution. An
unknown, missing, duplicated, wrongly shaped, or unit-incompatible calculation input does.

No controller or scenario fallback is permitted. Additional package slots are preserved but do
not influence this backend unless a later backend-contract version explicitly adds them.

### Reviewed scenario

The strict scenario contains only project-specific operating conditions:

| Field | Unit or enum | Validation |
| --- | --- | --- |
| `payload_mass` | `kg` | finite and greater than zero |
| `arm_radius` | `m` | finite and greater than zero |
| `rotation` | `rad` | finite and greater than zero |
| `move_duration` | `s` | finite and greater than zero |
| `hold_duration` | `s` | finite and at least zero |
| `cycle_duration` | `s` | greater than zero and at least move plus hold |
| `ambient_temperature` | `degC` | finite |
| `motion_profile` | `symmetric_triangular_velocity` | exact supported enum |

The UI may collect degrees, but it passes that value to the shared C++ scenario builder rather
than converting it in QML. C++ converts to radians and returns the exact typed value shown in the
final review. The scenario records explicit confirmation and user-stated intent. Assembly,
joint, entity, and package bindings belong to the analysis request rather than the reusable
scenario. Motor properties and gearbox efficiency do not appear in this contract.

Editing any reviewed scenario field produces new canonical bytes and a new scenario hash. A
prefilled value is not accepted until the user confirms the complete scenario.

### Authoritative backend

`motor_arm_builtin_v1` wraps the equations currently implemented by `analyze_motor_arm`. It owns
the gravitational constant, the symmetric triangular motion equations, the linear torque-speed
calculation, the algebraic current estimate, and the one-node periodic RC thermal estimate.
These are model definitions, not component facts.

The backend consumes a typed `MotorComponentInput` and `MotorArmScenario`; it never reads JSON,
Qt objects, files, databases, or network state. It returns typed calculations and applicability
diagnostics. It does not create user-facing prose or mutate persistence.

The C++ finding compiler evaluates four obligations:

1. `motor_arm.move_torque_speed`, movement torque-speed availability;
2. `motor_arm.hold_continuous_torque`, continuous horizontal holding torque;
3. `motor_arm.driver_current_limit`, driver current-limit compatibility; and
4. `motor_arm.thermal_peak`, simplified intermittent thermal limit.

Assembly center of gravity remains not evaluated because the backend models a point payload at
the reviewed arm radius and has no part-mass distribution. The result states that omission rather
than emitting the current information finding as though a center-of-gravity check ran.

The finding compiler copies relevant package limitations and model assumptions beside each
conclusion. It reports efficiency-range sensitivity separately from the nominal calculation. A
nominal pass cannot conceal that a declared range crosses the requirement boundary. Obligation
status is evaluated against the explicitly reviewed nominal efficiency; the range sensitivity
does not silently replace that value or change the scoped question. The fixed Motor B acceptance
case also passes at the declared minimum efficiency.

The four scalar acceptance comparisons are inclusive: available movement torque greater than or
equal to required torque, continuous rating greater than or equal to required holding torque,
driver limit greater than or equal to estimated current, and maximum temperature greater than or
equal to estimated peak temperature pass within scope. Equality has zero margin and no implied
safety factor. Non-finite values reject the execution before comparison.

### Bounded execution run store

Program 01B preserves the regular `.prometheus` project file and adds only the storage required
for exact package-driven runs. A version 2 project uses a sibling sidecar:

    arm.prometheus
    arm.prometheus.data/
      objects/
        sha256/
          <first two digest characters>/
            <remaining digest characters>

The sidecar path is derived rather than accepted from project content: `/path/arm.prometheus`
resolves only to `/path/arm.prometheus.data`. The mutable project file uses
`schema_version=2.0.0`, and the sidecar layout uses `execution_store_version=1.0.0`. The project
file retains the existing CAD, placement, connection, and geometry state and adds exact
package-hash bindings, the current scenario-object hash, an append-only ordered list of committed
run-manifest hashes, and bounded non-authoritative execution/replay observations. Only execution
package, scenario, request, result, and run-manifest bytes enter the sidecar. The analysis request
records the assembly artifact hash and entity ID, but Program 01B does not copy or index arbitrary
CAD and project artifacts.

Before the first package installation, binding, or run, an unsaved or version 1 project requires
explicit Save As to a new version 2 regular file. The source version 1 file is not rewritten. The
user must retain the project file, sidecar, and externally referenced CAD file. A missing sidecar
or CAD path is an explicit incomplete-project state. Programs 01D/02 own a self-contained
portable project bundle, universal object accounting, and general migration.

Run publication acquires an exclusive writer lock for the project, writes each bounded object to
a temporary file in the sidecar, flushes it, verifies bytes and hash, and atomically renames it
to the digest path. A path collision is idempotent only when the existing bytes match exactly.
After every referenced object is installed and reverified, the desktop atomically replaces the
project file with a manifest that references the new run hash, then releases the lock. A crash
may leave an unreferenced valid object but cannot leave a visible partial run. Garbage collection
is excluded from Program 01B.

Writer-lock acquisition waits at most five seconds and then returns `project_busy`; it never
continues unlocked. Stale-lock recovery requires the platform lock primitive to establish that
the owning process is no longer live. The application does not delete a lock merely because its
timestamp is old.

The store rejects symlinks, traversal, absolute sidecar or object references, digest/path
disagreement, oversized objects, unsupported media types, and referenced bytes that disagree
with their hashes. The project parser rejects duplicate keys, invalid Unicode, unsafe numbers,
and resource-limit violations even though the mutable index is not itself content addressed. The
store resolves every object path beneath the validated sibling sidecar root. Readers refuse an
actively writer-locked project. The regular project file remains a mutable local index: atomic
replacement prevents partial application writes, but it does not protect against an attacker
deleting run references. Programs 01D/02 own portable and stronger manifest integrity; immutable
object hashes still detect substitution of any retained reference.

### Desktop adapter

The desktop owns interaction, not calculation. It:

- lists exact local or service-exported packages with identity, revision, review state,
  execution readiness, limitations, capability, and abbreviated hash;
- binds a selected package hash to a stable CAD entity ID;
- displays package-derived component values as read-only and scenario values as reviewable;
- freezes the binding and reviewed scenario into an analysis request;
- invokes `prometheus_execution` on a worker thread;
- publishes the completed run through `prometheus_run_store`;
- renders scoped findings and their provenance; and
- opens recorded history and requests replay.

Changing a package creates a new binding revision. It retains earlier bindings and runs,
invalidates only the current motor result, and does not trigger execution automatically. Changing
the scenario behaves the same way. Geometry findings remain available and unchanged.

The current combined `EngineeringController::runChecks` path is split. Geometry checks continue
through the CAD capability, while motor execution enters only through the shared execution
library. `EngineeringController` becomes a Qt adapter and contains no PM-36 property, equation,
threshold, or finding classification.

### Replay CLI

The headless `prometheus_replay` executable accepts a version 2 `.prometheus` project-file path
and immutable run-manifest hash. It:

1. opens the store read-only;
2. requires the supplied manifest hash in the project's committed-run list, then verifies the
   manifest and every referenced object;
3. checks that the recorded backend and numeric execution profile are available;
4. calls the same `prometheus_execution` entry point with the stored package, scenario, and
   request;
5. compares the reproduced canonical result bytes and hash with the recorded result; and
6. reports a structured exact match or failure.

Exit status zero means exact result bytes and hash matched. A missing object, hash mismatch,
unsupported execution identity, execution failure, or result mismatch returns nonzero. The CLI
does not offer an option to skip verification, substitute inputs, tolerate numeric differences,
or update the recorded result. It opens the project and sidecar read-only and prints its replay
report; only an explicit desktop action may atomically append a non-authoritative replay
observation to the regular project file.

## Versioned contracts

The execution-component package remains schema v2. Program 01B adds four closed Draft 2020-12
contracts around it:

| Object | Schema ID | Media type | Purpose |
| --- | --- | --- | --- |
| Scenario | `urn:prometheus:schema:motor-arm-scenario:1.0.0` | `application/vnd.prometheus.motor-arm-scenario+json;version=1.0.0` | reviewed operating conditions |
| Request | `urn:prometheus:schema:analysis-request:1.0.0` | `application/vnd.prometheus.analysis-request+json;version=1.0.0` | frozen inputs and requested obligations |
| Result | `urn:prometheus:schema:analysis-result:1.0.0` | `application/vnd.prometheus.analysis-result+json;version=1.0.0` | calculations, findings, coverage, diagnostics |
| Manifest | `urn:prometheus:schema:run-manifest:1.0.0` | `application/vnd.prometheus.run-manifest+json;version=1.0.0` | immutable object graph and execution identity |

Every contract:

- forbids unknown properties;
- uses explicit schema and semantic versions;
- uses RFC 8785 canonical bytes and an external lowercase `sha256:` identity;
- rejects duplicate keys, invalid Unicode, unsafe numbers, negative zero, non-finite numbers,
  excessive depth, excessive nodes, and excessive byte length under the Program 01A policy;
- uses bounded arrays and strings;
- gives ordered arrays an explicit semantic order; and
- rejects unknown major versions rather than silently upgrading them.

Findings are a closed embedded definition inside the result rather than separately mutable
objects. The mutable project index has its own version 2 JSON Schema, but it is not canonical
execution evidence and is not one of these four immutable run contracts.

There is no in-place migration during replay. A future importer may create a new request and run
under a new contract, but it must retain the original objects and provenance.

### Analysis request

The request binds:

- exact package, scenario, and assembly-artifact hashes;
- the bound CAD entity ID;
- backend ID `motor_arm_builtin_v1` and backend contract version `1.0.0`;
- the four fixed obligation IDs in the order declared above;
- package-consumer contract hash; and
- request-contract version.

The request contains no wall-clock time, random identifier, UI label, network address, or mutable
database revision. Its object hash is the deterministic request identity.

### Analysis result

The result records:

- request and package hashes;
- backend ID, contract version, and numeric profile;
- normalized typed calculations with units;
- calculation-input and validation-input claim/slot IDs;
- available-but-unused claims;
- one outcome per requested obligation;
- missing information, limitations, assumptions, applicability, and coverage; and
- deterministic diagnostics that affect interpretation.

Finding identity derives from the stable obligation ID and request hash. It is not a random UUID.
Volatile log text, thread IDs, timing measurements, and invocation timestamps are excluded.
The Program 01B result contract requires `execution_disposition=completed`. Rejected,
unsupported, failed, and cancelled attempts return a typed execution failure with stable
diagnostics, but they do not create an analysis-result object or run manifest. The desktop may
append their non-authoritative attempt status to the project UI log without presenting an
engineering result.

### Run manifest

The manifest binds the exact package, scenario, request, result, assembly artifact, backend
contract, and numeric-profile identities. It contains no replay status because replay occurs after
the immutable run exists. The desktop may store a non-authoritative replay observation in the
regular project file without modifying the manifest or any run object.

Every stored-object reference in the project file or manifest records its hash, byte length,
media type, schema ID, and schema version. Replay verifies all five fields before parsing the
object.

The manifest hash is the immutable run identity. Repeating the same request with the same
execution identity may resolve to the same result and manifest objects; separate bounded
non-authoritative project events may record when the user requested each invocation.

## Motor A/B acceptance pair

The sealed Program 01A fixture remains blocked and unchanged. Program 01B creates two new draft
revisions, reviews them, and publishes two new exact packages after the package-consumer contract
exists.

Both packages use the same semantic component-input vector except:

| Package | `continuous_torque_nm` | Expected nominal hold outcome |
| --- | ---: | --- |
| Motor A | 0.208 N·m | fail |
| Motor B | 0.320 N·m | pass |

Component/revision IDs, claim IDs, review-event IDs, evidence hashes, package hashes, and labels
necessarily differ as provenance. Those identity fields are not part of the normalized
engineering-input-vector comparison.

The fixed acceptance scenario is:

- payload: 8 kg;
- arm radius: 0.20 m;
- rotation: π/2 rad;
- move duration: 1.2 s;
- hold duration: 4 s;
- cycle duration: 10 s;
- ambient temperature: 35 °C; and
- motion profile: symmetric triangular velocity.

At the reviewed nominal gearbox efficiency of 0.70, the backend calculates a 15.69064 N·m load
and a required motor holding torque of 0.224152 N·m. Motor A therefore has a nominal signed margin
of approximately -0.072058 and fails. Motor B has a nominal signed margin of approximately
0.427603 and passes. At the declared minimum efficiency of 0.55, Motor B retains an approximately
0.121688 positive margin; the A/B demonstration therefore does not hide a range-crossing pass for
Motor B.

Changing continuous torque must not change movement torque-speed, current, or thermal
calculations. It changes the holding margin and holding outcome. All findings still bind the
selected package hash, so the complete result objects differ even where a numerical subresult is
equal.

The consumer contract itself is a canonical, content-hashed supporting-input artifact included
in each new package. It records backend ID/version, accepted package schema and capability,
required slots/shapes/units, supported scenario contract, obligation IDs, applicability, and
`validation_level=synthetic_conformance_only` under media type
`application/vnd.prometheus.package-consumer-contract+json;version=1.0.0`. The satisfied
`package_consumer` execution gate references that artifact hash, and the compiled C++ consumer
checks it against its supported contract hash. The package therefore reports
`execution_readiness=ready`; the old Program 01A package remains
`execution_readiness=blocked`.

This consumer-contract artifact is the `backend contract` referenced by requests, results, and
manifests; Program 01B does not define a second descriptor with overlapping meaning.

Here, ready means that the reviewed package has a compatible, versioned consumer. It does not
upgrade synthetic source authority or physical-validation status, and every resulting finding
retains the synthetic-conformance limitation.

## Execution and persistence flow

### Acquire and bind

1. The desktop requests an exact sealed export.
2. C++ verifies bytes and expected ETag before installation.
3. The bounded run store installs the package under its object hash.
4. The user binds that hash to a CAD entity.
5. The binding record is append-only; replacement creates a successor.

### Review and freeze

1. The UI displays package values, provenance, model assumptions, and applicability.
2. The user confirms the scenario.
3. C++ serializes and verifies the canonical scenario.
4. The desktop freezes package, assembly, entity, scenario, backend, consumer contract, and
   obligations into the canonical request.

### Execute and publish

1. Execution reloads and re-verifies the package and scenario.
2. The consumer maps reviewed values into typed inputs.
3. The backend runs once and returns typed calculations.
4. C++ compiles findings and coverage.
5. The result and manifest are canonicalized and hashed.
6. The bounded run store installs all objects, reverifies their references, and atomically
   publishes the run reference in the project file while holding the project writer lock.

Cancellation before transaction commit leaves no completed run. The motor calculation is short
and synchronous inside the worker task; Program 01B does not add cooperative solver cancellation.
If the user cancels after calculation but before publication, calculated bytes are not presented
as a committed result.

### Reopen and replay

The desktop may display the immutable recorded result immediately and labels it Recorded. Replay
is a separate action. With the service stopped and networking unavailable, the desktop or CLI
loads exact local objects, recomputes through the shared library, and compares exact result bytes.
The UI labels the new state Exact match, Reproduction failed, or Backend identity unavailable.
It never relabels the original engineering outcome.

## Failure and status model

Execution disposition and engineering outcome are separate axes.

| Execution disposition | Meaning |
| --- | --- |
| `completed` | the authoritative backend produced and the store committed a valid result |
| `rejected_input` | integrity, contract, review, unit, gate, or request validation failed |
| `unsupported` | the requested capability, version, or applicability is unavailable |
| `failed` | calculation, serialization, integrity processing, or persistence failed |
| `cancelled` | the attempt stopped without a committed result |

Only a completed result may contain engineering conclusions. Each requested obligation has one
of these outcomes:

- `pass`: the requirement is satisfied within the declared scenario and applicability;
- `fail`: the requirement is violated within that scope;
- `indeterminate`: the backend supports the question but reviewed evidence cannot decide it; or
- `not_evaluated`: the question lies outside this backend's declared capability.

A malformed required value is rejected input rather than an indeterminate engineering result. A
valid optional unknown can yield indeterminate coverage for the question it would unlock. A
recognized question outside backend applicability is not evaluated. A backend exception,
non-finite output, or persistence failure is execution failure. None of those cases produces a
finding that says pass. Noncompleted attempts return typed diagnostics and no canonical result or
run manifest.

Diagnostics use stable stage and error codes plus bounded human-readable context. They name the
offending object, slot, claim, unit, contract, or store operation without interpreting the error
as an engineering conclusion.

Specific fail-closed cases include:

- package bytes or ETag do not match;
- canonical re-emission differs from stored bytes;
- package schema, capability, authority, or consumer-contract hash is unsupported;
- package readiness is blocked or any execution gate is unresolved;
- a selected claim is absent, duplicated, stale, cross-revision, unreviewed, unknown when
  required, wrongly shaped, or unit-incompatible;
- scenario confirmation is absent or a value violates the strict domain;
- request references disagree with the supplied objects;
- arithmetic emits a non-finite value or runs under an unsupported numeric profile;
- a sidecar path, object bytes, length, media type, or project reference disagrees;
- transaction publication fails or is interrupted; and
- replay obtains different canonical result bytes or a different hash.

## Determinism and numeric execution identity

RFC 8785 makes serialization deterministic for one value tree, but the current thermal equation
uses `std::exp`. Different standard libraries or architectures can return different final binary64
bits. Program 01B therefore conditions exact replay on the recorded numeric execution identity.

The numeric profile records:

- operating system and architecture;
- compiler identity and version;
- C++ standard-library and math-runtime identity;
- backend build/configuration fingerprint;
- floating-point contraction and fast-math policy;
- required rounding mode; and
- numeric serialization version.

Production builds disable unsafe fast-math transformations. Execution requires the supported
round-to-nearest mode and finite binary64 inputs and outputs. The profile is generated at build
time and exposed by the shared library, so the desktop and replay CLI from the same build declare
the same backend execution identity.

Exact replay requires that identity and exact canonical result bytes. A different profile is
reported as unavailable, not approximately reproduced. Cross-platform CI checks the same
engineering classifications and versioned numeric tolerances independently on each platform; it
does not compare platform result hashes unless profiles are identical.

Program 01B does not quantize outputs to manufacture equality, silently tolerate a delta, or
claim that a compatible-looking backend is the recorded backend. A future deterministic-math
backend would require a new backend contract version and a new run.

## Desktop workflow

### No package bound

Geometry remains usable. The motor capability reads Not evaluated — no verified component
package bound. The Run motor analysis action is disabled.

### Package selection and binding

The component panel shows identity, revision, source class, review state, execution readiness,
capability, limitations, and hash before binding. A blocked package remains visible with its
reason but cannot execute. The user selects Motor A or Motor B and confirms the target CAD entity.

### Input review

The review view separates:

- read-only package-derived component properties;
- editable scenario conditions;
- backend assumptions and applicability;
- validation-only and available-but-unused package claims; and
- currently uncovered questions.

The values and units shown in the final confirmation are the typed values C++ will execute.

### Findings and coverage

Motor findings appear under a distinct Motor arm capability run. Each finding shows outcome,
calculated value, allowable or required value, signed margin, unit, package hash, consumed claims,
scenario hash, backend identity, assumptions, and limitations. Geometry findings remain under
their own capability heading.

The project header reports counts by outcome and coverage. It does not collapse a motor pass and
geometry status into Project works.

### History and replay

The run history retains Motor A and Motor B. Selecting a run shows its recorded result and replay
state separately. Reopen does not automatically rerun or contact the backend service. The user
may request replay from the desktop or copy the immutable manifest hash into the CLI.

## Verification strategy

### C++ integrity and package-consumer tests

The existing shared RFC 8785 corpus remains mandatory. New tests cover the portable SHA-256
implementation and the complete Motor A/B exact-byte vectors. A mutation matrix changes one
integrity or semantic field at a time and verifies the stable rejection stage and code.

The package-consumer matrix covers every calculation input, validation input, optional unknown,
missing slot, duplicate name, selected-claim link, review fingerprint, gate, authority field,
capability ID, shape, unit, range, curve consistency, limit boundary, non-finite path, and resource
limit. Tests construct no `MotorComponentInput` after a rejection.

### Backend and finding tests

Backend tests use independently stated fixture values and expected numeric tolerances rather than
calling the production calculation as their oracle. They cover validation boundaries, each of
the four obligation thresholds, non-finite intermediate rejection, and the fixed Motor A/B
scenario.

Finding tests verify deterministic IDs, status mapping, provenance completeness, limitation
propagation, efficiency-range sensitivity, and the absence of a global project pass. Only the
holding margin and holding outcome may change in the normalized A/B calculation comparison.

### Contract and determinism tests

Python schema tests and independent C++ parsers read the same scenario, request, result, and
manifest vectors. Each vector includes human-readable JSON, expected canonical bytes, and
expected SHA-256. Unknown fields, wrong versions, shuffled contract-ordered arrays, duplicate
keys, unsafe numbers, and resource limits fail closed.

The same process runs an identical request twice and requires byte-identical result and manifest
objects. A fresh process from the same build repeats the check. Native Linux, macOS, and Windows
jobs each enforce local exact repeatability.

### Store, concurrency, and failure-injection tests

Tests corrupt or substitute every object type; disagree on length, media type, and hash; insert a
symlink or traversal path; contend for the exclusive writer lock; interrupt each object install
and project-manifest replacement boundary; reopen after a simulated crash; and provide a
mismatched project reference. No case exposes a partial completed run. Identical object
installation is idempotent; a hash collision with different bytes is fatal.

Version 1 Save As tests preserve the source file, restore entity IDs and existing project state,
create a valid version 2 regular file and sibling sidecar, and report the still-external CAD path.
A missing legacy CAD artifact remains visible rather than being presented as a portable project.

### Desktop integration and manual acceptance

Automated Qt tests exercise package acquisition, binding, scenario confirmation, motor execution,
history, Save As conversion, close, reopen, recorded-result display, and replay. Tests verify that
switching packages invalidates only the current motor run and retains geometry state and both
historical runs.

The release demonstration performs this exact path:

1. Open the checked-in motor-arm STEP assembly.
2. Acquire and bind Motor A.
3. Review the fixed acceptance scenario.
4. Run and inspect the failed holding-torque finding.
5. Bind Motor B without changing the scenario.
6. Run and inspect the passed holding-torque finding.
7. Confirm the other normalized numerical results and geometry result did not change.
8. Save the version 2 project file and its execution-object sidecar, then close the application.
9. Stop the Python service and disable network access.
10. Reopen the project file with its sibling sidecar and display both recorded runs.
11. Replay both in the desktop and with the CLI from the same build.
12. Record exact result-hash matches and the verification commands in the completion document.

The manual workflow runs in an OCCT-enabled desktop build. CI also retains the required no-OCCT
desktop build and tests so geometry availability cannot become a hidden condition for motor replay.

### Authority regression checks

Repository scans and link-boundary tests verify:

- no PM-36 component constant remains in `EngineeringController`, QML, replay, or another
  production adapter;
- production Python contains no motor equation, threshold, finding, or project verdict;
- only `prometheus_execution` invokes `motor_arm_builtin_v1` in production;
- both desktop and CLI link the shared execution implementation; and
- no test-only fallback is compiled into a production target.

The historical `backend/app/physics.py` is removed only after a parity test records the expected
C++ values. Its deletion closes the duplicate-authority risk; it is not replaced with another
Python reference calculation.

## Release gate

Program 01B is complete only when all of the following are true:

1. New Motor A and Motor B execution packages publish as immutable, exact, execution-ready v2
   objects; the Program 01A package remains byte-identical and blocked.
2. The normalized A/B engineering vectors differ only in `continuous_torque_nm`.
3. The C++ consumer independently rejects invalid, blocked, unsupported, unreviewed, incomplete,
   unit-incompatible, or tampered inputs.
4. No production motor property remains hardcoded outside the package fixtures.
5. The desktop binds an assembly entity to an exact package hash and shows the executed typed
   values before confirmation.
6. Motor A fails and Motor B passes the holding-torque obligation under the identical reviewed
   scenario, while other normalized calculations remain equal.
7. Every completed finding traces to exact input claims, scenario, request, backend, assumptions,
   limitations, and numeric profile.
8. Geometry and motor runs remain separate in storage, status, UI, and coverage.
9. Both immutable runs survive save, close, and reopen without service or network access.
10. Desktop and CLI replay with the recorded execution identity reproduce exact canonical result
    bytes and hashes.
11. Tampering, interruption, unsupported identity, calculation failure, persistence failure, or
    replay mismatch never becomes a completed pass.
12. Production Python performs no motor calculation or finding decision.
13. SQLite and PostgreSQL backend tests, shared contract tests, Qt-free C++ tests, no-OCCT desktop
    tests, OCCT-enabled tests, and native Linux, macOS, and Windows CI all pass without weakening
    the required target or test set.
14. The completion document records exact commits, dependency identities, commands, outputs,
    manual workflow evidence, known limits, and the next unimplemented roadmap boundary.

Passing this gate establishes the reusable evidence-to-execution kernel pattern. It does not
advance the roadmap labels for arbitrary intake, planning, or general solver execution.
