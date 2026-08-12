# Program 01A Amended Implementation Plan

- Status: ready for implementation review
- Design authority: [Program 01A amended trust-boundary design](../../superpowers/specs/2026-08-11-program-01a-amended-trust-boundary-design.md)
- Supersedes: [the original Program 01A implementation plan](../../superpowers/plans/2026-08-11-program-01a-integrity-and-contracts.md)
- Preserves as historical evidence: [the original completion record](01a-integrity-and-contracts.md)
- Operational companion: [Program 01A amended execution runbook](../../superpowers/plans/2026-08-11-program-01a-amended-trust-boundary-execution.md)

## Objective

Program 01A will establish a fixture-backed reviewed-input boundary that can be verified independently across Python and C++. A published v2 revision will reference one immutable RFC 8785 byte object; export will return those stored bytes only after canonical-form, schema, metadata, and SHA-256 verification.

This program does not determine whether an engineering project works. It does not add live research, parser workers, arbitrary project intake, package-driven engineering execution, new physics, or solver adapters. Program 01B still owns the first experiment in which reviewed package values change a C++ engineering result.

## Release claim

Program 01A may close only when the repository supports this bounded statement:

> Prometheus can ingest its one declared synthetic fixture, preserve the exact source bytes, represent revision-scoped candidate claims and append-only human reviews, publish the reviewed selection as immutable RFC 8785 bytes, and reproduce the same package identity in Python and C++ on the supported database and platform matrix.

The completion record must place these limits beside that statement:

- Human review records accountability; it does not establish physical truth.
- Package integrity establishes byte identity; it does not establish model completeness or applicability.
- A blocked execution gate may coexist with successful publication.
- A published package remains input-only until Program 01B connects it to the C++ decision path.
- No result from Program 01A supports a project-wide pass, safety, certification, or arbitrary-engineering claim.

## Implementation boundary

```text
checked-in synthetic source bytes
        |
        v
bounded local artifact ingestion
        |
        v
draft revision + slots + immutable candidate claims + evidence
        |
        v
claim-ID review events + explicit current selection + capability gates
        |
        v
v2 package compiler (no verdict)
        |
        v
RFC 8785 canonical bytes + SHA-256
        |
        v
immutable published object + permanent revision binding
        |                                  |
        v                                  v
exact verified API export      independent C++ canonical/hash verification
```

Draft relational state is authoritative before publication. The stored byte object is authoritative after publication. Relational projections may support search and display, but they cannot reconstruct or replace a sealed v2 package.

## Contract decisions

### Version and media identity

| Item | Required value |
| --- | --- |
| API base | `/api/v2` |
| Execution-package schema ID | `urn:prometheus:schema:execution-component:2.0.0` |
| Contract version | `2.0.0` |
| Canonicalization | `RFC8785` plus Prometheus negative-zero rejection |
| Package media type | `application/vnd.prometheus.execution-component+json;version=2.0.0` |
| Object identity | `sha256:` plus 64 lowercase hexadecimal characters |
| Python support | CPython `>=3.11,<3.15` |
| PostgreSQL conformance target | PostgreSQL 17 |

The package does not contain its own hash. The publication response, strong `ETag`, revision binding, and stored-object metadata carry the hash outside the hashed bytes.

### Checked-in v2 contracts

The implementation adds these Draft 2020-12 schemas without rewriting the v1 files:

- `schemas/engineering-value-v2.schema.json`
- `schemas/evidence-record-v2.schema.json`
- `schemas/review-request-v2.schema.json`
- `schemas/publication-request-v2.schema.json`
- `schemas/execution-component-v2.schema.json`
- `schemas/project-summary-v2.schema.json`

`backend/app/contracts_v2.py` is the typed application representation. `backend/scripts/export_contract_schemas.py` emits the checked-in schemas deterministically, and contract tests require a clean regeneration. `docs/openapi-v2.json` is the exact current application snapshot; `docs/openapi-v1.json` remains historical.

### Execution-package shape

The package contains these top-level members and no others:

- `$schema`, `schema_version`, `package_kind`, and `package_compiler`;
- revision and component identity;
- immutable artifact references;
- parameter slots and one selected candidate claim per emitted slot;
- effective claim-review events and evidence records;
- capability-specific publication and execution gates;
- `execution_readiness`, missing information, limitations, and the input-only authority declaration.

