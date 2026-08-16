# Mainline Structural Reconciliation Design

## Decision

Prometheus will retain the structural workflow now present on `main` as the
single product path. The stricter validation and reproducibility work on
`feature/phase3-structural-rover-gates` will be integrated into that path. The
merge will not preserve a second structural controller, setup panel, solver
result compiler, or finding authority.

This is a behavioral reconciliation, not an ours-versus-theirs file choice.
Feature-branch behavior is removed only after its required test has been moved
to the mainline authority and passes there.

## User-visible outcome

The desktop exposes one structural setup and execution workflow:

```text
project evidence
    -> prepared mesh and selectable boundary
    -> reviewed material, load, restraint, requirement, and scenario
    -> validated execution package
    -> isolated CalculiX execution
    -> validated solver evidence
    -> scoped findings
    -> immutable project archive
```

There is one Run action and one set of blockers. A project cannot be accepted
by a permissive path while being rejected by a stricter parallel path.

## Authority and ownership

Each stage owns one calculation and emits an immutable result for downstream
consumers.

| Stage | Authority | Immutable output | Consumers |
| --- | --- | --- | --- |
| Component evidence | Backend v2 review and publication services | Published component revision and package identity | Future component-to-structural adapter |
| Mesh preparation | Qt-free `prometheus_structural` library | Prepared mesh, diagnostics, exterior faces, source labels, and patch data | Structural controller and setup compiler |
| Setup compilation | Qt-free structural setup compiler | Validated structural request and reviewed setup evidence | Solver runner and archive writer |
| Solver execution | Isolated CalculiX runner | Captured process status and immutable raw output artifacts | Solver-evidence compiler |
| Solver-evidence compilation | Qt-free result compiler | Validated fields, extrema, convergence, backend identity, and artifact identities | Finding compiler, UI, and archive writer |
| Finding compilation | Qt-free finding compiler | Scoped evaluation with coverage and limitations | UI and archive writer |
| Persistence and replay | Mainline structural archive and project store | Versioned immutable archive graph | Reopen, restore, export, and offline verification |
| Presentation | `StructuralController` and `StructuralSetupPanel.qml` | Display state only | User |

The UI does not calculate engineering results. The archive writer does not
rerun the solver-evidence compiler. Both consume the same immutable validated
objects returned by the owning Qt-free stage.

## Single-computation invariant

Time-consuming work must not run twice merely because the user views a result
and then saves it. The following operations execute once for each immutable
input identity:

- reading and parsing a large mesh;
- checking tetrahedral orientation, connectivity, and quality;
- extracting the exterior boundary and measuring surface geometry;
- grouping boundary faces for a particular patch angle;
- hashing large source, deck, solver, and result artifacts;
- parsing displacement and stress fields;
- parsing convergence evidence and checking exact mesh-result coverage;
- compiling extrema, coverage, and findings.

Each expensive stage has a content-derived key:

- mesh preparation: mesh-byte SHA-256, coordinate scale, parser version, and
  mesh-validation version;
- patch grouping: prepared-mesh identity, patch angle, and grouping version;
- setup compilation: prepared-mesh identity plus canonical reviewed inputs and
  selected exact face identities;
- solver evidence: validated request/deck identity, solver executable identity,
  solver version, raw-output identities, and evidence-compiler version;
- findings: validated solver-evidence identity plus canonical obligations and
  refinement evidence.

The controller retains these immutable outputs for the active project. Editing
an input invalidates only that stage and its downstream dependents. For
example, changing a force invalidates setup, execution, and findings but does
not reparse the mesh. Changing the patch angle regroups prepared boundary
faces but does not recompute tetrahedral quality.

Saving a run installs or references the already validated artifacts and writes
the manifest from the existing validated result. It does not reparse the mesh,
reparse solver output, recompile findings, or rerun CalculiX. Large solver
outputs are streamed into immutable content-addressed storage while their
identities are computed, so later publication can reference those identities
without hashing the same bytes again in the active operation.

Reverification is required after a real trust-boundary transition: reopening a
project, importing an external archive, restoring a portable bundle, or
detecting that an artifact may have changed outside Prometheus. That
reverification is a new operation over untrusted persisted bytes, not a hidden
duplicate of the original screen-to-save flow. Its result is cached for the
remainder of that immutable session.

Cheap form checks may run when edited fields change, but property reads,
repaints, preview rendering, archive writing, and project saving cannot invoke
an expensive engineering stage.

## Structural contract reconciliation

### Mesh and surfaces

Mainline's exterior faces derived from the volume tetrahedra remain the
authoritative boundary because they do not trust optional mesher surface
exports. Feature-branch Gmsh physical and `ELSET` names are retained as
non-authoritative labels that help an engineer find candidate surfaces. A
label never assigns load, restraint, contact, or fastener meaning.

Mesh preparation incorporates the feature branch's fail-closed checks for:

- duplicate or invalid node and element identities;
- non-finite coordinates;
- zero-volume or inverted tetrahedra;
- face-disconnected volume regions;
- non-manifold exterior faces;
- invalid or duplicate source surface triangles;
- source triangles that are not part of the derived exterior boundary; and
- measured mean-ratio quality below the reviewed threshold.

### Reviewed setup and requests

