# Linux Structural Artifact Role Validation Design

**Date:** 2026-08-20
**Status:** Implemented and locally verified

## Problem

The `Native / Linux` GitHub Actions job for merge commit `b00f5af` builds
successfully but fails `prometheus_run_store_transaction` while publishing the
embedded structural-v3 fixture:

```text
store/structural_project_manifest_invalid
embedded structural artifact role is invalid
```

The failure reproduces under the repository's pinned Linux ARM64 validation
image with GCC 13.3. The same test passes on macOS. The archive packer emits a
strict canonical artifact sequence, but the publication validator independently
constructs its expected sequence by ranging over a conditional expression of
temporary `std::initializer_list` objects. That construction is the only
platform-sensitive step between the supplied canonical roles and the failing
ordered comparison.

## Decision

Replace the conditional temporary-initializer-list expression with explicit,
fixed iteration:

- legacy one-sample project manifests expect exactly `setup`, `deck`, `dat`,
  `frd`, `sta`, `stdout`, and `stderr`;
- two-sample project manifests expect those same seven roles first under
  `coarse/` and then under `fine/`;
- the validator continues to require exact order, exact uniqueness, exact
  artifact identity, and exact archive/project schema compatibility.

The change will not make role validation order-insensitive, accept unknown
roles, alter any archive schema, rewrite existing evidence, or run a solver.
It changes only how the already-defined expected sequence is constructed.

## Test strategy

The existing Linux `prometheus_run_store_transaction` failure is the RED
regression test. After the minimal production change:

1. rebuild and rerun that exact test in the pinned Linux container;
2. rerun the focused transaction test on macOS;
3. run the complete headless CTest suite;
4. run diff hygiene and conflict-marker checks; and
5. push only after separate user authorization, allowing GitHub Actions to
   confirm Linux, macOS, and Windows behavior.

Success means the v3 and v4 round trips publish and reconstruct while the
existing forged-role and malformed-manifest cases remain rejected.

## Verification

GitHub Actions run
[`32436491170`](https://github.com/Richard-Y-W/Prometheus/actions/runs/32436491170)
first exposed the regression in `Native / Linux`: the headless build completed,
then `prometheus_run_store_transaction` rejected publication of the embedded
structural-v3 fixture.

The exact focused test was reproduced before the change under GCC 13.3 in the
pinned `prometheus-structural-validation-arm64:ccx-2.23` image. It failed 0/1
with the same diagnostic. After replacing only the expected-role construction,
the same container test passed 1/1 in 33.24 seconds.

On macOS with Apple Clang 21, the focused transaction test passed 1/1 in 34.04
seconds. The complete headless suite then passed 17/17 in 56.90 seconds. These
are local feature-branch results; a new GitHub matrix result requires a
separately authorized push.
