# Windows UCRT structural process diagnostics

Date: 2026-08-20

## Problem

The manual Windows structural-validation run `32442944464` stopped before the
external CalculiX benchmarks. The UCRT64/MinGW build completed, but
`prometheus_structural_tests` failed at the assertion named `isolated solver
captures and compiles complete evidence exactly once`.

That assertion currently combines process status, exit code, result presence,
result completeness, setup identity, displacement, standard output, and
standard error. Its single failure message therefore cannot identify which
boundary differs under UCRT64. The same assertion passed in the Windows MSVC
desktop matrix on the identical source revision, and it passes on macOS.

## Decision

Refine this one test into named, sequential assertions after its single
`run_calculix` call. If an assertion fails, print a compact diagnostic snapshot
containing:

- solver run status and detail;
- process exit code;
- whether a validated result exists and is complete;
- result issues when present;
- captured standard-output and standard-error byte counts; and
- the observed displacement when available.

The snapshot must not contain source artifacts, credentials, arbitrary
environment variables, full solver output, or user project data.

## Execution and authority boundary

The fixture continues to execute exactly once. Diagnostics inspect the
returned `SolverRunResult`; they do not call the fixture, parser, compiler, or
solver a second time. Production process launching, CalculiX evidence
validation, numerical calculations, archive publication, and workflow gates
remain unchanged.

## Validation

1. The unchanged baseline structural test passes locally.
2. After the diagnostics change, the same local structural test still passes.
3. The diagnostic branch runs the Windows structural-validation workflow.
4. If UCRT64 still fails, its log names the first failed invariant and includes
   the bounded snapshot needed for root-cause analysis.
5. No production fix is made until that evidence identifies the failing
   boundary.

## Non-goals

- Do not change the Windows process implementation.
- Do not weaken or skip any structural assertion.
- Do not run CalculiX or the fixture more than once per existing test case.
- Do not treat a diagnostics run as structural validation success.
- Do not merge diagnostics as a substitute for correcting the root cause.