Mainline's `StructuralSetup` remains the reviewed domain object. Its compiled
request gains the provenance needed to prevent a solver deck from being
detached from reviewed mesh, material, load, and requirement evidence.
Validation incorporates the feature branch's strict lowercase SHA-256 checks,
safe heading text, nonzero resultant load, duplicate nodal-force rejection,
reviewed-force reproduction, normalized direction, explicit limit bases, mesh
identity, mesh controls, and material applicability.

The new Phase 5 manual component intake remains unchanged during this merge.
Standalone YUBI material records remain bounded evidence fixtures; they do not
become a competing component database. Binding a published component package
to a structural setup is a separate follow-up increment after reconciliation.

### Execution and solver evidence

Mainline's shell-free child-process runner remains the only CalculiX execution
path. Its output is passed once to the strengthened result compiler, which
requires:

- successful process launch and exit code zero;
- an exact solver executable SHA-256 and a safe captured version;
- no timeout or pre-existing output collision;
- the standalone CalculiX completion marker and no reported fatal error;
- a completed final `.sta` step at the requested time;
- an input deck identical to the validated request's generated deck;
- finite displacement and stress values;
- exactly one displacement result for every submitted node;
- expected stress integration-point identities for every submitted element;
  and
- no missing, duplicate, or foreign result identity.

A failed condition produces no metrics and no pass or violation finding.

### Findings and refinement

Mainline's project-facing finding and coverage model remains authoritative. It
is strengthened so a structural obligation is evaluated only when solver
evidence is complete and the required refinement evidence satisfies its
predeclared criterion. A value strictly below its reviewed limit can produce
`no_violation_detected_within_scope`; a value equal to or above the limit is a
violation. Missing limits remain unevaluated.

The finding retains exact evidence identities, assumptions, the bounded
linear-static scope, and explicit exclusions for fatigue, buckling, contact,
fasteners, nonlinear behavior, safety, and project-wide correctness.

### Archives and compatibility

New runs use a versioned archive contract that records the strengthened solver
identity, convergence evidence, artifact identities, exact field coverage,
refinement evidence, and findings. Existing mainline archives remain readable
through their existing verifier. They are not silently upgraded or granted
evidence they never contained.

The archive writer accepts a validated execution result and serializes it. It
does not reconstruct that result by independently reparsing the same files.
Offline replay deliberately recompiles from persisted bytes because those
bytes have crossed a trust boundary.

## Desktop reconciliation

`StructuralController` and mainline's `StructuralSetupPanel.qml` remain. The
feature branch's duplicate `StructuralSetupController` and panel are removed
after transferring their required behaviors:

- mesh and selected-surface highlighting;
- visible selected area and compiled resultant force;
- material evidence candidates with assumption labeling;
- mesh quality and source identity display;
- exact blocker codes;
- invalidation of scenario confirmation after a reviewed input changes; and
- a reviewed request/package preview.

Repeated QML property access must return stored presentation data. It cannot
trigger mesh preparation, setup compilation, solver-result parsing, or finding
compilation.

## Non-conflicting feature work

The deterministic JPL Rover intake trial, outside-user screening package,
Windows structural checkpoint, material evidence fixtures, and associated
documentation remain independent gates. They are merged without creating a
second structural runtime.

## Conflict-resolution order

1. Merge `origin/main` and preserve the Phase 4 and Phase 5 backend,
   persistence, and migration changes unchanged where the feature branch has
   no overlap.
2. Consolidate mesh, surface, setup, and request types around mainline's
   product path.
3. Move feature-branch fail-closed tests to the consolidated structural
   library before removing duplicate implementations.
4. Consolidate solver execution, evidence compilation, findings, and the
   versioned archive contract.
5. Consolidate the desktop controller and panel, retaining the mainline
   project lifecycle.
6. Merge the external trial gates and reconcile status documentation.
7. Remove superseded duplicate files only after the consolidated tests cover
   their required behavior.

## Verification

Focused tests must prove the single-computation invariant, not merely infer it
from code structure:

- load, preview, run, display, save, and project publication invoke mesh
  preparation once for one mesh identity;
- the same flow parses and validates solver evidence once;
- repeated QML reads and repaint events invoke no engineering stage;
- changing only reviewed force fields does not reparse or remeasure the mesh;
- changing only patch angle reruns grouping but not mesh parsing or quality;
- saving and exporting an active validated run do not reparse solver output or
  recompile findings;
- reopening or importing the archive performs one deliberate verification and
  detects changed bytes;
- failure at any strict evidence rule produces zero evaluated obligations;
- legacy archives remain readable without being relabeled as strengthened
  evidence; and
- the Phase 5 migration/API suite, structural suite, run-store suite, desktop
  suite, headless suite, CMake preset checks, and diff hygiene all pass before
  push readiness is reported.

Tests may inject stage functions or use existing process fixtures to count
calls. Production APIs will not expose mutable counters or test-only bypasses.

## Excluded from this reconciliation

- Running or claiming a real YUBI bracket result without user-reviewed loads,
  restraints, limits, material applicability, and comparison evidence.
- Binding Phase 5 published component packages to CAD or structural setups.
- Adding another structural solver or nonlinear analysis.
- Persistent cross-session performance caches beyond the immutable object
  identities already required for archives and projects.
- Treating an imported surface label or component parameter as reviewed
  engineering meaning without explicit user confirmation.
