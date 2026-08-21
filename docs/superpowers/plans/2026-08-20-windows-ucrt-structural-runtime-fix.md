# Windows UCRT Structural Runtime Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the checked-in Windows structural gate select one known UCRT64 runtime before any compiled executable runs and retain the exact evidence it produces.

**Architecture:** Keep runtime ownership in the PowerShell entry point used by CI and developers. Prepend and validate the installed UCRT64 tool directory before CMake/CTest, reuse the single resolved CalculiX executable, and make the existing workflow upload the script's actual bounded output root. Do not alter C++, physics, mesh sizes, or execution counts.

**Tech Stack:** PowerShell 7, CMake/CTest, MSYS2 UCRT64/MinGW, GitHub Actions.

---

### Task 1: Preserve the exact red reproduction

**Files:**
- Observe: `desktop/structural/tests/structural_tests.cpp`
- Observe: `scripts/run-structural-validation.ps1`
- Observe: GitHub Actions run `32444116480`

- [ ] **Step 1: Confirm the worktree and branch are isolated**

Run:

```bash
git status --short --branch
git branch --show-current
git merge-base HEAD main
```

Expected: branch `diagnose/windows-ucrt-structural-process`, a clean worktree,
and a merge base on `main`.

- [ ] **Step 2: Record the existing failing behavior**

Run:

```bash
gh run view 32444116480 --repo Richard-Y-W/Prometheus --log-failed
```

Expected: the structural fixture reports
`solver_run.status=nonzero_exit`, signed exit `-1073741511`, zero captured
output bytes, no validated result, and failure at `isolated solver process
completes`. This is the RED case and must not be re-dispatched.

### Task 2: Select UCRT64 before any compiled validation executable

**Files:**
- Modify: `scripts/run-structural-validation.ps1:34-63`
- Test: GitHub Actions workflow `structural-validation.yml`

- [ ] **Step 1: State the single hypothesis**

Record in the execution notes:

```text
The child fixture fails before main because raw CTest inherits the workflow
PowerShell PATH before UCRT64 is prepended, allowing an incompatible MinGW DLL
to satisfy an import with the wrong exported entry points.
```

- [ ] **Step 2: Make the smallest runtime-ordering change**

Before the first `cmake` call in `scripts/run-structural-validation.ps1`, add:

```powershell
$ucrtBin = 'C:\msys64\ucrt64\bin'
if (-not (Test-Path -LiteralPath $ucrtBin -PathType Container)) {
  throw "The required UCRT64 tool directory does not exist: $ucrtBin"
}
$env:Path = "$ucrtBin;$env:Path"
$solver = (Get-Command -Name 'ccx' -CommandType Application `
  -ErrorAction Stop).Source
if ((Split-Path -Parent $solver) -ine $ucrtBin) {
  throw "CalculiX resolved outside the required UCRT64 tool directory: $solver"
}
```

Delete the later duplicate `PATH` mutation and `Get-Command` assignment. Keep
the existing `$solverVersionOutput` query and reuse the early `$solver` value.
No solver process is added.

- [ ] **Step 3: Verify the source-level ordering contract**

Run:

```bash
python3 - <<'PY'
from pathlib import Path
p = Path('scripts/run-structural-validation.ps1').read_text()
assert p.count("$env:Path = \"$ucrtBin;$env:Path\"") == 1
assert p.count("Get-Command -Name 'ccx'") == 1
assert p.index("$env:Path = \"$ucrtBin;$env:Path\"") < p.index('cmake --preset')
assert p.index("Get-Command -Name 'ccx'") < p.index('ctest --test-dir')
PY
```

Expected: exit 0. This is a configuration contract check; the existing failed
Windows run remains the behavioral RED case until Task 4.

- [ ] **Step 4: Inspect and commit only the runtime fix**

Run:

```bash
git diff --check
git diff -- scripts/run-structural-validation.ps1
git add -- scripts/run-structural-validation.ps1
git diff --cached --check
git commit -m "fix: select UCRT runtime before structural tests"
```

Expected: one script is committed with no whitespace errors.

### Task 3: Make structural evidence retention fail closed

**Files:**
- Modify: `.github/workflows/structural-validation.yml:33-41`
- Test: GitHub Actions workflow `structural-validation.yml`

- [ ] **Step 1: Correct the upload contract**

Change the upload configuration to:

```yaml
          path: out/validation/structural
          if-no-files-found: error
