# Validation plan

Program 01A is complete under the amended `contract_tested` gate. The [completion record](program/01-trust-kernel/01a-amended-completion.md) cites one successful release run containing the entire matrix below. This plan names software-verification evidence only. It does not convert a synthetic fixture, canonical byte match, or passing test into physical-model validation or an engineering result.

## Required release matrix

| Surface | Required environment | Required command or targets |
| --- | --- | --- |
| Backend with SQLite | Linux, CPython 3.11, 3.12, 3.13, and 3.14 | locked dependency sync, vendored-byte verification, then the full `backend/tests` suite |
| Backend with PostgreSQL | PostgreSQL 17, CPython 3.12 | the same full backend suite with `PROMETHEUS_TEST_POSTGRES_URL` set; `test_postgresql_17_semantic_suite_is_explicitly_enabled` prevents a silent skip |
| Native C++/Qt | Linux, macOS, and Windows MSVC on `windows-2022` | `prometheus_core_tests`, `prometheus_project_summary_tests`, `prometheus_integrity`, `prometheus_review_payload_tests`, and `prometheus_cad_controller_no_occt_tests` |
| Archived frontend | Linux, Node 20 | tests, production build/type-check, and high-severity dependency audit |
| Vendored native sources | Backend jobs and offline native build | `scripts/verify-vendored-dependencies.py` plus its adversarial test module |

