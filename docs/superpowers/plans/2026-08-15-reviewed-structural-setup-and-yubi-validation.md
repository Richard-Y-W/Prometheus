# Reviewed Structural Setup and YUBI Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let an engineer visibly review one bounded linear-static setup, prove the complete CalculiX path on an analytic benchmark, and only then produce a narrowly scoped YUBI bracket result.

**Architecture:** Keep mesh, load, request, result, and finding calculations in the Qt-free `prometheus_structural` library. A Qt adapter exposes those typed outputs to QML; QML collects and displays choices but performs no engineering calculation. Windows scripts run the same core and result compiler used by the desktop.

**Tech Stack:** C++20, Qt 6/QML/Qt Quick 3D, CMake/CTest, Gmsh 4.15, CalculiX 2.23, PowerShell, canonical JSON evidence records.

**Dependency:** Complete `2026-08-15-structural-trust-seams.md` first. Do not start a YUBI solve until Tasks 1–4 below pass and Task 5 has a real user-reviewed setup.

---

## File map

- `desktop/app/structural_setup_controller.hpp` and `.cpp`: Qt adapter, review-state invalidation, mesh preview, and reviewed request export.
- `desktop/app/tests/structural_setup_controller_tests.cpp`: controller boundary and invalidation tests.
- `desktop/structural/include/prometheus/structural/structural_case.hpp` and
  `src/structural_case.cpp`: strict canonical reviewed-case contract shared by
  desktop and headless tools.
- `desktop/ui/StructuralSetupPanel.qml`: mesh/group display and explicit review inputs.
- `desktop/ui/Main.qml`: structural workspace entry point.
- `desktop/app/main.cpp` and `desktop/app/CMakeLists.txt`: controller registration, QML resource, and test wiring.
- `desktop/structural/include/prometheus/structural/structural_finding.hpp` and `src/structural_finding.cpp`: scoped pass/fail/indeterminate compiler.
- `desktop/structural/tools/export_structural_case.cpp`: deterministic reviewed request/deck export for scripts.
- `desktop/structural/tools/verify_structural_case.cpp`: one production result-verification entry point.
- `fixtures/evidence/aluminum-2024-candidates-v1.json`: source-located material candidates, never silent defaults.
- `fixtures/structural/ui/two-group-tetra.inp`: complete tiny surface-group
  fixture for controller and screenshot tests.
- `fixtures/structural/ui/two-group-tetra.geo`: exact source geometry for the
  UI fixture's geometry identity.
- `fixtures/structural/ui/two-group-tetra.candidate.json`: exact geometry/mesh
  identities and units for the UI fixture.
- `fixtures/structural/tension-bar/tension-bar.geo`: analytic validation geometry and named boundary groups.
- `fixtures/structural/tension-bar/expectations.json`: predeclared analytic values, limits, and refinement tolerances.
- `scripts/run-structural-validation.ps1`: mesh, solve, verify, and compare the benchmark cases.
- `scripts/mesh-yubi-structural-slice.ps1`: preserve one selectable group per imported Gmsh surface entity.
- `scripts/run-yubi-structural-slice.ps1`: blocked-until-reviewed YUBI execution.
- `docs/evidence/2024-aluminum-material-candidates.md`: research basis and applicability limits.
- `docs/trials/yubi-bracket-structural-result.md`: generated only after the gates pass.

### Task 1: Add a review-state model and Qt structural adapter

**Files:**
- Create: `desktop/app/structural_setup_controller.hpp`
- Create: `desktop/app/structural_setup_controller.cpp`
- Create: `desktop/app/tests/structural_setup_controller_tests.cpp`
- Create: `desktop/structural/include/prometheus/structural/structural_case.hpp`
- Create: `desktop/structural/src/structural_case.cpp`
- Create: `fixtures/structural/ui/two-group-tetra.inp`
- Create: `fixtures/structural/ui/two-group-tetra.geo`
- Create: `fixtures/structural/ui/two-group-tetra.candidate.json`
- Modify: `desktop/app/CMakeLists.txt`
- Modify: `desktop/structural/CMakeLists.txt`
- Modify: `desktop/structural/tests/structural_tests.cpp`
- Modify: `desktop/app/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing controller tests**

Load the checked-in two-group candidate manifest, then copy it and its mesh to
a `QTemporaryDir` for mutation tests. Test that the
controller:

- verifies both identities and loads the mesh through
  `parse_gmsh_abaqus_mesh()`;
- exposes node/element counts, diagnostics, group name, area, centroid, normal,
  and triangle count;
- has no implicit restraint or load selection;
- reports blockers until material applicability, both selections, force,
  requirement limits, mesh controls, and scenario are confirmed;
- compiles a resultant equal to the reviewed magnitude and normalized direction;
- clears `scenarioConfirmed` whenever any reviewed input changes;
- refuses export when any blocker remains;
- serializes a ready case to canonical JSON, parses it in the Qt-free library,
  and regenerates identical deck bytes;
- rejects unknown JSON members, duplicate keys, noncanonical stored bytes, and
  a case whose reviewed mesh or material provenance is missing.

Use exact issue codes in the assertions:

```cpp
require(hasBlocker(controller, "material_applicability_unreviewed"),
        "material applicability starts blocked");
