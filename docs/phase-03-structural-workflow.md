# Phase 3 structural workflow status

Status: active. Prometheus has executed and independently replayed one bounded
linear-static coarse/fine study on the selected YUBI bracket. The study did not
meet its complete predeclared refinement criterion, so the engineering
evaluation is `indeterminate`, not passed or validated. Phase 2's outside-user
folder-screening session also remains open.

## Consolidated authority

The product path is:

```text
project evidence
  -> manually supplied coarse tetrahedral mesh and reviewed surfaces
  -> locked refinement criterion and immutable coarse setup
  -> isolated CalculiX process -> validated coarse result
  -> retained immutable baseline
  -> manually supplied fine mesh and independently reviewed surfaces
  -> copied and locked shared physics plus reviewed boundary correspondence
  -> isolated CalculiX process -> validated fine result
  -> verified two-result comparison
  -> scoped findings or an honest indeterminate evaluation
  -> two-sample archive v4 and project publication
```

There is one desktop controller (`StructuralController`), one setup panel
(`StructuralSetupPanel.qml`), one solver-evidence compiler
(`compile_calculix_result()`), and one finding compiler. The retired parallel
controller, case exporter/verifier, smoke verifier, finding model, and surface
setup implementation have been removed. A source-tree CTest gate fails if any
retired file or active registration returns.

The UI collects and presents choices; it does not calculate structural values.
The authoritative Qt-free `prometheus_structural` library prepares the mesh,
groups patches, compiles the reviewed setup, runs CalculiX, validates raw
evidence, and compiles findings.

### Single-computation invariant

For one active coarse/fine session, the desktop performs each expensive stage
once for the immutable input that owns it:

- each supplied mesh is parsed and validated once;
- each exterior boundary and mesh-quality record is measured once;
- each mesh's patches are grouped once for its selected angle;
- each reviewed setup and deterministic deck is compiled once;
- CalculiX executes once for the coarse sample and once for the fine sample;
- each raw solver output set is parsed and validated once; and
- the pair comparison and findings compile once after the fine result exists.

Repeated property reads, QML repaints, result viewing, and material-candidate
browsing do not invoke those stages. Archive creation and publication consume
the existing pair, findings, and manifest; they do not rerun either solve or
recompile findings. Editing an input invalidates only the affected stage and
its downstream state. A fine-mesh or fine-surface edit retains the immutable
coarse baseline. Changing shared material, load, requirements, scenario, or
other shared physics requires an explicit baseline reset and a new coarse run.

Integrity hashing is intentionally separate from this engineering-stage
invariant. Mutable solver files are rehash-verified when they cross into an
archive or project object store. Structural publication now hashes and chunks
each artifact in one streaming read rather than reading the whole artifact once
to hash it and again to chunk it.

Reopening or importing persisted bytes is a new trust-boundary operation. V4
replay deliberately reconstructs both setup/result packages and returns one
typed pair snapshot. Restoration then regroups the visual patches for display
but does not execute a solver. Counting-backend desktop tests enforce these
stage counts directly.

## Selected real slice

The selected component is Toyota YUBI `BRACKET_GRIPPER` at upstream revision
`e8334ff04945ccf56c0576a56f6fab74b63daaa2`. Its exact STEP SHA-256 is
`4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a`.
The BOM identifies the material only as `A2024` and describes the component as
the gripper-to-UR5e attachment. That source does not establish temper, product
form, elastic constants, strength allowable, or the supplied material
certificate.

The project owner approved one explicitly hypothetical scenario on 2026-08-20.
Its immutable manifest records assumed 2024-T351 bare plate properties, a
100 N local negative-Y load on source CAD `Surface76`, a fully fixed source CAD
`Surface75`, a 0.50 mm informational displacement threshold, and a 10% global
displacement/global-stress refinement threshold. The material assumption,
load, restraint idealization, and threshold are not Toyota requirements.

Preflight the exact reviewed pair and run it on the supported Windows solver
host with:

```powershell
.\scripts\run-yubi-structural-slice.ps1
```