The exact fixture uses `private_upload` as its evidence transport class, `source_authority=synthetic_fixture`, and `physical_validation_status=unvalidated`. This avoids presenting the checked-in fixture as a manufacturer document. Its local provenance and limitation state explicitly that it is synthetic conformance data.

Raw solver output is never an evidence record. Future solver execution will store immutable result artifacts with backend, version, input-package, convergence, and manifest provenance; Program 01A adds no such execution path.

Arrays use contract-defined order before canonicalization: parameter slots by ASCII parameter name then slot ID; claims, evidence, reviews, artifacts, limitations, and gates by stable ASCII ID. Semantically ordered arrays such as curve points retain domain order.

### Project summary

The v2 project summary keeps `verdict`, `coverage`, and `execution_state` independent. The C++ decision core creates the summary; Python only validates and transports it. Tests must permit a confirmed violation, insufficient coverage, and blocked work in the same record. They must reject `satisfied_within_scope` whenever an applicable obligation is indeterminate, not evaluated, blocked, or absent from the assessed scope.

## File and responsibility map

### Python service

| File | Responsibility |
| --- | --- |
| `backend/app/contracts_v2.py` | Closed v2 request, package, evidence, gate, and project-summary types; no publication I/O or verdict generation. |
| `backend/app/canonical_json.py` | Strict JSON parsing, resource preflight, RFC 8785 emission, canonical-byte verification, and SHA-256 identity. |
| `backend/app/models_v2.py` | V2 artifact, slot, claim, link, selection, review, gate, published-object, ingestion, and idempotency models. |
| `backend/app/db_types.py` | Dialect-safe UTC timestamp and immutable-byte column helpers. |
| `backend/app/transaction.py` | SQLite `BEGIN IMMEDIATE`, PostgreSQL row locking, lock order, retry classification, and bounded retry policy. |
| `backend/app/artifact_store.py` | Bounded descriptor-based local ingestion and immutable source-artifact verification. |
| `backend/app/fixture_pipeline_v2.py` | Exact fixture identity lookup and creation of one v2 draft graph. No web, PDF, or LLM path. |
| `backend/app/review_service_v2.py` | Atomic claim-ID review batches, same-revision checks, effective-review lookup, and draft-version advancement. |
| `backend/app/package_compiler_v2.py` | Deterministic relational-draft-to-v2-value compilation and schema validation; no hash field and no verdict. |
| `backend/app/object_store.py` | Immutable published-object insertion/match and exact-byte verification. |
| `backend/app/publication_service_v2.py` | Locked publication state machine, persisted replay records, fault-injection seams, and one-commit sealing. |
| `backend/app/api_v2.py` | Fixture-ingestion, revision, review, publication, replay, and exact-export HTTP boundary. |
| `backend/app/http_policy.py` | V2 request-byte limit and strict duplicate-preserving JSON preflight. |
| `backend/app/api_v1.py` | Historical reads and deterministic `410 Gone` responses for retired v1 mutations and reconstructed export. |
| `backend/app/database.py` | Dialect configuration, SQLite foreign keys and busy timeout, and session factories. |
| `backend/app/main.py` | Application assembly and v2 route/middleware registration. |

`backend/app/execution_packages.py` and `backend/app/contracts_v1.py` remain labeled v1-only historical code. Production v2 publication and export must not import either module.

### Persistence and migrations

| File | Responsibility |
| --- | --- |
| `backend/migrations/versions/a41f0c93e2d7_amended_trust_boundary_v2.py` | Add v2 tables, revision-version/publication columns, conditional legacy classification, closed-state constraints, same-revision foreign keys, and immutability triggers. |
| `backend/migrations/env.py` | Register v2 metadata for autogeneration and schema comparison. |
| `backend/tests/test_migrations_v2.py` | Fresh install, upgrade from `7b6d91e2a4f0`, legacy preservation, constraint, trigger, and lossy-downgrade behavior. |

Existing v1 rows keep their original event and status vocabulary. Existing published rows without authoritative bytes receive `publication_integrity=legacy_unsealed`; they never become sealed v2 packages by migration. New v2 drafts carry the v2 schema identity and may transition only from `draft` to `published` with a complete sealed binding.