require(hasBlocker(controller, "restraint_surface_unselected"),
        "restraint surface starts blocked");
require(hasBlocker(controller, "load_surface_unselected"),
        "load surface starts blocked");
require(hasBlocker(controller, "scenario_unconfirmed"),
        "scenario starts blocked");
```

- [ ] **Step 2: Verify RED**

Run:

```bash
cmake --fresh --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug --target prometheus_structural_setup_controller_tests
```

Expected: configuration or compilation fails because the controller target does
not exist.

- [ ] **Step 3: Define the controller contract**

First define the shared canonical contract:

```cpp
inline constexpr std::string_view structural_case_schema =
    "urn:prometheus:structural-case:0.1.0";

struct CanonicalStructuralCase final {
  StructuralRequest request;
  std::string bytes;
  std::string object_hash;
};

[[nodiscard]] CanonicalStructuralCase
build_structural_case(const StructuralRequest &request);

[[nodiscard]] CanonicalStructuralCase
parse_structural_case(std::string_view canonical_bytes);
```

Use `nlohmann::json` for strict typed decoding and the shared integrity library
for RFC 8785 canonicalization and object hashing. Require an exact top-level
key set and bounded array sizes before allocating nodes, elements, groups, or
loads.

Expose these read-only properties: `sourcePath`, `geometrySha256`, `nodeCount`,
`elementCount`, `minimumMeanRatio`, `surfaceGroups`, `activeSurfaceGroup`,
`restraintGroups`, `loadGroups`, `selectedLoadAreaM2`, `compiledResultantN`,
`materialCandidates`, `blockingIssues`, `readyToExport`, `scenarioConfirmed`,
`meshGeometry`, and `highlightGeometry`.

Expose invokables that carry raw user intent into C++:

```cpp
Q_INVOKABLE bool loadCandidate(const QUrl &manifest);
Q_INVOKABLE void setActiveSurfaceGroup(const QString &name);
Q_INVOKABLE void setSurfaceRole(const QString &name, const QString &role,
                                bool selected);
Q_INVOKABLE bool loadMaterialEvidence(const QUrl &source);
Q_INVOKABLE void selectMaterialCandidate(const QString &candidateId,
                                         const QString &applicability);
Q_INVOKABLE void setMaterialReview(const QVariantMap &review);
Q_INVOKABLE void setForce(double magnitudeN, double directionX,
                          double directionY, double directionZ);
