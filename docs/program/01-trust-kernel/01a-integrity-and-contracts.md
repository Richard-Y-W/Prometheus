# Program 01A: integrity and contracts

- Status: historical completion under the former gate; superseded by the amended completion on 2026-08-12
- Originally closed: 2026-08-11
- Former validation level: `contract_tested`
- Verified former implementation commit: `a89ce37fb6e43f97d4df22ad6d1231f3a6bf20c7`
- Milestone-record commit: the commit containing this file with subject `chore: close Program 01A trust kernel`
- Reopening authority: [Program 01A amended trust-boundary design](../../superpowers/specs/2026-08-11-program-01a-amended-trust-boundary-design.md)
- Current plan: [Program 01A amended implementation plan](01a-amended-implementation-plan.md)
- Current completion record: [Program 01A amended completion](01a-amended-completion.md)

This document preserves what the repository implemented and tested at the time. Its former completion claim was superseded because publication reconstructed mutable rows, used a Python-specific canonical form, reviewed field labels rather than stable claims, and lacked durable concurrency and replay semantics. The amended gate later closed those specified gaps without erasing the tests recorded here.

## What this milestone establishes

Program 01A establishes a trustworthy reviewed-input boundary. It prevents the one synthetic fixture from impersonating public research, prevents absent review decisions or blank reviewer identities from becoming acceptance, and binds a published component revision to canonical hash-verifiable bytes. Every published metadata, cached-research, repeat-publication, and package-export path rechecks that binding before returning success.

This milestone does not establish that the fixed motor-arm checker consumes the package, that any physical design is valid, or that Prometheus can evaluate an arbitrary engineering project.

## Implemented behavior

- The fixture provider accepts only `Prometheus Fixture Works / PM-36-GM`. It hashes the exact checked-in synthetic source bytes, uses only the fixture URI for attribution, and rejects caller-supplied URLs or unsupported identities before database mutation.
- Engineering values use typed scalar, range, enumeration, curve, and unknown shapes. Contract validation rejects non-finite numbers, reversed ranges, duplicate names or evidence IDs, dangling evidence references, non-increasing curves, malformed source URIs, and blank reviewer identities.
- Every parameter requires one explicit review decision. Missing, duplicate, unknown, rejected, or ambiguous decisions cannot silently produce a publishable revision. Ambiguous and rejected decisions require notes. A failed review request does not partially modify evidence.
- Publication changes status, captures the publication time, builds and validates the package, assigns the canonical SHA-256 hash, rebuilds from persisted rows, verifies the hash, and commits once. Validation failure rolls the transaction back.
- `GET /v1/component-revisions/{revision_id}/execution-package` returns byte-stable canonical JSON only for a published revision. Persisted drift also blocks repeat publication, published job/revision metadata, and cached-research reuse rather than taking an idempotent success path.
- The package declares `package_role=reviewed_input` and `engineering_decision_authority=prometheus_cpp`. It contains no finding or verdict.
- The Qt workflow starts every decision row empty, validates one user-produced decision per displayed parameter, requires a named reviewer, and keeps review and publication as separate requests. The removed accept-all and demo auto-publish paths are absent.
- The unversioned Python evidence, planning, and execution routes return structured `410` or `501` failures. `backend/app/physics.py` is retained only as a historical equation reference; no production route imports it.
- The React rough-V1 UI is a labeled, non-executing geometry viewer.

## Verification evidence

The final backend verification used Python 3.11.15 from the existing repository virtual environment because `uv` was not installed on this machine. Frontend verification used Node 24.14.1 and npm 11.11.0. Native checks used CMake 4.4.2, Ninja 1.13.2, and Qt 6.11.1 on macOS.

