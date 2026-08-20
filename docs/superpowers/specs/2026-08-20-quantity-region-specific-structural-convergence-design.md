# Quantity- and Region-Specific Structural Convergence Design

**Date:** 2026-08-20  
**Status:** Proposed for implementation

## Problem

The current refinement compiler compares two solver-wide extrema:

- maximum nodal displacement; and
- maximum element von Mises stress.

That rule is fail-closed, but it treats every extreme as the same kind of
observable. The approved 80 x 12 x 12 and 120 x 18 x 18 cantilever runs show
why this is insufficient. Maximum displacement changes by about 2.18%, while
the global stress maximum changes by about 10.34%. Stress in the same physical
section away from the idealized fixed boundary changes by about 2.35%.

The global stress peak is at the fixed boundary. It is important evidence and
must remain visible, but it is not a stable observable for this benchmark pair.
Allowing that peak to disappear would be dishonest; allowing it to veto every
other converged quantity would make the benchmark uninformative. Prometheus
therefore needs to state exactly which quantity and physical region each
refinement claim covers.

This change must not weaken real-project structural checks, turn a
nonconverged peak into a pass, or execute CalculiX or parse its outputs twice
when the user saves or publishes a result.

## Decision

Prometheus will compile refinement from predeclared typed observables. Each
observable contains:

- a stable observable ID;
- a supported physical quantity;
- a deterministic reduction;
- a physical evaluation region;
- a maximum allowed coarse-to-fine relative change; and
- a canonical identity included in the locked refinement criterion.

The refinement compiler evaluates every observable from each sample's already
normalized, setup-bound result. A comparison is accepted only when every
required observable is complete and within its own threshold. Missing rows,
an empty region, duplicate rows, an unknown mesh entity, or a non-finite value
leaves the comparison indeterminate and cannot create a finding.

Global extrema remain in the normalized CalculiX result and archive even when
they are not acceptance observables. Prometheus reports their coarse/fine
changes as diagnostics and explicitly says whether each one participated in
acceptance.

## Alternatives considered

### Keep solver-wide extrema only

This requires the cantilever's clamp-edge peak to converge before any
benchmark conclusion. It is simple, but it cannot distinguish a stable
engineering observable from a boundary-sensitive extreme. Rejected.

### Add a cantilever-only exception

A special benchmark function could ignore the root peak. This is quick, but
the exception would not generalize to holes, contacts, point loads, sharp
corners, or other reviewed regions. It would also create a second,
benchmark-specific meaning of convergence. Rejected.

### Typed quantity and region observables

The shared refinement compiler evaluates declared observables for benchmarks
and real projects. This adds a small contract and evaluator while retaining one
solver and one normalized-result path. Selected.

An integrated cross-sectional force/moment resultant is a useful future
observable, but it requires tensor transformation, face/section integration,
and orientation contracts. It is deliberately deferred. The first
implementation uses a regional maximum that can be reproduced exactly from
the existing C3D4 element-centroid stress rows.

## Supported observable contract

The first version supports two quantities:

- `displacement_magnitude_m`, evaluated from normalized nodal displacement
  rows; and
- `von_mises_stress_pa`, evaluated from normalized C3D4 element stress rows.

The first version supports only the `maximum` reduction. This keeps the result
semantics explicit and avoids prematurely adding averages or percentiles whose
physical weighting would need separate review.

Regions are typed rather than free-form expressions:

- `all_nodes` for displacement;
- `all_elements` for stress; and
- `element_centroid_box_m` for stress, with finite inclusive minimum and
  maximum x, y, and z coordinates in the reviewed mesh coordinate frame.

An element belongs to a centroid box when the arithmetic mean of its four
reviewed node coordinates lies within all three inclusive bounds. Both meshes
evaluate the exact same SI-coordinate bounds from the locked criterion. Node
or element IDs are never copied between meshes to imply correspondence.

The compiler rejects duplicate observable IDs, an empty observable list,
unsupported quantity/region combinations, non-finite bounds, reversed bounds,
and thresholds outside `(0, 1]`. Canonical JSON includes every field in a
fixed order, so changing the quantity, region, reduction, or threshold changes
the criterion identity.

## Evaluation and coverage

For each sample, the evaluator:

1. consumes the existing `CompiledStructuralSetup` and
   `CompiledCalculixResult` objects;
2. verifies that the result is complete and bound to that setup;
3. maps normalized node or element rows to reviewed mesh entities;
4. selects rows using the declared region;
5. requires exact row coverage for every selected entity supported by the
   current C3D4 contract; and
6. computes the declared maximum once.

The pair compiler then computes:

```text
abs(fine - coarse) / max(abs(fine), abs(coarse))
```

with zero change when both values are zero. Each derived observation records
the definition identity, coarse value, fine value, coarse/fine selected-row
counts, relative change, threshold, and accepted/indeterminate status.