Q_INVOKABLE void setLimits(const QVariantMap &limits);
Q_INVOKABLE void setMeshReview(const QVariantMap &review);
Q_INVOKABLE void confirmScenario(bool confirmed);
Q_INVOKABLE bool exportReviewedCase(const QUrl &directory);
```

The candidate manifest contains schema, geometry path/hash, mesh path/hash,
coordinate scale, and an optional material-evidence path/hash. Resolve relative paths
inside the manifest directory, reject traversal and symlinks, hash each file
before parsing, and block on any mismatch. Accept only roles `restraint` and
`load`, and applicability `known` or
`assumed`. Every setter that changes effective input calls one
`invalidateScenario()` method. The controller calls
`compile_surface_setup()` and `validate_request()`; it does not calculate face
areas, normals, force distribution, stresses, or findings itself. Export calls
`build_structural_case()` and writes its exact canonical bytes; the controller
does not define a second JSON contract.

- [ ] **Step 4: Implement deterministic mesh visualization**

Build one `QQuick3DGeometry` from the union of boundary triangles and a second
geometry from the active group. Remap source node IDs to packed indices in
ascending ID order, compute display normals from triangle connectivity, and
retain coordinates in meters. Visualization errors are blockers; they do not
change or repair the analysis mesh.

- [ ] **Step 5: Wire the build and application context**

Require `PROMETHEUS_BUILD_STRUCTURAL=ON` when the desktop is enabled, link
`prometheus_desktop_support` to `prometheus_structural`, instantiate
`StructuralSetupController` in `main.cpp`, and publish it as
`structuralSetupController`. Support
`PROMETHEUS_STARTUP_STRUCTURAL_CANDIDATE=<absolute manifest path>` and
`PROMETHEUS_STARTUP_WORKSPACE=structural`; wait for the controller's
`meshLoaded` signal before taking an automated screenshot.

- [ ] **Step 6: Verify GREEN and commit**

Run:

```bash
cmake --fresh --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug --target prometheus_structural_setup_controller_tests
ctest --test-dir out/build/desktop-no-occt-debug -R '^prometheus_structural_setup_controller$' --output-on-failure
```

Then commit:

```bash
git add CMakeLists.txt desktop/app/CMakeLists.txt desktop/app/main.cpp \
  desktop/app/structural_setup_controller.cpp \
  desktop/app/structural_setup_controller.hpp \
  desktop/app/tests/structural_setup_controller_tests.cpp \
  desktop/structural/include/prometheus/structural/structural_case.hpp \
  desktop/structural/src/structural_case.cpp \
  desktop/structural/tests/structural_tests.cpp \
  desktop/structural/CMakeLists.txt fixtures/structural/ui/two-group-tetra.inp \
  fixtures/structural/ui/two-group-tetra.geo \
  fixtures/structural/ui/two-group-tetra.candidate.json
git commit -m "Add reviewed structural setup controller"
```

### Task 2: Add the minimal mesh and face-selection panel

**Files:**
- Create: `desktop/ui/StructuralSetupPanel.qml`
- Modify: `desktop/ui/Main.qml`
- Modify: `desktop/app/CMakeLists.txt`
- Modify: `desktop/app/tests/qml_authority_tests.cpp`

- [ ] **Step 1: Add failing QML boundary assertions**

Add `StructuralSetupPanel.qml` to the authority scan and instantiate it with a
probe object. Assert that these named controls exist:

```text
structuralMeshViewport
structuralCandidateFile
structuralSurfaceList
structuralRestraintToggle
structuralLoadToggle
structuralMaterialDesignation
structuralTemper
structuralProductForm
structuralYoungsModulus
structuralPoissonRatio
structuralMaterialEvidence
structuralMaterialCandidate
structuralForceMagnitude
structuralForceDirectionX
structuralForceDirectionY
structuralForceDirectionZ
structuralDisplacementLimit
structuralStressLimit
structuralMeshConfirmation
structuralScenarioConfirmation
structuralBlockingList
structuralExportButton
```

Assert that the export button is disabled when `readyToExport` is false and
enabled when the probe reports a reviewed case. Extend the forbidden-source
scan to reject `Math.sqrt`, `Math.pow`, stress formulas, and any QML-side
division of force by area.

- [ ] **Step 2: Verify RED**

Run:

```bash
cmake --build --preset desktop-no-occt-debug --target prometheus_qml_authority_tests
ctest --test-dir out/build/desktop-no-occt-debug -R '^prometheus_qml_authority$' --output-on-failure
```

Expected: FAIL because `StructuralSetupPanel.qml` is absent.

- [ ] **Step 3: Implement the panel**

Use a split layout: the left side is a `View3D` showing the boundary mesh and
active-group overlay; the right side is a scrollable setup form. Each surface
row displays name, area, centroid, normal, and triangle count and has separate
restraint/load checkboxes. Put this sentence immediately beside the force
fields:

```text
The total force is applied as uniform traction over the selected load groups
and deterministically distributed to their mesh nodes.
```

Show selected area and the compiled resultant returned by the controller.
Render every blocking issue; do not replace them with a generic disabled state.

- [ ] **Step 4: Integrate without displacing current CAD intake**

Add a `Structural setup` workspace/tab in `Main.qml`. Keep CAD intake as the
default view. Register the QML file in `desktop/app/CMakeLists.txt`; do not make
the panel auto-load or auto-confirm a mesh.

- [ ] **Step 5: Verify GREEN, launch, and visually inspect**

Run:

```bash
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug -R 'prometheus_(structural_setup_controller|qml_authority)' --output-on-failure
PROMETHEUS_STARTUP_STRUCTURAL_CANDIDATE="$PWD/fixtures/structural/ui/two-group-tetra.candidate.json" \
PROMETHEUS_STARTUP_WORKSPACE=structural \
PROMETHEUS_SCREENSHOT_PATH=/private/tmp/prometheus-structural-setup.png \
  out/build/desktop-no-occt-debug/desktop/app/prometheus_desktop
