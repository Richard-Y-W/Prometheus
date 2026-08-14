# Prometheus

Product goal: Prometheus is a local project compiler and solver-orchestration environment for heterogeneous engineering projects. It is intended to inventory project files, reconstruct a reviewable system model, compile requirements and scenarios into proof obligations, run bounded local analyses, and report failures, scoped successes, unknowns, and coverage with reproducible provenance.

Current repository: this codebase is a fixture-backed electromechanical vertical demonstrator with the Program 01A v2 trust boundary and one bounded Program 01B package-driven C++ backend. It cannot determine whether an arbitrary engineering project works.

![Prometheus CAD workspace](docs/images/cad-workspace.png)

## What is implemented

- The Python service ingests three exact checked-in synthetic fixtures: Motor A, Motor B, and the earlier `Prometheus Fixture Works / PM-36-GM`. It does not search the web, parse public datasheets, or run an LLM research provider.
- A v2 draft contains revision-scoped parameter slots, finalized candidate claims, explicit selections, typed evidence, and capability-specific gates. Review uses stable claim IDs, claim fingerprints, required notes, append-only events, and optimistic draft versions.
- Publication validates the reviewed graph, compiles an input-only execution-component value, stores exact RFC 8785 bytes under an external SHA-256 identity, and permanently binds the revision. Motor A and Motor B declare the exact `motor_arm_builtin_v1` consumer and can become execution-ready; the sealed PM-36 package remains blocked. Export verifies and returns stored bytes; it does not reconstruct them.
- The independent C++ integrity library checks the shared canonicalization policy, exact bytes, object hash, and supported schema identity. It does not validate the package's engineering truth or execute an analysis.
- The Qt workflow exposes the v2 draft version, selected claim identities, explicit per-claim decisions, a persistent publication retry key, publication state, and the separate execution-readiness state. It acquires exact published bytes, binds them to a selected assembly entity, previews typed scenario values, and keeps package review, scenario confirmation, and execution as separate actions. There is no accept-all path.
- The Qt-free `prometheus_execution` library independently verifies and consumes the bound package, runs the sole authoritative `motor_arm_builtin_v1` calculation, and compiles typed findings. Neither Python, QML, desktop controllers, persistence, nor the replay CLI duplicates the motor calculation.
- A bounded local object store publishes package, scenario, request, result, and run-manifest bytes transactionally. The desktop and headless replay CLI reopen the same records and compare exact result bytes when the recorded numeric execution identity is available.
- The C++ decision core remains authoritative for project-summary reduction across verdict, coverage, and execution state. Geometry and motor results retain separate status, findings, and coverage; no UI label says that the whole project works.
- The native Qt/C++ desktop imports real STEP/XDE through Open Cascade when that optional adapter is enabled, preserves hierarchy and persistent entity IDs, renders tessellation, supports inspected part placement, and performs exact static or explicitly sampled collision operations.
- A separate synthetic motor-arm assembly remains available for deterministic conformance tests. Its bounded package-driven C++ checks cover torque, current, a one-node thermal model, and selected geometry behavior; they are not evidence of arbitrary mechanical or cross-domain engineering support.
- V1 review, publication, and reconstructed package export are retired with structured `410 Gone` responses. Historical v1 metadata reads remain labeled; no production v2 route imports the old reconstruction path.
- The React rough-V1 interface remains a labeled, non-executing geometry viewer.

Package integrity is a byte-identity claim only. A package remains reviewed input and contains no requirement verdict or solver result. Program 01B makes the two synthetic motor packages consumable by one declared backend; it does not turn human acceptance into physical validation or make the older blocked package executable.

No external structural, thermal, electrical, CFD, or controls solver adapter exists. The repository makes no certification claim and no project-wide correctness claim. A failed, missing, nonconverged, or unsupported analysis must remain `indeterminate` or `not_evaluated`; it cannot become a pass.

The [master roadmap](docs/program/00-master-roadmap.md) defines the gates from this trust kernel to general project intake, a semantic engineering graph, proof-obligation planning, a local solver SDK, six bounded domain slices, validation, and product hardening.

## Program 01B status — complete under the bounded contract-tested gate

Program 01B closed on 2026-08-13 at verified implementation and CI commit `dd5b915ae0fa23f0d48fb7e4f8df4a9834c9816d`. The [completion record](docs/program/01-trust-kernel/01b-package-driven-execution-completion.md) cites the successful nine-job release run, local test counts, dependency identities, Motor A/Motor B package-request-result-manifest hashes, numeric profile, failure injection, and offline desktop/CLI replay.

Under one identical reviewed scenario, synthetic Motor A's 0.208 N*m continuous rating failed the 0.224152 N*m holding requirement, while synthetic Motor B's 0.320 N*m rating passed. The other calculation outputs remained equal, both records survived save/reopen, and the shared C++ execution path reproduced their exact result hashes offline.

