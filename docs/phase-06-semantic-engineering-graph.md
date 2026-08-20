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

## Checkpoint 3: promote the reviewed structural requirement to a real graph edge

The third entity pair with a real, human-reviewed relationship is a
component's geometry and its reviewed structural requirements — built by
Phase 7 checkpoint 1
([docs/phase-07-requirements-and-planning.md](phase-07-requirements-and-planning.md)),
which gave the structural workflow a real `ReviewedRequirement` model
(quantity, comparator, limit, unit, applicability, criticality, source) but
left it as transient review-draft state, the same gap checkpoints 1 and 2
started from before this one.

### What already exists, informally

- **Geometry ↔ reviewed requirement**: `StructuralController::rebuildPreview`
  compiles a `StructuralSetup`'s `requirements` list into a
  `StructuralRequest` after `validate_setup` confirms every requirement is
  reviewed, provenanced, and — for a supported quantity — has a positive
  limit. Requirements outside CalculiX's coverage compile through
  unchanged as visible uncovered work rather than being dropped.
- Until this checkpoint, the reviewed requirement list was `draft_` display
  state only, exactly like `CadPart::componentRevisionId` before checkpoint
  1 and the array-indexed joint before checkpoint 2: redefining a
  requirement silently overwrote the previous review, with no record of
  when or why a limit changed, and nothing outside `StructuralController`
  could query "what did this geometry's reviewed requirements say."

### What shipped — unlike checkpoint 1, this needed a small new record type

A requirement has no content-addressed byte blob to embed — like a joint,
just inline fields a human reviewed — so this checkpoint adds a new
`RequirementBinding` record (`binding_revision`,
`supersedes_binding_revision`, `geometry_sha256`, `analysis_id`, `quantity`,
`other_quantity_description`, `comparator`, `limit_value`, `unit`,
`applicability`, `criticality`, `source_or_exploratory_rationale`) to
`ExecutionIndex.requirement_bindings`, with parse/validate/serialize support
in `project_v2.cpp` and `run_store::install_requirement_binding` in
`run_store.cpp` — mirroring `install_joint_binding`'s supersession mechanics
exactly, minus the object-store step.

One deliberate design choice, distinct from both prior checkpoints: a
requirement is neither a single-entity relationship like a package binding
nor a symmetric two-entity relationship like a joint. It is identified by
*which geometry* and *which quantity* it reviews, so its supersession chain
is keyed by the triple `(geometry_sha256, quantity,
other_quantity_description)` — documented as `requirement_key` in
`project_v2.cpp` and mirrored in `run_store.cpp`'s `install_requirement_binding`.
The description joins the key only for an uncovered (`other`) requirement,
so two distinct uncovered requirements on the same geometry (e.g. "fatigue
life" and "corrosion resistance") open independent chains instead of
colliding, while re-reviewing the *same* uncovered requirement still
supersedes its own chain.

A second deliberate departure from checkpoints 1 and 2: `reviewSetup` is a
"Validate and preview request" button click that resubmits every reviewed
field every time, not a targeted action confirming one relationship the way
`defineRevoluteJoint` or a bind button is. Appending unconditionally on every
click — as `install_package_binding`/`install_joint_binding` themselves do,
by design, since each of *their* calls is already a deliberate one-relationship
action — would have spammed the graph with a redundant revision per
requirement on every click, even when nothing changed. `persistRequirementBindingEdges`
therefore compares each reviewed requirement against the geometry/quantity
key's currently active binding (a snapshot taken before the loop, since
`ProjectController::acceptProject` replaces the live project state after
each successful install) and only calls `install_requirement_binding` when
the reviewed content actually differs.

- `StructuralController` gained `compiled_requirements_` (the reviewed
  requirement list `rebuildPreview` already compiles, kept alongside
  `compiled_request_`) and a private `persistRequirementBindingEdges`,
  called from `reviewSetup` after `rebuildPreview` succeeds — not from
  `rebuildPreview` itself, which also runs from patch-selection toggles,
  mesh loads, and history restores, none of which are a deliberate
  requirement review.