### Shared fixtures and conformance data

| Path | Responsibility |
| --- | --- |
| `fixtures/evidence/pm-36-gm.synthetic-v2.json` | Exact bounded source artifact for v2 fixture ingestion. |
| `fixtures/contracts/execution-component-v2.pm-36-gm.json` | Human-readable semantic package vector. |
| `fixtures/contracts/execution-component-v2.pm-36-gm.jcs` | Exact expected RFC 8785 package bytes. |
| `fixtures/contracts/execution-component-v2.pm-36-gm.sha256` | Expected object identity for both languages. |
| `fixtures/conformance/rfc8785/manifest.json` | Case names, source files or deterministic byte recipes, expected canonical files/hashes, and expected failure codes. |
| `fixtures/conformance/rfc8785/input/` | Official/reference and Prometheus raw JSON inputs. |
| `fixtures/conformance/rfc8785/canonical/` | Exact canonical bytes for successful cases. |

The corpus covers decoded duplicate keys, invalid Unicode, lone surrogates, NFC/NFD distinction, control escapes, UTF-16 key order, nested arrays/objects, safe-integer limits, binary64 boundaries, fixed/scientific thresholds, exponent spelling, negative zero, non-finite values, underflow/overflow, and every configured resource limit. Invalid byte sequences and oversized cases use manifest-defined deterministic recipes consumed without skips by both language harnesses.

### C++ and Qt

| File | Responsibility |
| --- | --- |
| `desktop/integrity/include/prometheus/integrity/canonical_json.hpp` | Public strict-parse, canonicalize, verify, and hash interface. |
| `desktop/integrity/src/canonical_json.cpp` | Bounded SAX parser, UTF-16 property sorting, RFC 8785 strings, and numeric dispatch. |
| `desktop/integrity/src/ecmascript_number.cpp` | Ryu-backed ECMAScript binary64 formatting and Prometheus numeric rejection policy. |
| `desktop/integrity/tests/canonical_json_tests.cpp` | Shared corpus and complete-package byte/hash verification. |
| `desktop/core/include/prometheus/decision/project_summary.hpp` | Qt-free obligation counts, summary enums, and decision-core API. |
| `desktop/core/src/project_summary.cpp` | The sole v1 C++ summary reducer. |
| `desktop/core/tests/project_summary_tests.cpp` | Multidimensional summary truth-table tests. |
| `desktop/app/review_payload.*` | Claim-ID review requests with expected draft version and required notes. |
| `desktop/app/service_controller.*` | `/api/v2` state, persistent publication retry key, and exact fixture flow. |
| `desktop/ui/Main.qml` | Explicit claim review, visible draft version, publication versus execution readiness, and no accept-all path. |
| `desktop/core/include/prometheus/cad/types.hpp` | OCCT-independent CAD value types used by the Qt controller and OCCT adapter. |
| `desktop/cad/include/prometheus/cad/step_importer.hpp` | OCCT adapter interface built on the shared plain types. |
| `desktop/app/cad_controller.cpp` | Guard every `StepImporter` construction when OCCT is disabled and return explicit unavailable state. |

The integrity library vendors pinned source under `third_party/nlohmann-json/` and `third_party/ryu/`. `third_party/manifest.json` records upstream URL, commit, license, and SHA-256 for each vendored file. The build does not fetch these dependencies. The integrity target may link Qt Core for SHA-256; the decision core remains Qt-free.

## Delivery sequence

### 1. Mark the old gate as superseded

Add reopening notices to the original plan, completion record, README, roadmap, and milestone status. The notices must state what passed under the former gate, why the amended gate is stronger, and that Program 01B has not started. Do not erase former commands, results, or commit identities.

Exit evidence: every current status page says `amended Program 01A in progress`; the original completion record is visibly historical.

### 2. Establish strict canonical contracts

Pin `rfc8785==0.1.4`, declare Python `>=3.11,<3.15`, add the strict raw parser and resource walker, and check in the shared corpus. Generate the v2 schemas and full-package vector. Reject duplicate decoded keys, invalid Unicode, unsafe integers, non-finite values, nonzero underflow, overflow, and negative zero before canonicalization.

Exit evidence: Python passes every positive vector byte-for-byte and maps every negative vector to its declared failure code.