| Gate | Command or procedure | Exact result |
| --- | --- | --- |
| Python environment | `python -m pip check` | `No broken requirements found.` |
| Backend suite | `python -m pytest -q` from `backend/` | `59 passed, 1 warning`; the warning is the installed FastAPI/Starlette `httpx` deprecation notice |
| OpenAPI snapshot | `python backend/scripts/export_openapi.py`, then the full backend suite | Generated document exactly matched `app.openapi()` |
| Fresh migration | `PROMETHEUS_DATABASE_URL=sqlite:////private/tmp/prometheus-01a-verification.db alembic upgrade head`, then `alembic current` | Upgraded `cd418805b2c6 -> 7b6d91e2a4f0`; current is `7b6d91e2a4f0 (head)` |
| Frontend install | `npm ci` | 182 packages installed; npm reported 0 vulnerabilities |
| Frontend tests | `npm test` | 1 file and 4 tests passed |
| Frontend build | `npm run build` | Passed; Vite retained the non-blocking greater-than-500-kB bundle warning |
| Frontend audit | `npm audit --audit-level=high` | `found 0 vulnerabilities` |
| Headless C++ | `cmake --preset headless-debug`, `cmake --build --preset headless-debug`, `ctest --preset headless-debug --output-on-failure` | 1 of 1 core test passed |
| Qt review contract | Configure `out/build/macos-qt-debug` with Qt, desktop enabled, and OCCT disabled; build `prometheus_review_payload_tests`; run its CTest filter | 1 of 1 review-payload test passed |
| QML parse/lint | `qmllint -W -1 -I /opt/homebrew/opt/qt/qml desktop/ui/Main.qml` | Exit 0 and 0 errors. The file has 722 warnings, predominantly the inherited unqualified-identifier backlog; the base commit has 675 warnings. This is not recorded as a clean lint gate. |
| Forbidden-path search | The three `rg` commands from the implementation plan | All returned no matches for auto-publish, caller-attributed fixture URLs, or production Python-physics imports |
| Manual package inspection | Exact fixture research, draft export, explicit 17-field review, separate publish, and two exports through `TestClient` on a fresh temporary database | Draft export 409; review `reviewed`; publish `published`; exports byte-identical; observed hash `sha256:50a5cd33de797cd68df67aa781dfd9e51f2b28347a70082ab5687e455ded1c23`; only source URI `fixture://prometheus/pm-36-gm/fixture-1`; authority `prometheus_cpp`; no verdict key |

## Independent trust review

The closing review examined publication transaction boundaries, partial-review mutation, hash exclusions, semantic JSON references, source attribution, legacy-route reachability, Qt defaults, and error-to-success conversions.

It found and corrected two high-severity integrity paths in commit `a89ce37fb6e43f97d4df22ad6d1231f3a6bf20c7`:

1. Repeat publication previously returned an idempotent success without revalidating a published hash. Published drift now blocks repeat publication, metadata reads, and cached reuse with `execution_package_hash_mismatch`; the cached request creates no row.
2. A whitespace-only reviewer previously passed the API's length check. The API now trims and rejects it, while the Pydantic and JSON Schema evidence contracts reject blank audit identities.

No critical or high-severity finding remained after correction and the 59-test rerun. Medium residual risks are listed below rather than converted into pass claims.

## Checks not completed

- Native Windows verification was not run because this host is macOS and does not have the documented UCRT64 Qt/Open Cascade toolchain. The Windows desktop, CAD, project, and review integration therefore remain unverified for this commit.
- A full macOS Qt desktop build with `PROMETHEUS_ENABLE_OCCT=OFF` fails before linking because the existing CAD controller unconditionally includes `prometheus/cad/step_importer.hpp`. The isolated Qt review-payload target passes, and QML has no parse errors, but this does not substitute for launching and manually exercising the complete application.
- Open Cascade was not installed for a native macOS OCCT-enabled build. Headless tests do not exercise Qt, STEP/XDE import, project reopen, or OCCT collision behavior.

These gaps limit the validation level to `contract_tested`; they are not evidence that the unavailable paths pass.

## Unsupported behavior

