# CalculiX Archive Version Authority Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the fragile standalone CalculiX version process and bind the Windows validation summary to the backend identity in replay-verified axial evidence.

**Architecture:** Keep one resolved UCRT64 `ccx.exe` path. After the existing axial benchmark and archive replay succeed, read the v4 manifest, require matching coarse/fine backend versions and strict executable hashes, cross-check the archive hash against the selected executable bytes, and only then continue to cantilever refinement. No C++ or numerical behavior changes.

**Tech Stack:** PowerShell 7, canonical structural archive v4 JSON, CMake/CTest, GitHub Actions.

---

### Task 1: Preserve the version-probe red case

**Files:**
- Observe: `scripts/run-structural-validation.ps1`
- Observe: GitHub Actions run `32445027024`

- [ ] **Step 1: Confirm the isolated branch state**

Run:

```bash
git status --short --branch
git branch --show-current
git rev-parse HEAD
```

Expected: a clean `diagnose/windows-ucrt-structural-process` worktree at the
committed specification amendment.

- [ ] **Step 2: Confirm the existing source violates the new contract**

Run:

```bash
! rg -q '& \$solver -v' scripts/run-structural-validation.ps1
```

Expected: exit 1 because the standalone version process is still present.
This is the source-level RED check; do not dispatch another workflow.

- [ ] **Step 3: Preserve the behavioral RED evidence**

Run:

```bash
gh run view 32445027024 --repo Richard-Y-W/Prometheus --log-failed
```

Expected: `prometheus_structural_tests` passes in 3.05 seconds, then the script
throws `Could not obtain the CalculiX version.` before an axial benchmark is
started. The fail-closed evidence upload reports no files.

### Task 2: Derive backend identity from replay-verified axial evidence

**Files:**
- Modify: `scripts/run-structural-validation.ps1:65-130`
- Test: source contract in this plan and GitHub Actions structural validation

- [ ] **Step 1: Delete the standalone version process**

Delete:

```powershell
$solverVersionOutput = (& $solver -v 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or
    [string]::IsNullOrWhiteSpace($solverVersionOutput)) {
  throw 'Could not obtain the CalculiX version.'
}
$solverVersion = ($solverVersionOutput -replace '[\r\n]+', ' | ').Trim()
```

Do not replace it with another preflight process.

- [ ] **Step 2: Validate archive backend identity after replay**

Immediately after the axial replay log is accepted and printed, add:

```powershell
$archiveDocument = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($archiveDocument.schema_version -cne '4.0.0') {
  throw 'The axial archive does not use structural schema version 4.0.0.'
}
$coarseBackend = $archiveDocument.samples.coarse.backend
$fineBackend = $archiveDocument.samples.fine.backend
$coarseVersion = [string]$coarseBackend.version
$fineVersion = [string]$fineBackend.version
if ([string]::IsNullOrWhiteSpace($coarseVersion) -or
    $coarseVersion -cne $fineVersion) {
  throw 'The axial archive backend versions are missing or inconsistent.'
}
$coarseSolverSha256 = [string]$coarseBackend.executable_sha256
$fineSolverSha256 = [string]$fineBackend.executable_sha256
if ($coarseSolverSha256 -cnotmatch '^sha256:[0-9a-f]{64}$' -or
    $fineSolverSha256 -cnotmatch '^sha256:[0-9a-f]{64}$' -or
    $coarseSolverSha256 -cne $fineSolverSha256) {
  throw 'The axial archive backend executable identities are invalid.'
}
$selectedSolverSha256 = Get-PrefixedSha256 $solver
if ($coarseSolverSha256 -cne $selectedSolverSha256) {
  throw 'The axial archive backend does not match the selected CalculiX executable.'
}
$solverVersion = $coarseVersion
$solverSha256 = $coarseSolverSha256
```

This consumes the archive created by the existing two-sample axial benchmark.
It launches no process and must remain before `$refinementLog` is assigned.

- [ ] **Step 3: Reuse the validated identity in the summary**

Change the summary solver object from:

```powershell
  solver = [ordered]@{
    executable_sha256 = Get-PrefixedSha256 $solver
    version = $solverVersion
  }
```

to:

```powershell
  solver = [ordered]@{
    executable_sha256 = $solverSha256
    version = $solverVersion
  }
```