### 3. Add the multidimensional C++ decision core

Implement the Qt-free summary reducer before wiring any v2 execution input. A nonzero violation count dominates verdict selection; incomplete applicable coverage prevents satisfaction; workflow failure does not fabricate a requirement violation.

Exit evidence: the C++ truth table and Python transport-contract tests agree on valid and invalid summary records, while only C++ exposes a summary-construction function.

### 4. Add v2 persistence and database invariants

Create the v2 graph and publication tables, conditional revision columns, closed-state checks, composite same-revision foreign keys, confidence bounds, unique current selections, one-way claim finalization, and immutable-row triggers. Evidence links may be assembled only before a claim is finalized; only finalized claims may be selected or reviewed. Use aware `TIMESTAMPTZ` on PostgreSQL and validated UTC RFC 3339 `Z` text on SQLite.

Exit evidence: fresh and upgraded databases pass the same direct-write constraint tests on SQLite and PostgreSQL; the upgrade preserves and labels legacy rows.

### 5. Ingest the exact source artifact and compile a draft graph

Ingest only `prometheus.pm-36-gm.fixture-2`. Open the source through a bounded local descriptor, reject path escape and symlink substitution, compare pre/post file identity, copy exact bytes into the local artifact table, and verify the declared hash before creating evidence. Compile parameter slots, one candidate per slot, evidence links, explicit selections, and capability gates in one transaction.

Exit evidence: missing, unreadable, changed, deleted, hash-mismatched, normalization-lookalike, traversal, symlink, and change-during-read cases create no partial draft; deletion of the external source after success does not affect the stored artifact.

### 6. Implement append-only claim review

Accept revision-scoped claim IDs, nonblank audit label, nonblank notes, and `expected_draft_version`. Lock the revision, validate the complete batch, append review events at version `n+1`, and increment the revision once. Preserve rejected and ambiguous history when later events supersede them.

Exit evidence: duplicate, unknown, cross-revision, stale, oversized, blank, and partially invalid batches make no mutation; concurrent review/publication resolves by lock order and version.

### 7. Compile and store exact package bytes

Compile the selected draft into the v2 semantic value, validate it, canonicalize it, hash exact bytes, independently verify those bytes, insert or exactly match the immutable object, and permanently bind the revision. Publication gates control sealing; execution gates only determine `execution_readiness`.

Exit evidence: an accepted unknown can publish while a required execution gate remains blocked; an unresolved publication gate cannot publish.

### 8. Make publication durable under retries and races

Persist operation-scoped idempotency keys and request fingerprints. Store exact terminal response bytes and application headers. Use SQLite `BEGIN IMMEDIATE` with a five-second busy timeout and PostgreSQL `READ COMMITTED` plus `SELECT FOR UPDATE`; follow revision, idempotency, object, binding lock order and at most three PostgreSQL deadlock retries.

Exit evidence: same-key concurrent calls converge on one response, different keys yield one success and one explicit already-published response, restart and lost-response replays are byte-identical, and every injected precommit failure leaves no partial seal or terminal retry record.

### 9. Expose v2 and retire trust-sensitive v1 mutation

Provide exact-fixture ingestion, revision detail, review, publication, replay, and export under `/api/v2`. Export reads stored bytes, verifies them, and returns them unchanged with the strong object `ETag`. V1 mutation and reconstructed export return a stable `410` document; historical reads label unsealed rows.

Exit evidence: legacy payloads cannot be guessed into v2, unsupported schema versions fail without fallback, corrupt stored bytes fail closed, and OpenAPI regeneration is exact.

### 10. Add independent C++ package verification

Vendor the pinned JSON and Ryu source, implement the bounded parser and serializer independently, and run the shared corpus. Require strict reserialization equality before comparing SHA-256 and schema identity.

Exit evidence: Python and C++ produce the checked-in package bytes and object ID independently; changing bytes, metadata, schema, Unicode, or number form fails with the declared integrity error.

### 11. Move Qt review and publication to v2

Render stable claim IDs and current draft version, require explicit decisions and notes, and retain one publication idempotency key across network retries. Clear the key only when a new revision/version requires a new logical request. Display publication state separately from execution readiness.

Exit evidence: Qt contract tests prove claim-ID payloads and stale-version handling; searches find no accept-all or implicit-success path.

