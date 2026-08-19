# Prometheus master project status

Last audited: 2026-08-18

This document is the current implementation-status authority. Historical
completion records and implementation plans remain useful evidence for the
gate they describe, but several of them preserve statements that were true
before later programs landed. When a historical status sentence conflicts
with this audit, use this document and the named phase status file.

The audit covers the feature branch after merging `origin/main` at
`8e69f22`. The finite-bounds/YUBI fixes were first published at `36c1209`, and
the non-rewriting mainline reconciliation is recorded by merge commit
`d32c724`.

## Executive state

Prometheus now has working pieces of the proposed project compiler rather than
only a CAD viewer or contract prototype. It can account for a heterogeneous
folder, preserve source identities and bounded source bytes, import and inspect
real STEP assemblies, review and publish component inputs, bind a verified
component revision to a CAD entity, execute one bounded built-in motor model,
prepare and supervise one bounded CalculiX linear-static workflow, retain exact
run evidence, and reopen or relocate supported projects.

Prometheus still cannot determine whether an arbitrary engineering project
works. It has not produced a reviewed YUBI bracket result, its semantic graph
contains only the first production edge, and it has no general requirements
compiler, capability planner, project-wide coverage engine, or validated
thermal, circuit, CFD, or controls workflow.

The next consequential product claim is therefore narrow:

> A user can review one real YUBI bracket scenario, run the current coarse/fine
> CalculiX path, reproduce its evidence, and understand both the scoped result
> and the questions that remain unanswered.

That claim is not yet earned.

## Gate status

| Area | Current state | Evidence boundary |
| --- | --- | --- |
| Program 01A: reviewed-input trust kernel | Complete under its amended contract-tested gate | Exact reviewed package bytes, publication/replay, database invariants, and independent C++ integrity verification; not physical truth |
| Program 01B: package-driven built-in execution | Complete under its bounded gate | Motor A/B and one manually entered gearmotor feed the shared C++ motor backend; not general mechanics |
| Program 01C: Windows real-folder screen | Complete under its bounded gate | Pinned YUBI project inventory and STEP import; not a YUBI engineering result |
| Program 01D: multi-project evidence | Technical trials complete; human evidence open | YUBI, OpenArm, and JPL Rover are recorded; the required outside-user session is missing |
| Phase 3: structural workflow | Technical workflow implemented; real-component validation open | Reviewed setup, isolated CalculiX process, result validation, two-mesh refinement, findings, archives, replay, and desktop flow exist; the current Windows external gates and YUBI case remain open |
| Phase 4: persistence and portability | Thirteen implementation checkpoints complete; external trial open | Content-addressed project/run graphs, stale-state handling, bundle export/restore, backup recovery, inventory, and evidence archive are automated; a physically separate clean-machine trial is missing |
| Phase 5: component intake | Bounded exit gate met; broader acquisition open | Manual typed intake, review/publication, CAD binding, supersession, supported gearmotor consumption, and linked-page identity acquisition exist; document-derived specifications and desktop intake UI do not |
| Phase 6: semantic graph | Checkpoint 1 complete | A CAD-part-to-component-revision binding is a persisted append-only edge; the rest of the engineering graph is absent |
| Phases 7–8: requirements, planning, and validation | Workflow-specific fragments only | Structural and motor requests have typed obligations and limitations; there is no general compiler, planner, or calibrated multi-capability coverage layer |
| Phases 9–12: packaged product, pilots, and domain expansion | Not complete | No signed installer, clean-machine alpha, external engineer pilot, or multi-domain product exists |

## What the software can do now

### Account for and preserve a project folder

- Recursively inventory nested, hidden, readable, unreadable, symlinked,
  supported, and unsupported entries without silently dropping them.
- Record relative paths, sizes, classifications, analysis states, explanations,
  and SHA-256 identities for readable files.
- Compare later inventories and distinguish added, missing, changed, and
  state-changed files.
- Persist immutable inventory snapshots and a bounded inert archive of non-CAD
  evidence. Unknown executable-shaped content remains quarantined rather than
  executed.
- Export, verify, relocate, restore, and recover a supported Prometheus project
  bundle with only its reachable content-addressed objects.

This is file accounting and retention. It is not understanding of every file.

### Import and inspect real STEP assemblies

- Import STEP/XDE through Open Cascade while retaining hierarchy, instance
  names, persistent entity IDs, topology metadata, transforms, and a display
  mesh.
- Inspect bounds, volume, surface area, face/edge counts, selections,
  placements, measurement aids, and user-confirmed provisional connections.
- Run exact static common-volume checks for supported assemblies and sampled
  revolute sweeps. A sampled clear path is not continuous-clearance proof.
