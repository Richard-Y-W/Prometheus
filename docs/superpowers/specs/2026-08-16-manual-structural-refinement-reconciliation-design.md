# Manual Structural Refinement Reconciliation Design

**Date:** 2026-08-16
**Status:** Implemented and locally verified

## Problem

Before this increment, the reconciled structural branch had removed
QML-authored refinement flags, result hashes, and convergence deltas. That
removal closed a path by which presentation data could create findings, but it
also left the production desktop with no valid path from a completed solve to a
publishable structural archive. `LocalStructuralBackend` evaluated each run
without refinement, so its archive-writing condition could not be reached.

The remaining refinement object was also too weak for offline replay. Its
public fields let a caller state that a comparison was complete, supply two
result hashes, and claim an acceptable change. A v2 archive verifier
reconstructed the active result but trusted the claimed second result and
change. It therefore could not establish that two completed analyses used the
same reviewed scenario or that the second mesh was a legitimate refinement of
the first.

This design closes those two reconciliation gaps. It does not add automatic
meshing or another structural-analysis authority.

## Decision

The desktop will use a sequential, manually supplied coarse/fine workflow:

```text
load and review coarse mesh -> solve once -> retain immutable baseline
                                              |
load and review fine mesh   -> solve once -> compile verified comparison
                                              |
                              findings or honest indeterminate result
                                              |
                               archive both samples -> publish
```

The engineer supplies both Gmsh/Abaqus tetrahedral mesh files. Prometheus
prepares, reviews, and executes each mesh once. A Qt-free pair compiler derives
refinement evidence from the two validated results. The UI cannot submit a pass
flag, a result identity, or a calculated change.

This boundary is deliberately independent of mesh origin. A future mesher
adapter can produce the same two prepared-mesh inputs without changing setup
compilation, solver execution, comparison, findings, archive replay, or project
publication.

## User-visible workflow

### Coarse baseline

The engineer loads the coarse mesh, selects load and restraint surfaces,
reviews the material and requirements, confirms the bounded scenario, and
chooses the maximum allowed coarse-to-fine change. Prometheus locks that
criterion before starting the coarse solve.

A successful coarse solve produces an immutable baseline. It does not produce
a pass or violation finding by itself. The panel reports `baseline_ready` and
labels the solve as “execution completed; engineering evaluation pending.”

### Fine candidate

After the baseline is retained, the engineer loads the fine mesh. The panel
copies and locks the baseline's geometry identity, analysis identity,
component, material, force vector, requirements, and scenario text. The
engineer must independently select and review the load and restraint surfaces
on the fine mesh and review its mesh controls.

Before execution, Prometheus checks that the fine mesh has a distinct source
identity, more volume elements, and a smaller reviewed target size. These
checks prevent an identical or explicitly coarser mesh from consuming a solver
run under the fine label.

The panel displays the coarse and fine selected load/restraint areas together
and requires a separate confirmation that the fine selections represent the
same physical regions as the retained baseline. Because arbitrary external
meshes do not carry stable CAD-face identities, Prometheus records this
correspondence as a human-reviewed assumption. It does not claim to prove that
correspondence from mesh topology.

### Comparison and publication

After the fine solve completes, the pair compiler validates lineage and derives
the displacement and stress changes. An accepted comparison can create scoped
findings. A valid comparison above its predeclared threshold creates an
`indeterminate_refinement` evaluation with zero pass/violation findings. Both
accepted and indeterminate comparisons can be archived and embedded so failed
refinement evidence is not discarded.

An explicit “Discard baseline and start over” action clears both stages.
Changing shared physics requires this reset and a new coarse run. Replacing the
fine mesh or changing its mesh-specific selections retains the baseline but
invalidates the fine result and downstream comparison.

## State ownership

The controller owns one sequential state machine:

```text
coarse_setup
    -> executing_coarse
    -> baseline_ready
    -> fine_setup
    -> executing_fine
    -> comparison_accepted | comparison_indeterminate
```

A coarse failure returns to `coarse_setup`. A fine failure returns to
`fine_setup` while retaining the immutable baseline. A lineage or ordering
failure blocks the fine run when it can be detected before execution; a
backend or result mismatch detected afterward leaves both raw outputs visible
but creates no comparison or archive.

The controller exposes stored presentation maps to QML. QML property reads,
repaints, and result visualization do not invoke mesh preparation, setup
compilation, solver-result parsing, comparison, or finding compilation.

## Typed contracts

### Predeclared criterion

