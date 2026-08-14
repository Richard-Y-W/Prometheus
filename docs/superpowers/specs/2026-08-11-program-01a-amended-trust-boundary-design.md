# Program 01A Amended Trust-Boundary Design

- Status: approved design; implementation pending
- Decision date: 2026-08-11
- Applies to: Program 01A, Integrity and Generic Contracts
- Reopens milestone commit: `e827440`
- Replaces as the implementation authority: `docs/superpowers/plans/2026-08-11-program-01a-integrity-and-contracts.md`
- Preserves as historical evidence: `docs/program/01-trust-kernel/01a-integrity-and-contracts.md`
- Durable implementation plan: `docs/program/01-trust-kernel/01a-amended-implementation-plan.md`
- Inline execution runbook: `docs/superpowers/plans/2026-08-11-program-01a-amended-trust-boundary-execution.md`

## Decision

Program 01A remains a trust-boundary program, but the implementation recorded at `e827440` does not satisfy the amended release gate. It canonicalizes packages with Python-specific `json.dumps` behavior, reconstructs published packages from relational rows, reviews mutable field names, and does not define durable publication idempotency or cross-language verification. Those limits do not erase the earlier work. They reopen the milestone because the existing controls cannot yet support an independently verifying C++ consumer.

This design defines the amended boundary. A Program 01A v2 package is an immutable RFC 8785 byte sequence selected from explicitly reviewed, revision-scoped claims. The database binds one published revision to one content-addressed object. Python compiles the package; an independent C++ verifier parses the same semantic JSON, reproduces the canonical bytes, and verifies the object hash. Neither operation issues an engineering verdict.

Program 01A is complete only when the acceptance tests in this document pass against SQLite and PostgreSQL and the repository records the remaining non-claims. Until then, the repository remains a fixture-backed vertical demonstrator with an amended Program 01A in progress.

## Scope

### Included

- Version 2 engineering-input, evidence, review, publication, and project-summary contracts.
- RFC 8785 JSON Canonicalization Scheme in Python and C++.
- Immutable storage and exact export of canonical package bytes.
- Stable parameter-slot, candidate-claim, evidence, and review-event identities.
- Optimistic review concurrency and idempotent publication.
- Database constraints for persisted trust invariants.
- Evidence-class-dependent validation and a separate execution-artifact role.
- Capability-specific review-gate declarations.
- Explicit v1 retirement and legacy-history behavior.
- Adversarial tests for concurrency, restart, mutation, Unicode, numeric, source-artifact, and compatibility failures.
- Repair of the OCCT-disabled desktop include error that prevents full application verification.

### Excluded

- Live web research, PDF parsing, OCR, or datasheet extraction.
- Motor A versus Motor B execution or replacement of fixed C++ demonstrator inputs.
- New structural, thermal, circuit, CFD, fluid, controls, or other physics checks.
- External solver integration.
- Universal project intake, sandboxed parser workers, or the semantic system graph.
- A proof that a published package drives a C++ engineering calculation; Program 01B owns that experiment.

The v2 package establishes reviewed and reproducible input identity. It does not establish that the values are physically correct, complete for a capability, or sufficient to show that a design works.

## Trust invariants

The implementation must preserve these invariants across HTTP handlers, process restarts, and direct database writes:

1. A review decision names an immutable candidate claim in the same revision; a field label is never the reviewed identity.
2. Any semantic change to a claim creates a new claim ID and invalidates selection of the prior content for the changed assertion.
3. A draft revision has no published-object reference, content hash, or publication timestamp.
4. A sealed published revision has exactly one immutable object reference, which is also its SHA-256 content hash.
5. Export returns the stored bytes only after canonical-byte and hash verification; export never rebuilds a published v2 package from relational rows.
6. A missing review, stale version, unresolved required publication gate, parser error, unsupported version, or integrity mismatch cannot become publication success. An unknown execution input, unresolved execution gate, or nonconverged backend cannot become execution success or requirement satisfaction.
7. Exactly one versioned Prometheus decision core owns applicability, acceptance criteria, obligation outcomes, coverage, findings, and the project summary.
8. Program 01 uses the C++ decision core for that authority. Python workers and numerical engines cannot independently issue a Prometheus verdict.
9. Published content is immutable even if an application handler is bypassed. Export-time verification remains a second line of defense, not a substitute for immutability.
10. Unsupported and legacy content remains visible as unsupported or legacy; it is never silently dropped or upgraded.

## System boundary

