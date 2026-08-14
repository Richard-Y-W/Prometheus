# Windows-First Project Screening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user select a real mechanical-project folder in the desktop, account for every file, load an unambiguous STEP assembly, and see truthful static-geometry findings plus unevaluated questions.

**Architecture:** A small Qt/C++ scanner produces a session-only artifact inventory and exposes it through `ProjectIntakeController`. The existing `CadController` remains the sole STEP/OCCT boundary, while `EngineeringController` compiles the imported geometry facts into findings and non-passing unknowns. A focused QML panel connects the workflow without changing the strict project/run-store schemas.

**Tech Stack:** C++20, Qt 6 Core/Concurrent/Quick, Qt Test, QML, Open Cascade through the existing optional adapter, CMake/CTest.

---

## File map

- Create `desktop/app/project_intake.hpp`: scanner result types, pure scan API, and QML controller interface.
- Create `desktop/app/project_intake.cpp`: recursive accounting, hashing, classification, summaries, and asynchronous controller plumbing.
- Create `desktop/app/tests/project_intake_tests.cpp`: folder-accounting and controller tests.
- Modify `desktop/app/engineering_controller.hpp`: expose unknowns and the static-evaluation input.
- Modify `desktop/app/engineering_controller.cpp`: compile zero-intersection evidence and honest unknowns.
- Create `desktop/app/tests/engineering_controller_tests.cpp`: focused screening behavior tests.
- Create `desktop/ui/ProjectInventoryPanel.qml`: visible inventory and explicit STEP selection.
- Modify `desktop/ui/Main.qml`: folder chooser, automatic unambiguous STEP load, initial screen, and result access.
- Modify `desktop/app/main.cpp`: create and expose `ProjectIntakeController`.
- Modify `desktop/app/tests/qml_authority_tests.cpp`: provide the new context property and instantiate/click the inventory panel.
- Modify `desktop/app/CMakeLists.txt`: compile the controller, QML file, and focused tests.

## Task 1: Whole-folder artifact accounting

**Files:**
- Create: `desktop/app/project_intake.hpp`
- Create: `desktop/app/project_intake.cpp`
- Create: `desktop/app/tests/project_intake_tests.cpp`
- Modify: `desktop/app/CMakeLists.txt`

- [ ] **Step 1: Write the failing scanner test**

Create a temporary folder containing `assembly.step`, `docs/spec.pdf`,
`.notes.txt`, and `raw.bin`. Assert that `scanProjectFolder()` succeeds,
returns four deterministically ordered rows, records exact byte lengths and
`sha256:` digests, marks STEP `ready`, marks PDF/text `not_evaluated`, marks
the binary file `unsupported`, and selects the STEP file as `primary_step_path`.
Add separate assertions for an empty directory, a nonexistent root, and two
STEP files producing no automatic primary choice.

```cpp
const auto result = scanProjectFolder(QUrl::fromLocalFile(root).toLocalFile());
require(result.ok, "project folder scans");
require(result.artifacts.size() == 4, "every file is accounted for");
require(result.primary_step_path.endsWith("assembly.step"),
        "one STEP file is unambiguous");
require(result.artifacts.front().value("sha256").toString().startsWith("sha256:"),
        "readable file receives SHA-256");
```

- [ ] **Step 2: Configure and run the focused target to verify RED**

Run:

```bash
cmake -S . -B out/build/macos-occt-debug -DPROMETHEUS_BUILD_DESKTOP=ON -DPROMETHEUS_ENABLE_OCCT=ON
cmake --build out/build/macos-occt-debug --target prometheus_project_intake_tests
```

Expected: configuration or compilation fails because the intake files and
target do not exist.

- [ ] **Step 3: Implement the minimal scanner and controller**

Expose this API:

