# Program 01A amended completion record

- Status: complete under the amended `contract_tested` gate
- Closed: 2026-08-12
- Validation level: `contract_tested`
- Verified implementation commit: `2491df33ad6ae9032ea71f7994a3f137599e2dba`
- Evidence-bound closure commit: the commit containing this file with subject `chore: close amended Program 01A trust boundary`
- Release-gate evidence: [GitHub Actions run 31636414152](https://github.com/Richard-Y-W/Prometheus/actions/runs/31636414152), conclusion `success`
- Package contract: `urn:prometheus:schema:execution-component:2.0.0`, version `2.0.0`, compiler version `0.2.0`
- Independent verifier: `prometheus_integrity` at the verified implementation commit above

## Bounded release claim

Prometheus can ingest its one declared synthetic fixture, preserve the exact source bytes, represent revision-scoped candidate claims and append-only human reviews, publish the reviewed selection as immutable RFC 8785 bytes, and reproduce the same package identity in Python and C++ on the supported database and platform matrix.

This statement is limited in the same record:

- Human review records accountability; it does not establish physical truth.
- Package integrity establishes byte identity; it does not establish model completeness, applicability, or requirement satisfaction.
- The package is reviewed input, contains no finding or verdict, and remains execution-blocked because Program 01B has not added a package consumer.
- No Program 01A result supports a project-wide pass, safety, certification, physical-validation, solver-coverage, or arbitrary-engineering claim.

## Required release matrix

The push run for the verified implementation commit completed all nine required jobs without a missing or skipped job.

| Required job | Observed environment | Result |
| --- | --- | --- |
| Backend SQLite / Python 3.11 | Ubuntu; CPython 3.11.15 | `340 passed, 1 skipped, 1 warning`; the skip is the explicitly PostgreSQL-only enablement check |
| Backend SQLite / Python 3.12 | Ubuntu; CPython 3.12.3 | `340 passed, 1 skipped, 1 warning`; the skip is the explicitly PostgreSQL-only enablement check |
| Backend SQLite / Python 3.13 | Ubuntu; CPython 3.13.15 | `340 passed, 1 skipped, 1 warning`; the skip is the explicitly PostgreSQL-only enablement check |
| Backend SQLite / Python 3.14 | Ubuntu; CPython 3.14.7 | `340 passed, 1 skipped, 1 warning`; the skip is the explicitly PostgreSQL-only enablement check |
| Backend PostgreSQL 17 / Python 3.12 | PostgreSQL 17.10 on Debian, CPython 3.12.3 | `371 passed, 1 warning`; no tests skipped |
| Frontend / Node 20 | Ubuntu; Node 20.20.2, npm 10.8.2 | 1 test file and 4 tests passed; production build passed; 182 packages audited with 0 vulnerabilities |
| Native / Linux | Ubuntu 24.04.4 LTS, GNU C++ 13.3.0, Qt 6.8.3 | Headless 2/2, integrity 3/3, and OCCT-disabled desktop 5/5 tests passed |
| Native / macOS | macOS 26.5.2 arm64, AppleClang 21.0.0.21000101, Qt 6.11.1 | Headless 2/2, integrity 3/3, and OCCT-disabled desktop 5/5 tests passed |
| Native / Windows MSVC | Windows Server 2022 build 10.0.20348, MSVC 19.44.35228.0, Qt 6.8.3 | Headless 2/2, integrity 3/3, and OCCT-disabled desktop 5/5 tests passed |

Each SQLite job also verified the 10-file vendored-source manifest before running the suite. The backend warning is the installed FastAPI/Starlette `httpx` deprecation notice. GitHub also warned that selected JavaScript actions target the deprecated Node 20 action runtime and were forced onto Node 24; the repository's archived frontend itself ran under Node 20.20.2. Neither warning was converted into a clean-warning claim.

## Local release-gate evidence

The final local verification used macOS 26.5.2, AppleClang 21, CMake 4.4.2, Ninja 1.13.2, Qt 6.11.1, PostgreSQL 17.10, CPython 3.11.15, Node 24.14.1, and npm 11.11.0.

| Gate | Command or procedure | Observed result |
| --- | --- | --- |
| Python dependency consistency | Locked `uv` environment plus `python -m pip check` with an ephemeral locked `pip` installation | 36 installed packages compatible; `No broken requirements found.` |
| SQLite backend | `uv run --locked pytest -q --tb=short` | `340 passed, 1 skipped, 1 warning`; only the PostgreSQL-only enablement test skipped |
| PostgreSQL backend | Full locked suite with `PROMETHEUS_TEST_POSTGRES_URL` against PostgreSQL 17.10 | `371 passed, 1 warning`; no skips |
| Generated contracts and OpenAPI | Run both repository exporters, then compare `schemas/` and `docs/openapi-v2.json` | Regeneration produced no diff |
| Frontend | `npm ci`, `npm test`, `npm run build`, `npm audit --audit-level=high` | 4 tests passed; build passed; audit reported 0 vulnerabilities |
| Vendored native inputs | `python3 scripts/verify-vendored-dependencies.py` | 10 files, recorded commits, licenses, and SHA-256 values verified |
| Headless native | Configure, build, and test `headless-debug` | 2/2 tests passed |
| Independent integrity verifier | Configure, build, and test `integrity-debug` | 3/3 tests passed |
| Full desktop without OCCT | Configure, build, and test `desktop-no-occt-debug` | 5/5 tests passed, including review-payload and adapter-unavailable behavior |

The release repair added a regression that makes the compiler receive the same noncanonical parameter-slot order observed under the Linux PostgreSQL locale. The test failed with `contract_invalid` before the repair and passed after slot ordering moved from database collation to the contract's Python `(name, id)` order. The first CI attempt also exposed Qt 6.8.3 linking the unavailable AGL framework on `macos-latest`; the successful run kept the current macOS runner and used Qt 6.11.1 for that platform.

## Manual exact-byte audit

The final manual audit used a fresh SQLite database and two separately started Uvicorn processes. It ingested the exact synthetic fixture, submitted 17 explicit claim decisions, published once, replayed publication after restart, and exported the bound object three times.

| Observation | Recorded value |
| --- | --- |
| Revision ID | `a79f959d-30f4-4cd7-a4e8-fd39d514af6d` |
| Reviewed draft version | `1` |
| Object hash | `sha256:eb0d3e2a6337d3a0d3a612c282bfec15a2db18bed45be37cba67f42e24f4a2c9` |
| Stored byte length | `35735` |
| Three export hashes | All three equaled the object hash |
| Initial and replayed publication-response hashes | Both equaled `sha256:efc20b8b35c65b1b2ab9e8a669d7a83186a408de74ba366780dad5ec245c2f77` |
| Media type | `application/vnd.prometheus.execution-component+json;version=2.0.0` |
| ETag | The quoted object hash |
| Execution state | `blocked`; one `package_consumer` execution gate remained blocked |
| Authority | `package_role=reviewed_input`, `engineering_decision_authority=prometheus_cpp`, `authority_role=input_only` |

The stored package had no top-level `content_hash`, `object_hash`, or `verdict`. Restart replay returned the stored response without recompiling, and every export returned the same verified stored bytes.

## Residual risks and unavailable optional checks

- The required matrix builds the complete desktop with Open Cascade disabled. It verifies the explicit adapter-unavailable seam but does not exercise real STEP/XDE import on Linux, macOS, or the CI Windows runner.
- The separately installed OCCT-enabled Windows target was not part of this release run. Existing CAD tests and prior native demonstrations remain narrower evidence; they do not establish general CAD semantics.
- Browser pointer-event automation, packaged-product recovery, authorization, signatures, trusted timestamps, and protection against an attacker who can replace both application and database controls remain absent.
- The checked-in source is synthetic and marked `physical_validation_status=unvalidated`. A human can review an incorrect claim, and a structurally valid package can still contain the wrong unit, operating envelope, applicability condition, or component identity.
- The C++ motor-arm checker still uses fixed PM-36 constants. Program 01A does not test package-to-calculation mapping because that production consumer does not yet exist.
- No public acquisition pipeline, parser-worker sandbox, semantic system graph, proof-obligation compiler, capability planner, isolated solver runtime, external numerical backend, coverage engine, cross-solver benchmark, or physical-validation corpus exists.

These limits keep the validation level at `contract_tested`. The successful gate establishes the specified software trust boundary for one synthetic fixture; it does not establish engineering validity.

## Program 01B handoff

Program 01B starts from schema `urn:prometheus:schema:execution-component:2.0.0`, package compiler `0.2.0`, and `prometheus_integrity` at verified commit `2491df33ad6ae9032ea71f7994a3f137599e2dba`. Its first exit test must remove production PM-36 constants, make C++ consume a selected verified package, and show that two immutable packages differing in one reviewed decision-relevant value produce the expected different scoped result and reproduce after offline reopen. Python must not duplicate that calculation or issue the finding.