- The C++ motor-arm checker still contains fixed PM-36 values and does not fetch, validate, or parse the published package.
- No external structural, thermal, circuit, CFD, or controls solver adapter exists.
- Public web research, datasheet acquisition, PDF parsing, chart digitization, OCR, and LLM candidate extraction are absent.
- The content-addressed artifact store, parser sandbox, semantic graph, proof-obligation compiler, capability planner, solver runtime, coverage engine, and portable result packages remain future programs.
- The component contract does not validate unit equivalence, parameter applicability, physical consistency, or requirement satisfaction. It carries reviewed inputs, not solver conclusions.
- Historical GET routes can display old rough-V1 records. They cannot create new runs or findings and must remain labeled historical.

## Security implications

The fixture boundary closes caller-controlled attribution, complete review prevents omission from becoming acceptance, and hash verification detects persisted drift on every published success path. These controls protect against accidental mutation and partial workflow failure.

They do not provide authorization, signatures, trusted timestamps, append-only audit storage, or protection from an attacker who can alter both database content and its hash. The local HTTP service is unauthenticated, and the desktop trusts the service bound at `127.0.0.1:8000`. Parser sandboxing, SSRF controls, archive limits, signed packages, and access policy are not yet implemented.

## Licensing implications

The PM-36 source is synthetic project data, so Program 01A adds no third-party datasheet redistribution. No external numerical engine is distributed or invoked. Qt and Open Cascade remain separately installed native dependencies whose distribution terms must be checked for packaged releases. Future GPL solver executables remain out-of-process, separately installed backends subject to commercial and redistribution review; [ADR-0006](../../adr/0006-authoritative-analysis-backends.md) keeps their numerical outputs distinct from Prometheus findings.

## Known false-positive and false-negative risks

- A human can accept incorrect evidence. The review record establishes accountability, not physical truth.
- Structurally valid values may still have the wrong unit, operating envelope, source applicability, or component identity.
- Exact fixture-only intake intentionally rejects every real component, producing false negatives until safe acquisition exists.
- Contract tests cannot expose a package-to-C++ mapping bug because the production consumer does not exist until 01B.
- The fixed C++ demonstrator can produce a narrow conformance result from compiled constants; it must not be interpreted as a result from the reviewed package.
- QML's warning backlog and absent native Windows run leave UI integration defects possible even though the pure review contract and syntax checks pass.
- A local attacker able to impersonate the unauthenticated API or rewrite both stored inputs and hashes can defeat this milestone's integrity controls.

## Reproduction

From the repository root, using an environment with the locked backend dependencies installed:

```bash
cd backend
python -m pip check
python -m pytest -q
PROMETHEUS_DATABASE_URL=sqlite:////private/tmp/prometheus-01a-verification.db alembic upgrade head
PROMETHEUS_DATABASE_URL=sqlite:////private/tmp/prometheus-01a-verification.db alembic current

cd ../frontend
npm ci
npm test
npm run build
npm audit --audit-level=high

cd ..
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
```

Regenerate the API snapshot with `python backend/scripts/export_openapi.py` and rerun the backend suite. Reproduce the manual package path through the versioned endpoints in this order: create exact fixture job, confirm draft export is 409, submit one explicit decision for each returned field, publish separately, export twice, and recompute SHA-256 over canonical JSON without `content_hash`.

On the documented Windows toolchain, additionally run:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

## Program 01B entry criteria

Program 01B starts from the verified implementation commit above and must preserve all 01A gates. Its exit evidence must:

1. Add immutable Motor A and Motor B packages that differ in one decision-relevant reviewed parameter.
2. Remove production PM-36 constants from `EngineeringController`.
3. Make C++ validate and consume the bound execution package, including its schema version, hash, typed values, units, and applicability metadata.
4. Prove that changing only the package produces the expected different C++ result.
5. Persist the exact package, analysis inputs, backend identity, outputs, and manifest, then reproduce the result after offline reopen.
6. Demonstrate that Python neither reproduces the calculation nor issues the finding.
7. Pass the native Windows Qt/Open Cascade suite and manually verify the full review-publish-bind-execute state machine.