- Load the pinned Toyota YUBI assembly as one root, 90 leaves, and 37,367
  triangles.
- Recover finite display bounds from existing tessellation when Open Cascade
  returns an open or non-finite `Bnd_Box`, which fixes the black YUBI viewport
  without altering the source B-Rep.
- Defer automatic exact interference when fallback bounds identify the topology
  as unsafe for interactive in-process boolean work. Deferred remains
  `not_evaluated`, never clear.

The viewport fix establishes usable framing and mesh inspection. It does not
validate the YUBI topology, assembly intent, fits, contacts, or strength.

### Review, publish, and bind component evidence

- Ingest the three pinned synthetic fixtures and accept a typed manual
  component draft outside that catalog.
- Preserve known values, explicit unknowns, units, evidence, claim identities,
  fingerprints, review events, capability gates, and immutable canonical
  package bytes.
- Fetch a user-supplied public HTML product page under bounded SSRF controls,
  retain its exact bytes, and propose manufacturer/part-number identity from
  bounded `schema.org/Product` JSON-LD.
- Hash-verify a published execution-component package before binding it to a
  selected CAD entity.
- Mark a reopened binding unverified until rechecked and display a live
  supersession state when a newer sibling revision exists.
- Persist verified CAD-to-component bindings as one append-only supersession
  chain in the project graph when the project is writable.
- Feed a manually entered 17-slot gearmotor package into the same shared C++
  motor consumer used by Motor A/B.

The linked-page path extracts identity only. It does not extract torque,
material, current, strength, or other engineering specifications, and it does
not yet connect the retained page to document-sourced parameter claims.

### Execute bounded calculations without duplicate authority

- The Qt-free `prometheus_execution` library verifies package bytes, compiles
  the typed motor request, runs `motor_arm_builtin_v1`, and creates deterministic
  findings and run records.
- The desktop and replay CLI call that same implementation. They do not perform
  separate motor calculations.
- Motor A and Motor B produce the expected different holding result under one
  unchanged scenario, and the stored run reproduces offline.
- Save, archive, property reads, and QML repaint do not rerun the calculation.

This backend covers a fixed gearmotor/arm model. It does not establish arbitrary
mechanical analysis.

### Prepare and retain one bounded structural workflow

- Accept manually supplied coarse and fine first-order tetrahedral meshes in
  the supported Gmsh/Abaqus subset.
- Derive exterior faces from volume elements, validate topology and mesh
  quality, group selectable geometric patches, and retain exact selected face
  and node identities.
- Require reviewed material applicability, elastic properties, load and
  restraint surfaces, resultant force, limits, mesh controls, scenario, and a
  locked refinement criterion.
- Compile a deterministic CalculiX deck and run the solver as an isolated child
  process with timeout, logs, executable identity, and exact raw outputs.
- Reject zero load, stale or overlapping selections, failed launch, nonzero
  exit, timeout, fatal or incomplete solver status, malformed output,
  non-finite values, and incomplete/foreign result coverage.
- Retain the coarse baseline while the user loads and reviews a distinct finer
  mesh; require explicit correspondence of the physical load/restraint regions.
- Emit a scoped finding only after two validated results satisfy the locked
  refinement criterion. An above-threshold pair remains replayable but
  indeterminate.
- Publish a version-3 two-sample archive, embed it in the project, reconstruct
  it offline, and restore the result view without rerunning CalculiX.

For one active coarse/fine session, each mesh is prepared once, each deck is
compiled once, each solver sample runs once, and each result is normalized
once. Publication streams and verifies existing artifacts; it does not repeat
either expensive solve.

## Work completed in the 2026-08-18 reconciliation

### YUBI loading and viewport defect

The normal YUBI path previously accepted an open Open Cascade bounding box with
sentinel-scale coordinates near `2e+97 m`. The poisoned scene extent moved the
camera outside its useful clipping range and produced a black viewport. It also
prevented useful broad-phase rejection before exact interference.

The repaired importer now:

1. accepts only closed, ordered, finite B-Rep bounds;
2. derives finite bounds from the already-produced display mesh when needed;
3. carries finite conservative broad-phase boxes through placement and sweep;
4. rejects non-finite transforms before Open Cascade work; and
5. defers the complete interactive exact-interference batch when any leaf
   needed tessellation-derived bounds.

The exact B-Rep remains the only authority for a future exact interference
attempt. Tessellation can exclude separated candidates and frame the viewport;
it cannot create a collision or clearance finding.

### Mainline component work

The pulled mainline added three changes:

- the Program 01A amendment record;
- verified component binding, supersession handling, real manual-package
  consumption, and persistence of the CAD/component edge; and
- bounded named/linked product-page acquisition with exact-byte retention and
  JSON-LD identity proposals.