```cpp
struct ProjectIntakeResult final {
  bool ok{};
  QString root_path;
  QString error;
  QVariantList artifacts;
  QString primary_step_path;
};

ProjectIntakeResult scanProjectFolder(const QString &rootPath);

class ProjectIntakeController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString rootPath READ rootPath NOTIFY changed)
  Q_PROPERTY(QVariantList artifacts READ artifacts NOTIFY changed)
  Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
  Q_PROPERTY(int readyCount READ readyCount NOTIFY changed)
  Q_PROPERTY(int notEvaluatedCount READ notEvaluatedCount NOTIFY changed)
  Q_PROPERTY(int unsupportedCount READ unsupportedCount NOTIFY changed)
  Q_PROPERTY(QString primaryStepPath READ primaryStepPath NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
  explicit ProjectIntakeController(QObject *parent = nullptr);
  Q_INVOKABLE void scanFolder(const QUrl &folder);
  QString rootPath() const { return result_.root_path; }
  QVariantList artifacts() const { return result_.artifacts; }
  int totalCount() const { return result_.artifacts.size(); }
  int readyCount() const;
  int notEvaluatedCount() const;
  int unsupportedCount() const;
  QString primaryStepPath() const { return result_.primary_step_path; }
  QString status() const;
  QString error() const { return result_.error; }
  bool busy() const { return busy_; }
signals:
  void changed();
  void scanFinished(bool success);
private:
  void apply(ProjectIntakeResult result);
  ProjectIntakeResult result_;
  QFutureWatcher<ProjectIntakeResult> watcher_;
  bool busy_{};
};
```

Each artifact map has exactly these presentation fields:

```cpp
QVariantMap{{"relative_path", relativePath},
            {"absolute_path", absolutePath},
            {"name", information.fileName()},
            {"extension", information.suffix().toLower()},
            {"byte_size", information.size()},
            {"sha256", digest},
            {"category", category},
            {"analysis_state", state},
            {"detail", detail},
            {"loadable", state == "ready"}};
```

Use `QDirIterator` without `FollowSymlinks`, include hidden/system files, hash
readable regular files in 1 MiB chunks, retain symlinks as unsupported rows,
and sort by relative path. Recognize STEP/STP as ready; common CAD, table,
document, structured-data, and source-code extensions as not evaluated; and
everything else as unsupported. Run the pure scanner with `QtConcurrent::run`.
Reject a second scan while `busy` is true.

- [ ] **Step 4: Run GREEN verification**

Run:

```bash
cmake --build out/build/macos-occt-debug --target prometheus_project_intake_tests
ctest --test-dir out/build/macos-occt-debug -R '^prometheus_project_intake$' --output-on-failure
```

Expected: the focused intake test passes.

- [ ] **Step 5: Commit the scanner slice**

```bash
git add desktop/app/project_intake.* desktop/app/tests/project_intake_tests.cpp desktop/app/CMakeLists.txt
git commit -m "feat: account for complete project folders"
```

## Task 2: Findings and honest unknowns

**Files:**
- Create: `desktop/app/tests/engineering_controller_tests.cpp`
- Modify: `desktop/app/engineering_controller.hpp`
- Modify: `desktop/app/engineering_controller.cpp`
- Modify: `desktop/app/CMakeLists.txt`

- [ ] **Step 1: Write the failing screening test**

Cover four observable cases:

```cpp
EngineeringController clear;
clear.runGeometryChecks({}, {}, false, true);
require(clear.findings().size() == 1, "evaluated clear assembly has one result");
require(clear.findings().front().toMap().value("title") ==
            "No static solid interference found",
        "clear result is bounded to static imported geometry");
require(clear.unknowns().size() >= 4, "missing mechanics remain visible");

EngineeringController deferred;
deferred.runGeometryChecks({}, {}, false, false);
require(deferred.findings().isEmpty(), "deferred work cannot become a pass");
require(deferred.unknowns().front().toMap().value("question") ==
            "Static solid interference",
        "deferred static work is an unknown");
```

