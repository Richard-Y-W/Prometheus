# Linux Structural Artifact Role Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make strict embedded structural archive role validation deterministic under GCC/Linux without weakening role order, uniqueness, schema, or artifact-identity checks.

**Architecture:** Keep the existing archive packer, manifest schemas, and ordered comparison unchanged. Replace only the validator's conditional temporary `std::initializer_list` construction with fixed `std::array` iteration and explicit one-sample/two-sample branches.

**Tech Stack:** C++20, nlohmann/json, CMake/CTest, GCC 13 in the pinned Linux ARM64 validation image, and Apple Clang on macOS.

---

### Task 1: Make expected structural roles cross-compiler deterministic

**Files:**

- Modify: `desktop/run_store/src/run_store.cpp:662-671`
- Exercise: `desktop/run_store/tests/run_store_transaction_tests.cpp:1332-1650`

- [ ] **Step 1: Reconfirm the existing Linux regression is RED**

Run the repository read-only in the already-installed pinned image:

```sh
docker run --rm --platform linux/arm64 \
  -v /Users/byungkim/Prometheus:/workspace:ro \
  -w /workspace \
  prometheus-structural-validation-arm64:ccx-2.23 \
  /bin/sh -lc 'cmake -S . -B /tmp/prometheus-ci-repro -G Ninja \
    -DPROMETHEUS_BUILD_DESKTOP=OFF \
    -DPROMETHEUS_BUILD_EXECUTION=ON \
    -DPROMETHEUS_BUILD_INTEGRITY=ON \
    -DPROMETHEUS_BUILD_REPLAY=ON \
    -DPROMETHEUS_BUILD_RUN_STORE=ON \
    -DPROMETHEUS_ENABLE_OCCT=OFF \
    -DBUILD_TESTING=ON && \
    cmake --build /tmp/prometheus-ci-repro \
      --target prometheus_run_store_transaction_tests && \
    ctest --test-dir /tmp/prometheus-ci-repro \
      -R "^prometheus_run_store_transaction$" --output-on-failure'
```

Expected: FAIL at `publish embedded structural v3 archive` with
`structural_project_manifest_invalid` and
`embedded structural artifact role is invalid`.

- [ ] **Step 2: Replace the temporary-list construction with fixed arrays**

In `validate_embedded_structural_graph`, replace the current `expectedRoles`
loop with exactly this bounded construction:

```cpp
    constexpr std::array artifactRoles{
        "setup", "deck", "dat", "frd", "sta", "stdout", "stderr"};
    constexpr std::array refinementSamples{"coarse", "fine"};
    std::vector<std::string> expectedRoles;
    expectedRoles.reserve(expectedArtifactCount);
    if (projectReferenceV2) {
      for (const auto sample : refinementSamples)
        for (const auto role : artifactRoles)
          expectedRoles.push_back(std::string(sample) + "/" + role);
    } else {
      for (const auto role : artifactRoles)
        expectedRoles.emplace_back(role);
    }
```

Do not alter the comparison at `run_store.cpp:699`, the uniqueness set, the
declared-artifact lookup, accepted schema pairs, or any diagnostic code.

- [ ] **Step 3: Run the exact Linux regression to GREEN**

Rerun the command from Step 1 against the modified branch.

Expected: `prometheus_run_store_transaction` passes 1/1. Its v3 and v4
round trips succeed, while its malformed archive/project cases remain rejected.

- [ ] **Step 4: Run the focused macOS regression**

```sh
cmake --build --preset headless-debug \
  --target prometheus_run_store_transaction_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_run_store_transaction$' --output-on-failure
```

Expected: PASS 1/1.

- [ ] **Step 5: Inspect the production diff**

```sh
git diff -- desktop/run_store/src/run_store.cpp
git diff --check
```

Expected: one bounded role-construction change and no whitespace errors.

- [ ] **Step 6: Commit the implementation after explicit authorization**

```sh
git add -- desktop/run_store/src/run_store.cpp
git commit -m "fix: stabilize structural artifact role validation"
```

Expected: the commit contains only `desktop/run_store/src/run_store.cpp`.

### Task 2: Verify the complete affected boundary and record evidence

**Files:**

- Modify: `docs/superpowers/specs/2026-08-20-linux-structural-role-validation-design.md`

- [ ] **Step 1: Run the complete macOS headless suite**

```sh
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
```

Expected: all 17 registered headless tests pass.

- [ ] **Step 2: Recheck repository hygiene and branch scope**

```sh
git diff --check
git status --short --branch
git diff --name-only main...HEAD
```

Expected: no whitespace errors, no unrelated files, and only the approved
design/plan, the bounded production fix, and its validation record.

- [ ] **Step 3: Record the exact verification evidence**

Change the design document status to `Implemented and locally verified` and
append a `Verification` section containing the observed Linux focused result,
macOS focused result, full headless count, and the GitHub Actions run that first
exposed the regression. Do not claim a new GitHub matrix pass until a pushed
commit actually completes one.

- [ ] **Step 4: Commit the evidence after explicit authorization**

```sh
git add -- \
  docs/superpowers/specs/2026-08-20-linux-structural-role-validation-design.md
git commit -m "docs: record Linux role validation evidence"
```

Expected: the commit contains only the updated design record.

- [ ] **Step 5: Stop before external publication**

Report the commits and verification results. Push the feature branch or merge
it to `main` only after the user separately authorizes those actions.