This avoids hashing the same solver a second time while retaining the archive
versus selected-file cross-check.

- [ ] **Step 4: Run the source-level GREEN contract**

Run:

```bash
! rg -q '& \$solver -v' scripts/run-structural-validation.ps1
test "$(rg -F -c "Get-Command -Name 'ccx'" scripts/run-structural-validation.ps1)" -eq 1
test "$(rg -c 'Get-PrefixedSha256 \$solver' scripts/run-structural-validation.ps1)" -eq 1
rg -n "schema_version -cne '4.0.0'|cnotmatch '\^sha256:|coarseSolverSha256 -cne \$selectedSolverSha256" scripts/run-structural-validation.ps1
archive_line=$(rg -n '^\$archiveDocument = ' scripts/run-structural-validation.ps1 | cut -d: -f1)
refinement_line=$(rg -n '^\$refinementLog = ' scripts/run-structural-validation.ps1 | cut -d: -f1)
test "$archive_line" -lt "$refinement_line"
```

Expected: every command exits 0, exactly one solver lookup and one independent
solver hash remain, all fail-closed checks are present, and archive validation
precedes cantilever refinement.

- [ ] **Step 5: Inspect and commit only the script correction**

Run:

```bash
git diff --check
git diff -- scripts/run-structural-validation.ps1
git add -- scripts/run-structural-validation.ps1
git diff --cached --check
git commit -m "fix: derive CalculiX identity from validated evidence"
```

Expected: one script is committed with no whitespace errors.

### Task 3: Verify and publish the bounded correction

**Files:**
- Verify: `scripts/run-structural-validation.ps1`
- Verify: `.github/workflows/structural-validation.yml`
- Verify: `CMakePresets.json`

- [ ] **Step 1: Run fresh local regression checks**

Run:

```bash
cmake --build --preset headless-debug
ctest --preset headless-debug
cmake --list-presets=all
git diff --check main...HEAD
git status --short --branch
```

Expected: build exit 0, all 17 headless tests pass, presets parse, branch diff
has no whitespace errors, and the worktree is clean.

- [ ] **Step 2: Record the exact branch scope and head**

Run:

```bash
git log --oneline main..HEAD
git diff --stat main...HEAD
git rev-parse HEAD
```

Expected: only approved diagnostic/runtime/evidence work is present. Record the
exact head SHA.

- [ ] **Step 3: Push the authorized branch**

Run:

```bash
git push origin diagnose/windows-ucrt-structural-process
```

Expected: the remote branch advances to the recorded head without force.

- [ ] **Step 4: Dispatch exactly one additional Windows proof**

First confirm no structural-validation run exists at the new head. Then run:

```bash
gh workflow run structural-validation.yml \
  --repo Richard-Y-W/Prometheus \
  --ref diagnose/windows-ucrt-structural-process
```

Confirm the new run's `headSha` equals the recorded commit. Do not dispatch a
second run.

- [ ] **Step 5: Require the complete workflow boundary**

The exact run must contain and pass:

```text
prometheus_structural_tests
benchmark=passed
status=verified
refinement=passed
validation-summary.json
Retain exact solver evidence
workflow conclusion=success
```

If it fails, stop at the new first failure without another change or rerun.

### Task 4: Inspect retained evidence and finish the branch

**Files:**
- Inspect only: artifact from the exact successful workflow run

- [ ] **Step 1: Download the exact artifact to a fresh temporary directory**

Use `gh api` to identify the artifact for the exact run, create a directory
with `mktemp -d`, download and unzip there, and list relative paths. Do not copy
the artifact into the repository.

- [ ] **Step 2: Require the declared evidence inventory**

Require at least:

```text
axial/prometheus-structural-run.json
cantilever/prometheus-structural-run.json
validation-summary.json
```

Read `validation-summary.json` and the axial manifest. Require the summary
solver version/hash to equal both axial sample backend identities.

- [ ] **Step 3: Reconcile the approved specification**

Confirm the standalone version process is absent, one solver lookup and one
independent hash remain, no numerical code or execution count was added, all
local checks pass, the one additional Windows proof is green, and its artifact
contains both cases plus the matching summary.

- [ ] **Step 4: Invoke the branch-finishing workflow**

Do not merge automatically. Present the verified branch disposition choices
and wait for the user's selection.