```

Keep `if: always()`, the pinned action SHA, artifact name, and retention period
unchanged. Uploading files performs no solver calculation.

- [ ] **Step 2: Verify the workflow and script agree**

Run:

```bash
python3 - <<'PY'
from pathlib import Path
s = Path('scripts/run-structural-validation.ps1').read_text()
w = Path('.github/workflows/structural-validation.yml').read_text()
assert "'out/validation/structural'" in s
assert 'path: out/validation/structural' in w
assert 'if-no-files-found: error' in w
assert 'path: out/validation/structural/tension-bar' not in w
PY
```

Expected: exit 0.

- [ ] **Step 3: Inspect and commit only the upload fix**

Run:

```bash
git diff --check
git diff -- .github/workflows/structural-validation.yml
git add -- .github/workflows/structural-validation.yml
git diff --cached --check
git commit -m "ci: retain structural validation evidence"
```

Expected: one workflow file is committed with no whitespace errors.

### Task 4: Verify locally and prove the Windows boundary once

**Files:**
- Verify: `CMakePresets.json`
- Verify: `desktop/structural/tests/structural_tests.cpp`
- Verify: `scripts/run-structural-validation.ps1`
- Verify: `.github/workflows/structural-validation.yml`

- [ ] **Step 1: Run fresh local regression checks**

Run:

```bash
cmake --build --preset headless-debug
ctest --preset headless-debug
cmake --list-presets=all
git diff --check main...HEAD
git status --short --branch
```

Expected: build exit 0, all headless tests pass, presets parse, branch diff has
no whitespace errors, and the worktree is clean.

- [ ] **Step 2: Confirm the exact publication scope**

Run:

```bash
git log --oneline main..HEAD
git diff --stat main...HEAD
git rev-parse HEAD
```

Expected: only the approved diagnostic/specification/plan/runtime/upload work
is present. Record the exact head SHA.

- [ ] **Step 3: Push the authorized branch**

Run:

```bash
git push origin diagnose/windows-ucrt-structural-process
```

Expected: the remote branch advances to the exact recorded head without a
force push.

- [ ] **Step 4: Dispatch exactly one validation run**

Run:

```bash
gh workflow run structural-validation.yml \
  --repo Richard-Y-W/Prometheus \
  --ref diagnose/windows-ucrt-structural-process
```

Then find the new workflow-dispatch run and confirm its `headSha` equals the
recorded branch head before waiting for it. Do not dispatch a second run.

- [ ] **Step 5: Require the complete green boundary**

Inspect the single run and require all of the following from its job log and
conclusion:

```text
prometheus_structural_tests passes
benchmark=passed
status=verified
refinement=passed
validation-summary.json is written
Retain exact solver evidence succeeds
workflow conclusion=success
```

If any item is absent or the workflow fails, stop and report the new first
failure. Do not layer another change or automatically rerun.

- [ ] **Step 6: Inspect the retained artifact without changing the repository**

Use `gh api` to identify the artifact attached to the exact run, download it
to a fresh `mktemp -d` directory, and list its relative paths. Require at
minimum:

```text
axial/prometheus-structural-run.json
cantilever/prometheus-structural-run.json
validation-summary.json
```

The exact archive may contain additional declared raw evidence. Do not copy it
into the repository and do not delete user files.

### Task 5: Reconcile completion state

**Files:**
- Review: `docs/superpowers/specs/2026-08-20-windows-ucrt-structural-runtime-fix-design.md`
- Review: this plan

- [ ] **Step 1: Check every specification requirement against evidence**

Confirm runtime selection happens before CTest, solver discovery is singular,
no calculation count changed, missing artifacts fail closed, local regression
checks pass, the one Windows run is green, and the retained artifact contains
both validation cases plus the summary.

- [ ] **Step 2: Invoke the branch-finishing workflow**

Do not merge automatically. Present the verified branch disposition choices
and wait for the user's selection.
