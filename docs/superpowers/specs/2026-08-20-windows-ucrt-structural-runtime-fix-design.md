# Windows UCRT structural runtime fix

Date: 2026-08-20

## Evidence and root-cause boundary

The Windows UCRT64 structural-validation job builds successfully and starts
`prometheus_structural_tests`, but that test's isolated child fixture exits as
signed code `-1073741511` (`0xC0000139`) with no standard output, standard
error, or result artifacts. Microsoft defines that status as
`STATUS_ENTRYPOINT_NOT_FOUND`. The child therefore fails in the Windows loader
before its `main` function and before any solver or evidence calculation.

`CMakePresets.json` gives configure and build commands a UCRT64-first `PATH`,
but `scripts/run-structural-validation.ps1` invokes raw `ctest` from the
workflow PowerShell process before that script prepends
`C:\msys64\ucrt64\bin`. The child inherits the PowerShell process environment,
so its DLL search can select an incompatible runtime already present on the
host `PATH`.

## Approaches considered

1. **Make the validation script own runtime selection (selected).** Put the
   installed UCRT64 directory first before configuration, build, tests, and
   solver discovery. This fixes the environment at the executable boundary
   used by both CI and a developer running the checked-in script.
2. Set `PATH` only in the GitHub Actions workflow. This is smaller in YAML but
   leaves direct invocation of the validation script unsafe and duplicates an
   assumption outside the script that consumes it.
3. Statically link the fixture's MinGW runtime. This could hide the immediate
   child failure, but it would not validate the dynamic-runtime environment
   used by the real CalculiX tools and would change the build architecture to
   repair an orchestration defect.

## Selected design

At the beginning of `run-structural-validation.ps1`, define the canonical
UCRT64 binary directory, require it to exist, and prepend it to the process
`PATH`. Resolve `ccx` from that environment before any compiled test runs and
fail closed unless the resolved executable is inside the canonical directory.
Reuse that same resolved path for the existing version query and real
benchmarks; do not perform a second solver lookup or run.

Leave the C++ process implementation, structural fixture, parsers, numerical
acceptance criteria, timeouts, and benchmark meshes unchanged. The diagnostic
assertions stay because they are bounded, inspect the already-returned result,
and add no execution.

The workflow's evidence upload will retain the directory the script actually
writes: `out/validation/structural`. Missing evidence becomes a workflow error
instead of a warning. The directory contains the axial archive, cantilever
refinement evidence, and validation summary; it is bounded validation output,
not a user project tree.

## Execution count and performance

The workflow is dispatched exactly once after both configuration corrections
are committed. CTest performs its existing lightweight fixture executions.
The real axial benchmark and coarse/fine cantilever validation each run only
where the existing script already declares them. No additional solver call,
mesh generation, parsing pass, or calculation is introduced for diagnostics,
identity checks, saving, or artifact upload.

## Failure behavior

- Missing UCRT64 installation: stop before configuration.
- `ccx` resolving outside UCRT64: stop before build or tests.
- Child runtime mismatch: the existing bounded diagnostic remains red and
  reports the Windows exit status.
- Test, solver, replay, or refinement failure: stop without a passing summary.
- Missing upload directory: the artifact step fails instead of silently
  warning.

No failure is converted to a pass, and no partial solver output is treated as
validated evidence.

## Verification

1. Preserve the existing failed branch run as the red reproduction.
2. Run local headless build/tests and repository hygiene checks.
3. Push the bounded fix and dispatch the manual Windows structural workflow
   once at the exact branch head.
4. Require the structural CTest, axial analytic benchmark, offline replay,
   cantilever refinement, validation summary, and evidence upload all to pass.
5. Inspect the retained artifact inventory to confirm both validation case
   directories and `validation-summary.json` are present.

## Non-goals

- Do not alter structural physics or acceptance thresholds.
- Do not statically link or vendor a second runtime.
- Do not add retries that could repeat expensive solver calculations.
- Do not merge until the exact Windows workflow run is green.
- Do not combine unrelated structural feature work with this fix.
