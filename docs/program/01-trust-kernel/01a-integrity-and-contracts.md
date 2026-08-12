# Program 01A: integrity and contracts

- Status: implementation complete; final verification pending
- Implementation baseline before documentation: `042a97f`
- Validation level: `contract_tested` (provisional until Task 10 closes)
- Next gate: Program 01B package-to-C++ execution

## Purpose

Program 01A establishes a trustworthy input boundary. It prevents synthetic fixture data from impersonating public research, prevents missing review decisions from becoming acceptance, and binds a published component revision to canonical hash-verifiable bytes. It does not establish that the fixed motor-arm checker consumes those bytes or that Prometheus can evaluate an arbitrary engineering project.

## Implemented behavior

- The fixture provider accepts only `Prometheus Fixture Works / PM-36-GM`, reads the checked-in synthetic source artifact, and rejects caller-supplied attribution URLs without database writes.
- Engineering values use typed scalar, range, enumeration, curve, and unknown shapes. Invalid ranges, non-finite numbers, duplicate fields, invalid evidence references, and non-increasing curves fail validation.
- Every parameter requires one explicit review decision. Missing, duplicate, unknown, rejected, or ambiguous decisions cannot silently produce a publishable revision; ambiguous and rejected decisions require notes.
- Publication and package hashing occur in one database transaction. A validation or hash failure restores the draft state.
- `GET /v1/component-revisions/{revision_id}/execution-package` returns canonical JSON only for a published revision and detects changes to persisted published inputs.
- The Qt source separates review from publication and starts every decision row without a default. The former accept-all and auto-publish paths are absent.
- The unversioned Python evidence, planning, and execution routes fail with structured `410` or `501` errors. `backend/app/physics.py` remains only as a historical equation reference and no application route imports it.
- The React rough-V1 interface is a labeled, non-executing geometry viewer.

## Verification recorded before final close

| Check | Result |
| --- | --- |
| Backend unit, API, migration, schema, hash, OpenAPI, and fail-closed tests | 55 passed |
| Focused OpenAPI exact-match test | 1 passed after regeneration |
| Frontend tests | 4 passed |
| Frontend production build | Passed; Vite reported the existing bundle-size warning |
| Qt review-payload target | Source and CMake registration inspected; not compiled on this macOS environment because CMake and Qt are absent |

Task 10 will replace this table with clean full-suite results, Alembic verification, audit output, source-integrity searches, manual package inspection, native-test availability, and the exact closing commit.

## Unsupported behavior

- The C++ motor-arm checker still contains fixed PM-36 values. It does not fetch or parse the published execution-component package.
- No external structural, thermal, circuit, CFD, or controls solver adapter exists.
- Public web research, datasheet acquisition, PDF parsing, chart digitization, OCR, and LLM candidate extraction are absent.
- The artifact store, parser sandbox, semantic graph, proof-obligation compiler, capability planner, solver runtime, coverage engine, and portable result packages remain future programs.
- The current package contract covers reviewed component inputs; it contains no solver verdict and proves no requirement.

## Security and licensing implications

The exact fixture catalog closes caller-controlled source attribution and keeps unsupported identities visible. Canonical hashing detects persisted input drift but does not provide signatures, authorization, or tamper resistance against an attacker who can replace both data and hashes. Parser sandboxing, SSRF controls, archive limits, signed packages, and access policy remain unimplemented.

The checked-in PM-36 source is synthetic project data. No external numerical engine is distributed or invoked by Program 01A. Future GPL solver executables remain separately installed, out-of-process dependencies subject to commercial licensing review; [ADR-0006](../../adr/0006-authoritative-analysis-backends.md) keeps their numerical outputs distinct from Prometheus findings.

## Known error risks

- Contract tests can miss a C++ consumer bug because no production consumer exists until 01B.
- A human can accept incorrect synthetic or extracted evidence; explicit review records the decision but does not make it physically true.
- Canonical JSON and schema checks detect structural invalidity and drift, not incorrect units, wrong applicability, or incomplete physics.
- The fixture-only identity policy intentionally rejects every real component, producing false negatives for real research requests until 01C.
- Historical GET routes can display previously stored rough-V1 results; they do not create new findings and must remain labeled historical.

## Reproduction entry point

From `backend/`, run the test suite and OpenAPI generator with the repository environment. From `frontend/`, run `npm ci`, `npm test`, and `npm run build`. Native review-payload verification requires the documented Windows Qt toolchain and the `prometheus_review_payload_tests` CTest target.

The complete commands and exact outputs are recorded when Task 10 closes this milestone.

## Program 01B entry criteria

01B begins only after Task 10 confirms the 01A transaction, hash, provenance, review, retired-route, and OpenAPI gates. It must then:

1. Add immutable Motor A and Motor B packages that differ in a decision-relevant reviewed parameter.
2. Remove fixed production PM-36 constants from `EngineeringController`.
3. Parse and validate the bound execution package in C++.
4. Prove that changing only the package changes the expected C++ outcome.
5. Persist the package, run inputs, outputs, backend identity, and manifest for offline reopen.
6. Demonstrate that Python neither reproduces the calculation nor issues the finding.
