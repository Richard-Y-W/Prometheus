# Rover Regression and Outside-User Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the pinned NASA JPL Open Source Rover folder scan into a deterministic cross-platform regression and prepare—without fabricating—the outside-user session that remains required to close Program 01D.

**Architecture:** The existing production `scanProjectFolder()` path computes one root-independent inventory identity. A portable CMake driver prepares the pinned Git archive and compares scanner JSON with a checked-in expectation. PowerShell remains a thin Windows wrapper. Human-observation evidence is stored separately from automated evidence and can only be completed by a real participant.

**Tech Stack:** C++20/Qt Core, CMake script mode, CTest, Git archive, JSON, PowerShell deployment on Windows.

---

## File map

- `desktop/app/project_intake.hpp` and `.cpp`: root-independent inventory SHA-256.
- `desktop/app/tests/project_intake_tests.cpp`: digest contract and scan-only JSON tests.
- `cmake/AssertProjectIntakeSummary.cmake`: strict JSON expectation comparison.
- `cmake/tests/JplRoverTrialFixture.cmake`: offline pinned-archive and
  failed-promotion tests using a temporary local Git repository.
- `fixtures/trials/project-intake-summary/`: small pass/fail assertion fixtures.
- `docs/trials/jpl-open-source-rover-expectations.json`: pinned source and exact expected scanner output.
- `scripts/jpl-rover-trial.cmake`: safe preparation, build, scan, and assertion driver.
- `scripts/prepare-jpl-rover-trial.ps1` and `run-jpl-rover-trial.ps1`: thin Windows entry points.
- `docs/trials/jpl-open-source-rover-mixed-folder.md`: measured macOS and Windows evidence and claim boundary.
- `docs/trials/outside-user-screening-task-sheet.md`: the only instructions given to the participant.
- `docs/trials/outside-user-screening-observation-form.md`: blank, structured factual record.
- `docs/trials/outside-user-screening-facilitator-protocol.md`: eligibility and nonintervention rules.
- `scripts/package-outside-user-screening.ps1`: Windows-first deployable participant bundle.
- `scripts/verify-outside-user-bundle.ps1`: bundle manifest verification.
- `scripts/tests/outside-user-bundle-fixture.ps1`: small pass/tamper/missing/
  extra-file verifier regression.
- `docs/program/01-trust-kernel/01d-multi-project-evidence.md`: automated and human gate status.

### Task 1: Give project intake a root-independent inventory identity

**Files:**
- Modify: `desktop/app/project_intake.hpp`
- Modify: `desktop/app/project_intake.cpp`
- Modify: `desktop/app/tests/project_intake_tests.cpp`

- [ ] **Step 1: Write failing digest-contract tests**

Create the same two-file tree under two different `QTemporaryDir` roots:

```text
a.step          bytes: A
docs/readme.md  bytes: hello\n
```

Assert that both scans return:

```text
sha256:535ec79d19a301352ee1d10176bddd0e1080b4dea36241fc51daab29c34c296c
```

Then assert that changing one byte or renaming one path changes the inventory
identity. Extend `--scan-only` expectations to include `inventory_sha256`.

- [ ] **Step 2: Verify RED**

Run:

```bash
cmake --fresh --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug --target prometheus_project_intake_tests
ctest --test-dir out/build/desktop-no-occt-debug -R '^prometheus_project_intake$' --output-on-failure
```

Expected: compilation fails because `inventory_sha256` is absent.

- [ ] **Step 3: Implement the byte contract**

Add `QString inventory_sha256` to `ProjectIntakeResult` and expose it read-only
on `ProjectIntakeController`. After artifact rows are sorted by relative path,
hash one record per artifact using these exact bytes:

```text
UTF-8 relative path using '/' separators
NUL
base-10 byte length with no sign or leading zeroes
NUL
64 lowercase content-SHA-256 hex characters, without the 'sha256:' prefix
LF
```

Do not include absolute root, timestamps, classification, duplicate flag, or
elapsed time. If any artifact is unreadable or lacks a strict lowercase content
digest, leave `inventory_sha256` empty so an expectation gate cannot pass.

- [ ] **Step 4: Emit it from the production scanner CLI mode**

Add `inventory_sha256` to the compact JSON written by
`prometheus_project_intake_tests --scan-only`. Keep stdout JSON-only; write any
diagnostic to stderr.

- [ ] **Step 5: Verify GREEN and commit**

Run the focused test and:

```bash
git add desktop/app/project_intake.hpp desktop/app/project_intake.cpp \
  desktop/app/tests/project_intake_tests.cpp
git commit -m "Add deterministic project inventory identity"
```

### Task 2: Add a reusable fail-closed scan assertion

**Files:**
- Create: `cmake/AssertProjectIntakeSummary.cmake`
- Create: `fixtures/trials/project-intake-summary/expected.json`
- Create: `fixtures/trials/project-intake-summary/actual-good.json`
- Create: `fixtures/trials/project-intake-summary/actual-bad.json`
- Modify: `desktop/app/CMakeLists.txt`

- [ ] **Step 1: Add CTest cases before the assertion script exists**

Add one passing test and one `WILL_FAIL` test that invoke:

```text
cmake -DEXPECTED_JSON=<expected> -DACTUAL_JSON=<actual> \
  -P cmake/AssertProjectIntakeSummary.cmake
```

The bad fixture differs only in `ready_files`, proving a changed classification
cannot pass merely because inventory bytes are unchanged.

- [ ] **Step 2: Verify RED**

Configure and run:

```bash
cmake --fresh --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R '^prometheus_project_intake_summary_' --output-on-failure
```

Expected: tests fail because the assertion script is absent.

- [ ] **Step 3: Implement strict field comparison**

Use CMake `string(JSON ...)` to require integer or string values for exactly
these asserted fields:

```text
inventory_sha256
total_files
ready_files
not_evaluated_files
unsupported_files
unreadable_files
duplicate_copies
primary_step_path
```

Require `ok=true`, `error=""`, a strict lowercase SHA-256, nonnegative counts,
and `total_files = ready + not_evaluated + unsupported + unreadable`. Ignore
only `root_path` and `elapsed_ms`. Missing, malformed, or mismatched data is a
fatal error with the field name and both values.

- [ ] **Step 4: Verify GREEN and commit**

Run the two CTest cases and:

```bash
git add cmake/AssertProjectIntakeSummary.cmake \
  fixtures/trials/project-intake-summary desktop/app/CMakeLists.txt
git commit -m "Add strict project intake summary assertion"
```

### Task 3: Replace the trust-on-existing-folder Rover scripts

**Files:**
- Create: `docs/trials/jpl-open-source-rover-expectations.json`
- Create: `scripts/jpl-rover-trial.cmake`
- Create: `cmake/tests/JplRoverTrialFixture.cmake`
- Modify: `scripts/prepare-jpl-rover-trial.ps1`
- Modify: `scripts/run-jpl-rover-trial.ps1`
- Modify: `desktop/app/CMakeLists.txt`
- Modify: `.gitignore`

- [ ] **Step 1: Write an offline preparation failure test**

`JplRoverTrialFixture.cmake` creates a temporary local Git repository containing
`LICENSE`, `assembly.step`, and one ignored file, commits it, and invokes the
same pinned-archive helper with test-only work paths. Register two CTest cases:
one with a deliberately wrong license SHA that must leave a pre-existing valid
trial directory and sidecar byte-identical, and one with the correct
revision/license that must prepare only `LICENSE` and `assembly.step`.

- [ ] **Step 2: Verify RED**

Run the new CTest cases. Expected: FAIL because `jpl-rover-trial.cmake` does not
exist.

- [ ] **Step 3: Implement safe, pinned preparation**

The driver resolves repository paths from `CMAKE_CURRENT_LIST_DIR`, pins
revision `0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7`, and validates license
SHA-256 `112db3cf45a71a2db715c0d46dacfd619b4effac00bda5e4089ff44f3958bb29`.
It must:

1. keep the Git cache under `out/external-demo/open-source-rover`;
2. fetch only when the pinned commit object is absent;
3. use `git archive`, never a mutable checkout, as the trial source;
4. extract into a sibling staging directory;
5. validate revision and license before promotion;
6. store revision metadata in a sidecar outside the 967-file trial tree;
7. retain the last valid trial when fetch, archive, extract, or validation fails;
8. accept an explicit `PROMETHEUS_JPL_REFRESH=ON`, but never trust directory
   existence alone.

Guard every recursive cleanup by first proving the target is a child of the
repository's exact `out/trials` or `out/external-demo` directory.

- [ ] **Step 4: Build and invoke the production scanner in verify mode**