```

Inspect `/private/tmp/prometheus-structural-setup.png` and correct clipped,
overlapping, or illegible controls before committing.

- [ ] **Step 6: Commit**

```bash
git add desktop/ui/StructuralSetupPanel.qml desktop/ui/Main.qml \
  desktop/app/CMakeLists.txt desktop/app/tests/qml_authority_tests.cpp
git commit -m "Add structural mesh review panel"
```

### Task 3: Record material evidence without inventing YUBI applicability

**Files:**
- Create: `fixtures/evidence/aluminum-2024-candidates-v1.json`
- Create: `docs/evidence/2024-aluminum-material-candidates.md`
- Modify: `scripts/prepare-yubi-structural-slice.ps1`
- Modify: `desktop/app/tests/structural_setup_controller_tests.cpp`

- [ ] **Step 1: Add a failing applicability test**

Load the checked-in material evidence file in the controller test. Assert:

- the Toyota record contains designation `A2024` and no temper, product form,
  modulus, Poisson ratio, or allowable;
- the Kaiser record is labeled producer-typical and contains `73.1e9 Pa`;
- the MIL-HDBK-5J record is labeled canceled-handbook/reference data and
  preserves `10.7 Msi`, its derived `73.7739030369e9 Pa`, and `0.33` only with
  its T351 plate applicability fields;
- selecting either numerical candidate without choosing `known` or `assumed`
  applicability keeps export blocked;
- no candidate supplies a stress requirement automatically.

- [ ] **Step 2: Verify RED**

Run the focused controller test. Expected: FAIL because the evidence fixture
does not exist.

- [ ] **Step 3: Add source-located evidence records**

Write schema `urn:prometheus:material-candidate-evidence:0.1.0` with the exact
Toyota source commit and BOM artifact hash, source URLs, document title,
publisher, table/page locator, units, property basis, temper, product form,
thickness applicability, and limitation text. Record:

- Kaiser typical modulus `73.1 GPa` for its stated 2024 T4/T351 product scope;
- MIL-HDBK-5J tensile modulus `E = 10.7 Msi` and Poisson ratio `0.33` for
  bare 2024 sheet/plate, all tempers, at thickness `>= 0.250 in`, narrowed by
  the candidate to T351 plate, converted using exact pounds-force/inch² to
  `73.7739030369 GPa` and displayed as `73.8 GPa`; the table does not label
  this value as transverse;
- Toyota only as `A2024`, with unresolved temper and product form.

Do not add yield strength as a default requirement. In the evidence document,
state that MIL-HDBK-5J is canceled/superseded and neither generic source proves
the delivered bracket material.

- [ ] **Step 4: Keep the prepared YUBI candidate blocked**

Copy the evidence JSON into the ignored YUBI trial folder and extend its
manifest with `material_applicability: "unresolved"`. Keep all review booleans
false. Hash the copied evidence file in the manifest so a later reviewed setup
can identify its exact candidate source.

- [ ] **Step 5: Verify and commit**

Run the focused controller test and:

```bash
git diff --check
git add fixtures/evidence/aluminum-2024-candidates-v1.json \
  docs/evidence/2024-aluminum-material-candidates.md \
  scripts/prepare-yubi-structural-slice.ps1 \
  desktop/app/tests/structural_setup_controller_tests.cpp