### 12. Repair and test the OCCT-disabled desktop seam

Move plain CAD result types into the always-built core include path. Keep `StepImporter` declarations in the optional adapter and guard every construction/call in the Qt controller. The disabled path must report adapter unavailability without compilation failure or synthetic geometry success.

Exit evidence: the complete Qt application and its non-OCCT controller test build and run with `PROMETHEUS_ENABLE_OCCT=OFF`.

### 13. Expand the release matrix and documentation

Run SQLite backend tests on Python 3.11, 3.12, 3.13, and 3.14; run the semantic publication suite and migrations against PostgreSQL 17; run the headless decision core, integrity suite, and OCCT-disabled desktop on macOS, Linux, and Windows. Check vendored licenses and offline build inputs. Update architecture, validation, threat, migration, changelog, status, and OpenAPI records from observed results.

Exit evidence: no required job is skipped; the completion record lists exact versions and failures as observed. A new amended completion record is written only after every required gate passes.

## Required test suites

| Suite | Required cases |
| --- | --- |
| `backend/tests/test_canonical_json.py` | Every shared corpus case, programmatic values, exact package bytes/hash, limits, duplicate-key path. |
| `backend/tests/test_contracts_v2.py` | Schema self-validation, generated-file equality, evidence unions, known/unknown claims, package order, summary consistency. |
| `backend/tests/test_artifact_store.py` | Source lifecycle, expected hash, traversal, symlink, Unicode identity, descriptor race, immutable stored copy. |
| `backend/tests/test_review_v2.py` | Claim identity, multiple candidates, cross-revision rejection, append-only re-review, versions, atomicity, limits, capability isolation. |
| `backend/tests/test_publication_v2.py` | Gates, compilation, canonical bytes, collision defense, immutable binding, exact export, corruption failure. |
| `backend/tests/test_publication_concurrency.py` | Same/different keys, independent connections, review race, SQLite busy mapping, PostgreSQL locks, barriers without sleeps. |
| `backend/tests/test_publication_failures.py` | Every named fault stage, precommit rollback, postcommit/lost-response replay, process restart. |
| `backend/tests/test_api_v2.py` | HTTP happy/failure paths, request limits, unsupported versions, ETag/media type, exact body replay. |
| `backend/tests/test_legacy_api.py` | V1 `410` mutations/export, historical labels, no implicit migration. |
| `backend/tests/test_migrations_v2.py` | Fresh/upgrade schema, legacy classification, direct-write constraints, triggers, downgrade refusal with sealed data. |
| `desktop/integrity/tests/canonical_json_tests.cpp` | Same corpus and complete package vector as Python. |
| `desktop/core/tests/project_summary_tests.cpp` | Verdict/coverage/execution cross-product and fail-closed satisfaction. |
| `desktop/app/tests/review_payload_tests.cpp` | Claim IDs, notes, draft version, duplicates, unknowns, size constraints. |
| `desktop/app/tests/cad_controller_no_occt_tests.cpp` | Full disabled-adapter path reports unavailable and terminates cleanly. |

Concurrency tests use independent connections and synchronization barriers; timing sleeps are not evidence. Fault-injection hooks are internal callables or test-only constructor arguments and are never accepted from an HTTP request.

## Release blockers

Any of these conditions keeps Program 01A open:

- Python and C++ disagree on one canonical byte or one expected failure.
- Export reconstructs v2 content from relational rows or returns unverified stored bytes.
- A direct database update can replace, delete, or repoint sealed content.
- A review can target a field label or a claim in another revision.
- A stale review/publication changes state.
- A publication retry can create a second object or change its terminal response.
- PostgreSQL semantic tests are skipped or differ from SQLite without a documented fail-closed outcome.
- Any supported Python minor fails.
- The OCCT-disabled application does not compile.
- Current documentation says Program 01A is complete before the amended gate passes.
- The completion record implies arbitrary-project verification, physical validation, package-driven engineering execution, or solver coverage.

## Handoff to Program 01B

Program 01B may start after the amended completion record identifies the sealed package schema and C++ verifier version. Its first task remains unchanged: remove fixed production PM-36 values, make the C++ engineering path consume a selected verified package, and show that two packages differing in one reviewed decision-relevant value produce the expected different scoped result and reproduce offline.
