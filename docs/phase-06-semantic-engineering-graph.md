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

## Checkpoint 2: promote the confirmed revolute joint to a real graph edge

The second entity pair with a real, human-reviewed relationship already
built and proven is CAD part ↔ CAD part, connected by a confirmed revolute
joint.

### What already exists, informally

- **CAD part ↔ CAD part**: `EngineeringController::defineRevoluteJoint`
  records a single, human-confirmed (`confirmed_by_user: true`) joint
  between two CAD parts, and `runGeometryChecks`'s `sampled_joint_sweep`
  finding is a real, tested downstream consumer of it (exercised in
  `desktop/app/tests/project_tests.cpp`'s real-STEP integration test and in
  `desktop/app/tests/engineering_controller_tests.cpp`).
- Until this checkpoint, the joint was keyed by `source_index`/`target_index`
  — array indices into `cadController.parts`, not the stable
  `CadPart::persistentId` identity the CAD-to-component edge already uses —
  and stored in `run_store::EngineeringState.joint`, a single overwritable
  slot exactly like `CadPart::componentRevisionId` was before checkpoint 1.
  Redefining a joint silently replaced the old one, with no record of
  when or why it changed, and nothing outside `EngineeringController` could
  query "what joint, if any, connects entity X and Y."

### What shipped — unlike checkpoint 1, this genuinely needed a small new record type

Checkpoint 1 found that Program 01B's `PackageBinding`/`package_bindings`
already existed for a different reason and could be reused unchanged. A
joint has no analogous existing storage: it has no content-addressed byte
blob to embed (no downloaded package, no hash-verified artifact — just
inline parameters a human confirmed), so `install_package_binding`'s
object-store step does not apply, and no other append-only, provenanced
record in the codebase shares a joint's shape. This checkpoint is honest
about that: it adds a new `JointBinding` record
(`binding_revision`, `supersedes_binding_revision`,
`source_cad_entity_id`, `target_cad_entity_id`, `type`, `axis`,
`minimum_deg`, `maximum_deg`, `pivot_x`, `pivot_y`, `pivot_z`) to
`ExecutionIndex.joint_bindings`, with its own parse/validate/serialize
support in `project_v2.cpp` and its own `run_store::install_joint_binding`
in `run_store.cpp` — mirroring `install_package_binding`'s supersession
mechanics exactly, minus the object-store step, since the edge is
self-contained and inline.

One deliberate design choice: a package binding's supersession chain is
keyed by a single `cad_entity_id`, because a package is bound *to* one
entity. A joint connects *two* entities symmetrically, so its chain is keyed
by the **unordered pair** of `source_cad_entity_id`/`target_cad_entity_id` —
rebinding the same physical joint with source and target swapped supersedes
the same chain rather than opening a second, parallel one. This is
documented as a comment at both the validation site
(`project_v2.cpp`'s `joint_pair_key`) and the install site
(`run_store.cpp`'s `install_joint_binding`).

- `EngineeringController::defineRevoluteJoint` gained two additive
  `QString` parameters, `sourceEntityId`/`targetEntityId`, alongside its
  existing `source`/`target` array-index parameters — the indices remain
  the identity the QML rendering and sweep wiring already key on; the
  entity ids are additive, for graph-edge persistence only. `Main.qml`'s two
  call sites now also pass `cadController.parts[...].persistentId`.
  `EngineeringController` gained a `setProjectController(ProjectController*)`
  setter (main.cpp constructs `EngineeringController` before
  `ProjectController`, so constructor injection — checkpoint 1's approach
  for `ComponentBindingController` — was not directly available in the same
  order) and a private `persistJointBindingEdge`, called after `joint_` is
  set with `confirmed_by_user: true`.
- The persist step is best-effort and silent, matching
  `ComponentBindingController::persistBindingEdge`'s contract exactly: no
  project open, not writable, no execution store, an empty or unrecognized
  CAD entity id on either side — the confirmed joint already established in
  the session's `joint_` display state is never discarded over this, and no
  error of the controller's own is surfaced.

### Proof

- `desktop/run_store/tests/project_v2_tests.cpp`'s
  `joint_binding_revision_graph_is_strict` proves the same supersession-chain
  integrity rules checkpoint 1 proved for package bindings — duplicate/
  non-contiguous revisions reject, cross-pair supersession rejects, two
  simultaneously unsuperseded bindings on one pair reject, self-joints
  reject — plus the pair-symmetry rule: a bind with source and target
  swapped still supersedes the prior chain for the same unordered pair.
- `desktop/run_store/tests/run_store_transaction_tests.cpp`'s
  `test_joint_binding_supersession` proves `install_joint_binding`
  end-to-end: a first bind on (arm, base) appends revision one with no
  supersession; a second bind on the same pair appends revision two,
  superseding the first; a third bind with source/target swapped still
  supersedes revision two; a bind on an unrelated pair (arm, driver) opens
  its own chain without disturbing the arm/base chain.
- `desktop/app/tests/project_tests.cpp` extends its real-STEP,
  open → bind → save → reopen integration flow: after wiring a real
  `EngineeringController` to a real `ProjectController` via
  `setProjectController`, a confirmed `defineRevoluteJoint` call between the
  imported motor and arm CAD entities appends one `execution.joint_bindings`
  entry with no supersession, and a second confirmed call on the same two
  entities appends a second entry that supersedes the first — read back via
  `run_store::open_read_only`, independent of `EngineeringController`.

### Explicitly out of scope for checkpoint 2

- The remaining seven entity families (BOM row, material, load/restraint,
  requirement/scenario, analysis request/finding, source document/evidence
  claim beyond what Phase 5 already anchors) — added only when a real
  workflow needs them.
- A general graph query/traversal engine, or a query for "what joint
  connects entity X to any other entity" — only the per-pair active-binding
  resolution `install_joint_binding` itself performs exists.
- A desktop UI for joint-binding history — the data is persisted and
  provably queryable, but no panel renders it yet, same as checkpoint 1.
- Confidence scoring for inferred edges — this edge is always
  human-confirmed, never inferred.

### Exit gate for this checkpoint — met

- A confirmed CAD-to-CAD joint is a persisted graph edge, keyed by stable
  `CadPart::persistentId` identity, not transient display state keyed by
  array index.
- Redefining a joint between the same two entities appends a new edge and
  preserves the prior one's record instead of discarding it.
- The persisted edges are provably queryable
  (`run_store::open_read_only` → `execution.joint_bindings`) independent of
  `EngineeringController` — a desktop history panel remains future work.