git commit -m "Record bounded 2024 aluminum evidence"
```

### Task 4: Prove the full path on an analytic tension bar

**Files:**
- Create: `desktop/structural/include/prometheus/structural/structural_finding.hpp`
- Create: `desktop/structural/src/structural_finding.cpp`
- Create: `desktop/structural/tools/export_structural_case.cpp`
- Create: `desktop/structural/tools/verify_structural_case.cpp`
- Create: `fixtures/structural/tension-bar/tension-bar.geo`
- Create: `fixtures/structural/tension-bar/expectations.json`
- Create: `scripts/run-structural-validation.ps1`
- Modify: `desktop/structural/CMakeLists.txt`
- Modify: `desktop/structural/tests/structural_tests.cpp`
- Modify: `docs/phase-03-structural-workflow.md`

- [ ] **Step 1: Write failing finding-polarity tests**

Define a scoped finding with states `pass`, `fail`, and `indeterminate`. Assert
that complete converged metrics below a limit pass, metrics above the same
limit fail, and incomplete run evidence, a missing limit, or failed refinement
is indeterminate. Use equality as failure (`measured >= limit`) so the pass rule
is unambiguous.

- [ ] **Step 2: Verify RED**

Run the structural unit test. Expected: missing finding API compilation error.

- [ ] **Step 3: Implement finding compilation**

`compile_structural_findings()` accepts only typed compiled metrics, reviewed
limits, and refinement evidence. It emits separate displacement and von Mises
findings with scope, observed value, limit, unit, evidence hashes, assumptions,
and status. It cannot emit `pass` unless solver and refinement evidence are
complete.

- [ ] **Step 4: Check in the predeclared benchmark contract**

Create a 1.0 m × 0.1 m × 0.1 m prismatic bar with named `FIXED` and `LOADED`
end surfaces. Use `F = 1000 N`, `E = 70 GPa`, and `nu = 0.30`. Record before
running:

```text
analytic loaded-face axial displacement = F L / (A E) = 1.4285714286e-6 m
analytic central axial stress            = F / A       = 100000 Pa
coarse target size          = 0.100 m
medium target size          = 0.050 m
fine target size            = 0.025 m
fine analytic error         <= 5 percent for average loaded-face axial
                              displacement and volume-weighted axial stress
                              in the central x/L = 0.4 to 0.6 band
medium-to-fine change       <= 2 percent for average loaded-face axial displacement
known-pass displacement limit = 1.6e-6 m
known-fail displacement limit = 1.2e-6 m
```

Use `Surface In BoundingBox` in the `.geo` file to name the two end groups;
do not depend on OpenCASCADE's generated surface tag numbers.

- [ ] **Step 5: Implement file-based export and verification tools**

`prometheus_export_structural_case CASE_JSON MESH_INP OUTPUT_DIRECTORY` parses
the reviewed case with `parse_structural_case()`, compiles selected faces,
validates the request, and writes
the exact deck plus request manifest. `prometheus_verify_structural_case`
accepts that manifest and captured solver evidence, calls
`compile_calculix_result()`, then calls `compile_structural_findings()` and
writes a deterministic result manifest. Both tools return nonzero on a blocker.

- [ ] **Step 6: Write the Windows benchmark runner**

For each declared mesh size, run Gmsh with first-order tetrahedra and retained
physical surface elements, then export, run `ccx` as a child process with
captured streams, and verify. Assert the analytic and refinement tolerances
from `expectations.json`; then rerun the fine result through the known-pass and
known-fail limits and assert the expected polarity. Preserve meshes, decks,
raw outputs, manifests, solver version, and elapsed time under
`out/validation/structural/tension-bar`.

Calculate the analytic comparisons from normalized rows: average the axial
displacement of nodes in the `LOADED` group, and volume-weight `sigma_xx` for
elements whose centroids lie in `0.4 <= x/L <= 0.6`. Do not compare peak von
Mises stress at the fully fixed face with `F/A`; that peak can be
restraint-singular and remains a separate refinement observation.

- [ ] **Step 7: Run the benchmark gate**

On the reviewed Windows environment:

```powershell
.\scripts\run-structural-validation.ps1
```

Expected: three converged, coverage-complete solves; analytic tolerances pass;
medium-to-fine displacement change passes; one scoped pass and one scoped fail
are produced. Any unmet criterion blocks Task 5 and is recorded as
indeterminate, not relaxed after seeing the result.

- [ ] **Step 8: Commit code and recorded evidence separately**

```bash
git add desktop/structural fixtures/structural/tension-bar \
  scripts/run-structural-validation.ps1 docs/phase-03-structural-workflow.md
