# Program 01A v1-to-v2 migration

Program 01A v2 is a breaking trust-boundary change. It replaces field-oriented review and reconstructed exports with revision-scoped claims, append-only review events, and one immutable stored byte object. The package remains reviewed input only: byte integrity does not establish physical truth, model completeness, applicability, safety, or an engineering result.

## Contract identities

| Item | V2 value |
| --- | --- |
| API base | `/api/v2` |
| Schema ID | `urn:prometheus:schema:execution-component:2.0.0` |
| Schema version | `2.0.0` |
| Package media type | `application/vnd.prometheus.execution-component+json;version=2.0.0` |
| Canonicalization | RFC 8785 with the Prometheus negative-zero and bounded-input policy |
| Object identity | `sha256:` followed by 64 lowercase hexadecimal characters |

The v2 package does not contain its own object hash. The publication response, quoted strong `ETag`, revision binding, and stored-object metadata carry that identity outside the hashed bytes.

## Breaking changes

| V1 behavior | V2 behavior | Migration action |
| --- | --- | --- |
| Review decisions identify a mutable `field_name`. | Decisions identify a server-issued, revision-scoped `claim_id` and the request supplies `expected_draft_version`. | Read the current v2 revision, retain each selected claim ID and fingerprint, then submit decisions against that exact draft version. V2 does not infer a claim from a field name. |
| Evidence rows carry mutable review columns. | Review is an append-only event containing the claim fingerprint, decision, reviewer label, note, timestamp, and applied draft version. | Treat the latest event for a selected claim as effective; retain older rejected or ambiguous events as history. |
| The reconstructed package carries a top-level `content_hash`. | SHA-256 is the external object identity and strong `ETag`; no hash occurs inside the package bytes. | Compare the response `object_hash` and `ETag` with a SHA-256 recomputed over the exact response bytes. |
| Export reconstructs JSON from relational rows. | Export loads the bound object, verifies metadata, canonical bytes, and SHA-256, then returns the stored bytes unchanged. | Persist or compare the returned byte sequence directly. Do not reserialize it to test identity. |
| Evidence has a generic `confidence`. | `extraction_confidence` describes extraction reliability only. Source authority and physical-validation status are separate closed fields. | Do not translate extraction confidence into engineering confidence, acceptance, or physical validation. |
| V1 mutation and package-export endpoints are callable. | Those endpoints return `410 Gone` with the migration document; replacements live under `/api/v2`. | Move clients to the endpoint sequence below. Historical v1 metadata reads may remain available. |
| A formerly published row has a v1 hash but no authoritative stored package bytes. | Migration labels it `publication_integrity=legacy_unsealed`. | Keep it as historical metadata only. It cannot be exported or replayed as a sealed v2 object. |
| Publication updates a relational revision. | New publication creates or exactly matches an immutable object and permanently binds the revision as `publication_integrity=sealed_v2`. | Use the publication idempotency key for retries and export only through the v2 object route. |

Migration never fabricates authoritative bytes for a legacy row. A `legacy_unsealed` record is not silently upgraded to `sealed_v2`; create and review a new v2 draft instead.

## V2 endpoint sequence

All request models are closed: unknown properties are rejected. JSON mutation requests under `/api/v2` are limited to 8 MiB and reject invalid UTF-8, duplicate decoded object keys, and malformed JSON before model validation.

### 1. Create the exact fixture draft

```http
POST /api/v2/fixture-ingestions
Content-Type: application/json
Idempotency-Key: client-generated-token-0001

{"fixture_id":"prometheus.pm-36-gm.fixture-2","schema_version":"2.0.0"}
```

The success status is `201 Created`. The JSON response contains exactly `id`, `state`, `fixture_id`, and `revision`; `state` is `succeeded`. The nested revision contains `id`, `status`, `draft_version`, `contract`, `component`, `parameters`, `capability_gates`, `publication_integrity`, `object_hash`, and `published_at`. Each parameter exposes one `selected_claim` with its `claim_id` and `claim_fingerprint`.

`GET /api/v2/fixture-ingestions/{ingestion_id}` returns that ingestion resource with status `200`; `GET /api/v2/revisions/{revision_id}` returns the current revision resource with status `200`. Neither GET has a request body.

### 2. Append a claim-review batch

Use the `draft_version` and selected `claim_id` values returned by the revision resource:

```http
POST /api/v2/revisions/{revision_id}/reviews
Content-Type: application/json

{
  "expected_draft_version": 0,
  "reviewed_by": "local-reviewer-label",
  "decisions": [
    {
      "claim_id": "00000000-0000-4000-8000-000000000000",
      "status": "accepted",
      "note": "Accepted as synthetic conformance input only."
    }
  ]
}
```

The UUID above is a placeholder for the server-issued lowercase UUIDv4. `status` is exactly one of `accepted`, `rejected`, or `ambiguous`; every decision requires a nonblank note. A successful atomic batch returns the updated revision resource with status `200` and increments `draft_version` once. A batch may name a claim at most once. Any invalid decision rejects the entire batch without an event or version change.

Review has no idempotency key. Optimistic concurrency is its retry boundary: a repeated request against the old version returns `409 stale_draft_version`; a deliberate later review must first fetch and send the new version. `reviewed_by` is a local audit label, not an authenticated identity.

### 3. Publish the reviewed draft

```http
POST /api/v2/revisions/{revision_id}/publication
Content-Type: application/json
Idempotency-Key: client-generated-publish-token-0001

{
  "expected_draft_version": 1,
  "schema_id": "urn:prometheus:schema:execution-component:2.0.0",
  "schema_version": "2.0.0"
}
```