The closing evidence is [GitHub Actions run 31636414152](https://github.com/Richard-Y-W/Prometheus/actions/runs/31636414152). All nine required jobs completed successfully: four SQLite/Python jobs, PostgreSQL 17, the archived frontend, and three native platforms.

The native CI configures three independent presets on every platform: headless core, integrity verifier, and desktop without Open Cascade. Desktop configuration requires the declared Qt components; missing Qt cannot silently omit the application or its tests. The closing run used Qt 6.8.3 on Linux and Windows, Qt 6.11.1 on `macos-latest`, and the Visual Studio 17 2022 generator with Qt's MSVC 2022 ABI on Windows.

The optional `windows-debug` UCRT64 target exercises the real Open Cascade adapter when those separately installed dependencies exist. That geometry path remains valuable, but it is not a substitute for the required OCCT-disabled desktop seam and is not evidence that arbitrary CAD semantics or mechanics are understood.

## Canonicalization and contract tests

`backend/tests/test_canonical_json.py` and the C++ target `prometheus_integrity` consume the same checked-in `fixtures/conformance/rfc8785/manifest.json`. Together they cover:

- exact successful canonical bytes and SHA-256 identities;
- decoded duplicate keys, invalid UTF-8 and surrogates, UTF-8 BOM, control escapes, and UTF-16 property order;
- NFC/NFD preservation and distinction, without identity normalization;
- safe-integer boundaries, binary64 formatting thresholds, non-finite numbers, underflow, overflow, and every spelling of negative zero;
- inclusive raw-byte, depth, node, object-member, array-element, and string-byte limits;
- rejection of valid but noncanonical bytes and hashing only after canonical verification.

The named Python tests are `test_manifest_limits_match_the_public_contract`, `test_success_corpus`, `test_failure_corpus`, `test_configured_limits_are_inclusive`, `test_programmatic_values_use_the_same_preflight`, `test_utf8_bom_is_rejected`, `test_all_negative_zero_spellings_are_rejected`, `test_nfc_and_nfd_remain_distinct`, `test_verifier_rejects_valid_but_noncanonical_json`, `test_hashing_rejects_noncanonical_json`, `test_canonicalizer_never_emits_bytes_rejected_by_the_strict_parser`, `test_parser_rejects_non_bytes_and_trailing_content`, and `test_extreme_integer_and_depth_fail_with_stable_policy_errors`.

The C++ executable runs `test_shared_corpus`, `test_limits_are_inclusive`, `test_policy_edges`, and `test_complete_execution_component`. The final case checks the exact package bytes and hash, byte-flip rejection, and supported schema ID/version. This independent C++ verifier does not perform physical validation or a full engineering-schema interpretation.

`backend/tests/test_contracts_v2.py` regenerates the checked-in schemas, checks the exact package vector, exercises every engineering-value and evidence variant, enforces closed request/UUID/text rules, validates graph consistency, and keeps project verdict, coverage, and execution state independent. `backend/tests/test_package_compiler_v2.py` checks deterministic compilation, contract ordering, graph ownership, effective reviews, capability gates, source artifacts, size limits, canonical verification, and unchanged persisted missing-information/limitation identities. The compiled package remains input-only and contains no finding.

## Database, migration, and review tests

The full suite runs against SQLite on each supported Python minor and against PostgreSQL 17. `backend/tests/test_migrations_v2.py` covers a fresh head, v1 upgrade and `legacy_unsealed` classification, ambiguous legacy timestamp rejection, UTC round trips and direct-SQL shape checks, UUID/hash vocabularies, known/unknown claim shape, evidence vocabularies, same-revision keys, one-way claim finalization, append-only reviews, immutable artifacts/objects, sealed revision and child-graph immutability, gate/job/publication state machines, and downgrade refusal when sealed v2 data would be lost.

`backend/tests/test_review_v2.py` covers atomic selected-claim review, multiple candidates, nonexistent versus cross-revision identity, stale versions, post-publication rejection, UTF-8 byte limits, required notes for all three decisions, duplicate/oversized batches, no partial writes, fingerprint mismatch, superseded history, the database uniqueness backstop, injected rollback after event append, and the revision-lock barrier seam.

The synchronized concurrency tests in `backend/tests/test_publication_concurrency.py` are:

- `test_same_key_concurrent_publications_converge_on_one_response_and_object`;
- `test_different_keys_concurrently_produce_one_success_and_one_terminal_conflict`;
- `test_review_committing_first_makes_publication_stale_without_partial_object`;
- `test_publication_committing_first_makes_review_fail_without_partial_event`.

These tests run on both database backends. SQLite uses an immediate write transaction; PostgreSQL uses row locks and bounded retries for serialization, deadlock, and lock-availability SQLSTATEs.

## Publication, restart, and failure-injection tests

`backend/tests/test_publication_v2.py` verifies one-object sealing, exact stored-response replay, corruption detection during replay, durable deterministic failure responses, an already-published conflict from a different key, request-fingerprint conflicts, invalid-key non-persistence, a lost response after commit, and process restart. The exact recovery tests are `test_lost_response_after_commit_replays_without_recompilation` and `test_process_restart_replays_status_body_and_headers_byte_for_byte`.

`backend/tests/test_publication_failures.py::test_infrastructure_failure_at_every_stage_rolls_back_then_retries` injects failure after each of these stages:

1. idempotency resolution;
2. draft validation;
3. package compilation;
4. schema validation;
5. canonicalization;
6. hash computation;
7. byte verification;
8. object insertion;
9. revision binding;
10. response storage;
11. immediately before commit.

Every injected failure must leave the revision as a draft with no object or publication-request residue, after which the same request must succeed. A sealed package proves that the reviewed input bytes survived this transaction boundary; it does not prove that any analysis ran.

## Artifact and HTTP boundary tests

`backend/tests/test_artifact_store.py` names the bounded source-ingestion cases: external source deletion after ingestion, missing and unreadable sources, deletion between validation and open, expected-hash mismatch, mutation during read, descriptor-identity mismatch, root traversal, symlink escape, symlink substitution, exact NFC/NFD path identity, the 8 MiB limit, exact reingestion, same-hash/different-byte defense, and database-level update/delete rejection.

`backend/tests/test_api_v2.py` covers the complete create-review-publish-export flow, fixture-key replay/conflict, strict idempotency headers, unsupported schemas, duplicate-key/UTF-8 request rejection, declared and streamed size limits, stale and legacy review identities, and corrupt-object export. `backend/tests/test_legacy_api.py` requires the same `410` migration response from all retired v1 trust-boundary routes and ensures historical reads never expose reconstructed bytes. `backend/tests/test_openapi.py` requires the checked-in application snapshot and the documented v1 retirement/v2 boundary to match the live app.

`backend/tests/test_vendored_dependencies.py` and `backend/tests/test_vendored_dependency_verifier.py` check every listed byte, commit spelling, license, and the absence of unlisted or symlinked vendored files. These checks establish source inventory integrity, not the correctness of the third-party algorithms.

## C++ and Qt tests

- `prometheus_project_summary_tests` runs `test_truth_table`, `test_failure_and_satisfaction_boundaries`, and `test_invalid_inputs`; C++ must keep verdict, coverage, and execution state consistent and reject bad scope hashes or obligation totals.
- `prometheus_core_tests` retains the narrow fixed-input motor-arm conformance checks. It does not consume the v2 package and does not establish general mechanical validity.
- `prometheus_review_payload_tests` runs `testValidClaimReviewPayload`, `testDraftVersionAndClaimIdentityFailures`, `testEveryDecisionStatusRequiresANote`, and `testUtf8ByteAndCollectionLimits`; it emits claim IDs in displayed order, never field-name identity, and rejects partial or oversized review bodies.
- `prometheus_cad_controller_no_occt_tests` proves synchronous and asynchronous STEP requests fail once with `Open Cascade adapter is not enabled`, leave no synthetic parts, and terminate the busy state when OCCT is absent.
- `prometheus_integrity` independently covers the shared canonical corpus and exact execution-component byte/hash vector on Linux, macOS, and Windows.

## Validation still missing

- Program 01B has not shown that two reviewed packages drive different expected C++ results or reproduce an execution after offline reopen.
- No public document acquisition/parser, general artifact inventory, semantic system graph, requirements compiler, capability planner, isolated solver runtime, or coverage engine exists.
- No external solver adapter, analytic cross-check corpus, cross-solver benchmark, manufactured solution, or physical validation result exists for the planned six domains.
- Real OCCT parsing remains in the desktop process rather than a bounded worker, and the required cross-platform matrix disables OCCT.
- Browser pointer-event automation and packaged-product recovery tests remain absent.

Program 08 will set per-capability error thresholds and false-negative regression gates. Until then, contract-tested means the software rejects, preserves, and labels data as specified; it does not mean a physical model is validated.