git commit -m "Add analytic structural validation gate"
```

After the Windows run, add only the compact manifest and report—not bulky raw
solver artifacts—and commit:

```bash
git add docs/phase-03-structural-workflow.md
git commit -m "Record structural benchmark evidence"
```

### Task 5: Obtain and export the real YUBI setup review

**Files:**
- Modify: `scripts/mesh-yubi-structural-slice.ps1`
- Modify: `scripts/prepare-yubi-structural-slice.ps1`
- Create after review: `docs/trials/yubi-bracket-reviewed-setup.json`
- Modify: `docs/phase-03-structural-workflow.md`

- [ ] **Step 1: Preserve selectable YUBI surface entities**

Run Gmsh with `Mesh.SaveGroupsOfElements=2` and first-order tetrahedra so the
Abaqus export contains a stable group for each imported elementary surface as
well as `C3D4` volume elements. Reject the mesh if the parser does not account
for every boundary triangle exactly once. Only after the probe passes, write
the mesh filename, strict SHA-256, `coordinate_scale_to_m = 0.001`, Gmsh
version, and mesh controls into the candidate manifest using a staged-file
replace; a failed mesh must leave the prior valid manifest untouched.

- [ ] **Step 2: Open the exact mesh in the panel**

Use the pinned bracket SHA-256
`4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a`.
Display its group IDs, areas, centroids, and normals. Save screenshots of the
active groups chosen for load and restraint evidence.

- [ ] **Step 3: Ask for explicit review inputs and stop on unknowns**

Collect these choices from the user, one at a time:

1. material applicability: known supplied temper/product form, or an explicitly
   hypothetical `assumed 2024-T351` evaluation;
2. restraint surface group IDs representing the UR5e-side attachment;
3. load surface group IDs representing gripper load transfer;
4. total force magnitude and unit direction;
5. displacement and von Mises limits and the basis for each;
6. mesh target sizes and minimum mean-ratio threshold;
7. confirmation of the complete linear-static scenario.

If the user does not approve an assumption and no drawing/certificate resolves
temper and product form, keep the solve blocked. Do not infer a force, safety
factor, or allowable from the open-source files.

- [ ] **Step 4: Export and independently reload the reviewed setup**

Export canonical JSON containing all selected group identities, source hashes,
material applicability status, inputs, limits, review timestamps, and explicit
assumptions. Reload it in a fresh process, regenerate the deck, and require the
same deck SHA-256 before calling the setup reviewed.

- [ ] **Step 5: Commit only the reviewed setup**

```bash
git add docs/trials/yubi-bracket-reviewed-setup.json \
  docs/phase-03-structural-workflow.md \
  scripts/mesh-yubi-structural-slice.ps1 \
  scripts/prepare-yubi-structural-slice.ps1
git commit -m "Record reviewed YUBI structural setup"
```

### Task 6: Run and report the bounded YUBI case

**Files:**
- Create: `scripts/run-yubi-structural-slice.ps1`
- Create after execution: `docs/trials/yubi-bracket-structural-result.md`
- Modify: `docs/program/01-trust-kernel/01d-multi-project-evidence.md`
- Modify: `docs/milestone-status.md`

- [ ] **Step 1: Add fail-closed preflight**

The runner verifies the exact geometry, mesh, reviewed-setup, deck, solver
executable, and benchmark-manifest hashes. It exits before `ccx` when any
identity differs, any review is false, or the benchmark gate did not pass.

- [ ] **Step 2: Execute reviewed and refined meshes**

Run the approved target mesh and one finer mesh through the same export,
solver, result, and finding executables. Preserve process status, `.sta`,
`.dat`, stdout/stderr, row coverage, mesh diagnostics, runtime, and hashes.

- [ ] **Step 3: Apply predeclared refinement rules**

Report displacement only if the target-to-fine maximum-displacement change is
within the user-approved tolerance. Report peak stress as indeterminate if it
does not stabilize, especially at a fully fixed idealized boundary. Do not
discard the finer result or loosen the tolerance after seeing it.

- [ ] **Step 4: Write the bounded report**

The title must say `assumed 2024-T351` when applicable. Include geometry and
solver identities, selected groups and screenshots, compiled resultant,
material basis, convergence and coverage evidence, refinement table,
requirement-level findings, unknowns, and exclusions. State explicitly that
this is one isotropic linear-static bracket scenario—not a project-wide verdict
or certification result.

- [ ] **Step 5: Run the release checkpoint and commit**

Run:

```bash
git diff --check
cmake --fresh --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
cmake --fresh --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --preset desktop-no-occt-debug --output-on-failure
```

On Windows, rerun the benchmark and YUBI scripts. Then commit the scripts and
compact evidence:

```bash
git add scripts/run-yubi-structural-slice.ps1 \
  docs/trials/yubi-bracket-structural-result.md \
  docs/program/01-trust-kernel/01d-multi-project-evidence.md \
  docs/milestone-status.md
git commit -m "Record bounded YUBI structural result"
```

If any stop condition remains, do not create a pass/fail result document.
Instead update `docs/phase-03-structural-workflow.md` with the exact blocker and
leave Program 01D/Phase 3 open.