Support `PROMETHEUS_JPL_MODE=prepare` and `verify`. In verify mode, default to
`desktop-no-occt-debug` on macOS/Linux and `windows-release` on Windows, while
allowing `PROMETHEUS_JPL_PRESET` override. Configure/build the exact
`prometheus_project_intake_tests` target, capture its `--scan-only` JSON to a
file, and call `AssertProjectIntakeSummary.cmake`.

The expectation file must contain the pinned revision/license plus these
previously observed counts:

```text
total_files: 967
ready_files: 23
not_evaluated_files: 58
unsupported_files: 886
unreadable_files: 0
duplicate_copies: 191
primary_step_path: ""
```

Do not commit an inventory SHA until two fresh archive extractions in different
absolute roots produce the same strict scanner SHA. Then record that exact
value in the expectation JSON; no wildcard or update-on-mismatch mode is
allowed in normal verification.

- [ ] **Step 5: Reduce PowerShell to thin wrappers**

`prepare-jpl-rover-trial.ps1` calls CMake in `prepare` mode and prints the
validated path. `run-jpl-rover-trial.ps1` calls `verify`; add an explicit
`-OpenDesktop` switch for GUI launch so automated verification never hangs on
an open window.

- [ ] **Step 6: Verify offline behavior and commit**

Run the local fixture tests, simulate a corrupted cached trial, and verify that
normal mode rejects it while refresh restores only from the pinned archive.
Then:

```bash
git add .gitignore cmake/AssertProjectIntakeSummary.cmake \
  docs/trials/jpl-open-source-rover-expectations.json \
  cmake/tests/JplRoverTrialFixture.cmake desktop/app/CMakeLists.txt \
  scripts/jpl-rover-trial.cmake scripts/prepare-jpl-rover-trial.ps1 \
  scripts/run-jpl-rover-trial.ps1
git commit -m "Make the JPL Rover intake trial reproducible"
```

### Task 4: Run and record the real Rover regression

**Files:**
- Modify: `docs/trials/jpl-open-source-rover-mixed-folder.md`
- Modify: `docs/program/01-trust-kernel/01d-multi-project-evidence.md`
- Modify: `docs/milestone-status.md`

- [ ] **Step 1: Prepare the pinned snapshot with network available**

Run:

```bash
cmake -DPROMETHEUS_JPL_MODE=prepare -P scripts/jpl-rover-trial.cmake
```

The first run may download approximately 623 MB. Record the exact revision and
license validation; do not substitute a different branch tip when the pinned
commit is unavailable.

- [ ] **Step 2: Establish the inventory expectation independently**

Extract the same pinned `git archive` into two distinct ignored directories,
scan both through the production binary, and require identical inventory SHA,
counts, and empty primary selection. Add the resulting exact SHA to
`jpl-open-source-rover-expectations.json`.

- [ ] **Step 3: Run the normal gate twice**

Run:

```bash
cmake -DPROMETHEUS_JPL_MODE=verify -P scripts/jpl-rover-trial.cmake
cmake -DPROMETHEUS_JPL_MODE=verify -P scripts/jpl-rover-trial.cmake
```

The second run must reuse the validated local snapshot without network and
produce the same asserted output. Scan duration is recorded as an observation,
not a pass threshold.

- [ ] **Step 4: Record only the bounded claim**

Update the trial document with platform, scanner build identity, inventory SHA,
counts, measured duration, and exact commands. State that the gate proves file
accounting and honest assembly ambiguity only; it does not prove that Rover
CAD, electronics, controls, or mechanics work.

- [ ] **Step 5: Commit**

```bash
git add docs/trials/jpl-open-source-rover-expectations.json \
  docs/trials/jpl-open-source-rover-mixed-folder.md \
  docs/program/01-trust-kernel/01d-multi-project-evidence.md \
  docs/milestone-status.md
git commit -m "Record reproducible JPL Rover intake evidence"
```

### Task 5: Build a Windows-first outside-user screening package

**Files:**
- Create: `docs/trials/outside-user-screening-task-sheet.md`
- Create: `docs/trials/outside-user-screening-observation-form.md`
- Create: `docs/trials/outside-user-screening-facilitator-protocol.md`
- Create: `scripts/package-outside-user-screening.ps1`
- Create: `scripts/verify-outside-user-bundle.ps1`
- Create: `scripts/tests/outside-user-bundle-fixture.ps1`
- Modify: `docs/program/01-trust-kernel/01d-multi-project-evidence.md`

- [ ] **Step 1: Write the participant task before packaging**

The one-page task sheet asks the participant, without telling them how:

1. open the supplied Rover project folder;
2. say when all files appear accounted for;
3. find one loadable STEP candidate;
4. explain why no rover assembly was selected automatically;
5. explain `unsupported` versus `not evaluated`;
6. state whether Prometheus has proved the rover works.

Do not include developer vocabulary, expected counts, filter names, or answers.

- [ ] **Step 2: Define the factual observation record**

The blank form records participant eligibility, machine state, package hash,
start/end times, time to inventory, time to candidate, interpretation answers,
mistaken-pass statements, exact confusion quotes, attempted workarounds,
crashes, and facilitator interventions. Use `not observed` rather than inferred
answers. Include participant consent for notes or screen recording.

- [ ] **Step 3: Add a failing bundle verification fixture**

Make `scripts/tests/outside-user-bundle-fixture.ps1` create a small bundle and
prove `verify-outside-user-bundle.ps1` rejects it when one file
is missing, altered, extra, or when the embedded Rover expectation differs from
the checked-in source. It accepts only a complete manifest with lowercase
SHA-256 and relative paths.

- [ ] **Step 4: Package the Windows application and fixed trial**

`package-outside-user-screening.ps1` must:

- run the Rover verify gate first;
- build `windows-release`;
- stage `prometheus_desktop.exe` and deploy Qt with `windeployqt --release
  --qmldir desktop/ui --compiler-runtime`;
- include the exact validated 967-file Rover snapshot, task sheet, blank form,
  source/license notice, and a double-click launch script;
- set `PROMETHEUS_STARTUP_PROJECT_FOLDER` only in that launch script;
- generate a sorted SHA-256 manifest for every staged payload file (the
  manifest itself is the sole unlisted file);
- verify the stage, then create a versioned ZIP under `out/outside-user`.

The package must not contain source-tree paths, prior developer scan output, a
completed observation form, or expected answers.

- [ ] **Step 5: Test on a clean Windows user or VM**

Copy only the ZIP to a clean Windows account/VM with no source checkout,
extract it, verify the manifest, double-click the launcher, and confirm that
the app opens the fixed folder without installing build tools. This is package
verification by the developer, not the outside-user evidence session.

- [ ] **Step 6: Commit the protocol and packaging code**

```bash
git add docs/trials/outside-user-screening-task-sheet.md \
  docs/trials/outside-user-screening-observation-form.md \
  docs/trials/outside-user-screening-facilitator-protocol.md \
  scripts/package-outside-user-screening.ps1 \
  scripts/verify-outside-user-bundle.ps1 \
  scripts/tests/outside-user-bundle-fixture.ps1 \
  docs/program/01-trust-kernel/01d-multi-project-evidence.md
git commit -m "Prepare outside-user folder screening"
```

### Task 6: Conduct the outside-user session

**Files:**
- Create after a real session: `docs/trials/outside-user-screening-session-001.md`
- Modify after a real session: `docs/program/01-trust-kernel/01d-multi-project-evidence.md`
- Modify after a real session: `docs/milestone-status.md`

- [ ] **Step 1: Confirm participant eligibility**

The participant must not be the project owner, an implementing developer, or
an AI agent, and must not have rehearsed the task. Give them only the verified
package and participant sheet. The facilitator may handle consent and the
clock but may not explain the UI during the task.

- [ ] **Step 2: Observe without coaching**

Record timestamps, actions, statements, confusion, workarounds, failures, and
every intervention as they occur. If the participant asks for help, record the
question before deciding whether to end the unassisted portion.

- [ ] **Step 3: Preserve the raw factual result**

Copy the completed form into a dated session record, redact only personal
identifiers, and identify any redaction. Do not turn an incorrect answer into a
pass, omit friction, or infer understanding that the participant did not state.

- [ ] **Step 4: Update the gate status honestly**

Classify observed product problems separately from package/setup problems. Add
follow-up work for every consequential failure. Close Program 01D only if the
required independent session actually occurred and the record is committed;
the participant is not required to succeed for the evidence gate to be real.

- [ ] **Step 5: Commit the real session record**

```bash
git add docs/trials/outside-user-screening-session-001.md \
  docs/program/01-trust-kernel/01d-multi-project-evidence.md \
  docs/milestone-status.md
git commit -m "Record outside-user folder screening evidence"
```

Until a real participant completes these steps, report Task 6 as externally
blocked and leave Program 01D active. Automated tests, developer rehearsals,
and AI review cannot satisfy it.