The overall refinement is accepted only when all declared observations are
accepted. A valid pair whose observations exceed their thresholds remains
archivable but has an indeterminate engineering evaluation.

## Cantilever validation profile

The approved benchmark mesh pair remains:

- coarse: 80 x 12 x 12 structured cells; and
- fine: 120 x 18 x 18 structured cells.

The locked convergence threshold remains 10% to preserve the criterion that
was selected before the diagnostic runs. The benchmark declares:

1. `cantilever.maximum_displacement`: maximum displacement magnitude over
   `all_nodes`; and
2. `cantilever.section_von_mises`: maximum von Mises stress for elements whose
   centroids are inside the physical box
   `x=[0.100, 0.125] m`, `y=[0.000, 0.100] m`, and
   `z=[-0.050, 0.050] m`.

The box is identical for both meshes and spans the full cross-section. Its x
window starts one beam depth from the idealized clamp. That location is a
benchmark-specific declared choice, not a universal rule for real projects.

The benchmark report also retains:

- global maximum displacement and its change;
- global maximum von Mises stress and its change;
- the element IDs and coordinates associated with extrema where available;
- a diagnostic stating that the clamp-edge stress maximum exceeded the 10%
  refinement threshold; and
- the fact that this global peak did not participate in the regional stress
  acceptance decision.

The report must not call the root peak a mathematical singularity based on two
meshes alone. It calls the peak `not_converged_in_this_study` and leaves its
engineering interpretation unresolved.

## Real-project behavior

Existing project setups continue to create the conservative default profile:

- global maximum displacement over all nodes; and
- global maximum von Mises stress over all elements.

Therefore the YUBI workflow and other real-project runs do not automatically
inherit the cantilever's regional stress window. A real-project regional
observable must be explicitly created from a reviewed physical region in a
later UI increment. Until that review surface exists, a nonconverged global
YUBI stress peak remains indeterminate.

Refinement acceptance and requirement coverage remain separate. A converged
regional benchmark observable does not authorize a global real-component
stress finding. The finding compiler emits an obligation only when its
quantity and scope are supported by an accepted observable. Unsupported or
unmatched obligations remain declared but unevaluated with a stable reason.

## Archive and replay

The changed closed contract requires structural archive v4. New v4 archives
store:

- the locked observable definitions and identities;
- the derived per-observable values, row counts, changes, and statuses;
- both samples and all existing raw solver artifacts;
- global-extrema diagnostics and whether they participated in acceptance;
- coverage, findings, unknowns, and limitations.

The v4 verifier reconstructs both setups and normalized results from raw
evidence, reevaluates the declared observables, recompiles the comparison and
findings, and requires exact agreement with the manifest. Existing v1, v2, and
v3 archives remain readable under their original semantics and are never
relabelled as v4 evidence.

## Single-computation invariant

During an active run:

- CalculiX executes exactly once for the coarse mesh and once for the fine
  mesh;
- each output set is parsed and normalized exactly once;
- observable extraction consumes those in-memory normalized results;
- saving and archive creation serialize the compiled observations without
  rerunning CalculiX or reparsing DAT files; and
- project publication installs immutable archive objects without repeating
  engineering calculations.

Replay is allowed only when immutable evidence crosses a trust boundary, such
as opening or importing an archive. That verification is not part of the
interactive save path and is cached for the restored immutable session.

## Failure semantics

Stable new diagnostics include:

- `refinement_observable_invalid`;
- `refinement_observable_duplicate`;
- `refinement_region_invalid`;
- `refinement_region_empty`;
- `refinement_observable_row_missing`;
- `refinement_observable_row_duplicate`;
- `refinement_observable_entity_unknown`;
- `refinement_observable_nonfinite`; and
- `refinement_observable_not_converged`.

Contract errors prevent a baseline from starting. Evidence/coverage errors
make a completed pair invalid or indeterminate and produce no finding.
Above-threshold valid observations produce an indeterminate comparison rather
than a malformed-evidence error.

## Verification

Implementation will proceed test-first and cover:

- criterion identity changes for quantity, region, reduction, and threshold;
- invalid and duplicate observable definitions;
- deterministic inclusive centroid-box selection on both meshes;
- empty, missing, duplicate, unknown-entity, and non-finite result rows;
- unchanged conservative global defaults for real-project setups;
- an accepted synthetic pair with converged displacement and regional stress;
- an indeterminate pair when either required observable exceeds its threshold;
- no global stress finding when only regional stress converged;
- v4 write/replay and tamper rejection for every observable field;
- v1-v3 archive compatibility; and
- a benchmark command that runs each solver sample once and reports the
  nonconverged global clamp peak separately.

The expensive 120 x 18 x 18 mesh construction and native CalculiX execution
remain out of the ordinary unit-test suite. Unit tests use small deterministic
fixtures; the approved real pair runs once at the explicit structural
validation checkpoint.
