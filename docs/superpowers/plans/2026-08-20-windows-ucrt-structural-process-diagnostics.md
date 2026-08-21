# Windows UCRT Structural Process Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing single-execution structural fixture test identify the first UCRT64 process/evidence invariant that fails without changing production execution.

**Architecture:** Keep diagnostics entirely in `structural_tests.cpp`. Format a bounded snapshot from the already-returned `SolverRunResult`, test that formatter without launching a process, and replace the aggregate success assertion with sequential named assertions that all inspect the same one fixture run.

**Tech Stack:** C++20, CMake/CTest, Windows UCRT64/MinGW, GitHub Actions.

---

### Task 1: Add a bounded solver-run diagnostic formatter

**Files:**
- Modify: `desktop/structural/tests/structural_tests.cpp:17-40`
- Test: `desktop/structural/tests/structural_tests.cpp`

- [ ] **Step 1: Add a failing formatter contract test**

After `require`, add a declaration for `solverRunDiagnostic`, then add this
contract near the beginning of `main` before any process fixture runs:

```cpp
ps::SolverRunResult diagnosticExample;
diagnosticExample.status = ps::SolverRunStatus::launch_failed;
diagnosticExample.detail = "process_launch_failed:193";
diagnosticExample.exit_code = -1;
diagnosticExample.standard_output = "ab";
diagnosticExample.standard_error = "c";
const auto diagnostic = solverRunDiagnostic(diagnosticExample);
require(diagnostic.find("solver_run.status=launch_failed") !=
                std::string::npos &&
            diagnostic.find("solver_run.detail=process_launch_failed:193") !=
                std::string::npos &&
            diagnostic.find("solver_run.stdout_bytes=2") !=
                std::string::npos &&
            diagnostic.find("solver_run.stderr_bytes=1") !=
                std::string::npos &&
            diagnostic.find("solver_run.result_present=false") !=
                std::string::npos,
        "solver run diagnostics expose bounded process state");
```

- [ ] **Step 2: Build to verify the formatter contract is red**

Run:

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
```

Expected: compilation or link failure because `solverRunDiagnostic` has no
implementation.

- [ ] **Step 3: Implement the bounded formatter**

Add `#include <sstream>`. Define an exhaustive status-name function and the
formatter before `main`:

```cpp
const char *solverRunStatusName(const ps::SolverRunStatus status) {
  switch (status) {
  case ps::SolverRunStatus::completed: return "completed";
  case ps::SolverRunStatus::launch_failed: return "launch_failed";
  case ps::SolverRunStatus::timed_out: return "timed_out";
  case ps::SolverRunStatus::nonzero_exit: return "nonzero_exit";
  case ps::SolverRunStatus::output_conflict: return "output_conflict";
  case ps::SolverRunStatus::output_missing: return "output_missing";
  case ps::SolverRunStatus::result_invalid: return "result_invalid";
  }
  return "unknown";
}

std::string solverRunDiagnostic(const ps::SolverRunResult &run) {
  std::ostringstream output;
  output << "solver_run.status=" << solverRunStatusName(run.status)
         << "\nsolver_run.detail=" << run.detail
         << "\nsolver_run.exit_code=" << run.exit_code
         << "\nsolver_run.stdout_bytes=" << run.standard_output.size()
         << "\nsolver_run.stderr_bytes=" << run.standard_error.size()
         << "\nsolver_run.result_present="
         << (run.validated_result ? "true" : "false");
  if (run.validated_result) {
    output << "\nsolver_run.result_complete="
           << (run.validated_result->complete() ? "true" : "false")
           << "\nsolver_run.issue_count="
           << run.validated_result->issues.size()
           << "\nsolver_run.metrics_present="
           << (run.validated_result->metrics ? "true" : "false");
    for (const auto &issue : run.validated_result->issues)
      output << "\nsolver_run.issue=" << issue.code;
    if (run.validated_result->metrics)
      output << "\nsolver_run.maximum_displacement_m="
             << run.validated_result->metrics->maximum_displacement_m;
  }
  return output.str();
}

void requireSolverRun(const bool condition, const ps::SolverRunResult &run,
                      const char *message) {
  if (!condition) {
    std::cerr << solverRunDiagnostic(run) << '\n';
    fail(message);
  }
}
```