The runner verifies both complete setups before it creates an output directory,
then executes coarse once and fine once. The exact manifest and claim boundary
are in `fixtures/structural/yubi-bracket/`. The manual Windows workflow
[completed on run 32503165787](https://github.com/Richard-Y-W/Prometheus/actions/runs/32503165787)
at commit `6195ec6275097bdb37c921c646b99e3084169cc0`. Its v4 archive passed workflow
replay and an independent replay with the corrected local CLI. The retained
result is recorded in
[`docs/trials/yubi-bracket-structural-result.md`](trials/yubi-bracket-structural-result.md).

## Implemented structural boundary

### Mesh preparation and selectable surfaces

Prometheus accepts the supported Gmsh/Abaqus first-order tetrahedral subset,
applies an explicit coordinate scale, and retains source surface labels only as
non-authoritative navigation aids. It rejects duplicate or invalid identities,
non-finite coordinates, zero-volume or inverted tetrahedra, disconnected volume
regions, non-manifold boundaries, invalid source triangles, and mesh quality
below the reviewed threshold.

Exterior triangles are derived from volume tetrahedra rather than trusted from
optional mesher surface exports. Every face retains its exact node identity,
owner element, outward normal, centroid, and SI area. Deterministic geometric
patches help the user select faces, but no patch is inferred to be a bolt,
contact, load, or restraint.

When a user reviews a patch selection, transient patch IDs resolve into sorted
exact face and node identities plus total area. A reviewed total surface force
is distributed by triangle area to consistent first-order nodal forces. Tests
require the compiled nodal vectors to reproduce the reviewed resultant exactly
and reject duplicate, overlapping, missing, or stale selections.

The reviewed coarse YUBI mesh contains 2,446 nodes, 7,533 `C3D4` elements, and
4,616 exterior faces; its observed minimum mean ratio is about 0.314787. The
reviewed fine mesh contains 7,876 nodes, 29,015 `C3D4` elements, and 12,076
exterior faces; its observed minimum mean ratio is about 0.248135. Both exceed
the predeclared 0.20 quality floor. These are topology and preflight facts, not
a convergence result or a component finding.

### Reviewed setup compiler

`StructuralSetup` retains the meaning and provenance that cannot safely be
reduced to solver numbers:

- exact geometry and mesh identities, scale, sizing, quality, mesher, and
  review state;
- material designation, temper, product form, source identity, applicability,
  Young's modulus, Poisson ratio, and review state;
- exact loaded and fixed faces, total force vector, and review state;
- displacement and/or von Mises limits with a reviewed source or explicitly
  exploratory basis; and
- a non-empty scenario description and final confirmation.

The compiler rejects unsafe text, malformed SHA-256 values, zero resultant
force, duplicate force identities, load/restraint overlap, stale topology,
unreviewed fields, inconsistent mesh provenance, and missing limit rationale.
Only a valid setup becomes one `CompiledStructuralSetup` containing the typed
request, canonical setup evidence, deterministic CalculiX deck, and a
content-derived identity.

The checked-in 2024 aluminum evidence file contains three bounded candidates:
the YUBI BOM designation-only record, a Kaiser T4/T351 sheet/coil/plate modulus
record, and a MIL-HDBK-5J T351 plate reference. None identifies the delivered
bracket. Candidate selection never implies applicability, and no candidate
supplies an implicit strength limit.

### Isolated execution and validated evidence

The shell-free process adapter receives a `CompiledStructuralSetup`, a solver
executable, an empty working directory, a safe job name, and a timeout. It
captures standard output and error, process exit, elapsed time, executable
SHA-256, solver version, and exact deck, DAT, FRD, and STA identities.

Completed status requires all of the following:

- successful launch and exit code zero;
- no timeout or pre-existing raw-output collision;
- nonempty required DAT, FRD, and STA artifacts;
- a CalculiX completion marker and no fatal-error marker;
- a completed final STA step at the requested time;
- deck bytes identical to the compiled setup;
- finite displacement and stress rows; and
- exact node and element result coverage with no missing, duplicate, or foreign
  identities.

Failure, timeout, missing output, incomplete convergence, or invalid coverage
produces no completed metrics and no pass or violation finding.

The fixed smoke command uses `prometheus_run_calculix_job --axial-smoke`. That
tool constructs a reviewed, compiled axial benchmark in C++ and passes it to the
same runner used by the desktop. It does not accept arbitrary raw deck bytes.
The PowerShell wrapper does not invoke `ccx` directly or run a second verifier;
it fails unless the runner prints `status=completed evidence=validated`.
`prometheus_export_structural_smoke` separately emits the same deterministic
fixture's canonical setup, deck, and compiled identity for inspection.

### Findings and refinement

A completed result alone is insufficient for a finding. Before the coarse
solve, the engineer locks a finite maximum-change criterion. Prometheus then
requires two complete, identity-bound samples with the same reviewed material,
load, requirements, scenario, solver backend, and criterion. The fine mesh must
have a distinct source identity, more volume elements, and a smaller reviewed
target size.

The fine stage copies and locks the coarse sample's shared physics. Load and
restraint surfaces remain mesh-specific: the engineer selects them again on the
fine mesh and confirms that both selections represent the same physical regions
as the baseline. That correspondence is a reviewed assumption because arbitrary
external meshes do not provide stable CAD-face identities.

Only `VerifiedStructuralRefinement`, whose changes and status are derived from
the two validated results, can enter the finding compiler. QML and generic maps
cannot submit acceptance flags, result identities, or calculated deltas. The
compiler evaluates only the displacement and/or von Mises obligations present
in the reviewed request. A value strictly below its limit is
`no_violation_detected_within_scope`; equality or exceedance is `violated`.
A valid pair above the locked threshold is archived as
`comparison_indeterminate` with zero evaluated obligations and no pass or
violation findings.

Every finding retains the measured value, limit, margin, unit, evidence
identities, assumptions, and bounded isotropic linear-elastic `C3D4` scope. The
limitation explicitly excludes fatigue, buckling, contact, fasteners,
nonlinearity, safety certification, and project-wide correctness. Unit and
process-fixture tests exercise accepted, above-threshold, known-pass,
known-fail, equality, stale-output, timeout, malformed-output, lineage, mesh
ordering, boundary-review, backend, and coverage failures.

## Validation gates

The repository retains three distinct kinds of evidence:

1. Checked-in parser fixtures exercise exact DAT/STA/stream parsing and coverage
   without claiming that a solver ran.
2. Deterministic C++ axial and cantilever factories provide independent
   closed-form references and predeclared tolerances.
3. Windows scripts run the external CalculiX executable through the production
   runner and replay the resulting archive offline.

The axial reference is `u = F L / (A E)` and `sigma = F / A` for a
`1 m x 0.1 m x 0.1 m`, Poisson-ratio-zero bar under `1000 N`. The cantilever
reference is Euler-Bernoulli `u = F L^3 / (3 E I)` with root stress
`sigma = 6 F L / (b h^2)`. These equations are independent checks of the
external adapter, not evidence about YUBI.

The corrected Windows structural workflow passed at commit
`6195ec6275097bdb37c921c646b99e3084169cc0` in
[GitHub Actions run 32502246569](https://github.com/Richard-Y-W/Prometheus/actions/runs/32502246569).
That run exercised CalculiX 2.23 through the current solver, result,
refinement, finding, archive, and replay boundaries. It validates the bounded
structural backend; it does not by itself supply YUBI inputs or a YUBI result.
In its cantilever benchmark, global maximum von Mises stress changed by
`1.0341770734e-01` and was recorded as `not_converged_in_this_study`; that
diagnostic was predeclared with `participated_in_acceptance=false`. The bounded
gate passed on its accepted displacement and section-stress observables, not on
convergence of that global extremum.

Run the current external gates on the supported Windows environment with:

```powershell
.\scripts\run-calculix-smoke.ps1
.\scripts\run-structural-validation.ps1
```

The validation command first runs the focused known-pass/known-fail polarity
fixture. It then runs the axial analytic benchmark and the predeclared
cantilever pair, evaluates typed displacement and regional-stress observables,
writes v4 evidence, and verifies the retained archive offline. The global
cantilever clamp stress remains visible as a nonconverged diagnostic and does
not become a stress finding. Neither archive creation nor active publication
repeats a solve or finding compilation.

The separate `yubi-structural-trial.yml` workflow consumes the checked-in
reviewed pair. It does not regenerate meshes or repeat the analytic suite. Its
only expensive operations are one coarse and one fine CalculiX execution,
followed by archive replay at the release trust boundary.

[YUBI run 32503165787](https://github.com/Richard-Y-W/Prometheus/actions/runs/32503165787)
executed that exact path once on corrected `main`. Both solver jobs completed
with exit code zero and complete row coverage. Global maximum displacement
changed by `0.0704380451306449`, inside the locked `0.10` threshold; global
maximum von Mises stress changed by `0.1401320289035979`, outside it. The
archive therefore contains an indeterminate comparison, zero findings, and
`0/1` evaluated obligations. Workflow success establishes a reproducible
execution record, not an engineering pass.

## Desktop workflow

The structural panel exposes an explicit coarse stage, retained-baseline stage,
fine stage, and completed-comparison stage. The engineer manually loads a
supported coarse mesh, reviews its surfaces and shared physics, and locks the
criterion before starting the asynchronous baseline solve. A completed coarse
sample displays field coverage and extrema as
`execution_completed_evaluation_pending`; it has no scoped findings.

The fine stage accepts a second manually supplied mesh. Shared physics is shown
read-only from the baseline, while fine load/restraint surfaces and mesh
controls are reviewed independently. The panel displays both selected areas and
requires explicit load-region and restraint-region correspondence confirmation.
After the fine solve, the controller derives and displays displacement, stress,
and maximum changes. Accepted and above-threshold pairs both remain replayable;
only the accepted pair emits scoped findings.

Fine-only edits retain the coarse baseline and invalidate only the fine sample
and downstream comparison. Shared-physics edits require **Discard baseline and
start over**. The deformed exterior stress view uses an explicit deformation
scale and color range; it is not physical-scale motion, continuous collision
proof, or a safety claim.

## Archives, replay, and project publication

New structural writes use archive schema v4. Each manifest closes over fourteen
artifacts: reviewed setup, deck, DAT, FRD, STA, standard output, and standard
error for both coarse and fine samples. It also binds typed observable
definitions and identities, reviewed boundary correspondence, two
backend/execution records, per-observable comparisons, global-extremum
diagnostics, coverage, findings, unknowns, and limitation. Accepted and valid
above-threshold pairs can both be archived; the latter contains zero findings
and one unknown for each declared obligation.

Offline v4 verification treats persisted bytes as untrusted. It rehashes all
fourteen artifacts, reconstructs both reviewed setups and raw result packages,
reevaluates the declared observables, reruns the pair and finding compilers, and
requires the regenerated comparison, coverage, findings, and unknowns to match
the manifest. This trust-boundary replay is distinct from active-session
calculation.

V1, v2, and v3 remain legacy read contracts with their original semantics. New
desktop and reviewed-pair runs write v4.

A verified archive can be relocated and embedded in a Prometheus project as a
closed content-addressed artifact graph. V4 embedding retains all fourteen
sample artifacts. Reopen reconstructs and verifies both results, restores the
editable reviewed setup and visualization, and compares the archive's bound
assembly identity with the current project. Evidence bound to a changed source
remains viewable but is marked stale and cannot be rerun without renewed
geometry and surface review. Review, packaging, and final graph validation each
reject a structural geometry identity that differs from the project assembly.

Automatic coarse/fine generation is not implemented. A later mesher can plug
into the existing prepared-mesh boundary, but it must preserve the same two-run
comparison, solver authority, provenance, replay, and review rules.

## Phase 5 boundary

Phase 5 can publish manually entered component evidence packages, but the
structural workflow does not consume those packages yet. Loading the standalone
2024 aluminum candidate file is a bounded review aid, not a component-package
binding. A future adapter must explicitly bind a published component revision
and its reviewed properties into `StructuralSetup`; no such bridge is claimed
here.

## Remaining Phase 3 evidence

1. Keep the YUBI result indeterminate unless a newly reviewed finer mesh or
   another predeclared convergence study resolves the stress sensitivity. Do
   not change the 10% criterion after observing the result, and do not add a
   stress pass/fail without a reviewed stress allowable.
2. Repeat the reviewed workflow on at least two materially different real
   components so applicability failures are not inferred from one bracket.
3. Complete the outstanding outside-user folder-screening session.

Prometheus now has a bounded, fail-closed structural workflow and one replayed
native YUBI result. It has not validated the bracket and has not shown that an
arbitrary engineering project works.

## Reconciliation release gate — 2026-08-16

The manual coarse/fine release checkpoint produced this local evidence:

- all 405 backend tests passed and one PostgreSQL-only case skipped under the
  default SQLite environment;
- all 70 migration suites passed against an isolated UTF-8 PostgreSQL 17.10
  database;
- all 16 headless CTests passed;
- 28 of 29 desktop CTests passed inside the managed sandbox, whose socket
  policy prevented the remaining HTTP fixture from opening a loopback listener;
- that exact loopback test passed 1 of 1 outside the socket sandbox;
- the integrity regression first reached an unsafe allocation path, then passed
  after chunk sizes outside `std::streamsize` were rejected before allocation;
- source-authority, two-sample replay, structural-controller, QML-authority,
  run-store, rover-fixture, and preset-parse gates passed; and
- an inline review found no Critical or Important issue in reachable
  publication, private comparison construction, two-result replay, legacy
  isolation, fine-only invalidation, or exact stage counts.

This checkpoint did not run Windows. A second independent reviewer has not
assessed that historical inline diff. Later Windows run 32445951610 closed the
post-reconciliation backend-validation item, and the 2026-08-20 manifest closed
the selected YUBI input-review item. Corrected structural-validation run
32502246569 and YUBI run 32503165787 subsequently closed the native execution
and replay items. The YUBI evaluation remained indeterminate, and the
outside-user session remains open.

## Reviewed-pair preparation checkpoint — 2026-08-20

The current feature branch produced this bounded local evidence:

- all 19 headless CTests passed;
- 31 of 33 desktop-no-OCCT tests passed inside the managed socket sandbox;
- the two blocked loopback-listener tests passed 2 of 2 outside that sandbox;
- the exact 2,446-node/7,533-element coarse and
  7,876-node/29,015-element fine YUBI meshes passed hash, topology, quality,
  patch-geometry, material-candidate, setup, and refinement-order preflight;
- the fake-solver tool fixture created exactly two solver job sets, one v4
  archive, and a successful offline replay; and
- a changed coarse-mesh hash failed before the runner created any output
  directory.

This checkpoint did not execute native CalculiX on the YUBI pair. It establishes
that the reviewed inputs reach the existing authoritative structural backend
without adding another physics implementation or a duplicate solve path.

## Native YUBI evidence checkpoint — 2026-08-21

The explicit manual workflow ran the locked pair once at commit
`6195ec6275097bdb37c921c646b99e3084169cc0`. The retained runner log contains
one coarse and one fine CalculiX 2.23 execution. Both completed at step 1,
increment 1, attempt 1, and iteration 1. The archive retained solver executable
SHA-256
`913abf828a2d706f3e8c9da89d7a0eddd68ce817f8dabf1098cb013dfe3f94f6`
and manifest SHA-256
`7794c99815e7ccfed597e860fa16d60a566a19fd25d801f7ad8137ba030b12a7`.

The fine maximum displacement was `7.705012145689252e-9 m`, but the global
stress refinement change was `14.01320289035979%`, above the locked 10% limit.
Prometheus therefore emitted no finding and evaluated zero of one declared
obligations. The recorded identities, per-sample archive artifact hashes,
assumptions, and non-claims are in the
[YUBI result record](trials/yubi-bracket-structural-result.md).
