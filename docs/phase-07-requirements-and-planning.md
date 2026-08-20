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

Phase 6 is not formally closed (its own header still reads "checkpoint 2
landed," not "exit gate met" — see
[docs/phase-06-semantic-engineering-graph.md](phase-06-semantic-engineering-graph.md)).
Per `docs/program/00-master-roadmap.md`, these phases are not strict
sequential gates, and Phase 7 work is exactly what will motivate the next
Phase 6 checkpoint (requirement/scenario and analysis-request/finding graph
edges) once it exists as something real to promote — not a violation of
Phase 6's own rule to add entities only when a real workflow needs them.

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
