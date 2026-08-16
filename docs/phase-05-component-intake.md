# Phase 5 reviewed component and evidence intake status

Phase 5 is active. Its objective is to let a user add a component that is
not in the built-in synthetic catalog (Motor A, Motor B, the sealed PM-36
fixture) while retaining the same reviewed-input trust boundary Program 01A
established for those three fixtures.

## Checkpoint 1: manual typed component draft intake

The backend now accepts a live typed component submission and builds the
same Program 01A v2 draft graph shape produced by fixture ingestion, without
touching the fixture path, the review service, the publication service, or
the package compiler:

- `POST /api/v2/component-drafts` (idempotency-key required) validates a
  manufacturer/part-number/revision identity, a declared capability ID, and
  one or more typed parameters, each either a known engineering value with
  its original value/unit or an explicit unknown with a reason;
- every known parameter becomes a `user_measurement` evidence record with a
  required measurement method and observation timestamp, distinct from the
  fixture path's `private_upload` evidence class, because the source
  authority here is genuinely the submitting user, not a pinned fixture;
- the exact canonical submission is stored as an immutable artifact and
  referenced by the `source_artifact` publication gate, so a component with
  no attached document still retains exact, hashable evidence of what was
  typed and when;
- unknown parameters retain a reason and no evidence, matching the fixture
  pipeline's known/unknown split;
- the `claim_review` publication gate starts `pending` like every other v2
  draft; the `package_consumer` execution gate starts `blocked`, because no
  Prometheus capability yet consumes a manually entered component — this
  intake path adds no new engineering consumer and claims none;
- idempotency replay is keyed by idempotency key plus a canonical request
  fingerprint, so reusing a key with a materially different submission fails
  closed instead of silently returning the first draft.

A new `manual_component_draft_jobs_v2` table records the idempotency
ledger, following the same queued/running/succeeded shape as
`fixture_ingestion_jobs_v2`. `GET /api/v2/component-drafts/{id}` reads it
back the same way fixture ingestions are read back.

Tests prove the intake path builds one complete unreviewed draft graph,
that idempotency replay and conflict detection both work, that a duplicate
component revision and an invalid idempotency key are rejected before any
row is committed, that an injected mid-pipeline failure rolls back every
draft row, and — the most important integration proof for this checkpoint —
that the resulting draft compiles into a valid `blocked` execution package
through the **unmodified** `review_service_v2` and `package_compiler_v2`
pipeline, plus a full HTTP create/review/publish/export round trip.

This checkpoint only proves that a manually typed component can become a
reviewed, published, exported package. It does not yet:

- bind a published component package to a CAD entity (the desktop
  `CadController::bindComponent` path still only stores an opaque label —
  see the architecture survey captured for this work);
- invalidate a CAD binding when the bound revision is superseded or the
  underlying claims change (Phase 4's `assemblyArtifactInvalidated` signal
  is the template for this, but nothing consumes it for component bindings
  yet);
- add structured CSV/BOM import, manufacturer datasheet attachment, or any
  PDF/table extraction — the remaining first intake paths named in the
  [deployment plan's Phase 5 section](prometheus-product-to-deployment-plan.md#phase-5-add-reviewed-component-and-evidence-intake);
- add a real execution capability consumer for any manually entered
  component, so `package_consumer` stays honestly `blocked` for every draft
  produced by this path.

## Remaining Phase 5 evidence

1. Real CAD binding of a published non-catalog component revision, with
   hash/fingerprint verification instead of an opaque label.
2. Binding invalidation when a bound revision is superseded, mirroring the
   Phase 4 stale-source-invalidation pattern.
3. Structured CSV/BOM import and manufacturer datasheet attachment.
4. At least one real execution capability that consumes a manually entered
   component, so the exit gate's "a supported analysis consumes the
   reviewed values" claim has real evidence instead of a permanently
   blocked gate.