```text
Immutable source/artifact bytes
          │
          ▼
Relational draft: slots, claims, evidence, selections
          │
          ▼
Explicit revision-scoped human review
          │
          ▼
Capability-specific gate evaluation
          │
          ▼
Version 2 contract compilation
          │
          ▼
RFC 8785 canonical bytes
          │
          ▼
SHA-256 over those exact bytes
          │
          ▼
Immutable content-addressed database object
          │
          ├──────────────► exact verified export
          │
          └──────────────► independent C++ parse, recanonicalize, and hash
```

Relational records are authoritative while a revision is a draft. The stored byte object is authoritative after publication. Search indexes and relational projections may be regenerated from the object, but they cannot replace it.

## Version 2 package boundary

### Contract identity

- JSON Schema dialect: Draft 2020-12.
- Schema ID: `urn:prometheus:schema:execution-component:2.0.0`.
- Schema version: `2.0.0`.
- Canonicalization: `RFC8785` with the Prometheus negative-zero rejection policy below.
- Hash spelling: `sha256:` followed by 64 lowercase hexadecimal characters.
- Media type: `application/vnd.prometheus.execution-component+json;version=2.0.0`.
- API base: `/api/v2`.

The package does not contain its own content hash. Embedding that hash would create a recursive identity definition. The revision binding, publication response, `ETag`, and object metadata carry the hash outside the hashed bytes.

### Package content

The canonical package contains:

- schema ID and version;
- package role `reviewed_input`;
- revision and component identities;
- package-compiler name and version;
- immutable artifact references;
- parameter slots in deterministic contract order;
- the selected candidate claim for each slot;
- typed known or unknown engineering values;
- supporting evidence IDs;
- effective review-event records containing the accepted decision, reviewer audit label, notes, UTC review time, reviewed claim fingerprint, and event ID;
- the reviewed draft version;
- capability-gate declarations, their satisfied publication reviews, and current execution readiness;
- limitations and unresolved information;
- `engineering_decision_authority=prometheus_cpp` together with `authority_role=input_only`, declaring that the package is input to, not output from, the Prometheus decision core.

Wall-clock publication time is object metadata rather than package content. Compiling the same reviewed revision with the same compiler version therefore produces the same semantic bytes before publication.

Array order is part of the package contract because RFC 8785 does not reorder arrays. Parameter slots sort by ASCII parameter name and then slot ID; candidate, evidence, review-event, artifact, limitation, and gate arrays sort by their stable ASCII IDs. Curve points and other semantically ordered sequences retain their declared domain order. A compiler that emits a different order produces a different object and fails the checked-in package vector.

### Resource limits

Limits are part of contract behavior and fail before publication:

| Input | Limit |
| --- | ---: |
| Raw API request body | 8 MiB |
| Canonical execution package | 8 MiB |
| JSON nesting depth | 64 |
| Total JSON nodes | 100,000 |
| Members in one object | 10,000 |
| Elements in one array | 10,000 |
| One UTF-8 string value | 1 MiB |
| Decisions in one review batch | 1,000 |
| Reviewer label after trimming | 1 to 256 UTF-8 bytes |
| Review notes after trimming | 1 to 4,096 UTF-8 bytes |
| Idempotency key | 16 to 128 ASCII characters |

The idempotency-key alphabet is `[A-Za-z0-9._:-]`. Security identities use server-generated ASCII IDs and content hashes. Unicode remains allowed in display labels and evidence text, but it is not converted into a security identity.

Revision, slot, claim, evidence, review-event, gate, and publication-request IDs use lowercase hyphenated UUIDv4 strings generated by the server. A claim fingerprint is SHA-256 over RFC 8785 bytes containing its owning revision ID, slot ID, known/unknown state, typed value or reason, unit, provenance, and sorted evidence IDs. The revision and slot bindings therefore participate in the reviewed fingerprint rather than relying on a globally repeated field label.

## RFC 8785 canonicalization

### Normative behavior