The merge had two textual conflicts. The CMake conflict now registers both the
structural backend and the component-binding/execution-variant sources. The QML
test conflict now exposes both controllers to the same offscreen application.
No second physics or structural execution path was introduced.

## Verification recorded for the merged tree

| Check | Result on 2026-08-18 | Interpretation |
| --- | --- | --- |
| Backend, default SQLite environment | 454 passed, 1 PostgreSQL-only test skipped | Full local backend suite passed |
| New acquisition/intake/API subset | 75 passed | Pulled acquisition paths passed focused coverage |
| Headless C++ | 16/16 passed | Core, integrity, execution, run store, replay, and structural library passed |
| Desktop without Open Cascade | 28/30 passed in the managed socket sandbox; the two loopback tests passed 2/2 outside it | All 30 tests were observed passing; the split is environmental |
| macOS Open Cascade build | Built successfully | Combined CAD, structural, component-binding, and desktop sources compiled |
| macOS Open Cascade CTest set | 28/31 passed in the managed socket sandbox; the three loopback-dependent tests passed 3/3 outside it | All 31 tests were observed passing across the two runs |
| Pinned YUBI external import | `roots=1`, `leaves=90`, `triangles=37367`, `interferences=deferred` | The finite-bounds loading fix survived the mainline merge |
| PostgreSQL 17 local rerun | Not executed | The configured local server at `127.0.0.1:55432` was not running; connection was refused before test setup |
| Current external CalculiX gate | Not executed in this audit | The strengthened Windows workflow remains a required evidence item |
| GitHub Actions for the final merged/status commit | Pending publication | Automatic CI can be evaluated only after the final push |

The loopback split is not a product exemption. CI and normal developer hosts
must run those tests where local listeners are permitted.

## Open issues and missing evidence

### Blocks the first credible real-component result

1. **The strengthened external structural gate has not been rerun.** The
   current `run-calculix-smoke.ps1` and `run-structural-validation.ps1` paths
   need a recorded Windows CalculiX run after the structural reconciliation.
2. **The YUBI scenario is not reviewed.** `A2024` in the BOM does not identify
   temper, product form, delivered stock, elastic applicability, or an
   allowable. The load vector, load faces, restraint faces, displacement/stress
   limits, mesh controls, and scenario also require explicit user review.
3. **No coarse/fine YUBI pair has been accepted.** The current recorded mesh
   counts are preparation evidence, not a convergence study. The user must
   manually load and review two meshes and confirm physical-region
   correspondence.
4. **No YUBI bracket finding exists.** Prometheus must not report pass or fail
   until items 1–3 produce two complete, identity-bound solver results.
5. **The outside-user screening session is missing.** The YUBI, OpenArm, and
   Rover developer trials do not substitute for an unassisted participant.

### Limits general project compilation

6. **The semantic graph is narrow.** Only the append-only CAD-part-to-component
   edge is production graph state. BOM rows, documents, materials, joints,
   contacts, loads, restraints, requirements, scenarios, requests, and findings
   are still separate workflow records rather than one reviewable project
   graph.
7. **There is no general proof-obligation compiler or capability planner.** The
   motor and structural workflows compile their own fixed questions; Prometheus
   cannot yet take arbitrary project requirements and explain which supported
   analyses cover them.
8. **There is no project-wide coverage engine.** Individual workflows preserve
   findings and unknowns, but no current reducer accounts for all requirements,
   scenarios, artifacts, and unsupported questions in one project answer.
9. **Automatic meshing is absent.** Manual coarse/fine mesh loading is the
   current deliberate boundary. A later mesher can feed that boundary, but it
   must retain the same review, refinement, identity, replay, and no-duplicate-
   solve invariants.
10. **Component acquisition remains narrow.** There is no structured CSV/BOM
    import, datasheet attachment flow, deterministic PDF/table extraction, or
    reviewed machine-assisted specification extraction in the desktop.
11. **Acquired pages are not claim evidence yet.** The retained page can prefill
    identity, but parameter claims still use the manual measurement shape.
12. **No other engineering domain is validated.** Thermal, circuits/power,
    CFD, controls, fatigue, buckling, contact, tolerance, fastening, and
    manufacturability remain unsupported or not evaluated.

### Runtime, security, deployment, and UX debt

13. **Pathological exact Open Cascade booleans are not cancellable in-process.**
    YUBI import now returns promptly by deferring the batch. A safe user-triggered
    retry requires moving the exact operation behind an isolated cancellable
    process boundary.
14. **CAD parsing still runs inside the desktop process.** The long-term parser
    worker sandbox, resource limits, and crash containment are not implemented.
