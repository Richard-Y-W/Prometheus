# Windows PowerShell Test Launcher Design

**Date:** 2026-08-20
**Status:** Approved; implementation pending

## Problem

GitHub Actions run `32438121837` built the Windows desktop successfully and
passed 31 of 32 desktop tests. The only failure was
`prometheus_outside_user_bundle_fixture`, which stopped at
`scripts/tests/outside-user-bundle-fixture.ps1:60` because `Get-FileHash` was
not available.

The GitHub job runs under PowerShell 7 (`pwsh`), but the CTest registration in
`desktop/app/CMakeLists.txt` hard-codes Windows PowerShell (`powershell`). CTest
sits between those two processes, so PowerShell 7 cannot perform its normal
Windows-PowerShell module-path handoff when the legacy host starts. The legacy
host therefore inherits an incompatible module search path and cannot
autoload `Microsoft.PowerShell.Utility`, which provides `Get-FileHash`.

This is a test-launcher defect. It is not a structural solver failure and does
not change any engineering calculation or project evidence.

## Considered approaches

### 1. Discover PowerShell 7 first, then fall back to Windows PowerShell

At Windows configure time, use CMake `find_program` with ordered names `pwsh`
and `powershell`. Register the existing fixture with the resolved absolute
executable path and fail configuration if neither host exists.

This is the selected approach. GitHub's PowerShell 7 host remains within its
own compatible module environment, while Windows machines that only provide
the built-in Windows PowerShell retain support.

### 2. Require PowerShell 7 everywhere

Hard-code `pwsh` and fail when it is unavailable. This is simpler in CI but
would unnecessarily require developers and offline Windows users to install
PowerShell 7 even though the scripts are compatible with Windows PowerShell
when it starts with its normal environment.

### 3. Repair `PSModulePath` inside every script

Keep launching `powershell` and reconstruct its module path or import utility
modules by an absolute system path. This couples scripts to host installation
details, repeats environment repair across packaging and verification scripts,
and treats the symptom rather than selecting the compatible executable.

## Design

Within the existing `if(WIN32)` test-registration block:

1. resolve an executable into a narrowly named CMake variable, preferring
   `pwsh` and falling back to `powershell`;
2. require a resolved executable so the test cannot silently disappear;
3. pass the resolved absolute path as the CTest command; and
4. preserve every existing script argument and the fixture's pass/tamper/
   missing/extra-file checks.

No production application path, solver adapter, calculation, package schema,
or evidence contract changes. The fixture runs once, in the same desktop test
suite where it already runs.

## Test strategy

The failed Windows fixture in run `32438121837` is the RED regression evidence;
no second fixture is needed. After the minimal CMake change:

1. verify CMake preset parsing and configure the local macOS desktop preset;
2. run the complete local desktop and headless suites to ensure test
   registration and unrelated targets remain intact;
3. run repository diff-hygiene checks;
4. push the bounded change to `main`;
5. require the replacement GitHub matrix to pass all nine jobs, including all
   32 Windows desktop tests; and
6. only after that matrix is green, manually dispatch
   `structural-validation.yml` at the same commit.

The GitHub Windows run is the authoritative GREEN check because this failure
depends on Windows process-host behavior and cannot be reproduced faithfully
on the current macOS workstation.

## Success and failure behavior

Success means PowerShell 7 is selected when available, Windows PowerShell is
used only as a fallback, the outside-user bundle fixture passes unchanged, and
the entire automatic matrix is green.

If no PowerShell host exists, Windows desktop configuration fails explicitly.
If any automatic CI job fails, structural validation remains undispatched and
the exact failing step is reported.