Prometheus adopts [RFC 8785](https://www.rfc-editor.org/rfc/rfc8785.html) rather than Python dictionary ordering or a project-specific approximation. Canonicalization therefore uses:

- UTF-8 output without a byte-order mark;
- no insignificant whitespace;
- recursive object-property sorting by raw UTF-16 code units;
- unchanged array order;
- ECMAScript-compatible string escaping;
- ECMAScript-compatible binary64 number serialization;
- rejection of duplicate decoded property names, invalid Unicode, lone surrogates, and non-finite numbers.

RFC 8785 preserves Unicode strings as supplied and does not normalize NFC and NFD sequences. Prometheus follows that rule. Canonically equivalent display strings can therefore have different object hashes. Identifiers that must survive normalization attacks are ASCII IDs or SHA-256 hashes, not user-visible Unicode strings.

### Numeric policy

JSON numbers in hashed packages must parse to finite IEEE-754 binary64 values under the contract's type constraints. The compiler treats the resulting binary64 value—not the author's original decimal spelling—as the numeric value. Integers outside `[-9007199254740991, 9007199254740991]` are rejected as JSON numbers. Values requiring wider integers or exact decimal precision use a typed decimal-string representation defined by the engineering-value schema.

The parser rejects overflow to infinity, nonzero underflow to zero, NaN, positive or negative infinity, and negative zero. RFC 8785 itself serializes positive and negative zero as `0`; [verified erratum 7920](https://www.rfc-editor.org/errata/rfc8785) recommends rejecting parsed negative zero to prevent distinct inputs from collapsing to one representation. Prometheus adopts that fail-closed recommendation. The conformance corpus records the base RFC result and separately asserts the Prometheus rejection policy.

The serializer must not emit bytes outside that parser domain. In particular,
RFC 8785 can render an exponent-form binary64 value such as `1e20` as an
integer-looking token outside the safe-integer range. Prometheus rejects that
value or requires an exact-decimal string; it never publishes canonical bytes
that its own independent verifier would reject.

### Python implementation

Python package compilation uses `rfc8785==0.1.4`, pinned in repository dependency metadata and the lockfile. A Prometheus preflight walker applies the size, depth, duplicate-key, Unicode, numeric, and negative-zero policy before `rfc8785.dumps` produces bytes. Raw JSON test inputs use a duplicate-preserving parser path so duplicate keys cannot disappear before validation.

### C++ implementation

The desktop integrity library independently parses and canonicalizes JSON. It does not call Python and does not accept Python-generated bytes as proof of canonicalization.

The offline build vendors:

- `nlohmann/json` 3.12.0 at commit `55f93686c01528224f448c19128836e7df245f72` for bounded SAX parsing;
- the required Ryu source subset at commit `3377662b1958dbdefb679e2c110368512cccf4f6` for shortest binary64 conversion.

The repository records upstream URL, commit, license, and SHA-256 for every vendored file. The nlohmann files retain the MIT license; the selected Ryu files retain their upstream Apache-2.0 or Boost-1.0 notices. The parser layer rejects duplicate decoded keys and policy-invalid number tokens before building the canonical value tree. The serializer implements UTF-16 property comparison and RFC 8785 escaping rather than delegating those behaviors to a generic `dump` call. Qt's maintained `QCryptographicHash::Sha256` computes the final object hash. The integrity test may link Qt Core; the separate headless decision-core test remains Qt-free.

The verifier accepts stored package bytes and an expected object ID, then:

1. checks the byte and structural limits;
2. parses under the strict I-JSON policy;
3. independently emits RFC 8785 bytes;
4. requires the emitted bytes to equal the stored bytes exactly;
5. hashes the stored bytes;
6. requires the result to equal the expected object ID;
7. validates the supported schema ID and version.

### Shared conformance corpus

Python and C++ read the same checked-in manifest, input byte documents or deterministic generation recipes, expected canonical bytes, expected hashes, and expected failures. Generated recipes cover malformed UTF-8 and resource boundaries without committing invalid text or redundant multi-megabyte blobs; both harnesses must construct the same bytes and no case may be skipped. The corpus includes the official RFC/reference cases plus Prometheus cases for:

- Unicode property names and values;
- raw control characters and required escapes;
- NFC/NFD distinction;
- nested object and array ordering;
- integer and floating-point equivalence;
- smallest, largest, very small, and very large finite binary64 values;
- fixed/scientific notation thresholds;
- negative zero;
- NaN and infinities supplied through programmatic value paths;
- duplicate decoded keys, invalid UTF-8, and lone surrogates;
- unsafe integers, underflow, overflow, excessive depth, and excessive size;
- one complete shuffled Prometheus package that produces one checked-in canonical byte file and SHA-256 in both languages.

## Immutable object publication

### Stored object

`published_objects` stores:

- `object_hash`, the primary content-addressed key;
- exact canonical `payload_bytes` as a binary large object;
- byte length;
- media type;
- schema ID and version;
- canonicalization identifier;
- aware UTC creation time.

`component_revisions.published_object_hash` is both the object reference and the package content hash. It is null for drafts, and the publication binding is unique on `revision_id`, so a revision can name at most one object. More than one revision may reference an object only when the stored bytes and metadata are identical. The package bytes do not carry a second hash field that could disagree with the reference.

Database triggers reject update or deletion of a published object. They also reject changing or clearing a sealed revision's object reference, publication time, or sealed-integrity classification. A hash conflict can reuse an existing object only when its bytes and all contract metadata match exactly. A matching hash with different bytes or metadata raises an integrity error and aborts publication.

### Export

The v2 export path loads the referenced object, checks its length and metadata, runs the strict canonical-byte verifier, recomputes SHA-256, and returns the stored bytes unchanged. The response carries the object ID as a strong `ETag` and returns the versioned media type.

Missing bytes, a reference mismatch, noncanonical storage, an unsupported schema, or a hash mismatch returns an integrity failure. The handler does not fall back to relational reconstruction.

### Legacy rows

The migration preserves existing revisions and hashes. Existing published rows without stored authoritative bytes receive `publication_integrity=legacy_unsealed`. They remain queryable as historical records but cannot be exported as sealed v2 objects or used as v2 idempotent successes. New publication uses `publication_integrity=sealed_v2` only.

## Claims, evidence, and review

### Parameter and claim model

```text
Revision
  └─ Parameter slot
       ├─ Candidate claim A ── evidence links ── review events
       ├─ Candidate claim B ── evidence links ── review events
       └─ One explicit current selection
```

A parameter slot supplies the stable parameter name within a revision. Candidate claims hold exact asserted content. A claim includes:

- server-generated `claim_id` and owning `revision_id`;
- parameter-slot ID;
- `known` or `unknown` value state;
- typed value and unit when known;
- explicit reason when unknown;
- evidence links;
- claim provenance;
- a canonical fingerprint over the semantic claim content.

Claim semantic columns are immutable. Editing a value, unit, state, reason, or evidence set creates a new claim and preserves the old row. A separate selection record chooses at most one current claim for a slot. Program 01A's synthetic provider emits one candidate per slot, but the database and v2 contract permit multiple conflicting candidates.

The database enforces unique slot names within a revision, unique claim identities, one current selection per slot, and composite same-revision foreign keys for claim, evidence, selection, and review relationships.

### Review protocol

`POST /api/v2/revisions/{revision_id}/reviews` accepts one atomic batch containing:

- `expected_draft_version`;
- a nonblank reviewer label;
- one or more decisions, each with a `claim_id`, status `accepted`, `rejected`, or `ambiguous`, and nonblank notes.

The reviewer label is self-asserted in Program 01A because the local service has no authentication. It is an audit label, not proof of a person's identity.

Review events are append-only. Each event records the claim ID, claim fingerprint, reviewer label, notes, decision, owning revision, UTC review time, and draft version applied. A successful batch submitted against version `n` writes its events at version `n+1` and advances the revision to `n+1`. The effective review is the event with the greatest applied draft version for that claim. A later review can supersede `rejected` or `ambiguous` without deleting the earlier event.

The handler locks the draft revision, compares `expected_draft_version`, validates the complete batch, appends all events, and increments `draft_version` once. One batch may name a claim only once; duplicate claim IDs fail the whole request, and a unique `(claim_id, applied_draft_version)` constraint supplies the database backstop. A stale version returns `409 stale_draft_version` and stores no partial decisions. A claim or evidence ID owned by another revision fails closed. Published revisions reject review and draft mutation.

Publication requires one selected candidate for every parameter required by the package capability and a current effective `accepted` review for each selected claim. Rejected and ambiguous selected claims block publication. Accepted unknown claims can document an unresolved value, but an unknown parameter required by the selected capability keeps the corresponding execution gate blocked.

### Confidence dimensions

The generic `confidence` field is retired. `extraction_confidence` is optional and means only confidence in transcription or extraction. A database check constrains populated values to `[0,1]`; synthetic evidence uses null unless the fixture documents how the number was obtained.

The contracts keep these dimensions separate:

- evidence class and source authority;
- artifact/component identity match;
- extraction confidence;
- claim review decision;
- physical validation status;
- model completeness;
- applicability limits;
- uncertainty representation.

Prometheus does not combine them into an engineering-confidence score.

### Evidence classes

Every evidence record has a stable evidence ID, owning revision, evidence class, immutable provenance, and artifact or parent references required by its class.

| Evidence class | Required class-specific content | Fields that are conditional rather than universal |
| --- | --- | --- |
| `manufacturer_document` | Stored artifact hash and document identity | Public URI, excerpt, page/section locator |
| `private_upload` | Stored artifact hash and local provenance | Public URI |
| `user_measurement` | Measurement method, unit, observation time, and source artifact or recorded observation | Public URI, page, excerpt |
| `derived_claim` | Derivation method and same-revision parent claim/evidence IDs | Public URI, page |
| `validation_observation` | Test/inspection provenance and observation time | Public URI, document excerpt |

`source_uri` is required only when the record asserts an externally retrievable origin. A locator and excerpt are required only when the claim relies on a bounded region of a document; otherwise they are optional. Whenever one of these conditional fields is present, its format and internal consistency are still validated.

An unknown claim requires a reason. It may reference search records that explain what was checked, but it does not require evidence pretending that a numerical value exists.

Raw solver outputs are not evidence records. Future execution stores them as immutable execution-result artifacts with backend, version, input-package, convergence, and run-manifest provenance.

### Capability-specific gates

Each capability declares the review-gate IDs it requires and whether each gate controls `publication` or `execution`. A gate result records its capability, phase, required review type, state, and satisfying review/event references. The fixture package uses component-identity, source-artifact, claim-selection, and claim-review publication gates. It does not introduce mesh, contact, turbulence, control-approximation, or solver-setting gates.

Publication requires every publication gate declared for the package capability. A reviewed unknown claim may still be published so the package can state exactly what is missing; if that value is required for execution, the package records `execution_readiness=blocked` and the unresolved execution gate. Execution remains blocked until every execution gate named by the selected capability is satisfied. Gates not declared by that capability have no effect.

## Multidimensional project summary

The project summary separates engineering outcome, coverage, and workflow state:

```json
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
    "not_evaluated": 5
  },
  "obligation_total": 19,
  "assessment_scope_id": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "decision_core": {
    "name": "prometheus_cpp",
    "version": "1.0.0"
  }
}
```

Allowed verdicts are `satisfied_within_scope`, `requirements_violated`, and `indeterminate`. Coverage is independently `sufficient`, `insufficient`, or `not_assessed`. Execution states are `not_started`, `ready`, `running`, `blocked`, `completed`, `completed_with_blocked_work`, `failed`, and `cancelled`.

The decision core enforces:

- nonnegative counts whose sum equals the declared obligation total;
- any nonzero violation count produces `requirements_violated`, even with insufficient coverage or blocked work;
- absent violations, any applicable indeterminate or not-evaluated obligation produces `indeterminate` rather than `satisfied_within_scope`;
- coverage is `not_assessed` when no obligation set has been assessed, `insufficient` when an applicable obligation is indeterminate or not evaluated, and `sufficient` only when every applicable obligation has a conclusive outcome;
- `satisfied_within_scope` requires at least one applicable obligation, sufficient coverage, zero violations, and no blocked applicable obligation;
- execution failure, missing input, unsupported capability, parser failure, and nonconvergence do not imply a requirement violation, but they prevent unsupported satisfaction;
- `assessment_scope_id` identifies the immutable requirements, scenarios, model, evidence snapshot, applicability limits, and uncertainty basis for the statement.

The C++ decision core creates the project summary. Python may validate, store, and transport that record but cannot derive or alter its verdict, counts, or coverage. Any user-facing satisfaction statement must name or link the `assessment_scope_id` and state that satisfaction holds only for its requirements, scenarios, model, evidence, applicability range, uncertainty treatment, and numerical-validity conditions.

## Concurrency and idempotency

### Revision versioning

Every mutable revision has a monotonically increasing `draft_version`. Claim creation, selection changes, evidence-link changes, and successfully committed review batches increment it under a revision lock. Review and publication requests carry the expected value. A stale request returns `409` with the current version and makes no mutation.

A review-versus-publication race is deterministic: whichever transaction locks and commits the revision first advances or seals its state. The other request observes a stale version or published state and fails explicitly.

### Publication request

`POST /api/v2/revisions/{revision_id}/publication` requires:

- an `Idempotency-Key` header;
- `expected_draft_version`;
- requested package schema ID and version.

The persisted publication request stores the key, operation, revision ID, a SHA-256 request fingerprint, state, response status, exact response body bytes, replayed application headers, and published object ID. A unique `(operation, idempotency_key)` constraint makes the key durable for that operation for the lifetime of the project database. The request fingerprint is SHA-256 over RFC 8785 bytes for the operation, revision, expected version, and requested contract. Reusing a key with a different fingerprint returns `409 idempotency_conflict`.

The publication transaction performs these steps in order:

1. acquire the dialect-specific revision write lock;
2. resolve or insert the idempotency record;
3. verify the expected draft version and unpublished state;
4. verify selections, effective claim reviews, evidence, required publication gates, and artifact objects, and record execution-gate readiness without converting blocked execution into publication failure;
5. compile and schema-validate the package value;
6. produce RFC 8785 bytes and enforce byte limits;
7. compute SHA-256 and independently revalidate the bytes;
8. insert or exactly match the immutable object;
9. bind the revision permanently to the object and set its UTC publication time;
10. store the exact success response and terminal publication state;
11. commit once.

At step 2, a matching terminal record short-circuits the remaining publication steps. A stored success first re-verifies its immutable object and then replays; a stored terminal application failure replays directly.

An unexpected or retryable failure before the success commit rolls back the object insertion, revision status, publication timestamp, object reference, content hash, terminal job state, and success response together. Process death before commit leaves no published revision. Transport failure after commit is safe: a retry with the same key verifies the stored object and returns the stored application response byte-for-byte.

Successful responses and deterministic terminal application failures are persisted with their status, body bytes, and application-controlled headers. Replaying their key and fingerprint returns that record even if the surrounding revision later changes. A client must use a new key for a new expected version. Busy, deadlock, connection-loss, process-death, and injected infrastructure failures are retryable rather than terminal: they commit neither domain mutation nor terminal response, so the same key may retry. Generated transport headers such as `Date`, `Server`, and connection framing are not part of the stored application response.

Same-key concurrent requests converge on the committed response. Different-key concurrent requests for one revision serialize; one publishes and the other receives `409 revision_already_published` with the existing object reference. The unique revision/object binding is the final backstop.

### SQLite

SQLite development requires SQLite 3.35 or newer and uses foreign keys, a 5-second busy timeout, and `BEGIN IMMEDIATE` for review/publication writes. SQLite permits one writer. Failure to acquire the lock returns retryable `publication_busy`; it does not fall through to success. These assumptions follow SQLite's documented [transaction](https://www.sqlite.org/lang_transaction.html) and [isolation](https://www.sqlite.org/isolation.html) behavior.

### PostgreSQL

PostgreSQL 17 is the Program 01A production conformance target. It uses `READ COMMITTED`, a `SELECT FOR UPDATE` row lock on the revision, conflict-safe idempotency insertion, and unique constraints. Lock order is revision, idempotency record, object row, then publication binding. Deadlock or serialization errors receive at most three internal attempts with the same idempotency key. Support for another PostgreSQL major requires the same migration and semantic concurrency suite. These assumptions follow PostgreSQL's [transaction-isolation](https://www.postgresql.org/docs/current/transaction-iso.html) and [explicit-locking](https://www.postgresql.org/docs/current/explicit-locking.html) behavior.

## Artifact identity

Evidence binds to stored artifact bytes by SHA-256, not to a filename or URI. Ingestion opens the selected source, copies bounded bytes into the local content-addressed store, hashes the stored copy, and atomically installs it. The origin path and supplied name are provenance only.

- Missing or unreadable input fails before draft evidence is created.
- A changed file produces a different artifact identity and requires a new claim/review relationship.
- A source that changes during ingestion is rejected when the copied bytes, expected hash, or before/after file identity checks disagree.
- Deleting or changing the external original after successful ingestion does not change the stored artifact.
- Symlinks, Unicode-lookalike names, and NFC/NFD path spellings cannot substitute for the expected fixture because fixture selection checks the declared ASCII fixture ID and stored content hash.
- Program 01A performs no network fetch while resolving artifacts.

## Database invariants

Alembic migrations enforce trust rules below the HTTP layer where SQLite and PostgreSQL support them:

- check constraints for revision, publication, review, gate, job, execution, and obligation states;
- check constraints for extraction confidence and known/unknown claim shapes;
- unique slot names within a revision;
- unique IDs plus composite `(revision_id, id)` keys for same-revision foreign keys;
- one current claim selection per slot;
- one immutable object binding per published revision;
- sealed publication reference/hash/time presence and draft absence;
- immutable published object, claim content, review event, and publication binding triggers;
- SQLite foreign-key activation on every connection.

The persisted state vocabulary is closed rather than free-form:

| Record | Allowed states |
| --- | --- |
| Component revision | `draft`, `published` |
| Claim review event | `accepted`, `rejected`, `ambiguous` |
| Review gate | `pending`, `satisfied`, `blocked` |
| Publication request | `in_progress`, `succeeded`, `terminal_failure` |
| Ingestion job | `queued`, `running`, `succeeded`, `failed`, `cancelled`, `timed_out` |
| Execution | `not_started`, `running`, `completed`, `failed`, `cancelled`, `timed_out`, `backend_unavailable`, `nonconverged` |
| Proof obligation | `satisfied`, `violated`, `indeterminate`, `not_applicable`, `not_evaluated` |

Review completeness and execution readiness are derived from claims and gates rather than stored as extra revision states that could drift. Deleting or updating immutable claim content, claim-evidence links, review events, published objects, or publication bindings fails at the database boundary.

These closed states govern v2 tables. Existing v1 job and revision rows retain their original vocabulary in legacy history; migration does not relabel past events as though they occurred under the v2 state machine.

PostgreSQL stores timestamps as `TIMESTAMPTZ`. SQLite stores validated UTC RFC 3339 text ending in `Z`. The API emits UTC RFC 3339 only. Application validation remains useful for error messages, but direct invalid inserts must fail at the database boundary for the listed invariants.

## API compatibility and history

New review, publication, and exact-package export endpoints live under `/api/v2`. Requests declare a supported schema version; unsupported versions return an explicit version error without fallback. Legacy field-name review bodies sent to v2 return `claim_id_required` rather than being guessed or translated.

Existing `/v1` component-review, publication, and reconstructed-package endpoints return deterministic `410 Gone` responses with a machine-readable migration-guide reference. Existing unversioned Python analysis routes remain retired. Historical read-only metadata can remain available only when it labels unsealed content and cannot create, publish, or export a v2 package.

Repository history is additive:

- the earlier Program 01A milestone remains as evidence of what was implemented and tested at `e827440`;
- a dated reopening notice states which trust claims were superseded and why;
- the v1-to-v2 migration guide records breaking payloads, endpoints, statuses, and database classifications;
- retired paths carry explicit notices rather than disappearing from documentation;
- a new completion record is written only after the amended acceptance gate passes.

The durable architecture and implementation plan contain product decisions and verification gates. Agent skills, workstation-specific workflow requirements, and forced commit boundaries belong outside those documents. A separate operational runbook may record reproducible commands and environment setup.

## Python and backend support

Repository metadata requires Python `>=3.11,<3.15`, which makes CPython 3.11, 3.12, 3.13, and 3.14 the Program 01A support matrix as of this decision. Continuous integration tests all four. A later Python minor is added to the declared range only after the same suite passes. The lockfile and local reproduction record identify the exact interpreter used for a verification run. Durable documentation does not describe Python 3.12 as the sole supported runtime.

PostgreSQL support is part of the amended publication contract rather than a future production assumption. The same semantic integration suite runs against SQLite and an ephemeral PostgreSQL service. A skipped PostgreSQL suite cannot close Program 01A.

## Acceptance gate

### Canonicalization and contract tests

- Python and C++ pass every shared RFC 8785 and Prometheus vector.
- Both languages produce the exact checked-in complete package bytes and SHA-256.
- The suite covers Unicode, control escapes, NFC/NFD distinction, nested ordering, numeric boundaries, negative zero, NaN, infinities, duplicate keys, invalid Unicode, unsafe integers, underflow, overflow, depth, node count, and byte limits.
- Unsupported schema IDs and versions fail.
- Package export after application/database restart returns the original stored bytes.
- Project-summary tests permit violation plus insufficient coverage plus blocked work, and reject satisfaction with incomplete applicable obligations.

### Claim, evidence, and review tests

- Multiple candidates can coexist for one slot; review targets claim IDs.
- Cross-revision claim and evidence references fail at API and database boundaries.
- Stale review and publication versions fail without partial writes.
- Re-review after rejected or ambiguous decisions preserves history and can establish a new effective decision.
- Empty and whitespace-only reviewer labels and notes fail.
- Oversized review bodies, decision batches, labels, and notes fail.
- Known/unknown and class-specific evidence requirements hold.
- Direct invalid confidence, state, and same-revision relationship inserts fail.
- Capability A is not blocked by gates declared only for capability B.

### Publication and storage tests

- Two same-key publication calls using independent connections produce one object and the same response.
- Two different-key calls for one revision produce one object and one explicit already-published response.
- A new application process replays the stored response for the same key.
- A simulated lost response after commit replays safely.
- Failure injection after idempotency resolution, draft validation, package compilation, schema validation, canonicalization, hash computation, independent byte verification, object insertion, revision binding, response storage, and immediately before commit leaves no partial published state.
- Direct update, deletion, or repointing of sealed content is rejected.
- Export detects a deliberately prepared corrupt object and fails closed.
- A same-hash/different-bytes test path raises an integrity error.
- Fresh migrations and upgrades from the `e827440` schema preserve legacy rows and classify them `legacy_unsealed`.

Concurrency tests use independent database connections and synchronization barriers rather than timing sleeps. The same semantic cases run against SQLite and PostgreSQL.

### Artifact and identity tests

- Missing, unreadable, changed, deleted, and change-during-ingestion sources have explicit outcomes.
- Expected-hash mismatch fails without evidence creation.
- External deletion after successful ingestion leaves the stored artifact verifiable.
- Unicode-lookalike and NFC/NFD fixture-name attacks cannot select or replace the fixture.
- Artifact paths containing traversal components or symlink substitutions cannot escape the allowed ingestion boundary.

### Application and platform tests

- `/api/v2` review, publish, replay, and export happy/failure paths pass.
- Retired v1 mutation/export calls return the documented `410` response.
- A legacy body cannot invoke a v2 review or publication.
- Qt sends claim IDs and expected draft versions and contains no accept-all path.
- The OCCT-disabled desktop build succeeds after the unconditional STEP-importer include is repaired.
- Headless decision-core tests and the C++ integrity suite pass on macOS, Linux, and Windows.
- Python tests pass on the supported interpreter matrix.
- Dependency, license, and offline-build checks account for every vendored file and Python package.

### Documentation deliverables

- Revised architecture/design specification.
- Revised implementation plan without environment-specific agent directives.
- Operational verification runbook.
- v1-to-v2 API and database migration guide.
- Updated validation policy, threat model, changelog, roadmap status, and OpenAPI snapshot.
- Dated Program 01A reopening and, after all gates pass, amended completion records.
- Desktop copies of the approved design and implementation plan.

## Amendment traceability

| Required amendment | Design resolution |
| --- | --- |
| 1. Multidimensional project summary | `Multidimensional project summary` defines independent verdict, coverage, execution, counts, and scope. |
| 2. Cross-language canonicalization | `RFC 8785 canonicalization` fixes JCS behavior, dependencies, policies, and shared Python/C++ vectors. |
| 3. Immutable canonical bytes | `Immutable object publication` makes stored bytes authoritative and export non-reconstructing. |
| 4. Stable claim identities | `Claims, evidence, and review` uses immutable revision-scoped claim IDs and fingerprints. |
| 5. Separate confidence dimensions | `Confidence dimensions` retires generic confidence and adds database bounds. |
| 6. Evidence classes and execution records | `Evidence classes` uses conditional requirements and separates solver artifacts. |
| 7. Concurrency and idempotency | `Concurrency and idempotency` defines versioning, locks, replay, races, and rollback. |
| 8. Database invariants | `Database invariants` assigns allowed states, relationships, presence, and immutability to the database. |
| 9. Python support and authority | `Python and backend support` and `Multidimensional project summary` preserve Python 3.11+ and C++ decision authority. |
| 10. Capability-specific review | `Capability-specific gates` prevents irrelevant reviews from becoming universal blockers. |
| 11. Missing acceptance tests | `Acceptance gate` lists restart, concurrency, failure, mutation, Unicode, version, artifact, and legacy tests. |
| 12. Preserve history | `API compatibility and history` retains milestone evidence and records supersession/retirement. |
| 13. Separate execution instructions | `API compatibility and history` separates durable decisions from the operational runbook. |
| 14. Unchanged scope | `Scope` keeps physics, research, solver, universal intake, and package-driven C++ execution outside 01A. |

## Completion claim and non-claim

Passing this design's acceptance gate will support one narrow claim: Prometheus can publish a fixture-backed, explicitly reviewed component-input package as immutable RFC 8785 bytes and independently reproduce its identity in C++ across the supported database and platform matrix.

It will not show that Prometheus can determine whether an arbitrary engineering project works. That claim requires later programs to ingest heterogeneous artifacts, compile a reviewed semantic model and proof obligations, execute applicable engineering backends, and have the versioned C++ decision core derive scoped findings and coverage from those results.
