# Portable Structural Result Identity Design

**Date:** 2026-08-20
**Status:** Approved for implementation

## Problem

Structural archive replay currently hashes derived binary64 metrics into the
compiled CalculiX result identity and then requires replayed values to match
bit for bit. That rule confuses platform-specific math-library rounding with
changed engineering evidence.

The approved Linux ARM64 cantilever archive reproduces on its originating
platform, but macOS replay rejects the coarse sample with
`replay_result_identity_mismatch`. LLDB isolated the difference to maximum
displacement magnitude. Linux recorded the binary64 value
`0x3f29433a3b909a74`; macOS `std::hypot` reconstructed
`0x3f29433a3b909a75`. The values differ by one representable binary64 number.
The stress metric, setup identity, solver artifacts, backend identity, row
counts, and convergence evidence matched.

The raw CalculiX DAT row stores six decimal significant digits. A one-ULP
difference in a subsequently derived norm is many orders of magnitude smaller
than that source precision and does not represent a different solver result.
Prometheus must nevertheless continue to reject changed artifact bytes,
changed engineering decisions, and material numerical changes.

## Requirements

1. The existing ARM64 archive must verify on macOS without rewriting the
   manifest or executing CalculiX again.
2. New compiled result identities must be stable when identical raw evidence
   is replayed on supported platforms.
3. Deck, STA, DAT, FRD, stdout, stderr, setup, and backend identities remain
   exact.
4. Only explicitly identified derived numerical fields receive an equivalence
   allowance.
5. Entity selection, row coverage, convergence state, thresholds, findings,
   unknowns, and accepted/indeterminate decisions remain fail-closed.
6. Replay parses each sample once and never launches a solver.
7. Archive schemas v1 through v4 remain readable under their recorded
   semantics.

## Decision

Prometheus will separate evidence identity from derived floating-point
verification.

New completed results use
`urn:prometheus:schema:compiled-calculix-result:3.0.0` and compiler version
`calculix-evidence-compiler-v3`. The result identity binds deterministic source
evidence:

- compiled setup identity;
- request geometry identity;
- authoritative solver executable SHA-256 and version;
- exact deck, STA, DAT, FRD, stdout, and stderr identities; and
- the result schema and compiler version.

The identity document does not contain convergence summaries, displacement or
stress metrics, observable values, changes, or findings. Those values remain
mandatory replay checks against the exact raw evidence. The compiler version
binds the interpretation algorithm; a later semantic change requires another
compiler-version change.

Structural archive v4 does not change. A v4 sample can contain either a legacy
metric-bearing v2 result identity or the new evidence-root v3 identity. The
verifier determines the identity form by exact candidate matching; the archive
does not rely on an untrusted caller-authored version label.

## Numerical equivalence rule

Two finite derived binary64 values `stored` and `replayed` are equivalent when
they are exactly equal or satisfy:

```text
abs(stored - replayed)
    <= 64 * numeric_limits<double>::epsilon()
          * max(abs(stored), abs(replayed))
```

Both zero values are equal through the exact branch. NaN and infinity are
always rejected. The rule does not introduce an engineering acceptance
tolerance; it is approximately `1.42e-14` relative and applies only while
reconstructing derived values across a trust boundary. CalculiX's stored DAT
precision and the predeclared refinement thresholds remain unchanged.

The verifier applies this rule only to the following derived fields:

- v1-v4 maximum displacement and maximum von Mises metrics;
- v2-v4 replayed finding `measured` and `margin` values;
- v3 legacy displacement, stress, and maximum refinement changes;
- v4 observable coarse value, fine value, and change fraction; and
- v4 global-extremum coarse value, fine value, and change fraction.

Requirement limits, refinement thresholds, material properties, loads, mesh
controls, boundary areas retained in reviewed setup evidence, and
deck-precision extremum positions remain exact.

## New result identity flow

For an active run, the CalculiX evidence compiler continues to validate the
process exit, solver version marker, completion marker, status rows, requested
row coverage, mesh entities, and raw artifact identities. It parses and
normalizes DAT evidence once. After those checks pass, it constructs the v3
evidence-root identity without using derived metrics.

