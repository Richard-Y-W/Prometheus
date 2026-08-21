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

The first fix proof, workflow run `32445027024`, confirmed that boundary:
after the script selected UCRT64 before CTest, `prometheus_structural_tests`
passed in 3.05 seconds. The workflow then reached a previously unexecuted
standalone `ccx -v` probe and stopped before either real benchmark because the
probe required both nonempty output and exit code zero. CalculiX implements
`-v` by printing its version and invoking its Fortran stop routine, so the
script's exit-code assumption is not a portable backend contract.

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

For backend-version authority, the considered approaches are:

1. **Use the validated axial archive (selected).** The existing v4 archive
   records the version and executable SHA-256 from both completed solver
   samples. Cross-checking those values against each other and against an
   independent hash of the selected executable binds the summary to the
   backend that actually produced accepted evidence.
2. Accept a nonzero `ccx -v` exit when its text matches a version pattern. This
   preserves an extra platform-sensitive process whose output is not tied to a
   completed analysis.
3. Read the MSYS2 package version. This identifies installed package metadata,
   not necessarily the executable bytes that produced the archive.

## Selected design

At the beginning of `run-structural-validation.ps1`, define the canonical
UCRT64 binary directory, require it to exist, and prepend it to the process
`PATH`. Resolve `ccx` from that environment before any compiled test runs and
fail closed unless the resolved executable is inside the canonical directory.
Reuse that same resolved path for the real benchmarks; do not perform a second
solver lookup or standalone version run.

After the axial benchmark passes and its archive passes offline replay, parse
the already-validated v4 manifest. Require schema version `4.0.0` and nonempty,
identical backend versions for `samples.coarse` and `samples.fine`. Require
their executable identities to match strict lowercase
`sha256:[0-9a-f]{64}` syntax, to be identical, and to match an independent
SHA-256 of the resolved `ccx.exe`. Only then run cantilever refinement. Write
the matched archive version and hash to `validation-summary.json`. A missing
field, malformed identity, sample mismatch, or executable mismatch stops the
gate.

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

The first proof run confirmed the runtime correction and exposed the version
probe before any real benchmark. After replacing that probe, dispatch exactly
one additional proof run at its recorded commit. CTest performs its existing
lightweight fixture executions.

The real axial benchmark and coarse/fine cantilever validation each run only
where the existing script already declares them. No additional solver call,
mesh generation, parsing pass, or calculation is introduced for diagnostics,
identity checks, saving, or artifact upload. Removing `ccx -v` reduces the
total process count by one.

## Failure behavior

- Missing UCRT64 installation: stop before configuration.
- `ccx` resolving outside UCRT64: stop before build or tests.
- Child runtime mismatch: the existing bounded diagnostic remains red and
  reports the Windows exit status.
- Missing or inconsistent axial archive backend identity: stop before
  cantilever refinement and do not publish a passing summary.
- Axial archive executable identity differing from the selected `ccx.exe`:
  stop before cantilever refinement.
- Test, solver, replay, or refinement failure: stop without a passing summary.
- Missing upload directory: the artifact step fails instead of silently
  warning.

No failure is converted to a pass, and no partial solver output is treated as
validated evidence.

## Verification

1. Preserve run `32444116480` as the loader-failure red reproduction.
2. Preserve run `32445027024` as proof that UCRT64 selection fixes the child
   boundary and as the standalone-version-probe red reproduction.
3. Add a source contract proving `ccx -v` is absent and that archive identity
   is validated before cantilever refinement and summary publication.
4. Run local headless build/tests and repository hygiene checks.
5. Push the bounded correction and dispatch one additional manual Windows
   structural workflow at the exact branch head.
6. Require the structural CTest, axial analytic benchmark, offline replay,
   backend identity cross-check, cantilever refinement, validation summary,
   and evidence upload all to pass.
7. Inspect the retained artifact inventory to confirm both validation case
   directories and `validation-summary.json` are present.

## Non-goals

- Do not alter structural physics or acceptance thresholds.
- Do not statically link or vendor a second runtime.
- Do not add retries that could repeat expensive solver calculations.
- Do not merge until the exact Windows workflow run is green.
- Do not combine unrelated structural feature work with this fix.
