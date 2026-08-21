# Portable Structural Result Identity Design

**Date:** 2026-08-20
**Status:** Implemented and locally verified

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

## Verification record

Implementation proceeded through four recorded red-green cycles. The first
failure established that production still emitted the metric-bearing v2
identity. Subsequent failures isolated legacy identity replay, sample metrics,
and two-sample comparisons before the corresponding production changes were
made.

The primary regression fixture starts from a small valid structural archive.
Its copied manifest represents a coherent legacy archive produced by an
alternate math library: one stored displacement maximum moves by one binary64
step, and the helper recomputes the affected legacy result
identity, result-lineage references, observable/global values, changes, and
finding values. Exact raw artifacts remain unchanged. The fixture failed at
the exact comparison before the compatibility change and passed afterward.

The focused structural test also covers:

- a coherent derived difference within the bound to pass;
- a coherent derived difference beyond the bound to fail with
  `replay_numeric_mismatch`;
- new v3 result identities to remain unchanged when only an in-memory derived
  metric changes while source evidence is unchanged;
- changes to artifacts, thresholds, entities, counts, statuses, findings, or
  unknown reasons to remain rejected;
- existing v1-v4 compatibility and tamper suites to pass; and
- active-run archive creation and replay retaining one parse per sample and no
  solver call during Save, publication, or replay.

The final local checkpoint produced these results on macOS:

- focused `prometheus_structural_tests`: 1/1 passed in 4.09 seconds;
- complete headless suite: 17/17 passed in 53.54 seconds;
- desktop-no-OCCT suite: 29/31 passed inside the managed socket sandbox; the
  two listener-bound tests, `prometheus_exact_package_download` and
  `prometheus_component_binding_controller`, then passed 2/2 when rerun
  outside that socket sandbox; and
- unchanged Linux ARM64 archive replay: exit code 0 with
  `status=verified`, maximum displacement `0.000197038 m`, maximum von Mises
  stress `6.83325e+06 Pa`, and 1/2 evaluated obligations.

The one-ULP variants passed for archive-v1 metrics and findings, archive-v2
metrics and findings with a recomputed historical identity, archive-v3
comparison values, and archive-v4 samples, comparisons, global extrema, and
findings. Coherent `1.0e-10` relative changes failed with
`replay_numeric_mismatch`. Existing threshold, selected-row, entity,
participation, status, unknown, artifact, and setup tamper cases remained
rejected.

The ARM64 replay retained these SHA-256 values before and after execution:

- coarse INP: `e31d297471576b3245e33f228720de4420e49de8b50144e4ae4b613ba9dc2a84`;
- coarse DAT: `6fad931d8a3620f0404801d158253989d9ce4c6dfa3b920eeae7257f70ad0fc3`;
- coarse FRD: `f3b73d723d5fc053964c5074f88e07cb75e414b6c6d54da20de85e78a9bd1ed3`;
- coarse STA: `01e324ec0ea28f4d1fd7f5e2da1277af53812b873e218be1604c17a4c8e4990d`;
- fine INP: `43443a33266ffbb5e3804c3420e91dba7d4fcbee34f6cfbef4918b4d9f95e5d1`;
- fine DAT: `ad8f317af1f7869afff6301f65e007a0dcd6246b18872999d0ad521d8ba9cead`;
- fine FRD: `8d6557bd588d2fde6c28e61d184b768c9b7a8ee1b9daff24a427e4d262544fe2`;
- fine STA: `01e324ec0ea28f4d1fd7f5e2da1277af53812b873e218be1604c17a4c8e4990d`; and
- archive manifest: `3fdd97810b08aa8febb1ef1503c4a39be34a0138941a449d08625a98c9d80fe9`.

CalculiX was not executed during this checkpoint. The replay CLI accepts only
an archive path, and the archive audit found one evidence-compilation call in
the shared v3/v4 sample path, one in the v2 root path, one DAT parse in the v1
path, and no solver-runner call.

## Non-goals

This change does not add a structural results UI, create archive v5, alter a
material property or engineering threshold, generate a mesh, run Windows
validation, or execute another solver study. It fixes portable identity and
replay semantics only.