Archive writing remains unchanged above the sample boundary. New v3 result
identities populate the existing `validated_result_identity` and
`comparison.result_sha256` fields. Save, publication, and archive export reuse
the completed in-memory result and do not execute or parse the analysis again.

## Legacy replay flow

For a metric-bearing legacy result, the verifier:

1. verifies all artifact byte lengths and SHA-256 identities exactly;
2. reconstructs the legacy v2 result identity from the stored sample fields;
3. requires that reconstructed identity to equal the archived result identity;
4. compiles the raw solver evidence once on the current platform;
5. requires exact setup, backend, convergence, row-count, and artifact fields;
6. compares only the allowlisted derived metrics with the numerical
   equivalence rule; and
7. restores the archived result identity as the sample's lineage identity.

The stored metrics are not accepted merely because their legacy hash is
self-consistent. They must also agree with the values recalculated from the
exact raw DAT evidence. A caller who changes a metric and recomputes its legacy
identity therefore remains bounded by the numerical equivalence check.

Archive v1 has no compiled result identity. Its exact artifact verification is
retained, while its derived metrics and finding values use the same allowlisted
numeric comparison. Archive v2 uses the single-result legacy path. Archive v3
and v4 use the shared two-sample path.

## Comparison and finding replay

Replay recompiles boundary correspondence, observables, refinement, coverage,
findings, and unknowns from the two verified samples. It compares persisted and
replayed structures field by field rather than applying a generic tolerant JSON
comparison.

The following remain exact:

- schema and compiler identities;
- artifact, setup, result, geometry, and observable-definition hashes;
- backend identity and version;
- sample roles, convergence integers and times, entity IDs, row counts, and
  selected-row counts;
- observable quantities, reductions, regions, and thresholds;
- global-extremum participation flags and entity locations;
- accepted or indeterminate statuses;
- finding obligations, dispositions, limits, units, scopes, assumptions, and
  evidence identities;
- coverage counts, unknown codes and details, and limitations.

Only the allowlisted derived values use numerical equivalence. If platform
rounding changes an extremum entity, threshold decision, finding disposition,
coverage result, or unknown reason, replay fails closed. Replayed normalized
rows remain available for visualization. Legacy lineage retains the exact
archived identity after its compatibility checks pass.

## Diagnostics

The verifier will distinguish these failures:

- `replay_result_identity_mismatch`: neither the new evidence-root identity nor
  the reconstructed legacy identity matches;
- `replay_numeric_mismatch`: an allowlisted derived value exceeds the numerical
  equivalence bound; and
- the existing contract, setup, artifact, refinement, and finding mismatch
  codes for all nonnumeric or decision-level failures.

Numeric mismatch detail names the JSON-style field path and prints the stored
and replayed values at round-trip precision. A mismatch never becomes an
accepted finding.

## Verification

Implementation will proceed test-first.

The primary regression fixture will start from a small valid structural
archive. Its copied manifest will represent a coherent legacy archive produced
by an alternate math library: one stored displacement maximum will move by one
binary64 step, and the helper will recompute the affected legacy result
identity, result-lineage references, observable/global values, changes, and
finding values. Exact raw artifacts remain unchanged. This fixture must fail
before the compatibility implementation and pass afterward.

Additional tests will require:

- a coherent derived difference within the bound to pass;
- a coherent derived difference beyond the bound to fail with
  `replay_numeric_mismatch`;
- new v3 result identities to remain unchanged when only an in-memory derived
  metric changes while source evidence is unchanged;
- changes to artifacts, thresholds, entities, counts, statuses, findings, or
  unknown reasons to remain rejected;
- existing v1-v4 compatibility and tamper suites to pass; and
- active-run archive creation and replay to retain one parse per sample and no
  solver call during Save, publication, or replay.

After focused tests pass, the release check runs the complete headless and
desktop-no-OCCT suites once. The existing Linux ARM64 cantilever archive is
then replayed with the macOS binary. Its manifest and all INP, DAT, FRD, and
STA hashes must remain unchanged. CalculiX will not run during this checkpoint.

## Non-goals

This change does not add a structural results UI, create archive v5, alter a
material property or engineering threshold, generate a mesh, run Windows
validation, or execute another solver study. It fixes portable identity and
replay semantics only.