- The persist step is best-effort and silent, matching
  `persistJointBindingEdge`'s contract exactly: no project open, not
  writable, or no execution store means the edge is simply not persisted —
  the reviewed requirements already established in the session's `draft_`
  are never discarded over this, and no error of the controller's own is
  surfaced. Unlike checkpoints 1 and 2, there is no CAD-entity-id
  recognition check: a requirement is keyed by `geometry_sha256` (the
  analyzed component's content identity), not a live CAD entity in the
  current assembly.
- `structural_archive.cpp`'s three quantity/comparator/criticality label
  switch statements (added in Phase 7 checkpoint 1 for the reviewed-setup
  evidence document) were promoted to canonical `to_string(RequirementQuantity)`
  / `to_string(RequirementComparator)` / `to_string(RequirementCriticality)`
  free functions in `structural_setup.hpp`/`.cpp`, so the archive, the
  controller's QML-facing labels, and the new graph-edge persistence share
  one mapping instead of three independent copies.

### Proof

- `desktop/run_store/tests/project_v2_tests.cpp`'s
  `requirement_binding_revision_graph_is_strict` proves the same
  supersession-chain integrity rules checkpoints 1 and 2 proved — duplicate/
  non-contiguous revisions reject, cross-key supersession rejects, two
  simultaneously unsuperseded bindings on one key reject — plus
  requirement-specific rules: an `other` requirement must carry a
  description and a supported quantity must not, a supported quantity needs
  a positive limit, and two distinct uncovered-requirement descriptions on
  the same geometry open independent chains rather than colliding.
- `desktop/run_store/tests/run_store_transaction_tests.cpp`'s
  `test_requirement_binding_supersession` proves `install_requirement_binding`
  end-to-end: a first bind on (geometry, displacement) appends revision one;
  a tightened limit on the same key supersedes it; a bind on
  (geometry, von_mises_stress) opens its own chain; two `other` binds with
  distinct descriptions each open their own chain without disturbing each
  other.
- `desktop/app/tests/structural_controller_tests.cpp` extends its real
  open→review→run→archive→commit→reopen flow: after reviewing a setup with
  three reviewed requirements (displacement, von Mises, one uncovered), all
  three appear in `execution.requirement_bindings` — read back via
  `run_store::open_read_only`, independent of `StructuralController` — each
  keyed by its own quantity; re-reviewing with only the displacement limit
  changed appends exactly one new edge that supersedes the prior
  displacement binding, while the von Mises and uncovered chains are
  provably undisturbed.

### Explicitly out of scope for checkpoint 3

- The remaining six entity families (BOM row, material, load/restraint,
  scenario as its own entity, analysis request/finding, source
  document/evidence claim beyond what Phase 5 already anchors) — added only
  when a real workflow needs them.
- The motor-arm workflow — its four obligations are always-on by design,
  with no reviewed/human-authored "should this apply" input anywhere in
  that path, unlike structural's `applicability`/`reviewed` fields. Forcing
  it into this shape would be exactly the false-schema problem Phase 6's
  own rules forbid.
- A general graph query/traversal engine, or a query for "what is the
  complete reviewed-requirement history for geometry X" — only the
  per-key active-binding resolution `install_requirement_binding` itself
  performs exists.
- A desktop UI for requirement-binding history — the data is persisted and
  provably queryable, but no panel renders it yet, same as checkpoints 1
  and 2.
- Confidence scoring for inferred edges — this edge is always
  human-reviewed, never inferred.

### Exit gate for this checkpoint — met

- A reviewed structural requirement is a persisted graph edge, keyed by
  which geometry and which quantity it reviews, not transient `draft_`
  display state.
- Re-reviewing a requirement with changed content appends a new edge and
  preserves the prior one's record instead of discarding it; re-reviewing
  with unchanged content does not spam the chain with redundant revisions.
- The persisted edges are provably queryable
  (`run_store::open_read_only` → `execution.requirement_bindings`)
  independent of `StructuralController` — a desktop history panel remains
  future work.

## Checkpoint 4: promote the reviewed structural material to a real graph edge

The fourth entity pair is a component's geometry and its reviewed
structural material — `material`, one of the nine entity families Phase 6
originally named, and, like the requirement before checkpoint 3, already
real, reviewed data (`ReviewedMaterial`) sitting in `draft_` display state
only.

### What already exists, informally

- **Geometry ↔ reviewed material**: `validate_setup` requires a material's
  designation, source SHA-256, applicability, and elastic properties
  (Young's modulus, Poisson ratio) to be reviewed and provenanced before
  `compile_structural_request` will compile a setup at all. Exactly one
  material governs a given analysis at a time — there is no list, unlike
  requirements.
- Until this checkpoint, that reviewed material was `draft_` state only:
  redefining it silently overwrote the previous review, with no record of
  when or why a material changed, and nothing outside `StructuralController`
  could query "what material did this geometry's last review specify."

### What shipped

`MaterialBinding` (`binding_revision`, `supersedes_binding_revision`,
`geometry_sha256`, `analysis_id`, `designation`, `source_sha256`,
`applicability`, `youngs_modulus_pa`, `poisson_ratio`) was added to
`ExecutionIndex.material_bindings`, with `run_store::install_material_binding`
mirroring `install_package_binding`'s supersession mechanics: exactly one
active binding per key, superseded on rebind — but, like `JointBinding` and
`RequirementBinding`, with no object-store step, since a material's fields
are inline, not a content-addressed blob. Unlike the requirement's
composite key, a material's key is the single `geometry_sha256` — there is
only ever one active material per geometry, the same shape as a package
binding's single `cad_entity_id` key.

`StructuralController::persistMaterialBindingEdge`, called from
`reviewSetup` alongside `persistRequirementBindingEdges`, reuses exactly the
same dedup discipline checkpoint 3 established: compare the reviewed
material against the geometry's currently active binding and only call
`install_material_binding` when the content actually differs, so re-clicking
"Validate and preview request" without changing the material does not
append a redundant revision.

### Proof

- `desktop/run_store/tests/project_v2_tests.cpp`'s
  `material_binding_revision_graph_is_strict` proves the same
  supersession-chain integrity rules checkpoints 1–3 proved, plus
  material-specific validation: a non-positive Young's modulus and an
  out-of-range Poisson ratio both reject.
- `desktop/run_store/tests/run_store_transaction_tests.cpp`'s
  `test_material_binding_supersession` proves `install_material_binding`
  end-to-end: a first bind appends revision one; a revised material on the
  same geometry supersedes it; a different geometry opens its own,
  undisturbed chain.
- `desktop/app/tests/structural_controller_tests.cpp` extends its real
  open→review→run→archive→commit→reopen flow: reviewing a setup appends one
  `MaterialBinding`; re-reviewing with an unchanged material does not append
  a second revision; re-reviewing with a changed Young's modulus appends a
  second revision that supersedes the first — all read back via
  `run_store::open_read_only`, independent of `StructuralController`.

### Explicitly out of scope for checkpoint 4

- Load and restraint — the remaining structural setup fields with real
  reviewed data, deferred because their `BoundarySelection` geometry payload
  (face/node identities, area) is materially more complex to serialize as
  provenance than material's scalar fields; a real future checkpoint, not
  ruled out.
- The remaining entity families (BOM row, scenario as its own entity,
  analysis request/finding, source document/evidence claim beyond what
  Phase 5 already anchors) — added only when a real workflow needs them.
- The motor-arm workflow — unchanged for the same reason as checkpoint 3.
- A desktop UI for material-binding history — the data is persisted and
  provably queryable, but no panel renders it yet, same as checkpoints 1–3.

### Exit gate for this checkpoint — met

- A reviewed structural material is a persisted graph edge, keyed by
  geometry, not transient `draft_` display state.
- Re-reviewing a material with changed content appends a new edge and
  preserves the prior one's record; re-reviewing with unchanged content
  does not spam the chain with redundant revisions.
- The persisted edges are provably queryable
  (`run_store::open_read_only` → `execution.material_bindings`) independent
  of `StructuralController` — a desktop history panel remains future work.

## Checkpoint 5: promote the reviewed load and restraint selections to real graph edges

The fifth and sixth entity pairs — `load and restraint`, the remaining
named Phase 6 entity family this checkpoint closes — are a component's
geometry and its reviewed surface load and fixed-restraint selections.
Checkpoint 4 deferred these deliberately, not because they were less real
than material, but because their `BoundarySelection` payload (exact face
and node topology, not just a scalar) is materially more to serialize as
provenance. This checkpoint does that work.

### What already exists, informally

- **Geometry ↔ reviewed load/restraint**: `validate_setup` requires each
  selection's `BoundarySelection` to resolve to the mesh's exact boundary
  topology (`valid_selection` in `structural_setup.cpp`) and to be reviewed
  before compilation proceeds — exactly the same reviewed-and-provenanced
  bar checkpoints 3 and 4 already promoted. Exactly one load selection and
  one restraint selection govern a given analysis at a time, the same
  single-key shape as material.
- Until this checkpoint, both were `draft_` state only.

### What shipped

`LoadBinding` and `RestraintBinding` were added to `ExecutionIndex`, both
keyed by `geometry_sha256` alone like `MaterialBinding`. Each carries
`selection_label`, `face_node_ids` (the exact triangles a visual patch
selection resolved to — `prometheus::structural::BoundarySelection`'s
durable topology, not a transient patch id), `node_ids`, and `area_m2`;
`LoadBinding` additionally carries the reviewed total force vector.
`install_load_binding`/`install_restraint_binding` mirror
`install_material_binding`'s single-key supersession mechanics exactly.
New bounds (`maximum_selection_faces`/`maximum_selection_nodes = 200000`)
cap array size independent of the overall 8 MB project-file limit, since a
fine mesh selection can be large.

`StructuralController::persistLoadBindingEdge`/`persistRestraintBindingEdge`,
called from `reviewSetup` alongside the material/requirement persistence,
reuse the same dedup discipline: compare the reviewed selection (including
its exact face/node arrays, via `std::vector`/`std::array` equality)
against the geometry's currently active binding and only append when the
content actually differs.

### Proof

- `desktop/run_store/tests/project_v2_tests.cpp`'s
  `surface_selection_binding_graphs_are_strict` runs the same
  supersession-chain integrity rules checkpoints 1–4 proved against both
  `LoadBindingV1` and `RestraintBindingV1` in one parametrized pass, plus
  selection-specific validation: an empty face list, a malformed
  (non-triangular) face, and a non-positive area all reject.
- `desktop/run_store/tests/run_store_transaction_tests.cpp`'s
  `test_surface_selection_binding_supersession` proves
  `install_load_binding`/`install_restraint_binding` end-to-end, including
  that a geometry's load and restraint chains are independent of each other.
- `desktop/app/tests/structural_controller_tests.cpp` extends its real
  open→review→run→archive→commit→reopen flow: reviewing a setup appends one
  `LoadBinding` and one `RestraintBinding`; re-reviewing with an unchanged
  selection does not append redundant revisions; re-reviewing with a
  changed load force appends a second `LoadBinding` revision without
  disturbing the restraint chain — all read back via
  `run_store::open_read_only`, independent of `StructuralController`.

### Explicitly out of scope for checkpoint 5

- The remaining entity families (BOM row, scenario as its own entity,
  analysis request/finding, source document/evidence claim beyond what
  Phase 5 already anchors) — added only when a real workflow needs them.
  With this checkpoint, every structural setup field with real reviewed
  data (material, load, restraint, requirement) is now a persisted graph
  edge; mesh controls and scenario description remain `draft_` state, not
  yet demonstrated as needing graph-edge provenance the way limits and
  selections did.
- The motor-arm workflow — unchanged for the same reason as checkpoints 3
  and 4.
- A desktop UI for load/restraint-binding history — the data is persisted
  and provably queryable, but no panel renders it yet, same as checkpoints
  1–4.

### Exit gate for this checkpoint — met

- A reviewed load and restraint selection are each a persisted graph edge,
  keyed by geometry, carrying their exact durable boundary topology, not
  transient `draft_` display state.
- Re-reviewing either with changed content appends a new edge; re-reviewing
  with unchanged content does not spam the chain with redundant revisions.
- The persisted edges are provably queryable
  (`run_store::open_read_only` → `execution.load_bindings` /
  `execution.restraint_bindings`) independent of `StructuralController` —
  a desktop history panel remains future work.

## Checkpoint 6: make reviewed-input history actually reviewable in the desktop app

Checkpoints 1 through 5 each closed their own exit gate on "provably
queryable" — via `run_store::open_read_only`, from a test or the CLI — and
each explicitly left "a desktop history panel" as future work. Phase 6's
own exit gate says every consequential edge must be "reviewable," not just
queryable by a program. This checkpoint is the first to close that gap: it
adds no new graph-edge type, only a real view onto the five edge kinds that
already exist.

### What shipped

- `StructuralController::rebuildReviewedInputHistory` reads
  `execution.{requirement,material,load,restraint}_bindings` for the
  currently reviewed geometry, groups by kind, and marks each revision
  `active` (not superseded) or not — using the same superseded-revision-set
  computation `persist*BindingEdge` already uses, so "what the panel shows"
  and "what the persist functions treat as current" can't drift apart.
  Exposed as a new `reviewedInputHistory` QML property. Joint and package
  bindings are CAD-entity-keyed, not geometry-keyed, and are deliberately
  out of scope here — a real future extension to the CAD panel, not folded
  in speculatively.
- Rebuilt on every point the project's execution index can change under
  this controller: `reloadProject` (project opened/saved), after
  `reviewSetup`'s persist calls, and after `restoreStoredRun` sets `draft_`
  — so the panel reflects a reopened project's history, not just the live
  session's.
- `StructuralSetupPanel.qml` gained a "REVIEWED INPUT HISTORY" list,
  dimmed for superseded revisions, showing each entry's kind, revision
  number, and a kind-specific human-readable summary (e.g.
  "displacement ≤ 0.0010 m", "6061-T6 aluminum • E=6.9e10 Pa • ν=0.33").

### Proof

`desktop/app/tests/structural_controller_tests.cpp` extends its real
open→review→run→archive→commit→reopen flow: after the first review,
`reviewedInputHistory` holds exactly the 6 freshly reviewed edges (3
requirements + material + load + restraint), all active; after a session of
displacement, load, and material edits followed by reverting to the
original draft, it holds exactly the 12 distinct revisions those edits
produced (5 requirement + 3 material + 3 load + 1 restraint), with exactly
6 marked active — one per live chain, proving dedup and supersession are
both reflected correctly, not just recorded. After project close and
reopen, the restored controller's `reviewedInputHistory` still holds all
12 entries, proving the panel is populated from the persisted project, not
session-local state.

### Explicitly out of scope for checkpoint 6

- CAD-binding and joint history (package/joint bindings) — CAD-entity-keyed,
  not geometry-keyed; a real future extension to the CAD panel, not this
  checkpoint's structural-setup panel.
- Filtering, search, or pagination for the history list — no real project
  has produced enough revisions yet to need it.
- A "revert to this revision" action — the data is reviewable; making a
  past revision actionable again is a separate, real feature question.

### Exit gate for this checkpoint — met

- Every one of checkpoints 3–5's persisted graph edges is visible in the
  desktop app for the geometry currently under review, not just queryable
  from a test or the CLI.
- The panel distinguishes active from superseded revisions using the exact
  same logic the persist functions use, so the two can't disagree.
- The panel reflects a reopened project's persisted history, not just the
  current session's edits.
