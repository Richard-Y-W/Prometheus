# Phase 3 structural workflow status

Status: active. Prometheus has one bounded linear-static structural workflow,
but it has not validated the selected YUBI bracket. Phase 2's outside-user
folder-screening session also remains open; neither item is counted as complete
evidence.

## Consolidated authority

The product path is:

```text
project evidence
  -> prepared tetrahedral mesh and exterior boundary
  -> reviewable geometric patches
  -> reviewed material, load, restraint, requirement, mesh, and scenario
  -> immutable CompiledStructuralSetup
  -> isolated CalculiX process
  -> one validated CompiledCalculixResult
  -> refinement-gated scoped findings
  -> versioned structural archive and project publication
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

For one active immutable input identity, the desktop performs each expensive
stage once:

- mesh bytes are parsed and validated once;
- exterior faces and mesh quality are measured once;
- patches are grouped once for a selected angle;
- the reviewed setup and deterministic deck are compiled once;
- CalculiX executes once;
- raw solver fields, convergence, coverage, extrema, and findings compile once.

Repeated property reads, QML repaints, result viewing, and material-candidate
browsing do not invoke those stages. Publishing the active run uses its existing
compiled setup, validated result, findings, and manifest; it does not rerun the
solver or recompile the setup. Editing an input invalidates only the affected
stage and its downstream state. For example, changing force data invalidates
setup and execution but does not reparse the mesh; changing the patch angle
regroups the retained prepared boundary but does not remeasure tetrahedra.

Reopening or importing persisted bytes is a new trust-boundary operation. It
deliberately verifies those bytes once and returns one typed restore snapshot.
Restoration then regroups the visual patches once but does not compile a new
setup or execute a solver. Counting-backend desktop tests enforce these stage
counts directly.

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

A completed result alone is insufficient for a finding. The finding compiler
also requires complete refinement evidence satisfying its predeclared maximum
change. It evaluates only the displacement and/or von Mises obligations present
in the reviewed request. A value strictly below its limit is
`no_violation_detected_within_scope`; equality or exceedance is `violated`.

Every finding retains the measured value, limit, margin, unit, evidence
identities, assumptions, and bounded isotropic linear-elastic `C3D4` scope. The
limitation explicitly excludes fatigue, buckling, contact, fasteners,
nonlinearity, safety certification, and project-wide correctness. Unit and
process-fixture tests exercise known-pass, known-fail, equality, missing
refinement, stale-output, timeout, malformed output, and coverage failure.

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
fixture, then runs the compiled axial benchmark, writes its v2 archive without
reparsing the active result, verifies that archive offline, and runs the
cantilever refinement gate. The checked-in Gmsh tension-bar geometry and
expectations remain historical validation inputs; the retired parallel case
exporter/verifier no longer executes them. A post-reconciliation Windows run
must be recorded before claiming these current external gates passed.

## Desktop workflow

The one structural panel can load and render a supported mesh, show mesh and
source identities, display quality and boundary counts, highlight load and
restraint selections, show selected area and compiled resultant, load bounded
material candidates, collect all reviewed inputs, and display exact blocker
codes. Changing a boundary role invalidates the corresponding review, scenario
confirmation, and refinement state.

Execution is asynchronous. A completed run displays field coverage, exact
displacement and stress extrema, scoped findings, limitations, and a deformed
exterior stress view with an explicit deformation scale and color range. The
view is not physical-scale motion, continuous collision proof, or a safety
claim.

## Archives, replay, and project publication

New runs write structural archive schema v2 only. V2 binds the complete reviewed
setup, compiled setup identity, solver/backend identity, convergence, exact raw
artifact identities, normalized metrics and fields, accepted refinement,
coverage, findings, and limitations. The writer consumes the already validated
active objects; it does not run the solver-evidence or finding compilers again.

Legacy v1 archives remain readable under their original narrower claims. They
are not upgraded or granted v2 convergence, setup, or finding evidence. Offline
v2 verification deliberately rehashes persisted artifacts, reconstructs the
compiled setup, reparses DAT evidence, and recompiles findings because those
bytes crossed a trust boundary.

A verified v2 archive can be relocated and embedded in a Prometheus project as
a closed content-addressed artifact graph. Reopen reconstructs and verifies it,
restores the editable reviewed setup and visualization, and compares its bound
assembly identity with the current project. Evidence bound to a changed source
remains viewable but is marked stale and cannot be rerun without renewed
geometry and surface review.

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
3. Produce acceptable YUBI mesh-refinement evidence for that exact reviewed
   setup.
4. Execute the reviewed bracket and retain a physically independent
   real-component comparison.
5. Complete the outstanding outside-user folder-screening session.

Until those items are complete, Prometheus has a bounded, fail-closed
structural workflow—not a validated YUBI result and not proof that an arbitrary
engineering project works.

## Reconciliation release gate — 2026-08-16

The local post-reconciliation gate measured:

- 45 focused SQLite Phase 5 intake/migration tests passed and one
  PostgreSQL-only case skipped as declared;
- 70 PostgreSQL 17 migration tests passed against an isolated temporary local
  database;
- all 16 headless CTests passed;
- 28 of 29 desktop CTests passed inside the managed sandbox, whose socket
  policy prevented the remaining HTTP fixture from opening a loopback listener;
- that exact loopback test passed 1 of 1 outside the socket sandbox; and
- the structural controller's single-computation test passed again in
  isolation.

These results cover the consolidated native and persistence paths. They do not
replace the still-open post-reconciliation Windows CalculiX run, reviewed YUBI
scenario, independent component comparison, or outside-user session.