Retain assertions for existing unclassified intersection and sampled-motion
collision behavior.

- [ ] **Step 2: Build the focused target to verify RED**

```bash
cmake --build out/build/macos-occt-debug --target prometheus_engineering_controller_tests
```

Expected: compilation fails because `unknowns()` and the fourth argument do
not exist.

- [ ] **Step 3: Implement the minimal screening behavior**

Add:

```cpp
Q_PROPERTY(QVariantList unknowns READ unknowns NOTIFY changed)
Q_PROPERTY(QVariantMap coverage READ coverage NOTIFY changed)
QVariantList unknowns() const { return unknowns_; }
QVariantMap coverage() const { return coverage_; }
Q_INVOKABLE void runGeometryChecks(const QVariantList &interferences = {},
                                   const QVariantList &sweepResults = {},
                                   bool sweepEvaluated = false,
                                   bool staticInterferenceEvaluated = true);
```

When static work completed with zero hits, add one informational
`static_interference` finding with `0 m³` and an assumption explicitly bounded
to the imported pose. When static work was deferred, add no finding and add a
`not_evaluated` unknown. Always expose material/mass, loads/restraints, and
structural strength as unknown. Expose motion as unknown unless a sampled sweep
was evaluated. Store `coverage` as counts of evaluated and not-evaluated
questions plus the literal status `partial` whenever unknowns remain.

- [ ] **Step 4: Run GREEN and existing geometry regression tests**

```bash
cmake --build out/build/macos-occt-debug --target prometheus_engineering_controller_tests prometheus_project_tests
ctest --test-dir out/build/macos-occt-debug -R 'prometheus_(engineering_controller|project_tests)' --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 5: Commit the screening slice**

```bash
git add desktop/app/engineering_controller.* desktop/app/tests/engineering_controller_tests.cpp desktop/app/CMakeLists.txt
git commit -m "feat: report bounded geometry coverage and unknowns"
```

## Task 3: Desktop folder-to-finding workflow

**Files:**
- Create: `desktop/ui/ProjectInventoryPanel.qml`
- Modify: `desktop/ui/Main.qml`
- Modify: `desktop/app/main.cpp`
- Modify: `desktop/app/tests/qml_authority_tests.cpp`
- Modify: `desktop/app/CMakeLists.txt`

- [ ] **Step 1: Write the failing QML workflow assertions**

Provide `projectIntakeController` when creating `Main.qml`. Instantiate
`ProjectInventoryPanel.qml` with a probe exposing one ready STEP row, find the
button with `objectName: "loadArtifactButton_0"`, click it, and assert that the
panel emits `loadRequested` with the exact absolute path.

```cpp
auto *load = requiredChild(panelRoot.get(), "loadArtifactButton_0");
require(QMetaObject::invokeMethod(load, "click"), "STEP load action clicks");
require(requestedPath == stepPath, "panel preserves the selected exact path");
```

- [ ] **Step 2: Build the QML test to verify RED**

```bash
cmake --build out/build/macos-occt-debug --target prometheus_qml_authority_tests
```

Expected: build or offscreen test fails because the new context property and
QML panel do not exist.

- [ ] **Step 3: Implement the panel and main-window wiring**

The panel exposes:

```qml
property var projectIntakeController
signal loadRequested(string path)
signal closeRequested()
```

It renders summary counts and one row per artifact, including state, byte
count, digest, and reason. Only a `loadable` row has a **Load assembly** button.

In `main.cpp`, construct `ProjectIntakeController intake;` and expose it as
`projectIntakeController`. In `Main.qml`, add **Open Folder**, a `FolderDialog`,
an inventory popup, and these connections:

```qml
Connections {
  target: projectIntakeController
  function onScanFinished(success) {
    if (success && projectIntakeController.primaryStepPath !== "")
      cadController.importStepAsync(projectIntakeController.primaryStepPath)
  }
}
Connections {
  target: cadController
  function onImportFinished(success) {
    if (!success) return
    setView(-24, -38, false)
    engineeringController.runGeometryChecks(
      cadController.interferences, [], false, !cadController.collisionDeferred)
    resultsDialog.open()
  }
}
```

Read `PROMETHEUS_STARTUP_PROJECT_FOLDER` in `main.cpp`, convert it with
`QUrl::fromLocalFile`, expose that URL as `startupProjectFolder`, and have
`Component.onCompleted` call
`projectIntakeController.scanFolder(startupProjectFolder)` when the URL is
nonempty. Provide an empty `QUrl{}` `startupProjectFolder` context property in
the offscreen QML test.

Update the existing sweep call to pass `!cadController.collisionDeferred` as
the fourth argument. Add an always-reachable **Screen Results** action when
parts are loaded. Display `engineeringController.unknowns` below the evaluated
findings in the existing result popup.

- [ ] **Step 4: Run GREEN offscreen verification**

```bash
cmake --build out/build/macos-occt-debug --target prometheus_desktop prometheus_qml_authority_tests
ctest --test-dir out/build/macos-occt-debug -R '^prometheus_qml_authority$' --output-on-failure
```

Expected: desktop and QML test pass.

- [ ] **Step 5: Commit the desktop slice**

```bash
git add desktop/ui/ProjectInventoryPanel.qml desktop/ui/Main.qml desktop/app/main.cpp desktop/app/tests/qml_authority_tests.cpp desktop/app/CMakeLists.txt
git commit -m "feat: open mechanical project folders in desktop"
```

## Task 4: Real-project evidence and Windows checkpoint

**Files:**
- Modify: `README.md`
- Modify: `docs/program/00-master-roadmap.md`

- [ ] **Step 1: Exercise the small real workflow**

Scan a folder containing `fixtures/assemblies/motor-arm.step`, import the sole
assembly, and verify the initial screen reports the fixture's confirmed static
intersection plus the explicit mechanics unknowns.

```bash
PROMETHEUS_STARTUP_PROJECT_FOLDER="$PWD/fixtures/assemblies" \
  out/build/macos-occt-debug/desktop/app/prometheus_desktop