15. **Linked-page fetching has an accepted DNS-rebinding residual risk.** The
    current fetch validates public addresses and rejects redirects, but it does
    not pin the connected address against rebinding.
16. **Component-edge persistence can be session-only.** When no writable open
    project exists, a verified live binding is retained in the session but the
    graph-persist step is intentionally skipped; the UI does not yet present a
    binding-history panel.
17. **Clean-machine evidence remains incomplete.** Bundle export/restore is
    automated, but the supported project has not completed a physically
    separate clean-machine recovery trial or a signed installer flow.
18. **Automatic CI does not exercise Open Cascade or CalculiX on every push.**
    The required matrix covers the no-OCCT desktop; structural validation and
    participant packaging are manual Windows workflows. This is acceptable for
    prototype checkpoints but remains a release-evidence boundary.
19. **Living documentation has drifted.** `README.md`, `product-scope.md`,
    `architecture.md`, `validation-plan.md`, and `validation-policy.md` retain
    pre-01B, pre-structural, or pre-Phase-4 statements. Historical completion
    records should stay immutable, but living documents need a focused
    harmonization pass.
20. **Implementation-plan checkboxes are not reliable status.** Several plan
    files retain unchecked execution steps after their commits landed. Plans
    describe the intended sequence; this document and phase evidence records
    describe current completion.
21. **Minor dependency/UI warnings remain.** The backend emits a Starlette
    deprecation warning for the current test-client bridge, and manual macOS
    runs have exposed Qt native-style/customization and shutdown-time QML
    warnings. They did not fail the verified workflows but should be cleaned up
    before an alpha.

## Master path from here

### Checkpoint A: earn the first real structural result

1. Publish this reconciled branch and require the automatic matrix to pass.
2. Run and retain the current Windows CalculiX smoke, analytic, known-pass,
   known-fail, and refinement evidence.
3. Review the YUBI material applicability, load/restraint regions, load vector,
   limits, scenario, mesh controls, and locked refinement threshold.
4. Load the manual coarse and fine meshes, confirm their physical boundary
   correspondence, run each solver sample once, and archive the pair.
5. Report the bounded YUBI result with its assumptions, convergence evidence,
   exclusions, and uncovered project questions.

### Checkpoint B: establish that the workflow generalizes

1. Conduct the unassisted outside-user folder-screening session.
2. Repeat the structural workflow on at least two materially different real
   components rather than adding more synthetic cases.
3. Record setup time, import/mesh failures, wrong or confusing assumptions,
   numerical sensitivity, and the first finding that changes an engineering
   decision.
4. Use those failures to decide whether automatic meshing, BOM intake, or
   another structural setup primitive is the next highest-value addition.

### Checkpoint C: complete the mechanical project compiler

1. Promote the real relationships exposed by those projects into the semantic
   graph: BOM/document evidence, material applicability, joints/contacts,
   loads/restraints, requirements/scenarios, and analysis/finding lineage.
2. Add a thin requirement/scenario compiler that creates explicit proof
   obligations and preserves unsupported questions.
3. Add a capability registry and planner only for the checks that now exist,
   with required inputs, applicability, backend identity, validation level,
   and reasons a question cannot run.
4. Add project-level coverage reduction across obligations and scenarios.
5. Move unsafe CAD/parser and exact-geometry work into bounded workers; retain
   the existing isolated CalculiX execution boundary.

### Checkpoint D: validate and package the bounded mechanical product

1. Complete clean-machine bundle recovery and a Windows installer/package.
2. Run a small external mechanical-engineer pilot and measure whether users
   understand pass-within-scope, unknown, unsupported, and not evaluated.
3. Set per-capability tolerance and false-negative release gates from analytic,
   independent-solver, and where practical physical evidence.
4. Harden only failures observed in real projects, clean-machine trials, or
   pilot use.

### Checkpoint E: expand by one bounded domain at a time

Add thermal, circuits/power, CFD, and controls only when a real project supplies
a concrete question. Each new slice needs one declared authoritative backend,
reviewed inputs, analytic or authoritative benchmarks, known-pass and known-fail
cases, explicit applicability limits, retained raw evidence, and honest
coverage. Domain labels alone do not create general engineering support.

## Immediate recommendation

Do not add another general framework before obtaining the first real YUBI
result. The shortest evidence-bearing sequence is:

1. green automatic CI on the reconciled branch;
2. current Windows structural-validation workflow;
3. user-reviewed YUBI setup and manual coarse/fine pair;
4. bounded YUBI report;
5. outside-user and clean-machine trials; and
6. only then the next graph/planner/automation increment selected from observed
   friction.

This sequence uses the trust, persistence, and structural code that already
exists. It does not rerun calculations during save or viewing, and it avoids
building more architecture before a real project tests the current one.