This prints only controlled status/detail values, issue codes, sizes, and one
numeric metric. It never prints captured streams, paths, environment variables,
decks, or source artifacts.

- [ ] **Step 4: Build and run the formatter contract**

Run:

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug --output-on-failure -R '^prometheus_structural_tests$'
```

Expected: build succeeds and the existing structural test passes once.

### Task 2: Name each invariant after one fixture execution

**Files:**
- Modify: `desktop/structural/tests/structural_tests.cpp:1451-1463`
- Test: `desktop/structural/tests/structural_tests.cpp`

- [ ] **Step 1: Replace the aggregate assertion**

Keep this call unchanged and singular:

```cpp
const auto completed = runFixture("success", std::chrono::seconds(5));
```

Replace the combined `require` with:

```cpp
requireSolverRun(completed.status == ps::SolverRunStatus::completed,
                 completed, "isolated solver process completes");
requireSolverRun(completed.exit_code == 0, completed,
                 "isolated solver exits successfully");
requireSolverRun(completed.validated_result.has_value(), completed,
                 "isolated solver produces a validated result");
requireSolverRun(completed.validated_result->complete(), completed,
                 "isolated solver evidence is complete");
requireSolverRun(
    completed.validated_result->compiled_setup_identity == compiled.identity,
    completed, "isolated solver result retains compiled setup identity");
requireSolverRun(
    std::abs(completed.validated_result->metrics->maximum_displacement_m -
             2.0e-5) < 1e-15,
    completed, "isolated solver result retains expected displacement");
requireSolverRun(
    completed.standard_output.find("CalculiX Version 2.23") !=
        std::string::npos,
    completed, "isolated solver captures standard output");
requireSolverRun(
    completed.standard_error.find("fixture stderr") != std::string::npos,
    completed, "isolated solver captures standard error");
```

Each check consumes the same `completed` object. A failing earlier check exits,
so later optional dereferences remain guarded by the preceding assertions.

- [ ] **Step 2: Prove diagnostics add no fixture execution**

Run:

```bash
rg -n -C 2 'const auto (completed|staleOutputs) = runFixture\("success"' \
  desktop/structural/tests/structural_tests.cpp
```

Expected: the unchanged `completed` call performs the one successful process
execution. The unchanged later `staleOutputs` call verifies
`SolverRunStatus::output_conflict`; `run_calculix` returns before process launch
because the first call's output files already exist. Diagnostics add no call.

- [ ] **Step 3: Run focused and full local native checks**

Run:

```bash
cmake --build --preset headless-debug
ctest --preset headless-debug
git diff --check
```

Expected: the headless build succeeds, all headless tests pass, and diff hygiene
passes.

- [ ] **Step 4: Commit the diagnostics implementation**

```bash
git add -- desktop/structural/tests/structural_tests.cpp
git commit -m "test: diagnose Windows structural process boundary"
```

### Task 3: Publish and collect the UCRT64 evidence

**Files:**
- No source changes.
- Observe: `.github/workflows/structural-validation.yml`

- [ ] **Step 1: Confirm branch scope before publishing**

Run:

```bash
git status --short --branch
git diff --check main...HEAD
git log --oneline main..HEAD
```

Expected: a clean diagnostics branch with only the approved design, plan, and
test-diagnostics commits.

- [ ] **Step 2: Push the authorized diagnostics branch**

Run:

```bash
git push -u origin diagnose/windows-ucrt-structural-process
```

Expected: the remote branch is created without force pushing.

- [ ] **Step 3: Dispatch the manual structural workflow once on the branch**

Run:

```bash
gh workflow run structural-validation.yml \
  --repo Richard-Y-W/Prometheus \
  --ref diagnose/windows-ucrt-structural-process
```

Expected: exactly one new `workflow_dispatch` run whose `headSha` equals the
diagnostics branch head.

- [ ] **Step 4: Inspect the first failed invariant**

After the run completes, retrieve the failed job log and record:

- the named assertion;
- `solver_run.status` and `solver_run.detail`;
- exit code;
- output byte counts;
- result/metrics presence and completeness;
- issue codes and observed displacement when available.

Expected: the UCRT64 failure is localized to one process/evidence boundary. Do
not merge the diagnostics branch or implement a production fix in this plan.