```

Expected: the folder inventory accounts for the STEP file, the assembly loads,
and the screen contains an intersection result and unknowns.

- [ ] **Step 2: Exercise the large-project boundary**

```bash
PROMETHEUS_STARTUP_PROJECT_FOLDER="$PWD/out/external-demo" \
  out/build/macos-occt-debug/desktop/app/prometheus_desktop
```

Expected: OpenArm loads, static all-pairs interference is visibly deferred,
and no zero-interference pass is produced.

- [ ] **Step 3: Document the implemented boundary**

Update the README run instructions with **Open Folder**, the exact supported
formats, and the explicit unknown behavior. Replace the roadmap's immediate
01C universal-foundation gate with the new project-screening evidence gate;
retain acquisition, generalized graph, solver, and hardening work as later
evidence-driven steps.

- [ ] **Step 4: Run the focused local verification checkpoint**

```bash
cmake --build out/build/macos-occt-debug
ctest --test-dir out/build/macos-occt-debug --output-on-failure
git diff --check
```

Expected: build succeeds, all tests pass (the loopback HTTP test may require
the already-approved local-network permission), and the diff check is clean.

- [ ] **Step 5: Commit the evidence boundary**

```bash
git add README.md docs/program/00-master-roadmap.md
git commit -m "docs: adopt project-value capability gates"
```

- [ ] **Step 6: Push once and inspect the Windows job**

```bash
git push origin feature/program-01a-integrity-contracts
gh run list --branch feature/program-01a-integrity-contracts --limit 1
```

Expected: the branch push triggers the repository workflow. The Windows native
job must compile and test the new controller/QML code. The existing workflow is
not expanded into additional matrices in this slice. Pushing requires explicit
user authorization if it was not already granted for this work.