This is evidence for one fixed four-obligation motor-arm backend and its reusable package/run boundary. It is not arbitrary project verification, general engineering coverage, physical validation, universal intake, external solver execution, or an assembly-wide verdict. Program 01C safe evidence acquisition is next and has not started.

## Program 01A status — complete under the amended contract-tested gate

Program 01A closed on 2026-08-12 at verified implementation commit `2491df33ad6ae9032ea71f7994a3f137599e2dba`. The [amended completion record](docs/program/01-trust-kernel/01a-amended-completion.md) cites the successful nine-job GitHub release run, exact local and CI test counts, platform versions, manual byte audit, and residual risks. The [amended design](docs/superpowers/specs/2026-08-11-program-01a-amended-trust-boundary-design.md) and [v1-to-v2 migration guide](docs/migration/program-01a-v1-to-v2.md) remain the design and client-transition authorities.

The [former completion record](docs/program/01-trust-kernel/01a-integrity-and-contracts.md) is retained as historical evidence for the superseded v1 gate. Its reconstructed package and field-name review claims do not satisfy the amended boundary. Program 01B builds on the amended v2 boundary without retroactively making the sealed PM-36 package execution-ready.

The amended closure run verified:

- the full SQLite suite on CPython 3.11, 3.12, 3.13, and 3.14;
- the full semantic suite on PostgreSQL 17;
- shared Python/C++ RFC 8785 conformance and exact-package verification;
- headless decision-core, integrity, and OCCT-disabled Qt desktop builds on Linux, macOS, and Windows MSVC;
- vendored-source, frontend, migration, concurrency, restart, failure-injection, and documentation checks.

This gate establishes only the bounded synthetic-fixture, review, immutable-byte, and cross-language integrity claim. It does not establish package-driven engineering execution, physical-model validation, or arbitrary-project analysis.

## Prerequisites

- CMake 3.24+ and a C++20 compiler;
- Qt 6.5+ with Qt Quick, Quick Controls 2, Quick 3D, Shader Tools, Concurrent, Network, and Test for desktop verification;
- CPython 3.11 through 3.14 and `uv` for the backend;
- Node 20 for the archived frontend;
- optional Windows MSYS2 UCRT64 Open Cascade dependencies for the OCCT-enabled adapter path.

The required CI matrix builds the desktop with Open Cascade disabled so the trust-boundary UI and explicit adapter-unavailable seam cannot disappear behind an optional dependency. The OCCT-enabled Windows path remains a separate native geometry check. MuJoCo is optional and uninstalled; it is not a current Prometheus analysis backend.

## Verify the repository

On Windows PowerShell with the required toolchains available:

```powershell
./scripts/bootstrap.ps1
./scripts/verify.ps1
```

The script verifies locked backend and frontend dependencies, the backend and frontend suites, the headless C++ core, package execution, run storage, replay, the independent integrity library, and the Qt desktop with Open Cascade disabled. If the UCRT64 Qt and Open Cascade packages are installed, it also runs the optional OCCT-enabled target.

The GitHub Actions workflow defines the cross-platform and Python-version release matrix. A local run on one machine remains useful evidence but does not replace the successful matrices recorded in the Program 01A and Program 01B completion records.

## Run the native desktop

After installing the UCRT64 dependencies described in `scripts/bootstrap-native.ps1`:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
./out/build/windows-debug/desktop/app/prometheus_desktop.exe
```

Set `PROMETHEUS_DEMO_RESEARCH=1` only to expose the exact synthetic fixture catalog for manual review. There is no auto-accept, auto-publish, or auto-run switch.

## Run the versioned service

```powershell
./scripts/run-services.ps1
```

The health endpoint is `GET http://127.0.0.1:8000/v1/health`. The trusted flow is `POST /api/v2/fixture-ingestions`, `POST /api/v2/revisions/{revision_id}/reviews`, `POST /api/v2/revisions/{revision_id}/publication`, then `GET /api/v2/revisions/{revision_id}/execution-package`. The accepted fixture IDs are the two Program 01B motors and the older blocked PM-36 fixture; this is a closed synthetic catalog, not general intake. Use the [migration guide](docs/migration/program-01a-v1-to-v2.md) for exact bodies, status codes, and retry rules.

## Archived React viewer

```powershell
cd frontend
npm ci
npm run dev
```

Open `http://localhost:5173` with the service running. The page can load the local fixture geometry; component research and engineering execution are visibly disabled.

## Design and policy

- [Approved compiler/solver architecture](docs/superpowers/specs/2026-08-11-prometheus-general-engineering-platform-design.md)
- [Architecture boundary](docs/architecture.md)
- [Authoritative analysis backends](docs/adr/0006-authoritative-analysis-backends.md)
- [Component model](docs/component-model.md)
- [Product scope](docs/product-scope.md)
- [Validation plan](docs/validation-plan.md)
- [Validation policy](docs/validation-policy.md)
- [Threat model](docs/threat-model.md)