`StructuralRefinementCriterion` contains the maximum allowed change fraction
and canonical identity. The criterion must be finite, greater than zero, no
greater than one, and locked before baseline execution. The fine stage cannot
replace it.

### Completed sample

`CompletedStructuralSample` contains:

- its coarse or fine role;
- the immutable compiled structural setup;
- the completed `SolverRunResult` and `CompiledCalculixResult`;
- mesh source identity, target size, node count, and element count; and
- the locked criterion identity.

The sample is produced only from a completed, identity-bound execution. It does
not contain a caller-supplied refinement verdict.

### Reviewed boundary correspondence

`ReviewedBoundaryCorrespondence` binds the coarse and fine compiled-setup
identities to the engineer's confirmation that their load and restraint
selections represent the same physical regions. It also records both selected
areas for display and replay. This review statement supplies engineering
meaning that cannot be inferred from arbitrary meshes; it does not supply the
comparison delta, result identities, or accepted status.

### Verified comparison

The former public `StructuralRefinementEvidence` aggregate is replaced by a
type whose comparison fields cannot be assigned by callers. A Qt-free factory
accepts two completed samples plus their reviewed boundary-correspondence
record and either returns a verified comparison or typed diagnostics.

The factory requires exact agreement on:

- analysis, component, and project geometry identities;
- material designation, temper, product form, applicability, source identity,
  elastic modulus, and Poisson ratio;
- total force vector and reviewed load meaning;
- displacement and stress requirements, their bases, and review state;
- scenario description and confirmation;
- mesh coordinate scales and the request schema's SI-unit contract;
- the locked refinement criterion; and
- authoritative solver executable identity and version.

Both boundary selections and both mesh-control records must be independently
reviewed, and the boundary-correspondence record must bind the two setup
identities. Mesh identities must differ; the fine setup must contain more
elements and a smaller target size. This first workflow therefore excludes a
local-only refinement whose total element count does not increase. Both
compiled results must be complete and bound to their respective compiled setup
identities.

The factory computes, for displacement and von Mises stress,

```text
abs(fine - coarse) / max(abs(fine), abs(coarse))
```

with zero change when both values are zero. The comparison value is the larger
of the two metric changes. The criterion is satisfied only when that derived
value is finite and no greater than the locked maximum.

The resulting type retains the two immutable completed samples and exposes
their setup/result identities, derived metric changes, maximum change,
criterion, and accepted/indeterminate status. Its construction boundary
prevents UI data or parsed archive JSON from asserting these fields.

## Findings

The finding compiler accepts a verified comparison, not an optional mutable
evidence aggregate. It evaluates the fine result because that result belongs
to the more resolved mesh. It emits findings only when:

- the comparison is valid and meets its criterion;
- the fine result remains complete;
- all requirement and scenario review conditions remain true; and
- each finding's evidence contains both setup and both result identities.

A valid comparison that misses the criterion reports all declared obligations
as unevaluated. It is evidence that the current discretization study did not
support a mesh-stable engineering conclusion, not evidence that the component
passed or failed its requirements.

## Archive v3 and replay

New writes use
`urn:prometheus:schema:structural-run-archive:3.0.0`. The v3 manifest contains
two closed sample records. Each record references its canonical reviewed setup,
deck, DAT, FRD, STA, standard output, and standard error, together with the
captured execution, backend, convergence, metrics, and artifact identities.
The manifest also records the locked criterion, independently reviewed
boundary-correspondence statement, derived comparison, coverage, findings,
and limitation.

The archive writer accepts the already compiled comparison and its two samples.
Before copying mutable source files, it rehashes them for integrity. It does not
rerun CalculiX, parse solver output again, or compile findings again.

After an archive crosses a trust boundary, the v3 verifier:

1. checks the canonical manifest and exact closed key sets;
2. verifies every artifact length and SHA-256;
3. reconstructs and recompiles both reviewed setups;
4. reconstructs both validated CalculiX results from their raw artifacts;
5. runs the pair compiler on those reconstructed samples;
6. regenerates evaluation status, coverage, and findings; and
7. requires the regenerated comparison and findings to equal the manifest.

Changing a stored pass status, delta, setup, result identity, or raw artifact
therefore invalidates replay. Persisted derived fields are displayable records,
not replay authority.

The reader retains explicit v1 and v2 dispatch. Those archives remain readable
under their original contracts and are not relabeled as containing a replayed
two-result comparison. New desktop runs do not write v2.

Project embedding, reconstruction, relocation, and schema allowlists gain v3
without removing v1/v2 compatibility.

## Single-computation invariant

For an active coarse/fine session:

- each mesh is read and prepared once;
- each setup is compiled once for its immutable reviewed input;
- CalculiX executes once per sample;
- each solver output set is parsed and validated once;
- the pair comparison and findings are compiled once;
- archive creation serializes those existing objects; and
- project publication installs the archive graph without replaying engineering
  calculations.

Integrity hashing may read mutable files when they enter an archive or object
store. That read detects byte substitution; it is not a second FEA or result
interpretation. Packaging continues to hash and chunk each file in one stream.

Reopening, importing, restoring, or relocating an archive deliberately
reverifies both samples because persisted bytes have crossed a trust boundary.
That replay is cached for the immutable restored session.

## Failure and status semantics

The desktop uses distinct execution and engineering states:

- `execution_completed_evaluation_pending`: one solve completed, but no paired
  engineering evaluation exists;
- `comparison_accepted`: the two completed results met the locked refinement
  criterion and scoped findings were compiled;
- `comparison_indeterminate`: the pair is valid but missed the criterion, so
  zero obligations were evaluated;
- `execution_failed`: the authoritative backend did not produce a complete
  result; and
- `comparison_invalid`: ordering, lineage, boundary review, backend identity,
  or result evidence did not support a comparison.

Only `comparison_accepted` uses the success color. Pending and indeterminate
states use a neutral or amber presentation. Parser errors, missing artifacts,
invalid ordering, result-coverage gaps, solver failure, or refinement failure
cannot produce a pass.

Stable diagnostics include:

- `refinement_baseline_required`;
- `refinement_criterion_invalid`;
- `refinement_mesh_identity_reused`;
- `refinement_mesh_not_finer`;
- `refinement_lineage_mismatch`;
- `refinement_boundary_review_required`;
- `refinement_backend_mismatch`; and
- `refinement_result_incomplete`.

An absent or invalid criterion blocks baseline execution with
`refinement_criterion_invalid`. Exceeding a valid locked criterion produces an
indeterminate evaluation rather than a malformed-pair diagnostic.

## Future automatic mesh generation

A later mesher adapter may consume reviewed geometry plus coarse/fine mesh
controls and emit two `PreparedMesh` objects. It must record its executable or
library identity, version, settings, and output hashes. Stable CAD-face mapping
may prepopulate boundary selections, but the engineer must confirm those roles
unless the adapter can preserve and verify exact source-face identities.

The mesher adapter ends at the prepared-mesh boundary. It does not change the
two-sample compiler, solver authority, archive v3, replay, or finding rules.
Automatic generation still requires two solves for a two-mesh refinement
study.

## Acceptance tests

Implementation is accepted when tests establish all of the following:

1. The production controller runs a reviewed coarse mesh once and a reviewed
   fine mesh once, derives accepted refinement, creates a v3 archive, and
   publishes it through `StructuralController::commitLastRun()`.
2. Repeated property reads, result display, archive writing, export, and
   publication do not increment solver or evidence-compiler counts.
3. Fine-mesh edits retain the baseline, while shared-physics edits require a
   new baseline.
4. Pair compilation rejects identical mesh identities, non-finer candidates,
   lineage mismatches, backend mismatches, incomplete results, and unreviewed
   boundaries.
5. A valid above-threshold pair produces a replayable indeterminate v3 archive
   with zero findings.
6. Offline replay reconstructs both results and independently regenerates
   ordering, changes, status, coverage, and findings.
7. Tampering with a stored delta, status, identity, setup, or raw artifact
   fails verification.
8. V1 and v2 fixtures remain readable under their original claims; only v3
   returns the two-result replay claim.
9. QML cannot submit refinement booleans, deltas, result identities, or a
   post-baseline threshold.
10. A single completed run is displayed as evaluation pending rather than an
    engineering success.
11. The streaming SHA-256 API rejects chunk sizes that exceed the stream
    interface's representable range.

Focused tests run during implementation. Before push readiness is reported,
one release checkpoint runs the full structural, desktop, run-store, headless,
and backend suites, followed by CMake preset and diff-hygiene checks. Platform
CI remains the cross-platform release gate; it is not repeated after each
small implementation edit.

## Excluded from this increment

- Automatic coarse or fine mesh generation.
- Automatic proof that separately selected mesh patches represent the same CAD
  face.
- A second structural solver, nonlinear analysis, contact, fatigue, buckling,
  or fastener analysis.
- Treating an accepted two-mesh comparison as proof of safety or
  project-level correctness.
- Running or reporting a YUBI bracket result without its separately reviewed
  loads, restraints, material applicability, requirements, and mesh study.
