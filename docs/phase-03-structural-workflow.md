# Phase 3 structural workflow status

Status: active. Prometheus has one bounded linear-static structural workflow,
but it has not validated the selected YUBI bracket. Phase 2's outside-user
folder-screening session also remains open; neither item is counted as complete
evidence.

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
  -> two-sample archive v3 and project publication
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

Reopening or importing persisted bytes is a new trust-boundary operation. V3
replay deliberately reconstructs both setup/result packages and returns one
typed pair snapshot. Restoration then regroups the visual patches for display
but does not execute a solver. Counting-backend desktop tests enforce these
stage counts directly.

## Selected real slice

The selected component is Toyota YUBI `BRACKET_GRIPPER` at upstream revision
`e8334ff04945ccf56c0576a56f6fab74b63daaa2`. Its exact STEP SHA-256 is
`4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a`.
The BOM identifies the material only as `A2024` and describes the component as
the gripper-to-UR5e attachment. That is candidate identity, not a reviewed
temper, product form, elastic model, strength allowable, or material
certificate.

Prepare the deliberately blocked slice with:

```powershell
.\scripts\prepare-yubi-structural-slice.ps1
```

The generated manifest leaves material applicability, loads, restraints,
requirements, mesh controls, and scenario confirmation unresolved. Prometheus
must not turn that folder into a structural pass.

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

The recorded local YUBI mesh contains 2,451 nodes, 7,566 `C3D4` volume
elements, and 4,616 exterior triangles. At the desktop's 15-degree grouping
angle it produces 263 geometric patches. Those counts are topology evidence,
not an engineering interpretation or convergence result.

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

Earlier Windows development runs recorded CalculiX 2.23 completing the
one-tetrahedron wiring smoke and the analytic benchmark cases. Those values are
historical evidence for the pre-reconciliation path. They are not substituted
for a run of the strengthened current scripts.

Run the current external gates on the supported Windows environment with:

```powershell
.\scripts\run-calculix-smoke.ps1
.\scripts\run-structural-validation.ps1
```

The validation command first runs the focused known-pass/known-fail polarity
fixture. It then runs coarse and fine forms of the compiled axial benchmark,
derives their displacement and stress changes, writes one two-sample v3 archive,
and verifies both raw result packages plus the derived comparison offline. The
cantilever refinement gate uses the same typed pair API and v3 writer. Neither
archive creation nor active publication repeats a solve or finding compilation.
The checked-in Gmsh tension-bar geometry and expectations remain historical
validation inputs; the retired parallel case exporter/verifier no longer
executes them. A post-reconciliation Windows run must be recorded before
claiming these current external gates passed.

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

New structural writes use archive schema v3. Each manifest closes over fourteen
artifacts: reviewed setup, deck, DAT, FRD, STA, standard output, and standard
error for both coarse and fine samples. It also binds the locked criterion,
reviewed boundary correspondence, two backend/execution records, derived
comparison, coverage, findings, and limitation. Accepted and valid
above-threshold pairs can both be archived; the latter contains zero findings.
The writer consumes already validated objects and does not rerun either solver,
reparse active output, or compile findings again.

Offline v3 verification treats persisted bytes as untrusted. It rehashes all
fourteen artifacts, reconstructs both reviewed setups and raw result packages,
reruns the pair compiler, and requires the regenerated comparison, coverage,
and findings to match the manifest. This trust-boundary replay is distinct from
active-session calculation.

V1 and v2 remain legacy read contracts. V1 retains its narrower historical
claim. V2 reconstructs its one stored active result and reproduces its original
scoped finding claim through an isolated compatibility routine, but it cannot
produce `VerifiedStructuralRefinement` or enter the new finding compiler. New
desktop runs never write v1 or v2.

A verified archive can be relocated and embedded in a Prometheus project as a
closed content-addressed artifact graph. V3 embedding retains all fourteen
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

1. Rerun and record the strengthened external smoke, analytic, refinement, and
   offline-replay gates on the supported Windows CalculiX environment.
2. Have the user review the YUBI bracket's actual temper, product form,
   applicability, elastic properties, load faces and vector, restraint faces,
   requirements, mesh controls, and scenario.
3. Manually supply and review coarse and fine YUBI meshes, including explicit
   physical-region correspondence and an acceptable locked-threshold study.
4. Execute the reviewed bracket and retain a physically independent
   real-component comparison.
5. Complete the outstanding outside-user folder-screening session.

Until those items are complete, Prometheus has a bounded, fail-closed
structural workflow—not a validated YUBI result and not proof that an arbitrary
engineering project works.

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
assessed the final inline diff. The post-reconciliation Windows CalculiX run,
reviewed YUBI scenario, independent component comparison, and outside-user
session remain open.
