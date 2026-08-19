# Phase 6 semantic engineering graph status

Phase 6's objective (per
[the deployment plan](prometheus-product-to-deployment-plan.md#phase-6-build-the-first-semantic-engineering-graph))
is to connect the artifacts real mechanical workflows already produce —
CAD assembly/part, BOM row, component and revision, source document and
evidence claim, material, joint and contact, load and restraint,
requirement and scenario, analysis request and finding — as a reviewable
graph, not a fixed schema imposed ahead of real use. Its own rules require
starting only with entities "demonstrated by real projects," so this phase
begins with the one cross-entity relationship the codebase has already
built and proven real, rather than designing a general graph engine ahead
of evidence.

## What already exists, informally

Two entity families and one edge between them are already real, working,
and reviewed by a human at bind time:

- **CAD part**: a persistent entity ID from STEP/XDE import
  (`CadPart::persistentId`).
- **Component revision**: a reviewed, published, hash-identified
  `ComponentRevision` (Phase 5).
- **The edge**: `CAD part --bound_to--> component revision`, hash-verified
  at bind time (Phase 5 checkpoint 2), with live supersession detection
  (checkpoint 3), and a real consuming analysis (checkpoint 4).

Today this edge is *display* state only: `CadPart::componentRevisionId`/
`componentVerified` in memory, and `{cad_entity_id, revision_id, label}` in
the project snapshot. It is not a reviewable, provenanced graph edge — a
rebind silently overwrites the previous one, there is no record of *when*
or *why* a binding changed, and nothing else in the codebase can query "what
is bound to this CAD entity" except the CAD controller itself.

## Checkpoint 1: promote the CAD-to-component binding to a real graph edge

Give the one relationship that already exists a genuine graph-edge
representation: persisted, provenanced, reviewable, and appendable rather
than overwritable — satisfying Phase 6's own rule ("corrections append
review state instead of rewriting source evidence") for the first time,
using the smallest possible entity set (two families, one edge kind)
instead of building a general graph schema speculatively.

### What shipped — reuse turned out to be complete, not partial

The original sketch proposed a new `ComponentBindingEdge` record type
mirroring `PackageBinding`. Investigation found that was unnecessary:
`run_store::install_package_binding` and `ExecutionIndex.package_bindings`
(built for Program 01B's execution-binding flow, flagged in Phase 5
checkpoint 2 as the deferred "second real embedding need") already store
exactly the right shape — `PackageBinding{binding_revision,
supersedes_binding_revision, cad_entity_id, package}` — and already resolve
"the active binding for CAD entity X" as the one whose revision is not in
any other entry's `supersedes_binding_revision` set. That's a real,
multi-entity-aware, append-only graph edge with zero new schema. This
checkpoint wires the CAD-binding panel into that *existing* mechanism
instead of building a second one beside it.

- `ComponentBindingController` gained an optional `ProjectController*`
  (new 3-arg constructor; existing 2-arg forms unaffected). After a
  verified bind's package bytes are hash-checked and the supersession
  metadata check completes, `persistBindingEdge` calls the same
  `install_package_binding` the motor-execution flow already uses —
  embedding the exact verified bytes content-addressed and appending a new
  `PackageBinding` that supersedes whatever was previously bound to that
  entity, from *either* flow. The execution-binding path and the CAD-panel
  path now share one chain per entity, not two.
- The persist step is best-effort and silent by design: it checks only
  read-only accessors (`project()`, `saveAsRequired()`,
  `executionStoreAvailable()`, `hasCadEntityId()`) rather than calling
  `ProjectController::ensureExecutionWritable()`, which would overwrite the
  project's shared error state for an operation the user did not directly
  initiate. No project open, not writable, or an unrecognized CAD entity
  means the edge is simply not persisted — the hash-verified binding
  already established in the session is never discarded over this.
- `executionComponentReference` (the `PackageInspection` →
  `StoredObjectReference` mapping) was extracted into
  `execution_component_variant.{hpp,cpp}` alongside the earlier
  `executionComponentVariant`, so `ExecutionController` and
  `ComponentBindingController` share the one mapping instead of each
  computing it.

### Proof

`desktop/app/tests/project_tests.cpp` (the real-STEP-import, OCCT-enabled
integration test) extends its existing open → bind → save → reopen flow: a
fake HTTP backend (`FakeRevisionServer`) serves real Motor A/B package
fixtures, and after `ExecutionController::acceptExactPackage` has already
anchored one binding for the CAD-imported motor entity, two further
`ComponentBindingController` binds against that *same* entity are proven
to append — not replace — onto the same chain:

- after the first CAD-panel bind, `execution.package_bindings` has grown
  from 1 to 2 entries, and the new entry's `supersedes_binding_revision`
  points at the execution flow's own entry;
- after a second CAD-panel rebind, there are 3 entries with an exact,
  linearly ordered supersede chain (motor A → motor B → motor A again),
  each preserving its own package's exact object hash;
- the live `CadPart` reflects the latest bind as verified with the correct
  package hash throughout.

### Explicitly out of scope for checkpoint 1

- The other eight entity families (BOM row, material, joint/contact,
  load/restraint, requirement/scenario, analysis request/finding) — added
  only when a real workflow needs them, per Phase 6's own rule.
- A general graph query/traversal engine — only "what's the active binding
  for entity X" exists (already used by `ExecutionController`), not
  arbitrary graph queries.
- A desktop UI for binding *history* (viewing prior superseded edges,
  distinct from the already-shipped "current binding" display) — the data
  is persisted and provably queryable, but no panel renders it yet.
- Confidence scoring for inferred edges — this edge is always
  human-confirmed (hash verification), never inferred, so "confidence" is
  trivially "confirmed" for now; inferred-edge confidence is a real design
  question for whichever entity family first needs it.

### Exit gate for this checkpoint — met

- A CAD-to-component binding is a persisted, hash-verified graph edge, not
  transient display state.
- Rebinding appends a new edge and preserves the prior one's record instead
  of discarding it.
- The persisted edges are provably queryable (`run_store::open_read_only` →
  `execution.package_bindings`) — a desktop history *panel* remains future
  work, but the "not just its current label" data is real.
