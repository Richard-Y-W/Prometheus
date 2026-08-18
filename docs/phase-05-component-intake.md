# Phase 5 reviewed component and evidence intake status

Phase 5's objective was to let a user add a component that is not in the
built-in synthetic catalog (Motor A, Motor B, the sealed PM-36 fixture)
while retaining the same reviewed-input trust boundary Program 01A
established for those three fixtures. Its
[deployment-plan exit gate](prometheus-product-to-deployment-plan.md#phase-5-add-reviewed-component-and-evidence-intake)
— a user adds a component not compiled into Prometheus; its critical
specifications trace to reviewed sources; the component binds to a CAD
part; a supported analysis consumes the reviewed values; conflicting or
missing values remain visible and block unsupported claims — is met as of
checkpoint 4. Structured CSV/BOM import and named/linked acquisition remain
open as valuable additional intake paths, not exit-gate blockers; they can
land later without reopening this phase.

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

- bind a published component package to a CAD entity with hash
  verification — closed by checkpoint 2, below;
- invalidate a CAD binding when the bound revision is superseded or the
  underlying claims change (Phase 4's `assemblyArtifactInvalidated` signal
  is the template for this, but nothing consumes it for component bindings
  yet — still open, see remaining evidence item 1);
- add structured CSV/BOM import, manufacturer datasheet attachment, or any
  PDF/table extraction — the remaining first intake paths named in the
  [deployment plan's Phase 5 section](prometheus-product-to-deployment-plan.md#phase-5-add-reviewed-component-and-evidence-intake);
- add a real execution capability consumer for any manually entered
  component, so `package_consumer` stays honestly `blocked` for every draft
  produced by this path.

## Checkpoint 2: real CAD binding of a published revision

The desktop no longer treats "bind a component to a CAD entity" as a single
trust level. `CadController::bindComponent` is retired in favor of two
distinct, explicitly labeled paths:

- `bindProvisionalCandidate` keeps the existing local file-scan candidate
  flow (unpublished, unreviewed) but now stores its label prefixed
  `"(unpublished candidate) "` and marks the binding `componentVerified:
  false`, so it can never render indistinguishably from a real binding;
- `bindComponentRevision` is the first real "bind a *published* revision"
  entry point. It fetches `GET /api/v2/revisions/{id}/execution-package`
  through the same generic, already-proven infrastructure Program 01B built
  for the motor fixtures (`ExactPackageDownload`'s header/ETag/length
  verification, then `execution::inspect_execution_component` checking the
  body's own SHA-256 against its declared ETag), and only calls
  `CadPart::bindComponent(..., verified: true, packageHash)` after that
  verification succeeds. A new `ComponentBindingController` owns this fetch
  in its own `ExactPackageDownload` instance — deliberately not sharing
  `ServiceController`'s, whose `exactPackageAcquired` signal carries no CAD
  entity tag and is already claimed by `ExecutionController`'s fixture flow.

Binding state is not persisted as a trust claim: `snapshotCadState`/
`restoreCadState` still only round-trip `{cad_entity_id, revision_id,
label}` (no schema change), and every restored binding now applies with
`componentVerified: false` — closing a reopen never silently re-trusts old
stored text. The desktop shows this as a distinct amber "cached — unverified
since last reopen" state with an explicit "Reverify" action
(`reverifyComponentBinding`), separate from the green verified state.

Tests cover: the provisional-candidate path staying unverified; a verified
fetch correctly threading the CAD entity id through to the applied binding;
a failed fetch or hash mismatch never mutating any part's state
(`failVerifiedComponentBinding` only ever sets a surfaced error); a verified
binding requested against a CAD entity that no longer exists reporting its
own failure instead of silently dropping; and the full OCCT
open/bind/save/reopen integration test asserting `componentVerified` is
`false` immediately after a provisional bind and again after reopening a
project with a previously bound (previously provisional) entity.

This checkpoint does not yet:

- embed the verified package bytes in the project so a relocated/offline
  project can show a verified binding without re-fetching (the existing
  `run_store::install_package_binding` / `ExecutionIndex.package_bindings`
  content-addressed, supersede-aware mechanism — already built for the
  fixture-to-CAD-execution path — is the natural reuse target for this,
  left for a later checkpoint rather than pulled in here);
- automatically reverify a binding on project open (reverification is a
  manual action, treating an automatic *network* call on file open as
  different in kind from the existing local-only assembly-hash recheck);
- add invalidation when the bound revision is superseded by a newer
  publication — closed by checkpoint 3, below.

## Checkpoint 3: binding invalidation when a revision is superseded

A component revision can be superseded: a manual-draft submission with the
same manufacturer/part number but a new `revision` string reuses the
existing `Component` row and adds a sibling `ComponentRevision` under it
(`manual_component_intake_v2.py` already did this before this checkpoint —
`parent_revision_id` is never set, so the two revisions are siblings, not a
formal chain). Supersession is a live fact, not a schema field: `GET
/api/v2/revisions/{id}` now computes and returns `superseded_by` — the
most-recently-published sibling revision, when one newer than the requested
revision exists — by querying `ComponentRevision` rows sharing the same
`component_id`, filtering `status == "published"`, and comparing
`published_at`. No migration was needed; this is a read-time computation
over the existing table.

On the desktop, `ComponentBindingController::bindRevision` issues its
existing package fetch/verify unchanged, then — only after that succeeds —
a second, lightweight `GET /api/v2/revisions/{id}` (using the *verified*
`revision_id` from the inspected package bytes, not the caller's input
string) to learn `superseded_by`. This mirrors the checkpoint-2 principle
that immutable package bytes never carry a fact that changes over time: a
superseding publication is checked live, separately, the same way Phase 4
checks a project's live assembly hash rather than baking staleness into an
immutable structural archive. A failure of this secondary check does not
discard an already hash-verified binding — it only leaves supersession
unknown for that bind, never claims false certainty either way.

`CadPart` gained `componentSupersededByRevisionId`. The desktop now shows
four binding states instead of three: unbound (muted), cached/unverified
since reopen (amber, existing), verified and current (green, existing), and
verified-but-superseded (red, new) with an explicit "superseded by a newer
published revision — rebind to update" message. Rebinding to the newer
revision reuses the existing "look up a published revision by ID" +
"bind to selected CAD entity" flow manually; this checkpoint does not add
an automated "bind the superseding revision" one-click action.

Tests cover: the backend correctly reports `superseded_by` for an older
sibling and `null` for the current one (a strict field-set assertion in
`test_api_v2.py` was extended, and the checked-in `docs/openapi-v2.json`
regenerated); the desktop merging a reported supersession into the emitted
binding; and a metadata-check failure still emitting a verified binding
with supersession left unknown, never discarding the proof already
established by hash verification.

## Checkpoint 4: a real execution capability consumes a manually entered component

The `package_consumer` gate no longer hardcodes `blocked` for every manual
draft. `manual_component_intake_v2.py` now checks the submitted
`capability_id` against `_MANUALLY_CONSUMABLE_CAPABILITIES` — currently just
`{DC_GEARMOTOR_CAPABILITY: CONSUMER_HASH}`, the same capability and consumer
Motor A/B already run against. A manual draft that declares it and supplies
the same 17-slot parameter contract gets `package_consumer: satisfied`, and
the pipeline idempotently installs the same well-known
package-consumer-contract artifact fixture ingestion already installs (via
the existing `ingest_local_artifact`, keyed by the same hash) so
`package_compiler_v2` can bind it — no new consumer, no schema change, and
no duplication of the contract's field-level shape in Python: a satisfied
gate is only the capability-level claim "a real consumer exists," exactly
mirroring `FixtureDefinition.consumer_gate_state`; the C++ decision layer
still independently determines whether *this* submission's specific values
make the package `ready`.

This was proven genuinely end-to-end, not just at the gate level. A real
component ("Northline Motion Co. NM-42-GM") was submitted through the actual
HTTP create → review → publish → export round trip, producing a package with
`execution_readiness: "ready"`. Its exact exported bytes are committed as a
static fixture (`fixtures/contracts/execution-component-v2.manual-motor-c.jcs`
+ `.sha256` sidecar — not part of the regenerate-and-diff pipeline, since
manual-draft UUIDs are genuinely random per submission, so it's committed
once like `fixtures/assemblies/motor-arm.step`). A new case in
`desktop/execution/tests/package_consumer_tests.cpp`
(`test_manual_motor_consumption`) feeds those bytes into the **unmodified**
`consume_motor_component` and asserts it extracts the manually typed
engineering values (continuous torque 0.31 N·m, stall torque 2.85 N·m, gear
ratio 64:1 — all distinct from Motor A/B) into the same 12-calculation +
2-validation slot contract. This is the evidence the Phase 5 exit gate
required: a supported analysis consumes reviewed values from a component
that was never compiled into Prometheus.

Backend coverage: a gearmotor-capability draft gets a satisfied gate with
the exact expected consumer-hash reference and publishes `ready`; a draft
declaring any other capability (the existing default) still gets `blocked`
with no references — the allow-list is opt-in, not a blanket unblock.

## Remaining Phase 5 evidence

1. Structured CSV/BOM import and manufacturer datasheet attachment.
2. Named/linked component acquisition: a user supplies a manufacturer part
   name or a URL to a product page or datasheet instead of typing every
   field by hand. Prometheus fetches the source artifact, retains its exact
   bytes and locator, and runs a bounded extractor (structured-page parser
   first; PDF/table extraction and any machine-assisted, e.g. LLM-based,
   extraction land only as a deliberately small, documented set per the
   deployment plan's "what add any available component should mean"
   section) to propose typed parameter candidates. Every proposed value is a
   `candidate` with its source locator attached — identical in trust
   standing to a manually typed value before review, never auto-published.
   Conflicting or unextractable fields remain visible as unknown rather than
   silently guessed. This path reuses the same draft/review/publish
   pipeline as checkpoint 1; it only changes how the draft's parameters are
   first proposed.
