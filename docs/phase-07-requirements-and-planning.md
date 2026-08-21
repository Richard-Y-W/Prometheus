# Phase 7 requirements and planning status

Phase 7's objective (per
[the deployment plan](prometheus-product-to-deployment-plan.md#phase-7-add-requirements-scenarios-and-thin-planning))
is to make analyses answer explicit engineering questions: a reviewed
requirement form with quantity, comparator, limit, unit, applicability,
criticality, scenario, and source; compiling supported requirements into
proof obligations; matching obligations to available validated capabilities;
showing required missing inputs; recommending an analysis only when its
applicability is explainable; requiring confirmation before execution;
preserving unsupported requirements as visible uncovered work; and showing
why each finding satisfies, violates, or cannot answer its obligation.

Phase 6 is not formally closed (its own header currently reads "checkpoint 7
landed," not "exit gate met" — see
[docs/phase-06-semantic-engineering-graph.md](phase-06-semantic-engineering-graph.md)).
Per `docs/program/00-master-roadmap.md`, these phases are not strict
sequential gates. Unlike when Checkpoint 1 below was first written, this is
no longer forward-looking: [Phase 6 checkpoint
3](phase-06-semantic-engineering-graph.md#checkpoint-3-promote-the-reviewed-structural-requirement-to-a-real-graph-edge)
already promoted the reviewed structural requirement to a real, persisted
`RequirementBinding` graph edge, and later Phase 6 checkpoints did the same
for reviewed material and reviewed load/restraint selections — Phase 7 work
motivated exactly the graph-edge promotions it predicted. Analysis
request/finding graph edges remain future work.

## What already exists, informally

Before this checkpoint, only one real, tested, human-reviewed pipeline
existed that resembled any part of Phase 7's ambition: the structural
(CalculiX linear-static) workflow. It already gated execution on a reviewed
displacement and/or von Mises stress limit
(`ReviewedStructuralRequirement` — two hardcoded `optional<double>` fields, a
rationale string, and a `reviewed` bool), compiled that into a
`StructuralRequest`, and turned a completed solver run's metrics into
pass/fail `StructuralFinding` records. That was a real but narrow slice: no
comparator field (implicitly "≤"), no unit field (baked into the field
name), no applicability or criticality field, and no way to record a
requirement the capability couldn't evaluate — it either fit the two
hardcoded slots or it did not exist. A second real piece,
`decision::summarize` (`desktop/core/include/prometheus/decision/project_summary.hpp`),
already computed a verdict/coverage rollup from typed obligation counts, but
sat completely unwired from any real evaluation pipeline. The motor-arm
workflow has its own, separately-tested fixed-obligation pattern
(`finding_compiler.cpp`, four hardcoded obligation IDs) and is untouched by
this checkpoint. No capability registry or applicability-matching engine
exists anywhere in the codebase; analysis selection is hardcoded to whichever
single backend a workflow already uses.

## Checkpoint 1: real reviewed requirements for the structural workflow

Turn the structural workflow's narrow, ad hoc requirement into the real
thing Phase 7 asks for, and wire the existing-but-unused coverage/verdict
aggregator into it — using the smallest possible scope (one subsystem, its
existing two supported quantities) instead of building a capability registry
speculatively ahead of a second capability that would need one.

### What shipped

- `ReviewedStructuralRequirement` (two hardcoded optional limits) was
  replaced by `std::vector<ReviewedRequirement>` on `StructuralSetup`. Each
  `ReviewedRequirement` carries `quantity` (`displacement`,
  `von_mises_stress`, or `other`), `comparator` (`less_or_equal` — the only
  value populated today, but now an explicit field, not an implicit
  convention), `limit_value`, `unit`, `applicability`, `criticality`
  (`informational`/`advisory`/`critical`), `source_or_exploratory_rationale`,
  and `reviewed`. A requirement whose `quantity` is `other` records a real
  reviewed requirement the CalculiX linear-static capability cannot
  evaluate — e.g. fatigue life — with an explicit
  `other_quantity_description`, instead of that requirement being
  unrepresentable.
- `validate_setup` (`structural_setup.cpp`) validates the list: every
  requirement needs review, provenance, and a unit; a supported quantity
  needs a positive limit; at most one requirement per supported quantity is
  allowed (`requirement_duplicate_quantity` — ambiguous which one would
  govern the solver limit); and at least one positive, supported requirement
  must exist, or compilation is blocked (`requirement_unsupported_only`) even
  when the list is non-empty. An `other` requirement never blocks validation
  by itself — this is the literal "preserve unsupported requirements as
  visible uncovered work" exit-gate line.
- `compile_structural_request` scans the list for the two supported
  quantities and compiles them into `StructuralRequest`'s existing
  `displacement_limit_m`/`von_mises_limit_pa` fields exactly as before — the
  solver-facing contract is unchanged; only how a human enters and reviews
  the source requirement changed.
- `StructuralFindingDisposition` gained `cannot_answer`, produced by
  `compile_structural_findings` for every declared obligation when the
  solver run did not complete. Previously a failed run left `findings` empty
  while `declared_obligations` stayed set — the obligation silently
  disappeared rather than resolving to an explicit state. Now every declared
  obligation always resolves to exactly one of satisfied, violated, or
  cannot-answer.
- `StructuralController` (`desktop/app/structural_controller.cpp`) builds the
  requirement list from the review form, exposes unsupported requirements as
  a new `uncoveredRequirements` QML property (persisted through save/reopen
  via the reviewed-setup-evidence document, not the cryptographically
  replayed run archive — see below), and computes a `decision::ProjectSummary`
  from each completed run's findings plus its uncovered-requirement count,
  exposed as `lastRun.assessment` (`verdict`/`coverage`/`execution_state`).
  Using `execution_state: completed_with_blocked_work` when uncovered
  requirements exist is deliberate: `decision::summarize`'s own rule is that
  verdict can only reach `satisfied_within_scope` when `execution_state ==
  completed`, so a run with real, reviewed, uncovered requirements can never
  be reported as simply "satisfied" — it stays `indeterminate` until that
  work is addressed, even when every capability-supported obligation passed.
- `StructuralSetupPanel.qml` gained applicability/criticality fields for the
  reviewed requirement set and one additional "other requirement" entry
  (description/unit/limit), plus panels showing uncovered requirements and
  the assessment rollup, and a `cannot_answer` finding style distinct from
  violated/satisfied.
- The reviewed-setup-evidence document's schema (embedded, hashed,
  independently verified — `serialize_structural_setup_evidence`) bumped from
  `reviewed-structural-setup:1.0.0` to `:1.1.0`: its `requirement` object key
  became a `requirements` array. This is a deliberate, visible breaking
  change to that document's shape, consistent with how this codebase has
  always handled contract changes (no silent dual-support). The run
  archive's own cryptographically replayed contract
  (`structural-run-archive:1.0.0` — `artifacts`/`metrics`/`coverage`/
  `findings`) is unchanged: it never needed to change, because
  `cannot_answer` can only be produced for a non-completed run and
  `write_structural_archive` has always refused to archive anything but a
  completed one, and because the solver-facing `requirements` mirror
  (`displacement_limit_m`/`von_mises_limit_pa`) was never touched by this
  checkpoint.

### Proof

- `desktop/structural/tests/structural_tests.cpp` extends the reviewed-setup
  fixture with real `ReviewedRequirement` entries; adds cases for an
  uncovered `other` requirement that does not block a supported setup, a
  missing description on an `other` requirement staying blocked
  (`requirement_description_missing`), a duplicate supported quantity being
  rejected (`requirement_duplicate_quantity`), and a setup with only
  unsupported requirements failing closed
  (`requirement_unsupported_only`); and rewrites the prior "failed execution
  produces no findings" assertion into "failed execution resolves each
  declared obligation to an explicit cannot-answer finding."
- `desktop/app/tests/structural_controller_tests.cpp` extends its real
  open→review→run→archive→commit→reopen flow with an uncovered "fatigue
  life" requirement: after a completed run, `uncoveredRequirements` holds it
  with `criticality: critical`, and `lastRun.assessment` reports
  `verdict: indeterminate`, `coverage: sufficient`,
  `execution_state: completed_with_blocked_work` — proving the verdict
  refuses to claim satisfaction while real uncovered work remains, even
  though both capability-supported obligations passed. After project close
  and reopen, the restored controller's `uncoveredRequirements` still holds
  the identical entry, proving the uncovered requirement round-trips through
  the immutable project store exactly, not just within one session.
- The full native suite (25/25 `ctest` targets, including the real-STEP/OCCT
  `prometheus_project_tests` integration suite) passes with these changes.

### Explicitly out of scope for checkpoint 1

- A capability registry or applicability-matching engine — only one
  capability (CalculiX linear-static) exists anywhere in the codebase;
  "matching" reduces honestly to "is this quantity kind one CalculiX
  supports," implemented as a fixed check, not a general registry that would
  have no second capability to prove itself against.
- Any change to the motor-arm subsystem (`finding_compiler.cpp`,
  `execution_controller.cpp`) — it has its own working, separately-tested
  fixed-obligation pattern; nothing here demonstrates it needs to change.
- Per-requirement scenarios, or a shared `Scenario` type between structural
  and motor-arm — each subsystem keeps its own scenario notion (structural:
  `scenario_description`/`scenario_confirmed`; motor-arm:
  `MotorArmScenario`) until a real workflow needs them unified.
- New Phase 6 graph entities (analysis request/finding; scenario as its own
  entity) — the requirement/scenario entity family itself is no longer out
  of scope: [Phase 6 checkpoint 3](phase-06-semantic-engineering-graph.md#checkpoint-3-promote-the-reviewed-structural-requirement-to-a-real-graph-edge)
  promoted the reviewed structural requirement built here to a real,
  persisted, append-only `RequirementBinding` graph edge, keyed by geometry
  and quantity, closing the requirement-history gap noted below. Analysis
  request/finding remains future work.

### Exit gate for this checkpoint — partially met

- A reviewed requirement now carries quantity, comparator, limit, unit,
  applicability, criticality, and source — met, for the structural workflow.
- Requirements outside the capability's coverage are preserved as visible,
  reviewable uncovered work rather than being unrepresentable or silently
  dropped — met.
- Every declared obligation resolves to an explicit satisfied, violated, or
  cannot-answer finding, and a coverage/verdict rollup explains why the
  overall run cannot claim satisfaction while uncovered work remains — met.
- Confirmation before execution — met, unchanged from the existing
  `reviewed`/`scenario_confirmed` gates this checkpoint builds on.
- "Recommend an analysis only when its applicability is explainable" and
  "match obligations to available validated capabilities" — not really
  exercised yet, because only one capability exists to match against; this
  checkpoint's "matching" is a fixed check, not evidence that general
  capability matching works. This needs a second structural (or other
  domain) capability before it can honestly be called met.
- Scope stayed within the structural subsystem; the motor-arm workflow's
  fixed-obligation pattern is untouched and does not yet share this shape.

## Checkpoint 2: a second structural capability (modal/frequency)

Checkpoint 1 left "recommend an analysis only when its applicability is
explainable" and "match obligations to available validated capabilities"
formally unmet, because only one capability (CalculiX linear-static)
existed anywhere in the codebase to match against. This checkpoint adds a
real second capability — CalculiX modal/frequency (eigenvalue) analysis —
so capability matching is now a real decision between two genuinely
different solver workflows, not a single fixed check with nothing to
distinguish it from.

Between Checkpoint 1 and this checkpoint, an unrelated, much larger parallel
effort rewrote the structural evaluation pipeline around mandatory paired
coarse/fine mesh-convergence refinement for the existing linear-static
capability (`VerifiedStructuralRefinement`, `structural_refinement.cpp`,
`reviewed_pair.cpp`). Direct review of that new architecture (mesh
convergence is built specifically around a loaded, spatially-varying
displacement/stress field reduced over node/element regions) showed a modal
eigenvalue extraction — no applied load, no per-node/per-element field to
reduce or compare between two mesh densities — has no natural fit through
that same comparison machinery. Modal/frequency is therefore implemented as
a parallel, single-run capability that bypasses the coarse/fine refinement
pipeline entirely, the same way the archive format already coexists
multiple `archive_kind` shapes (v1 through v4) behind one verification
entry point rather than forcing every shape through one contract.

### What shipped

- A new `StructuralCapability{linear_static, modal_frequency}` enum
  (`types.hpp`) and a `recommend_capability(requirements)` resolver
  (`structural_setup.cpp`, file-local) that reads the reviewed requirement
  list and resolves to `linear_static` (a displacement/von Mises
  requirement, no frequency one), `modal_frequency` (the mirror case), or
  flags `requirement_capability_ambiguous` when a setup reviews requirements
  for both at once — this is the real "matching obligations to available
  validated capabilities" the Checkpoint 1 exit gate was waiting on a second
  capability to exercise.
- `RequirementQuantity` gained `natural_frequency` and `RequirementComparator`
  gained `greater_or_equal` (a frequency floor, not a ceiling like
  displacement/stress). `ReviewedMaterial` gained an optional
  `density_kg_m3`, required and positive only when the resolved capability
  is `modal_frequency` (`material_density_missing`/
  `material_density_invalid`). When the resolved capability is
  `modal_frequency`, load review is not required — a modal deck has no
  `*CLOAD` — while restraint review stays required for both capabilities.
- `StructuralRequest` gained `capability`, `density_kg_m3`, and
  `minimum_natural_frequency_hz`; its schema bumped to
  `calculix-structural-request:0.2.0`, and the reviewed-setup evidence
  schema bumped from `2.1.0` to `2.2.0` — both deliberate, visible breaking
  changes, not silent dual-support, consistent with how every prior contract
  change in this codebase has been handled. `2.0.0`/`2.1.0` remain
  read-only replay targets so previously committed evidence keeps
  reproducing its exact original bytes.
- `calculix_deck.cpp` emits `*DENSITY` and a `*STEP`/`*FREQUENCY,
  SOLVER=SPOOLES` step (no `*CLOAD`) for a modal request instead of the
  linear-static `*STATIC` step. `SOLVER=SPOOLES` is a real, empirically
  necessary choice: the default PaStiX solver failed on a small system
  during verification against the real `ccx` binary.
- `calculix_result.cpp` gained an eigenvalue-table parser
  (`parse_calculix_dat`'s `E I G E N V A L U E` section, guarded against the
  following participation-factor table's rows also matching the same "an
  integer then four doubles" shape) and a capability-aware completeness
  check: a `*FREQUENCY` step's `.sta` file was verified empirically (by
  directly invoking `ccx` on a hand-written modal deck) to contain only
  header lines with no data rows, so `CompiledCalculixResult::complete()`
  accepts a populated `first_natural_frequency_hz` as an alternative to the
  linear-static convergence-evidence requirement, rather than requiring
  both.
- A new, additive findings compiler,
  `compile_modal_structural_findings(CompiledStructuralSetup, SolverRunResult)`
  (`structural_findings.hpp`/`.cpp`), reuses the existing
  `StructuralFinding`/`StructuralEvaluation`/`StructuralUnevaluatedObligation`
  types with `≥` limit semantics. The existing
  `compile_structural_findings(VerifiedStructuralRefinement)` is untouched —
  modal findings never go through it.
- `StructuralBackend` gained `executeModalRun`, implemented in the real
  backend (`structural_backend.cpp`: runs CalculiX, compiles modal findings,
  writes the modal archive) and in the test fake
  (`structural_controller_tests.cpp`'s `CountingStructuralBackend`,
  delegating like its other methods).
- A fifth, structurally simpler archive shape,
  `write_modal_structural_archive`/`verify_modal_archive`
  (`structural_archive.cpp`, schema
  `structural-run-archive-modal:1.0.0`) — single-run, no coarse/fine
  samples, no refinement criterion, no boundary correspondence — registered
  alongside the existing v1 through v4 shapes behind the same
  `verify_structural_archive` dispatcher and the same `run_store`
  `committed_manifest` reference whitelist.
- `MaterialBinding`/`MaterialBindingInput` gained `density_kg_m3`;
  `RequirementBinding`'s quantity/comparator whitelist gained
  `natural_frequency`/`greater_or_equal` — the same append-only graph-edge
  mechanism Phase 6 checkpoints 3–5 established, extended rather than
  duplicated.
- `StructuralController` gained a capability-aware branch
  (`draft_resolves_modal_capability`, mirrored in QML as `modalDraft`) that
  skips building a `StructuralRefinementCriterion` and the coarse/fine
  review lifecycle for a modal review, a parallel `runModalAnalysis`
  execution path and `completed_modal_run_` state, and restore-path support
  for reopening a committed modal archive (`restoreStoredRun`'s
  `modalRestore` branch — a modal archive legitimately never reviewed a load
  selection, which is not a restoration failure).
- `StructuralSetupPanel.qml` gained density and minimum-frequency input
  fields, and a `modalActive` capability signal that hides the load-force
  inputs, the "load selection reviewed" checkbox, and the coarse/fine
  refinement-criterion field for a modal review, replacing the run button's
  "coarse baseline"/"fine comparison" labeling with "run modal analysis" and
  showing the measured first natural frequency instead of
  displacement/stress in the run summary.
- `cantilever_modal_benchmark` (`structural_benchmarks.cpp`/`.hpp`) — a 1 m
  x 0.1 m x 0.1 m cantilever, no applied load, compared against the
  closed-form first Euler-Bernoulli cantilever bending mode
  (`f1 = (β1 L)² / (2π L²) · √(E I / (ρ A))`, `β1 L = 1.875104`) — and a
  `cantilever-modal` case in the `prometheus_run_structural_benchmark` tool,
  mirroring the existing `axial`/`cantilever` real-solver benchmark cases.

### Proof

- Real `ccx` invocation (not a synthetic fixture) via
  `prometheus_run_structural_benchmark ccx.exe <output> cantilever-modal`
  measured a first natural frequency of 93.109460 Hz against a closed-form
  expectation of 81.538065 Hz — 14.2% relative error, within the
  benchmark's 15% tolerance (a Euler-Bernoulli slender-beam approximation
  against a modest 24×4×4 solid-element mesh is expected to carry
  double-digit error; the linear-static cantilever benchmark's own
  tolerance is comparably wide, 15%/25%, for the same reason). The run
  archived cleanly under the new `structural-run-archive-modal:1.0.0`
  schema and its manifest independently re-verified.
- `desktop/structural/tests/structural_tests.cpp` extends the existing
  fixture-driven flow with: a compiled modal request declaring
  `capability=modal_frequency` with a reviewed density and frequency limit
  and no load requirement; a fully reviewed modal setup validating cleanly;
  a modal deck asserted to contain `*DENSITY` and `*FREQUENCY,
  SOLVER=SPOOLES` and to omit `*CLOAD`; `requirement_capability_ambiguous`
  raised when both a displacement and a frequency requirement are reviewed
  together; `material_density_missing` raised when a modal setup's density
  is unreviewed; a fixture-driven modal solver run (the shared
  `solver_fixture.cpp` test double gained a canned eigenvalue-table branch
  for job names containing "modal") that completes on its eigenvalue row
  alone with no convergence evidence required; and
  `compile_modal_structural_findings` exercised across all three
  dispositions — satisfied when the measured frequency is at or above its
  reviewed limit, violated when it is below, and `cannot_answer` when a run
  completes without a measured frequency.
- `desktop/app/tests/structural_controller_tests.cpp`'s
  `CountingStructuralBackend` gained `executeModalRun`; the full suite
  (including the real-STEP/OCCT `prometheus_project_tests` integration
  suite) passes with these changes.
- All 35 `ctest` targets pass.

### Explicitly out of scope for checkpoint 2

- Routing modal/frequency through mesh-convergence refinement at all —
  physically questionable to force an eigenvalue extraction through the
  same displacement/stress change-fraction machinery built for a loaded
  field; a future checkpoint could add eigenvalue-specific convergence
  (e.g. comparing the first few modes across mesh densities) if a real
  workflow needs it.
- Running both capabilities from one reviewed setup or one click — a
  reviewed setup still resolves to exactly one capability per review.
- Any change to `reviewed_pair.cpp`/`structural_refinement.cpp`/
  `structural_observables.cpp` or the motor-arm workflow — the parallel
  coarse/fine refinement architecture for linear-static is untouched.
- A general capability registry — still exactly two capabilities, resolved
  by one file-local function; a registry remains premature until a third
  capability exists to prove it against.

### Exit gate for this checkpoint — partially met

- "Match obligations to available validated capabilities" is now a real
  decision between two capabilities, not a single fixed check with nothing
  to distinguish it from — met, closing the gap Checkpoint 1 left open.
- "Recommend an analysis only when its applicability is explainable" —
  partially met: `recommend_capability` explains *which* capability a
  reviewed requirement set resolves to and flags ambiguity, but there is
  still no general applicability-matching engine (e.g. checking a
  capability's stated regime against project context) — the same registry
  gap noted as out of scope above.
- Every declared frequency obligation resolves to an explicit satisfied,
  violated, or cannot-answer finding, matching the same rule Checkpoint 1
  established for displacement/von Mises — met.
- Reproducibility and replay: the modal archive schema, the `2.2.0`
  reviewed-setup evidence schema, and the extended `RequirementBinding`/
  `MaterialBinding` graph edges all independently re-verify, and every
  pre-existing `2.0.0`/`2.1.0` archive continues reproducing its exact
  original bytes — met.
- Scope stayed within the structural subsystem; the motor-arm workflow and
  the parallel linear-static coarse/fine refinement pipeline are both
  untouched.
