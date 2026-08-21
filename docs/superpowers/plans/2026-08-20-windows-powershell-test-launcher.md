# Windows PowerShell Test Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing Windows outside-user bundle fixture run under a compatible PowerShell host without weakening or duplicating the fixture.

**Architecture:** Resolve one PowerShell executable while CMake configures the Windows desktop tests, preferring PowerShell 7 (`pwsh`) and falling back to Windows PowerShell (`powershell`). Register the existing fixture with that absolute executable path; retain its current script, arguments, and single CTest execution.

**Tech Stack:** CMake 3.24+, CTest, PowerShell 7, Windows PowerShell 5.1, GitHub Actions `windows-2022`.

---

### Task 1: Select a compatible PowerShell test host

**Files:**

- Modify: `desktop/app/CMakeLists.txt:101-107`
- Exercise unchanged: `scripts/tests/outside-user-bundle-fixture.ps1:1-126`

- [ ] **Step 1: Reconfirm the existing Windows regression is RED**

Inspect GitHub Actions run `32438121837`, job `96643310313`:

```sh
gh run view 32438121837 \
  --repo Richard-Y-W/Prometheus \
  --job 96643310313 \
  --log-failed
```

Expected: `prometheus_outside_user_bundle_fixture` is the only failed desktop
test, at `outside-user-bundle-fixture.ps1:60`, because `Get-FileHash` is not
recognized. The other 31 Windows desktop tests pass. This observed failure is
the behavioral RED test; do not add a duplicate fixture.

- [ ] **Step 2: Implement the minimal launcher selection**

Replace the current Windows-only registration block with exactly:

```cmake
  if(WIN32)
    find_program(PROMETHEUS_TEST_POWERSHELL_EXECUTABLE
      NAMES pwsh powershell
      REQUIRED
    )
    add_test(
      NAME prometheus_outside_user_bundle_fixture
      COMMAND "${PROMETHEUS_TEST_POWERSHELL_EXECUTABLE}"
        -NoProfile -ExecutionPolicy Bypass
        -File ${PROJECT_SOURCE_DIR}/scripts/tests/outside-user-bundle-fixture.ps1
    )
  endif()
```

Do not modify the fixture, its verifier, any application target, any solver
adapter, or any workflow. CMake's default multi-name search considers the
names in order, so a clean configure selects `pwsh` before `powershell`.
`REQUIRED` preserves the fail-closed test boundary.

- [ ] **Step 3: Reconfigure the existing macOS desktop build**

Run:

```sh
cmake --preset desktop-no-occt-debug
```

Expected: configuration and generation complete successfully. This catches
CMake syntax and scope errors; the Windows GitHub job remains the authoritative
host-selection check.

- [ ] **Step 4: Inspect the bounded production diff**

Run:

```sh
git diff -- desktop/app/CMakeLists.txt
```

Expected: one `find_program` call and one replacement of the hard-coded
`powershell` command with the resolved executable. No other production file is
changed.

### Task 2: Verify the affected local boundary

**Files:**

- Verify: `CMakePresets.json`
- Verify: `desktop/app/CMakeLists.txt`

- [ ] **Step 1: Build the complete desktop target set**

Run:

```sh
cmake --build --preset desktop-no-occt-debug
```

Expected: build exits zero. Because the CMake-only change does not alter C++
sources, this should be incremental.

- [ ] **Step 2: Run the complete macOS desktop suite**

Run in an environment that permits loopback listeners:

```sh
ctest --preset desktop-no-occt-debug --output-on-failure
```

Expected: 31/31 tests pass. The Windows-only outside-user fixture is not
registered on macOS.

- [ ] **Step 3: Run the complete macOS headless suite**

Run:

```sh
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
```

Expected: 17/17 tests pass.

- [ ] **Step 4: Validate presets and repository hygiene**

Run:

```sh
cmake --list-presets
git diff --check
git status --short --branch
```

Expected: presets parse, no whitespace errors are reported, and the only
uncommitted implementation change is `desktop/app/CMakeLists.txt`.

- [ ] **Step 5: Commit the verified implementation**

Run:

```sh
git add -- \
  desktop/app/CMakeLists.txt
git commit -m "fix: select compatible PowerShell test host"
```

Expected: the implementation commit contains only the CMake correction. The
approved design status and this plan remain in their earlier documentation
commit.

### Task 3: Integrate, publish, and enforce the CI gate

**Files:**

- No additional repository files
- Observe: `.github/workflows/verify.yml`
- Dispatch after green: `.github/workflows/structural-validation.yml`

- [ ] **Step 1: Refresh and compare remote main without modifying it**

Run:

```sh
git fetch origin main
git rev-list --left-right --count main...origin/main
git log --oneline --decorate -5 origin/main
```

Expected: determine whether `origin/main` moved after `2b7c007`. If it moved,
inspect and deliberately reconcile before integration; never force-push or
overwrite remote commits.

- [ ] **Step 2: Fast-forward the primary checkout after reconciliation**

From `/Users/byungkim/Prometheus`, run only when main and the feature branch
have a clean fast-forward relationship:

```sh
git merge --ff-only fix/windows-powershell-test-launcher
```

Expected: local `main` advances to the verified feature commit without a merge
conflict or duplicated change.

- [ ] **Step 3: Verify the integrated commit and push main**

Run:

```sh
git status --short --branch
git log --oneline --decorate -4
git push origin main
```

Expected: the checkout is clean before push, and `origin/main` advances without
a force update.

- [ ] **Step 4: Require the replacement automatic matrix to be GREEN**

Find the push-triggered `verify` run whose `headSha` is the pushed commit, then
monitor it to completion:

```sh
gh run list \
  --repo Richard-Y-W/Prometheus \
  --workflow verify.yml \
  --branch main \
  --limit 5 \
  --json databaseId,headSha,status,conclusion,url
```

Expected: all nine jobs pass, and the Windows desktop suite reports 32/32.
If any job fails, do not dispatch structural validation; retrieve and report
that job's exact failed log.

- [ ] **Step 5: Dispatch structural validation exactly once after GREEN**

Run only after Step 4 succeeds:

```sh
gh workflow run structural-validation.yml \
  --repo Richard-Y-W/Prometheus \
  --ref main
gh run list \
  --repo Richard-Y-W/Prometheus \
  --workflow structural-validation.yml \
  --branch main \
  --limit 5 \
  --json databaseId,headSha,event,status,conclusion,createdAt,url
```

Expected: exactly one new `workflow_dispatch` run exists at the same pushed
commit. Do not retry blindly and do not claim the structural workflow passed
until its own run completes successfully.