A success returns `201 Created`, `Content-Type: application/json`, a quoted object-hash `ETag`, and `Location: /api/v2/revisions/{revision_id}/execution-package`. Using valid-format placeholders for the generated hash and revision UUID, its body contains exactly these members:

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

`blocked` is the truthful result for the current fixture because the Program 01B package consumer is absent. Publication success means the reviewed input bytes were sealed; it is not an engineering pass or a claim that the package is executable.

### 4. Export the stored object

```http
GET /api/v2/revisions/{revision_id}/execution-package
```

Success is `200 OK` with the versioned package media type and `ETag: "sha256:..."`. The response body is the exact stored RFC 8785 byte sequence. The server rechecks canonical form, metadata, byte length, and SHA-256 before returning it. The body is not reconstructed from relational rows.

## Error contract

V2 application errors use this JSON shape, with documented context members added where shown:

```json
{"detail":{"code":"stable_error_code","message":"Human-readable explanation."}}
```

| Status | Code | Condition and retry rule |
| --- | --- | --- |
| `400` | `request_json_invalid` | Invalid UTF-8, malformed JSON, or duplicate decoded keys. Correct the bytes; no idempotent operation is created. |
| `413` | `request_too_large` | Declared or streamed v2 JSON body exceeds 8 MiB. Reduce the request. |
| `422` | `request_validation_error` | The body violates its closed transport model. Correct it. |
| `422` | `idempotency_key_required` | Fixture creation or publication omitted `Idempotency-Key`. |
| `422` | `invalid_idempotency_key` | The key is not 16–128 characters from `[A-Za-z0-9._:-]`. No publication record is created. |
| `422` | `claim_id_required` | A v1-style review decision supplies `field_name` without `claim_id`. Fetch the revision and use its selected claim identity. |
| `422` | `unsupported_fixture_id` | Fixture intake names anything other than the one declared synthetic fixture. |
| `422` | `unsupported_schema` | Fixture intake requests an unsupported schema version. |
| `404` | `fixture_ingestion_not_found` | The ingestion ID does not exist. |
| `404` | `revision_not_found` | The revision ID does not exist. |
| `404` | `claim_not_found` | A review claim ID does not exist. |
| `409` | `idempotency_conflict` | A fixture or publication key is already bound to a different request fingerprint. Generate a new key for a genuinely different operation. |
| `409` | `fixture_ingestion_incomplete` or `revision_integrity_failure` | Persisted v2 state is incomplete or violates the revision contract. Do not treat it as a partial success. |
| `409` | `stale_draft_version` | Review or publication used an old version. The response includes `current_draft_version`; refetch before deciding whether to retry. |
| `409` | `revision_not_draft` | Review targeted a published or otherwise non-reviewable revision. |
| `409` | `cross_revision_claim` | A review claim belongs to another revision. |
| `409` | `claim_not_finalized`, `claim_integrity_failure`, or `claim_fingerprint_mismatch` | Stored claim construction or identity is invalid. Do not retry unchanged. |
| `409` | `claim_review_gate_missing` | The draft lacks its declared claim-review publication gate. Repair the persisted graph; no review success is recorded. |
| `409` | `publication_review_incomplete` | Not every selected claim has an effective accepted review. |
| `409` | `publication_gate_blocked` | A required publication gate is missing or unresolved. Execution-only gates do not cause this error. |
| `409` | `unsupported_schema` | Publication requested a schema ID or version other than the exact v2 pair. The deterministic failure is stored for that key. |
| `409` | `execution_package_invalid` | The draft cannot satisfy the v2 execution-component contract. |
| `409` | `revision_already_published` | Another publication key reached an already sealed revision; the response includes `published_object_hash`. |
| `409` | `revision_not_published` | Export targeted a draft. |
| `409` | `publication_integrity_unsealed` | Export targeted historical metadata without a sealed v2 object binding. |
| `409` | `published_object_integrity_error` | Stored bytes, metadata, schema binding, or hash failed verification. This never degrades to success. |
| `503` | `review_busy`, `publication_busy`, or `publication_in_progress` | The bounded database write retries were exhausted or a matching publication is still active. Retry the identical operation; retain the publication key. |

Publication persists terminal `201` and deterministic `409` application responses. Repeating the same publication key with the same revision, expected version, and schema replays the stored status, body, and headers byte-for-byte, including after a lost response or process restart, without recompiling. A matching request that is still in progress yields a retryable failure rather than a second publication.

Fixture creation uses a separate lifetime key namespace. Reusing the same key and exact request resolves to the same ingestion graph; reusing it with a changed request returns `409 idempotency_conflict`. It does not promise byte-for-byte replay of an earlier revision representation after that draft has changed.

## Retired v1 routes

| Retired route | V2 replacement |
| --- | --- |
| `POST /v1/research-jobs` | `POST /api/v2/fixture-ingestions` |
| `POST /v1/research-jobs/{job_id}/review` | `POST /api/v2/revisions/{revision_id}/reviews` |
| `POST /v1/research-jobs/{job_id}/publish` | `POST /api/v2/revisions/{revision_id}/publication` |
| `GET /v1/component-revisions/{revision_id}/execution-package` | `GET /api/v2/revisions/{revision_id}/execution-package` |

Every retired route returns `410 Gone` with exactly:

```json
{
  "detail": {
    "code": "v1_trust_boundary_retired",
    "message": "The v1 review/publication boundary is retired.",
    "migration_guide": "/docs/migration/program-01a-v1-to-v2.md"
  }
}
```

Historical `GET /v1/components/...` metadata reads remain available and expose `publication_integrity`, but they never return reconstructed package bytes. Unversioned analysis routes retain their separate retirement behavior; they are not aliases for v2 publication or C++ execution.
