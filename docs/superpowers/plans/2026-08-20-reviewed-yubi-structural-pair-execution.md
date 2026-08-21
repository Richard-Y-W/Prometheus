# Reviewed YUBI Structural Pair Execution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the approved manual coarse/fine YUBI bracket case through the existing Qt-free structural core exactly once per mesh and retain a replayable v4 archive.

**Architecture:** A strict reviewed-pair manifest compiler verifies all shared inputs and both supplied meshes before execution. A small CLI receives the compiled pair, calls the existing CalculiX runner once for each sample, and passes the retained results to the existing refinement, finding, and archive compilers. A dedicated manual Windows workflow runs only this case and replays the resulting archive at the release boundary.

**Tech Stack:** C++20, nlohmann/json, Prometheus RFC 8785/SHA-256 integrity library, existing `prometheus_structural` library, CMake/CTest, PowerShell 7, GitHub Actions, CalculiX 2.23.

**Execution status:** Tasks 1–6 are complete. [Run
32503165787](https://github.com/Richard-Y-W/Prometheus/actions/runs/32503165787)
completed at commit `6195ec6275097bdb37c921c646b99e3084169cc0`; its
independently replayed engineering evaluation is `indeterminate`.

---

## File Map

- Create `desktop/structural/include/prometheus/structural/reviewed_pair.hpp`: public preflight result and manifest compiler declaration.
- Create `desktop/structural/src/reviewed_pair.cpp`: bounded file loading, strict manifest/material decoding, mesh preparation, patch drift checks, and setup/refinement compilation.
- Create `desktop/structural/tools/run_reviewed_structural_pair.cpp`: exactly-two-sample execution and concise result output.
- Create `desktop/structural/tests/reviewed_pair_tests.cpp`: focused manifest and preflight tests.
- Create `desktop/structural/tests/reviewed_pair_tool_fixture.cmake`: fake-solver end-to-end test and no-output-on-preflight-failure assertion.
- Modify `desktop/structural/CMakeLists.txt`: register the source, CLI, tests, and tool fixture.
- Create `fixtures/structural/reviewed-pair-smoke/*`: small deterministic pair, material record, and reviewed manifest for ordinary tests.
- Create `fixtures/structural/yubi-bracket/*`: exact approved meshes, material record, license notice, reviewed manifest, and claim-boundary README.
- Create `scripts/run-yubi-structural-slice.ps1`: bounded Windows build, execution, and replay entry point.
- Create `.github/workflows/yubi-structural-trial.yml`: manual Windows trial and artifact retention.
- Modify `docs/phase-03-structural-workflow.md`: record the prepared execution path without claiming a result before one exists.
- Modify `docs/master-project-status.md`: replace stale structural-gate status and identify the remaining YUBI execution/result step.

### Task 1: Compile a reviewed pair before any solver can run

**Files:**
- Create: `desktop/structural/include/prometheus/structural/reviewed_pair.hpp`
- Create: `desktop/structural/src/reviewed_pair.cpp`
- Create: `desktop/structural/tests/reviewed_pair_tests.cpp`
- Create: `fixtures/structural/reviewed-pair-smoke/coarse.inp`
- Create: `fixtures/structural/reviewed-pair-smoke/fine.inp`
- Create: `fixtures/structural/reviewed-pair-smoke/material-evidence.json`
- Create: `fixtures/structural/reviewed-pair-smoke/reviewed-pair.json`
- Modify: `desktop/structural/CMakeLists.txt`

- [x] **Step 1: Add the small pair fixtures**

Copy the existing one-tetra UI mesh into `coarse.inp` and the six-tetra package
mesh into `fine.inp`. Add a one-candidate material document and a manifest with
exact hashes, safe local filenames, shared hypothetical inputs, distinct job
names, patch IDs and expected patch geometry. Use a `0.05` mesh-quality floor
for this numerical plumbing fixture; it is not YUBI evidence.

- [x] **Step 2: Write failing preflight tests**

Define the intended API in the test:

```cpp
const auto pair = ps::preflight_reviewed_structural_pair(manifest);
require(pair.coarse_setup.request.nodal_forces.size() > 0U,
        "coarse reviewed force compiles");
require(pair.fine_setup.request.nodal_forces.size() > 0U,
        "fine reviewed force compiles");
require(pair.criterion.observables().size() == 2U,
        "global displacement and stress observables are locked");
require(pair.coarse_job_name != pair.fine_job_name,
        "pair jobs are distinct");
```

Copy the fixture directory to a temporary directory for mutation tests. Assert
that duplicate JSON members, an unknown member, a changed mesh hash, a changed
material value, an incorrect patch area, an absent patch ID, a repeated mesh
hash, and a fine mesh with a non-smaller target size each throw a stable
`reviewed_pair_*` error.

- [x] **Step 3: Run the test to verify RED**

Run:

```bash
cmake --fresh --preset headless-debug
cmake --build --preset headless-debug --target prometheus_reviewed_pair_tests
```

Expected: configuration or compilation fails because the reviewed-pair API and
test target do not exist.

- [x] **Step 4: Define the minimal public contract**

Add:

```cpp
struct PreparedReviewedStructuralPair final {
  std::filesystem::path manifest_path;
  std::string manifest_identity;
  std::string coarse_job_name;
  std::string fine_job_name;
  StructuralRefinementCriterion criterion;
  CompiledStructuralSetup coarse_setup;
  CompiledStructuralSetup fine_setup;
  ReviewedBoundaryCorrespondence boundary_correspondence;
};

[[nodiscard]] PreparedReviewedStructuralPair
preflight_reviewed_structural_pair(
    const std::filesystem::path &manifest_path);
```

Keep execution out of this type. A caller cannot receive a prepared pair until
both samples and their shared refinement boundary have compiled.

- [x] **Step 5: Implement strict bounded decoding**

In `reviewed_pair.cpp`:

- bound the manifest at 256 KiB and each mesh at 64 MiB;
- canonicalize with the shared RFC 8785 implementation before parsing so
  duplicate members fail;
- require exact key sets at every manifest object;
- accept only schema/version 1.0.0 and `review.status == "approved"`;
- validate lowercase prefixed SHA-256 values and safe nonempty text;
- accept only a bare filename for every referenced artifact and reject
  symlinks;
- verify every referenced file hash before parsing;
- locate exactly one selected material candidate and compare all approved
  material fields; and
- reject non-finite, non-positive, or out-of-domain engineering inputs.

Throw stable errors such as:

```cpp
throw std::invalid_argument(
    "reviewed_pair_mesh_hash_mismatch: coarse mesh bytes changed");
```

- [x] **Step 6: Prepare and cross-check both samples**

For each sample, call:

```cpp
auto prepared = prepare_gmsh_abaqus_mesh(mesh_bytes, coordinate_scale_to_m);
auto patches = group_boundary_faces(
    prepared.boundary_faces, selection_patch_angle_degrees);
auto load = resolve_boundary_selection(
    shared_load_label, patches, load_patch_ids);
auto restraint = resolve_boundary_selection(
    shared_restraint_label, patches, restraint_patch_ids);
```

Compare counts, computed minimum mean ratio, and selected patch area, centroid,
and normal with the manifest. Build `StructuralSetup` from computed objects and
call `compile_structural_setup()`. After both compile, require distinct mesh
hashes, more fine elements, a smaller fine target size, equal coordinate scale,
and explicit load/restraint correspondence confirmation. Compile
`global_structural_observable_specs(0.10)` and call
`review_structural_boundary_correspondence()`.

- [x] **Step 7: Run focused tests to verify GREEN**

Run:

```bash
cmake --build --preset headless-debug --target prometheus_reviewed_pair_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_reviewed_pair_tests$' --output-on-failure
```

Expected: one focused CTest passes and every mutation fails with its expected
stable code.

- [x] **Step 8: Commit the preflight compiler**

```bash
git add desktop/structural/CMakeLists.txt \
  desktop/structural/include/prometheus/structural/reviewed_pair.hpp \
  desktop/structural/src/reviewed_pair.cpp \
  desktop/structural/tests/reviewed_pair_tests.cpp \
  fixtures/structural/reviewed-pair-smoke
git commit -m "Add reviewed structural pair preflight"
```

### Task 2: Execute each prepared sample once and archive retained results

**Files:**
- Create: `desktop/structural/tools/run_reviewed_structural_pair.cpp`
- Create: `desktop/structural/tests/reviewed_pair_tool_fixture.cmake`
- Modify: `desktop/structural/CMakeLists.txt`

- [x] **Step 1: Write the failing tool-fixture test**

Register a CMake fixture that runs the future CLI with the checked-in smoke
manifest and existing fake solver. Require:

```text
status=completed
refinement=accepted
evaluation=no_violation_detected_within_scope
archive_schema_version=4.0.0
```

Assert that the output directory contains exactly the two distinct `.inp`,
`.dat`, `.frd`, and `.sta` job sets plus one
`prometheus-structural-run.json`. Run the existing replay CLI on that manifest
and require `status=verified`.

Create a mutated manifest with a false coarse hash, run the CLI against a fresh
output path, require nonzero exit, and assert that the output path was never
created.

- [x] **Step 2: Run the fixture to verify RED**

Run:

```bash
cmake --build --preset headless-debug --target prometheus_reviewed_pair_tool_fixture
```

Expected: the target is unavailable because the CLI has not been added.

- [x] **Step 3: Implement the thin CLI**

Parse:

```text
prometheus_run_reviewed_structural_pair \
  REVIEWED_PAIR_JSON CCX OUTPUT_DIRECTORY [TIMEOUT_SECONDS]
```

Call `preflight_reviewed_structural_pair()` before creating the output
directory. Require that the output path does not exist. Then:

```cpp
auto coarse_run = ps::run_calculix(coarse_options, pair.coarse_setup);
auto coarse = ps::compile_completed_structural_sample(
    ps::StructuralSampleRole::coarse, pair.criterion,
    coarse_options, pair.coarse_setup, std::move(coarse_run));

auto fine_run = ps::run_calculix(fine_options, pair.fine_setup);
auto fine = ps::compile_completed_structural_sample(
    ps::StructuralSampleRole::fine, pair.criterion,
    fine_options, pair.fine_setup, std::move(fine_run));
```

Stop after a failed coarse run; do not start fine. After both complete, compile
the existing refinement, findings, and v4 archive. Print the manifest identity,
both setup/result identities, observable changes, refinement status, finding or
unknown disposition, archive path, and archive hash. Return zero for a complete
archived indeterminate evaluation, and nonzero for preflight, solver, evidence,
or archive failure.

- [x] **Step 4: Run tool and replay tests to verify GREEN**

Run:

```bash
cmake --build --preset headless-debug --target \
  prometheus_run_reviewed_structural_pair \
  prometheus_reviewed_pair_tool_fixture
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_reviewed_pair_(tests|tool_fixture)$' --output-on-failure
```

Expected: both focused tests pass; the valid run has two job sets and the
preflight-failure run has no output directory.

- [x] **Step 5: Commit the runner**

```bash
git add desktop/structural/CMakeLists.txt \
  desktop/structural/tools/run_reviewed_structural_pair.cpp \
  desktop/structural/tests/reviewed_pair_tool_fixture.cmake
git commit -m "Run reviewed structural pairs once"
```

### Task 3: Freeze the approved YUBI meshes and review record

**Files:**
- Create: `fixtures/structural/yubi-bracket/BRACKET_GRIPPER.coarse.inp`
- Create: `fixtures/structural/yubi-bracket/BRACKET_GRIPPER.fine.inp`
- Create: `fixtures/structural/yubi-bracket/aluminum-2024-candidates-v1.json`
- Create: `fixtures/structural/yubi-bracket/YUBI-HARDWARE-LICENSE.txt`
- Create: `fixtures/structural/yubi-bracket/reviewed-pair.json`
- Create: `fixtures/structural/yubi-bracket/README.md`
- Modify: `desktop/structural/tests/reviewed_pair_tests.cpp`

- [x] **Step 1: Install the exact reviewed artifacts**

Copy the approved mesh bytes with these hashes:

```text
coarse sha256:0f1e3dcd8d6a7e80ae7dd580e45e6fbca42db3d15e204b1f686fea80e4462a0e
fine   sha256:20295abe4b4748072f35362234e99a66a5e4651a31a5ae66f4c2179706ea51a4
```

Copy the existing material-evidence bytes and the pinned Toyota
CERN-OHL-W-2.0 license notice into the same directory. Record each exact hash in
the manifest.

- [x] **Step 2: Add the approved manifest and claim boundary**

Record every value in the design spec, including the exact per-sample counts,
quality values, selected patch IDs, selected area/centroid/normal, source
commit, source STEP path/hash, and review date. The README must state that the
material, load, restraint idealization, and displacement threshold are
exploratory assumptions and list all excluded analyses beside the runnable
command.

- [x] **Step 3: Write and run a YUBI preflight test**

Add a test that loads the real manifest without a solver and requires:

```cpp
require(pair.coarse_setup.request.nodes.size() == 2446U,
        "reviewed YUBI coarse node count");
require(pair.coarse_setup.request.elements.size() == 7533U,
        "reviewed YUBI coarse element count");
require(pair.fine_setup.request.nodes.size() == 7876U,
        "reviewed YUBI fine node count");
require(pair.fine_setup.request.elements.size() == 29015U,
        "reviewed YUBI fine element count");
require(pair.fine_setup.request.displacement_limit_m == 0.0005,
        "informational displacement threshold retained");
require(!pair.fine_setup.request.von_mises_limit_pa,
        "no unsupported YUBI stress allowable invented");
```

Run the focused test and expect PASS. This test parses and compiles meshes but
does not start CalculiX.

- [x] **Step 4: Commit the reviewed YUBI inputs**

```bash
git add fixtures/structural/yubi-bracket \
  desktop/structural/tests/reviewed_pair_tests.cpp
git commit -m "Record reviewed YUBI structural pair"
```

### Task 4: Add one explicit Windows execution checkpoint

**Files:**
- Create: `scripts/run-yubi-structural-slice.ps1`
- Create: `.github/workflows/yubi-structural-trial.yml`

- [x] **Step 1: Add a script contract test before the script**

Extend `desktop/structural/tests/reviewed_pair_tool_fixture.cmake` or add a
source-contract assertion in `reviewed_pair_tests.cpp` that the future script:

- names only the reviewed-pair runner and replay tool;
- does not invoke Gmsh or `run-structural-validation.ps1`;
- rejects an existing output directory; and
- requires the runner and replay success markers.

Run the focused test and expect FAIL because the script is absent.

- [x] **Step 2: Implement the bounded PowerShell entry point**

Resolve `ccx.exe` only from `C:\msys64\ucrt64\bin`, configure the existing
`windows-structural-release` preset, and build only:

```text
prometheus_run_reviewed_structural_pair
prometheus_replay_structural_run
prometheus_reviewed_pair_tests
```

Run the focused CTest, invoke the YUBI manifest once, extract the emitted
archive path, replay it once, and write the runner and replay console streams
to the trial directory. Do not call Gmsh or the analytic validation script.

- [x] **Step 3: Add the manual workflow**

Use `windows-2022`, SHA-pinned checkout, MSYS2 setup, and artifact-upload
actions. Install UCRT64 CalculiX, GCC, and Ninja, run
`scripts/run-yubi-structural-slice.ps1`, and upload
`out/validation/yubi-bracket` even when the engineering evaluation is
indeterminate. Do not add the workflow to pull-request or push triggers.

- [x] **Step 4: Run source checks and commit**

Run:

```bash
pwsh -NoProfile -File scripts/run-yubi-structural-slice.ps1 -WhatIf
```

If the local host cannot execute the Windows-only solver section, use the
script's bounded preflight/source-check mode and rely on the CMake fake-solver
fixture for local execution coverage. Then run the focused CTest and commit:

```bash
git add scripts/run-yubi-structural-slice.ps1 \
  .github/workflows/yubi-structural-trial.yml \
  desktop/structural/tests/reviewed_pair_tool_fixture.cmake \
  desktop/structural/tests/reviewed_pair_tests.cpp
git commit -m "Add manual YUBI structural trial"
```

### Task 5: Verify locally and update status without overclaiming

**Files:**
- Modify: `docs/phase-03-structural-workflow.md`
- Modify: `docs/master-project-status.md`

- [x] **Step 1: Run the bounded local release checks**

Run:

```bash
cmake --fresh --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug --output-on-failure
cmake --fresh --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure
git diff --check
cmake --list-presets
```

Expected: all available local suites pass, the diff has no whitespace errors,
and CMake parses every preset. If the managed sandbox blocks only existing
loopback HTTP tests, rerun those exact tests outside that socket sandbox and
record both outcomes.

- [x] **Step 2: Update the two status documents**

At that preparation checkpoint, record that the current Windows structural
validation is already green, the reviewed YUBI execution path and exact inputs
are prepared, and a native YUBI result will not exist until the manual workflow
completes. Remove stale statements that the user choices or coarse/fine pair
are unresolved. Preserve the explicit non-claims about material identity,
strength, safety, and project-wide correctness.

- [x] **Step 3: Commit the verified implementation record**

```bash
git add docs/phase-03-structural-workflow.md docs/master-project-status.md \
  docs/superpowers/specs/2026-08-20-reviewed-yubi-structural-pair-execution-design.md \
  docs/superpowers/plans/2026-08-20-reviewed-yubi-structural-pair-execution.md
git commit -m "Document reviewed YUBI execution boundary"
```

- [x] **Step 4: Run final branch verification**

Run the headless and desktop suites again from the final commit, inspect
`git status --short --branch`, and confirm that no existing structural
numerical, solver, refinement, finding, or archive source was changed. If any
such source changed, schedule the full manual structural-validation workflow in
addition to the YUBI trial.

### Task 6: Produce the native YUBI evidence checkpoint

**Files:**
- Create after a successful workflow only: `docs/trials/yubi-bracket-structural-result.md`
- Modify after a successful workflow only: `docs/phase-03-structural-workflow.md`
- Modify after a successful workflow only: `docs/master-project-status.md`

- [x] **Step 1: Push the reviewed branch and dispatch the manual workflow**

Push only after local verification and explicit integration approval. Dispatch
`.github/workflows/yubi-structural-trial.yml` at the exact pushed commit.

- [x] **Step 2: Inspect and replay the retained artifact**

Download the workflow artifact, verify its hashes, and run:

```bash
out/build/headless-debug/desktop/structural/prometheus_replay_structural_run \
  /absolute/path/to/prometheus-structural-run.json
```

Expected: `status=verified`. Record the exact workflow URL, commit, CalculiX
version and executable hash, archive hash, coarse/fine mesh hashes, observable
changes, refinement status, finding or unknown, and limitations.

Recorded evidence:

- workflow run: `32503165787`, attempt 1;
- artifact: `yubi-structural-trial-32503165787-1`;
- artifact digest:
  `sha256:42030c8fd85c944b7c313c12bdae0ce4b3c5bedf6a34440c904391ec82891b25`;
- archive manifest SHA-256:
  `7794c99815e7ccfed597e860fa16d60a566a19fd25d801f7ad8137ba030b12a7`;
- independent replay: `status=verified`, fine maximum displacement
  `7.70501e-09 m`, fine maximum von Mises stress `143224 Pa`, and coverage
  `0/1`.

- [x] **Step 3: Write the evidence-bound result document**

Create `docs/trials/yubi-bracket-structural-result.md` only from the replayed
archive. State the measured displacement and its informational comparison if
the refinement is accepted. If either global observable exceeds 10%, state
that the evaluation is indeterminate and do not alter the criterion. Never add
a stress pass/fail because no stress allowable was reviewed.

The result is recorded in
[`docs/trials/yubi-bracket-structural-result.md`](../../trials/yubi-bracket-structural-result.md).
Global displacement changed by `7.04380451306449%`; global von Mises stress
changed by `14.01320289035979%`. The locked 10% rule therefore retained an
indeterminate evaluation with zero findings and `0/1` evaluated obligations.

- [x] **Step 4: Commit and rerun documentation hygiene**

Run `git diff --check`, inspect every numerical claim against the archive, and
commit with a literal result-scoped message such as:

```bash
git commit -m "Record exploratory YUBI displacement result"
```

Use `Record indeterminate YUBI refinement result` instead if the pair does not
meet the predeclared convergence criterion.
