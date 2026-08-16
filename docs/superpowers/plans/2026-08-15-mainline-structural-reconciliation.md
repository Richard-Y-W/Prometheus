# Mainline Structural Reconciliation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge `origin/main` into the Phase 3 feature branch, retain mainline's complete structural/project workflow, and replace its weaker trust seams with the feature branch's fail-closed checks without repeating expensive calculations during view, save, archive, or publication.

**Architecture:** Mainline's `StructuralController`, `StructuralSetup`, CalculiX runner, structural archive, project store, and Phase 5 backend remain the product spine. Qt-free stages emit immutable prepared-mesh, compiled-setup, and validated-result objects; the desktop and persistence layers consume those objects instead of rerunning engineering work. Old archives remain readable, while newly produced archives use a versioned strengthened contract.

**Tech Stack:** C++20, CMake/CTest, Qt 6 Quick/Quick3D/Concurrent, CalculiX, canonical JSON and SHA-256 integrity library, SQLite/PostgreSQL SQLAlchemy migrations, Python 3.12/pytest, PowerShell/CMake trial drivers.

---

## File ownership map

- `desktop/structural/include/prometheus/structural/gmsh_mesh.hpp` and
  `desktop/structural/src/gmsh_mesh.cpp`: parse source mesh bytes and retain
  volume plus source surface labels.
- `desktop/structural/include/prometheus/structural/prepared_mesh.hpp` and
  `desktop/structural/src/prepared_mesh.cpp`: perform topology, quality,
  exterior-boundary, and mesh-identity work once.
- `desktop/structural/include/prometheus/structural/structural_setup.hpp` and
  `desktop/structural/src/structural_setup.cpp`: own reviewed inputs and emit one
  immutable compiled setup containing the request, canonical evidence, deck,
  and identities.
- `desktop/structural/include/prometheus/structural/calculix_runner.hpp`,
  `desktop/structural/src/calculix_runner.cpp`, and platform process files: run
  CalculiX once and capture process/raw-artifact evidence.
- `desktop/structural/include/prometheus/structural/calculix_result.hpp` and
  `desktop/structural/src/calculix_result.cpp`: parse and validate raw solver
  evidence once.
- `desktop/structural/include/prometheus/structural/structural_findings.hpp` and
  `desktop/structural/src/structural_findings.cpp`: compile scoped findings from
  validated evidence and refinement evidence.
- `desktop/structural/include/prometheus/structural/structural_archive.hpp` and
  `desktop/structural/src/structural_archive.cpp`: serialize an already
  validated completed run; only replay reparses persisted bytes.
- `desktop/app/structural_backend.hpp` and
  `desktop/app/structural_backend.cpp`: injectable desktop adapter around the
  Qt-free stages, used to prove stage call counts.
- `desktop/app/structural_controller.*` and
  `desktop/ui/StructuralSetupPanel.qml`: presentation and lifecycle only.
- `desktop/run_store/src/structural_archive_store.cpp`: embed verified artifact
  bytes into project storage without invoking structural calculations.
- `backend/app/manual_component_intake_v2.py` and its migration/tests: accepted
  unchanged from `main`; no structural binding is introduced in this merge.

### Task 1: Merge mainline and establish one buildable structural spine

**Files:**
- Resolve: `desktop/app/CMakeLists.txt`
- Resolve: `desktop/app/main.cpp`
- Resolve: `desktop/app/project_intake.hpp`
- Resolve: `desktop/structural/CMakeLists.txt`
- Resolve: `desktop/structural/include/prometheus/structural/calculix_result.hpp`
- Resolve: `desktop/structural/src/calculix_result.cpp`
- Resolve: `desktop/structural/src/gmsh_mesh.cpp`
- Resolve: `desktop/structural/tests/structural_tests.cpp`
- Resolve: `desktop/ui/Main.qml`
- Resolve: `desktop/ui/StructuralSetupPanel.qml`
- Resolve: `docs/phase-03-structural-workflow.md`
- Resolve: `scripts/run-calculix-smoke.ps1`
- Preserve: `backend/app/manual_component_intake_v2.py`
- Preserve: `backend/migrations/versions/f1a9c02e5b6d_manual_component_intake_v2.py`

- [x] **Step 1: Record the exact merge inputs and require a clean worktree**

Run:

```bash
git status --short --branch
git rev-parse HEAD
git rev-parse origin/main
```

Expected: branch `feature/phase3-structural-rover-gates`, no uncommitted files,
feature tip at or after `a0172e3`, and `origin/main` at or after `0ca5112`.

- [x] **Step 2: Start the merge without committing it**

Run:

```bash
git merge --no-ff --no-commit origin/main
```

Expected: the twelve reviewed conflicts listed in the design investigation;
the Phase 5 backend and migration merge without conflict.

- [x] **Step 3: Select mainline's product spine for the overlapping controller, runner, archive-facing UI, status document, and smoke script**

Run these targeted resolutions:

```bash
git checkout --theirs desktop/app/CMakeLists.txt desktop/app/main.cpp
git checkout --theirs desktop/structural/CMakeLists.txt
git checkout --theirs desktop/structural/include/prometheus/structural/calculix_result.hpp
git checkout --theirs desktop/structural/src/calculix_result.cpp
git checkout --theirs desktop/structural/src/gmsh_mesh.cpp
git checkout --theirs desktop/structural/tests/structural_tests.cpp
git checkout --theirs desktop/ui/Main.qml desktop/ui/StructuralSetupPanel.qml
git checkout --theirs docs/phase-03-structural-workflow.md
git checkout --theirs scripts/run-calculix-smoke.ps1
```

Expected: these paths contain mainline's `StructuralController`, isolated
runner, archive/replay workflow, project integration, and single setup panel.

- [x] **Step 4: Resolve project intake as a union of the mainline archive objects and the root-independent trial digest**

Replace the conflicted result and controller additions in
`desktop/app/project_intake.hpp` with:

```cpp
struct ProjectIntakeResult final {
  bool ok{};
  QString root_path;
  QString error;
  QVariantList artifacts;
  QVariantList candidate_components;
  QString primary_step_path;
  QString inventory_sha256;
  std::optional<prometheus::run_store::ObjectToStore> inventory_snapshot;
  std::optional<prometheus::run_store::ProjectEvidenceArchiveObjects>
      evidence_archive;
};

// In ProjectIntakeController's Q_PROPERTY list and public accessors:
Q_PROPERTY(QString inventorySha256 READ inventorySha256 NOTIFY changed)
QString inventorySha256() const { return result_.inventory_sha256; }
const ProjectIntakeResult &result() const { return result_; }
```

Keep both declarations:

```cpp
[[nodiscard]] ProjectIntakeResult scanProjectFolder(const QString &rootPath);
[[nodiscard]] prometheus::run_store::ObjectToStore
buildProjectInventorySnapshot(const ProjectIntakeResult &result);
```

- [x] **Step 5: Restore mainline-compatible versions of feature-modified files that are not textual conflicts but would activate the retired stack**

Run:

```bash
git restore --source=origin/main --staged --worktree -- desktop/structural/include/prometheus/structural/types.hpp
git restore --source=origin/main --staged --worktree -- desktop/structural/src/structural_request.cpp
git restore --source=origin/main --staged --worktree -- desktop/structural/tools/export_structural_smoke.cpp
git restore --source=origin/main --staged --worktree -- desktop/structural/tools/structural_mesh_probe.cpp
git restore --source=origin/main --staged --worktree -- desktop/app/tests/qml_authority_tests.cpp
```

Do not delete the unbuilt feature-only sources in this task; Tasks 3 through 9
move their enumerated tests and behavior before Task 9 removes them.

- [x] **Step 6: Confirm every merge conflict is resolved and that Phase 5 is present**

Run:

```bash
git diff --name-only --diff-filter=U
test -f backend/app/manual_component_intake_v2.py
test -f backend/migrations/versions/f1a9c02e5b6d_manual_component_intake_v2.py
```

Expected: the first command prints nothing and both file checks return zero.

- [x] **Step 7: Build and run the mainline structural baseline before the merge commit**

Run:

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug --target prometheus_structural_tests prometheus_run_store_transaction_tests
ctest --test-dir out/build/headless-debug --output-on-failure -R 'prometheus_structural_tests|prometheus_run_store_transaction'
```

Expected: configuration/build return zero and both selected tests pass. If a
feature-only auto-merge prevents this baseline, restore that specific file from
`origin/main` and record it in the merge commit message rather than patching a
second runtime into the baseline.

- [x] **Step 8: Commit the reviewed merge baseline**

Run:

```bash
git add --all
git diff --cached --check
git commit -m "Merge mainline structural and project workflow"
```

Expected: one merge commit with two parents; no push.

### Task 2: Retain deterministic project-intake and outside-user gates

**Files:**
- Modify: `desktop/app/CMakeLists.txt`
- Modify: `desktop/app/project_intake.cpp`
- Modify: `desktop/app/project_intake.hpp`
- Modify: `desktop/app/tests/project_intake_tests.cpp`
- Retain: `cmake/AssertProjectIntakeSummary.cmake`
- Retain: `cmake/tests/JplRoverTrialFixture.cmake`
- Retain: `scripts/jpl-rover-trial.cmake`
- Retain: `scripts/tests/outside-user-bundle-fixture.ps1`

- [x] **Step 1: Run the reconciled intake test directly and confirm the root-independent digest assertions are active**

Run:

```bash
cmake --build --preset desktop-no-occt-debug --target prometheus_project_intake_tests
out/build/desktop-no-occt-debug/desktop/app/prometheus_project_intake_tests
```

Expected before completing this task: fail at the inventory digest assertion or
fail to build because the unioned declaration/implementation is incomplete.

- [x] **Step 2: Keep one file scan and derive both identities from its stored artifact rows**

In `scanProjectFolder()`, retain this order after sorting `result.artifacts`:

```cpp
result.inventory_sha256 = inventoryDigest(result.artifacts);
// Duplicate accounting and candidate discovery use result.artifacts.
result.inventory_snapshot = buildProjectInventorySnapshot(result);
// Evidence archive construction also uses result.artifacts; it must not rescan
// or rehash the folder.
```

The `inventoryDigest()` input remains sorted `relative_path`, `byte_size`, and
content SHA-256. `buildProjectInventorySnapshot()` may add classification and
detail to its canonical object, but it must consume the same rows.

- [x] **Step 3: Register the offline intake gates without adding the 623 MB Rover checkout to ordinary CI**

Add these existing tests to `desktop/app/CMakeLists.txt` below
`prometheus_project_intake`:

```cmake
add_test(
  NAME prometheus_project_intake_summary_good
  COMMAND ${CMAKE_COMMAND}
    -DEXPECTED_JSON=${PROJECT_SOURCE_DIR}/fixtures/trials/project-intake-summary/expected.json
    -DACTUAL_JSON=${PROJECT_SOURCE_DIR}/fixtures/trials/project-intake-summary/actual-good.json
    -P ${PROJECT_SOURCE_DIR}/cmake/AssertProjectIntakeSummary.cmake
)
add_test(
  NAME prometheus_project_intake_summary_bad
  COMMAND ${CMAKE_COMMAND}
    -DEXPECTED_JSON=${PROJECT_SOURCE_DIR}/fixtures/trials/project-intake-summary/expected.json
    -DACTUAL_JSON=${PROJECT_SOURCE_DIR}/fixtures/trials/project-intake-summary/actual-bad.json
    -P ${PROJECT_SOURCE_DIR}/cmake/AssertProjectIntakeSummary.cmake
)
set_tests_properties(prometheus_project_intake_summary_bad PROPERTIES WILL_FAIL TRUE)
add_test(
  NAME prometheus_jpl_rover_prepare_fixture
  COMMAND ${CMAKE_COMMAND}
    -DCASE=correct
    -DREPOSITORY_ROOT=${PROJECT_SOURCE_DIR}
    -DTEST_BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/jpl-rover-fixture-correct
    -P ${PROJECT_SOURCE_DIR}/cmake/tests/JplRoverTrialFixture.cmake
)
add_test(
  NAME prometheus_jpl_rover_failed_promotion_fixture
  COMMAND ${CMAKE_COMMAND}
    -DCASE=wrong_license
    -DREPOSITORY_ROOT=${PROJECT_SOURCE_DIR}
    -DTEST_BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/jpl-rover-fixture-wrong-license
    -P ${PROJECT_SOURCE_DIR}/cmake/tests/JplRoverTrialFixture.cmake
)
```

Retain the Windows-only PowerShell fixture registration from the feature
branch. Do not invoke the live external Rover trial from CTest.

- [x] **Step 4: Run the focused intake gates**

Run:

```bash
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug --target prometheus_project_intake_tests
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure -R 'prometheus_project_intake|prometheus_jpl_rover'
```

Expected: the project intake test, positive/negative summary fixtures, and two
portable Rover preparation fixtures pass.

- [x] **Step 5: Commit the intake reconciliation**

```bash
git add desktop/app/CMakeLists.txt desktop/app/project_intake.cpp desktop/app/project_intake.hpp desktop/app/tests/project_intake_tests.cpp cmake scripts fixtures docs/trials .github/workflows
git diff --cached --check
git commit -m "Retain deterministic project intake gates"
```

### Task 3: Prepare each mesh once with strict topology and quality evidence

**Files:**
- Create: `desktop/structural/include/prometheus/structural/prepared_mesh.hpp`
- Create: `desktop/structural/src/prepared_mesh.cpp`
- Modify: `desktop/structural/include/prometheus/structural/gmsh_mesh.hpp`
- Modify: `desktop/structural/src/gmsh_mesh.cpp`
- Modify: `desktop/structural/src/mesh_validation.cpp`
- Modify: `desktop/structural/CMakeLists.txt`
- Test: `desktop/structural/tests/structural_tests.cpp`

- [x] **Step 1: Add failing prepared-mesh tests**

Add tests that exercise the public API below:

```cpp
constexpr std::string_view twoGroupTetra = R"(*HEADING
Synthetic two-group tetrahedron; coordinates are millimetres
*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
*ELEMENT, TYPE=CPS3, ELSET=FixedFaces
1, 1, 3, 2
*ELEMENT, TYPE=CPS3, ELSET=LoadedFaces
2, 1, 2, 4
3, 1, 4, 3
4, 2, 3, 4
*ELEMENT, TYPE=C3D4, ELSET=Volume
5, 1, 2, 3, 4
)";
const auto prepared = ps::prepare_gmsh_abaqus_mesh(twoGroupTetra, 0.001);
require(prepared.mesh.nodes.size() == 4, "prepared mesh retains nodes");
require(prepared.mesh.elements.size() == 1, "prepared mesh retains C3D4 elements");
require(prepared.boundary_faces.size() == 4,
        "prepared mesh derives the complete exterior once");
require(prepared.source_surface_groups.size() == 2,
        "prepared mesh retains source labels as hints");
require(prepared.diagnostics.connected_components == 1,
        "prepared mesh records face connectivity");
require(prepared.diagnostics.minimum_mean_ratio > 0.0,
        "prepared mesh records tetra quality");
require(prepared.identity.source_sha256.starts_with("sha256:"),
        "prepared mesh binds exact source bytes");
```

Also add `requireThrows()` cases for an inverted tetrahedron, two
face-disconnected tetrahedra, a non-manifold face, a duplicate surface
triangle, and a source surface triangle not present on the derived exterior.
Add this helper beside the existing `require()` test helper:

```cpp
template <typename Function>
void requireThrows(Function &&function, const std::string_view expected,
                   const char *message) {
  try {
    std::forward<Function>(function)();
  } catch (const std::exception &error) {
    require(std::string_view(error.what()).find(expected) !=
                std::string_view::npos,
            message);
    return;
  }
  fail(message);
}
```

- [x] **Step 2: Run the test and verify the new API is absent**

Run:

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
```

Expected: compilation fails because `prepared_mesh.hpp` and
`prepare_gmsh_abaqus_mesh()` do not exist.

- [x] **Step 3: Define immutable prepared-mesh contracts**

Create `prepared_mesh.hpp` with:

```cpp
#pragma once

#include "prometheus/structural/gmsh_mesh.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace prometheus::structural {

struct MeshDiagnostics final {
  std::size_t connected_components{};
  double minimum_mean_ratio{};
  double maximum_mean_ratio{};
};

struct PreparedMeshIdentity final {
  std::string source_sha256;
  double coordinate_scale_to_m{};
  std::string parser_version;
  std::string validation_version;
};

struct PreparedMesh final {
  VolumeMesh mesh;
  std::vector<BoundaryFace> boundary_faces;
  std::vector<SourceSurfaceGroup> source_surface_groups;
  MeshDiagnostics diagnostics;
  PreparedMeshIdentity identity;
};

[[nodiscard]] PreparedMesh prepare_gmsh_abaqus_mesh(
    std::string_view source_bytes, double coordinate_scale_to_m);

} // namespace prometheus::structural
```

Extend `gmsh_mesh.hpp` with a `SourceSurfaceGroup` containing its source label,
sorted exact face-node triples, sorted unique node IDs, area, centroid, and
representative normal. Keep `BoundaryFace` as the volume-derived authority.

- [x] **Step 4: Implement one preparation pass**

Move the feature branch's parsing and `mesh_validation.cpp` algorithms behind
`prepare_gmsh_abaqus_mesh()` with this fixed operation order:

```cpp
PreparedMesh prepare_gmsh_abaqus_mesh(const std::string_view bytes,
                                      const double scale) {
  auto parsed = parse_gmsh_abaqus_source(bytes, scale); // one text parse
  auto diagnostics = validate_volume_topology(parsed.mesh); // one element pass
  auto boundary = extract_boundary_faces(parsed.mesh); // one face-count pass
  auto labels = validate_and_measure_source_surfaces(
      parsed.source_surface_groups, parsed.mesh, boundary); // no mesh reparse
  return {std::move(parsed.mesh), std::move(boundary), std::move(labels),
          diagnostics,
          {integrity::sha256_bytes(bytes), scale,
           "gmsh-abaqus-c3d4-v2", "tetra-topology-v2"}};
}
```

Reject signed volume at or below the scale-aware floor; do not take its
absolute value. Derive exterior faces from C3D4 volumes even when source
surface groups are present.

- [x] **Step 5: Build and run the strict mesh tests**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests prometheus_structural_mesh_probe
ctest --test-dir out/build/headless-debug --output-on-failure -R prometheus_structural_tests
```

Expected: all prepared-mesh, topology, source-label, and pre-existing mainline
boundary/patch tests pass.

- [x] **Step 6: Commit the prepared-mesh authority**

```bash
git add desktop/structural
git diff --cached --check
git commit -m "Prepare structural meshes once"
```

### Task 4: Compile one immutable reviewed setup and deck

**Files:**
- Modify: `desktop/structural/include/prometheus/structural/types.hpp`
- Modify: `desktop/structural/include/prometheus/structural/structural_setup.hpp`
- Modify: `desktop/structural/src/structural_setup.cpp`
- Modify: `desktop/structural/src/structural_request.cpp`
- Modify: `desktop/structural/src/calculix_deck.cpp`
- Test: `desktop/structural/tests/structural_tests.cpp`

- [x] **Step 1: Add failing strict-request and compiled-setup tests**

Port the feature-branch cases and assert these exact blocker codes:

```cpp
auto reviewedSetup = reviewedTetraSetup();
const auto reviewedRequest = ps::compile_structural_request(reviewedSetup);
auto zeroForce = reviewedRequest;
zeroForce.nodal_forces = {{1, {0.0, 0.0, 0.0}}};
auto duplicateForce = reviewedRequest;
duplicateForce.nodal_forces.push_back(duplicateForce.nodal_forces.front());
auto uppercaseHash = reviewedRequest;
uppercaseHash.geometry_sha256 =
    "sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
auto injectedHeading = reviewedRequest;
injectedHeading.component_name = "bracket\n*INCLUDE, INPUT=other.inp";
auto inverted = reviewedRequest;
std::swap(inverted.elements.front().node_ids[0],
          inverted.elements.front().node_ids[1]);
auto loadMismatch = reviewedRequest;
loadMismatch.nodal_forces.front().force_n[2] += 1.0;

require(hasIssue(ps::validate_request(zeroForce), "zero_resultant_load"),
        "all-zero force cannot enter a deck");
require(hasIssue(ps::validate_request(duplicateForce), "duplicate_load_node"),
        "duplicate nodal loads require deterministic aggregation");
require(hasIssue(ps::validate_request(uppercaseHash),
                 "invalid_geometry_identity"),
        "SHA-256 identity is strict lowercase");
require(hasIssue(ps::validate_request(injectedHeading), "unsafe_heading_text"),
        "heading text cannot inject CalculiX keywords");
require(hasIssue(ps::validate_request(inverted), "inverted_element"),
        "inverted tetrahedra cannot enter a deck");
require(hasIssue(ps::validate_request(loadMismatch), "compiled_load_mismatch"),
        "compiled nodal force reproduces the reviewed resultant");

const auto compiled = ps::compile_structural_setup(reviewedSetup);
require(compiled.calculix_deck == ps::generate_calculix_deck(compiled.request),
        "compiled setup retains its exact deterministic deck");
require(compiled.identity.starts_with("sha256:"),
        "compiled setup has a canonical content identity");
```

- [x] **Step 2: Run the focused test and verify the strict cases fail**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
out/build/headless-debug/desktop/structural/prometheus_structural_tests
```

Expected: fail because mainline still accepts at least the zero-resultant,
uppercase-hash, or injected-heading fixture and lacks
`compile_structural_setup()`.

- [x] **Step 3: Extend the request provenance without creating a second setup type**

Append these fields to the existing `StructuralRequest`:

```cpp
std::string material_designation;
std::string material_temper;
std::string material_product_form;
std::string material_applicability;
std::string material_evidence_sha256;
std::string mesh_sha256;
double mesh_coordinate_scale_to_m{1.0};
double reviewed_force_magnitude_n{};
std::array<double, 3> reviewed_force_direction{};
double selected_load_area_m2{};
double mesh_target_size_m{};
double minimum_mean_ratio_threshold{};
double observed_minimum_mean_ratio{};
std::string displacement_limit_basis;
std::string von_mises_limit_basis;
bool mesh_reviewed{};
```

Extend mainline's existing reviewed material, requirement, and mesh-control
objects so `compile_structural_setup()` populates every field rather than
defaulting it.

- [x] **Step 4: Define the one compiled setup consumed downstream**

Add to `structural_setup.hpp`:

```cpp
struct CompiledStructuralSetup final {
  StructuralRequest request;
  std::string canonical_setup_evidence;
  std::string calculix_deck;
  std::string identity;
};

[[nodiscard]] CompiledStructuralSetup
compile_structural_setup(const StructuralSetup &setup);
```

`compile_structural_setup()` validates `StructuralSetup`, compiles the request,
validates the request once, canonicalizes setup evidence, generates the deck,
and hashes a canonical object containing the evidence hash and deck hash. The
desktop stores this object; it does not call `generate_calculix_deck()` again.

- [x] **Step 5: Implement strict request rules and keep the public deck API fail-closed**

Port the feature validator's strict SHA, safe text, positive signed volume,
nonzero resultant, unique loaded-node, reviewed-force reproduction, unit
direction, material applicability, mesh quality, and requirement-basis rules.
Keep `generate_calculix_deck(const StructuralRequest&)` validating arbitrary
external callers; add an internal `generate_validated_calculix_deck()` used by
`compile_structural_setup()` so its already validated request is not checked a
second time.

- [x] **Step 6: Run the structural tests**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug --output-on-failure -R prometheus_structural_tests
```

Expected: all strict request, compiled setup, existing surface-selection, and
benchmark-construction tests pass.

- [x] **Step 7: Commit the compiled setup boundary**

```bash
git add desktop/structural
git diff --cached --check
git commit -m "Compile reviewed structural setups once"
```

### Task 5: Compile solver evidence once inside the authoritative runner

**Files:**
- Modify: `desktop/structural/include/prometheus/structural/calculix_result.hpp`
- Modify: `desktop/structural/src/calculix_result.cpp`
- Modify: `desktop/structural/include/prometheus/structural/calculix_runner.hpp`
- Modify: `desktop/structural/src/calculix_runner.cpp`
- Modify: `desktop/structural/CMakeLists.txt`
- Modify: `desktop/structural/tests/solver_fixture.cpp`
- Test: `desktop/structural/tests/structural_tests.cpp`

- [x] **Step 1: Add failing complete/incomplete solver-evidence tests**

Use the checked-in `.sta`, `.dat`, stdout, and stderr fixtures to assert:

```cpp
constexpr std::string_view expectedSolverHash =
    "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
const auto request = reviewedTetraRequest();
const auto completeEvidence = completeCalculixEvidence(
    request,
    fixtureBytes(std::filesystem::path(PROMETHEUS_REPOSITORY_ROOT) /
                 "fixtures/structural/calculix-smoke/complete/"
                 "prometheus_tetra_smoke.sta"),
    fixtureBytes(std::filesystem::path(PROMETHEUS_REPOSITORY_ROOT) /
                 "fixtures/structural/calculix-smoke/complete/"
                 "prometheus_tetra_smoke.dat"));
const auto compiled = ps::compile_calculix_result(request, completeEvidence);
require(compiled.complete(), "complete converged evidence produces metrics");
require(compiled.backend.executable_sha256 == expectedSolverHash,
        "result binds exact solver executable");
require(compiled.convergence.has_value() &&
            compiled.convergence->total_time == 1.0,
        "result binds the completed final step");
require(compiled.normalized.displacements.size() == request.nodes.size(),
        "result covers every submitted node exactly");

auto missingCompletion = completeEvidence;
missingCompletion.standard_output = "CalculiX Version 2.23\n";
auto staleStep = completeEvidence;
staleStep.status_bytes = "1 1 1 1 0.5 0.5 0.5\n";
auto wrongDeck = completeEvidence;
wrongDeck.deck_bytes += "** changed\n";

require(hasResultIssue(ps::compile_calculix_result(request, missingCompletion),
                       "solver_completion_marker_missing"),
        "missing completion remains indeterminate");
require(hasResultIssue(ps::compile_calculix_result(request, staleStep),
                       "solver_step_incomplete"),
        "incomplete STA step remains indeterminate");
require(hasResultIssue(ps::compile_calculix_result(request, wrongDeck),
                       "deck_request_mismatch"),
        "raw output cannot detach from the compiled deck");
```

Retain cases for nonzero exit, timeout, solver error text, unsafe version,
non-finite rows, and missing/duplicate/foreign node or element identities.
Implement `fixtureBytes()`, `reviewedTetraRequest()`, and
`completeCalculixEvidence()` as local test helpers by moving the exact fixture
construction already exercised on the feature branch; the helper must generate
the deck from its supplied request and must not contain a second result parser.
Add this compile definition to the existing structural test target:

```cmake
target_compile_definitions(prometheus_structural_tests PRIVATE
  PROMETHEUS_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
  PROMETHEUS_SOLVER_FIXTURE_PATH="$<TARGET_FILE:prometheus_structural_solver_fixture>"
)
```

- [x] **Step 2: Run the test and verify the mainline parser is insufficient**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
```

Expected: compilation fails because `CompiledCalculixResult`, convergence, and
artifact/backend identity fields are absent.

- [x] **Step 3: Consolidate result types**

Use one public result model:

```cpp
struct CalculixRunEvidence final {
  int process_exit_code{};
  std::string solver_executable_sha256;
  std::string solver_version;
  std::string deck_bytes;
  std::string standard_output;
  std::string standard_error;
  std::string status_bytes;
  std::string data_bytes;
  std::string frd_sha256;
  std::uintmax_t frd_byte_length{};
};

struct CalculixArtifactIdentity final {
  std::string sha256;
  std::uintmax_t byte_length{};
};

struct CalculixArtifactIdentities final {
  CalculixArtifactIdentity deck;
  CalculixArtifactIdentity sta;
  CalculixArtifactIdentity dat;
  CalculixArtifactIdentity frd;
  CalculixArtifactIdentity standard_output;
  CalculixArtifactIdentity standard_error;
};

struct CompiledCalculixResult final {
  std::optional<CalculixMetrics> metrics;
  CalculixDat normalized;
  std::vector<CalculixResultIssue> issues;
  CalculixArtifactIdentities artifacts;
  CalculixBackendIdentity backend;
  std::optional<CalculixConvergenceEvidence> convergence;
  std::string identity;

  [[nodiscard]] bool complete() const {
    return metrics.has_value() && issues.empty() && convergence.has_value();
  }
};
```

Keep one pure `parse_calculix_dat()` returning typed rows. Remove the separate
mainline binding validator after its exact coverage checks are part of
`compile_calculix_result()`.

- [x] **Step 4: Make the runner consume `CompiledStructuralSetup` and compile evidence once**

Change the runner entry point to:

```cpp
[[nodiscard]] SolverRunResult run_calculix(
    const SolverRunOptions &options,
    const CompiledStructuralSetup &setup);
```

The runner writes `setup.calculix_deck`, executes the child process, captures
streams, reads `.sta` and `.dat` once, computes the executable/FRD identities,
and calls `compile_calculix_result()` once. Store the completed object as:

```cpp
std::optional<CompiledCalculixResult> validated_result;
```

in `SolverRunResult`. `completed` requires
`validated_result->complete()`. No controller or archive method calls the
compiler again.

- [x] **Step 5: Run process and evidence tests**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests prometheus_run_calculix_job
ctest --test-dir out/build/headless-debug --output-on-failure -R prometheus_structural_tests
```

Expected: success, nonzero exit, missing output, timeout, completion, STA,
solver identity, deck binding, and exact result coverage cases pass.

- [x] **Step 6: Commit the one-pass solver evidence boundary**

```bash
git add desktop/structural
git diff --cached --check
git commit -m "Compile CalculiX evidence once"
```

### Task 6: Gate findings on validated results and refinement evidence

**Files:**
- Modify: `desktop/structural/include/prometheus/structural/structural_findings.hpp`
- Modify: `desktop/structural/src/structural_findings.cpp`
- Modify: `desktop/structural/include/prometheus/structural/structural_benchmarks.hpp`
- Modify: `desktop/structural/src/structural_benchmarks.cpp`
- Test: `desktop/structural/tests/structural_tests.cpp`

- [x] **Step 1: Add failing scope, equality, and refinement tests**

```cpp
constexpr std::string_view coarseHash =
    "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
constexpr std::string_view fineHash =
    "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
const ps::StructuralRefinementEvidence accepted{
    true, true, 0.04, 0.10,
    {std::string(coarseHash), std::string(fineHash)}};
const auto passing = ps::compile_structural_findings(
    request, completed.validated_result, accepted);
require(passing.evaluated_obligations == 2,
        "validated refined evidence evaluates declared obligations");

auto equality = *completed.validated_result;
equality.metrics->maximum_displacement_m = *request.displacement_limit_m;
const auto equalityResult = ps::compile_structural_findings(
    request, equality, accepted);
require(equalityResult.findings.front().disposition ==
            ps::StructuralFindingDisposition::violated,
        "a zero margin is not reported as a pass");

const auto unrefined = ps::compile_structural_findings(
    request, completed.validated_result, std::nullopt);
require(unrefined.evaluated_obligations == 0 && unrefined.findings.empty(),
        "missing refinement evidence produces no pass or violation");
```

- [x] **Step 2: Run the focused test and verify current mainline behavior fails**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
```

Expected: fail because mainline treats equality as no violation and does not
gate findings on refinement.

- [x] **Step 3: Add refinement and evidence identities to the one mainline finding model**

```cpp
struct StructuralRefinementEvidence final {
  bool complete{};
  bool criteria_satisfied{};
  double coarse_to_fine_change_fraction{};
  double maximum_allowed_change_fraction{};
  std::vector<std::string> result_sha256;
};

struct StructuralFinding final {
  std::string obligation;
  StructuralFindingDisposition disposition{};
  double measured_value{};
  double limit_value{};
  double margin_to_limit{};
  std::string unit;
  std::string scope;
  std::vector<std::string> evidence_sha256;
  std::vector<std::string> assumptions;
};
```

`compile_structural_findings()` accepts
`std::optional<CompiledCalculixResult>` and
`std::optional<StructuralRefinementEvidence>`. Missing or invalid inputs leave
all declared obligations unevaluated. A non-violation requires a strictly
positive margin.

- [x] **Step 4: Make analytic benchmark/refinement tools produce the same evidence type**

Adapt `run_structural_benchmark.cpp` and `run_structural_refinement.cpp` to
construct `StructuralRefinementEvidence` from exact coarse/fine result
identities and the predeclared change criterion. They must call the same
finding compiler; no benchmark-only pass logic may bypass it.

- [x] **Step 5: Run structural tests and the checked-in benchmark fixture path**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests prometheus_run_structural_benchmark prometheus_run_structural_refinement
ctest --test-dir out/build/headless-debug --output-on-failure -R prometheus_structural_tests
```

Expected: known-pass, known-fail, equality, missing-refinement, invalid-result,
and analytic tolerance cases pass.

- [x] **Step 6: Commit the refined finding authority**

```bash
git add desktop/structural
git diff --cached --check
git commit -m "Gate structural findings on refinement evidence"
```

### Task 7: Version strengthened archives and avoid active-run replay

**Files:**
- Modify: `desktop/structural/include/prometheus/structural/structural_archive.hpp`
- Modify: `desktop/structural/src/structural_archive.cpp`
- Modify: `desktop/run_store/include/prometheus/run_store/project_v2.hpp`
- Modify: `desktop/run_store/src/run_store.cpp`
- Modify: `desktop/run_store/src/structural_archive_store.cpp`
- Test: `desktop/run_store/tests/run_store_transaction_tests.cpp`
- Test: `desktop/structural/tests/structural_tests.cpp`

- [x] **Step 1: Add failing v1/v2 compatibility and no-replay publication tests**

Add tests that assert:

```cpp
const auto archive = ps::write_structural_archive(
    workingDirectory, jobName, compiledSetup, completedRun, evaluation);
require(archive.schema_version == "2.0.0",
        "new archive records strengthened evidence contract");
require(archive.validated_result_identity ==
            completedRun.validated_result->identity,
        "archive reuses the active validated result");

require(ps::verify_structural_archive(v1Fixture).valid,
        "legacy archive remains readable under its original claim");
require(ps::verify_structural_archive(archive.manifest_path).valid,
        "persisted v2 archive replays after crossing the trust boundary");
```

In the desktop publication fixture, make a counting backend assert that commit
does not increment result-parser or finding-compiler calls.

- [x] **Step 2: Run the tests and verify archive creation/publication currently replays work**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests prometheus_run_store_transaction_tests
ctest --test-dir out/build/headless-debug --output-on-failure -R 'prometheus_structural_tests|prometheus_run_store_transaction'
```

Expected: fail because the v2 contract and active validated-result identity are
absent. Task 8 proves the call-count guarantee through the injected desktop
backend after this contract exists.

- [x] **Step 3: Add registered v1 and v2 structural archive identities**

In `project_v2.hpp`, retain the current v1 constant and add:

```cpp
inline constexpr std::string_view structural_manifest_schema_id_v1 =
    "urn:prometheus:schema:structural-run-archive:1.0.0";
inline constexpr std::string_view structural_manifest_schema_id_v2 =
    "urn:prometheus:schema:structural-run-archive:2.0.0";
```

New writers use v2. Readers and project-reference validators accept exactly v1
or v2 and dispatch to a version-specific closed key set. Do not reinterpret a
v1 archive as containing solver hashes, convergence, or refinement evidence.

- [x] **Step 4: Serialize the active validated objects without reparsing them**

Extend the archive handle in `structural_archive.hpp`:

```cpp
struct StructuralArchive final {
  std::filesystem::path manifest_path;
  std::string manifest_sha256;
  std::string schema_version;
  std::string validated_result_identity;
};
```

Change the writer signature to:

```cpp
[[nodiscard]] StructuralArchive write_structural_archive(
    const std::filesystem::path &working_directory,
    std::string job_name,
    const CompiledStructuralSetup &setup,
    const SolverRunResult &run,
    const StructuralEvaluation &evaluation);
```

Populate metrics, fields, convergence, backend, artifact identities, coverage,
and findings directly from `setup`, `run.validated_result`, and `evaluation`.
The writer may write canonical manifest/setup/stream bytes, but it cannot call
`parse_calculix_dat()`, `compile_calculix_result()`, or
`compile_structural_findings()`.

- [x] **Step 5: Keep replay as the explicit persisted-byte trust boundary**

`verify_structural_archive()` dispatches by schema version. The v2 verifier
rehashes declared artifacts, rebuilds the compiled setup/deck identity, parses
DAT/STA once, recompiles evidence/findings once, and compares the resulting
identities to the manifest. Cache the returned
`StructuralArchiveVerification` in the restore operation so the controller
does not verify the same archive again.

- [x] **Step 6: Remove the controller's pre-publication structural replay**

Project publication accepts the trusted `StructuralArchive` handle created in
the same active run. `build_structural_archive_objects()` still validates file
lengths and hashes while installing immutable chunks, but neither it nor the
controller invokes structural parsing or finding compilation.

- [x] **Step 7: Run structural and run-store compatibility tests**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests prometheus_run_store_transaction_tests prometheus_replay_structural_run
ctest --test-dir out/build/headless-debug --output-on-failure -R 'prometheus_structural_tests|prometheus_run_store_transaction'
```

Expected: v1 read, v2 write/replay, tamper rejection, project embedding,
relocation, and single active-run parse tests pass.

- [x] **Step 8: Commit the archive boundary**

```bash
git add desktop/structural desktop/run_store
git diff --cached --check
git commit -m "Version strengthened structural archives"
```

### Task 8: Consolidate the desktop and prove expensive stages do not repeat

**Files:**
- Create: `desktop/app/structural_backend.hpp`
- Create: `desktop/app/structural_backend.cpp`
- Modify: `desktop/app/structural_controller.hpp`
- Modify: `desktop/app/structural_controller.cpp`
- Modify: `desktop/app/CMakeLists.txt`
- Modify: `desktop/ui/StructuralSetupPanel.qml`
- Test: `desktop/app/tests/structural_controller_tests.cpp`
- Test: `desktop/app/tests/qml_authority_tests.cpp`

- [x] **Step 1: Add a counting-backend integration test before the adapter exists**

Define a test backend that delegates to real Qt-free functions and increments
atomic counters for `prepareMesh`, `groupPatches`, `compileSetup`, and
`execute`. `execute` owns the one solver-evidence and finding compilation, so
neither operation is exposed as a second controller-callable stage. Exercise:

```cpp
struct StageCounts final {
  int prepare_mesh{};
  int group_patches{};
  int compile_setup{};
  int execute{};
};

class CountingStructuralBackend final : public StructuralBackend {
public:
  CountingStructuralBackend() : delegate_(makeLocalStructuralBackend()) {}

  ps::PreparedMesh prepareMesh(std::string_view bytes,
                               double scale) const override {
    ++prepare_mesh_;
    return delegate_->prepareMesh(bytes, scale);
  }
  std::vector<ps::SurfacePatch> groupPatches(
      const ps::PreparedMesh &mesh, double angle) const override {
    ++group_patches_;
    return delegate_->groupPatches(mesh, angle);
  }
  ps::CompiledStructuralSetup compileSetup(
      const ps::StructuralSetup &setup) const override {
    ++compile_setup_;
    return delegate_->compileSetup(setup);
  }
  DesktopStructuralRun execute(
      const ps::SolverRunOptions &options,
      const ps::CompiledStructuralSetup &setup,
      std::optional<ps::StructuralRefinementEvidence> refinement) const override {
    ++execute_;
    return delegate_->execute(options, setup, std::move(refinement));
  }
  StageCounts counts() const {
    return {prepare_mesh_, group_patches_, compile_setup_, execute_};
  }

private:
  std::shared_ptr<const StructuralBackend> delegate_;
  mutable std::atomic<int> prepare_mesh_{};
  mutable std::atomic<int> group_patches_{};
  mutable std::atomic<int> compile_setup_{};
  mutable std::atomic<int> execute_{};
};
```

Then exercise:

```cpp
auto countingBackend = std::make_shared<CountingStructuralBackend>();
StructuralController controller(&project, nullptr, countingBackend);
const auto solverUrl = QUrl::fromLocalFile(
    QString::fromUtf8(PROMETHEUS_SOLVER_FIXTURE_PATH));
const auto outputRootUrl = QUrl::fromLocalFile(temporary.path());
controller.loadMesh(meshUrl, 0.001, 15.0);
controller.meshSummary();
controller.surfacePatches();
controller.meshSummary(); // repeated property read
controller.setPatchSelected(1, "load", true);
controller.setPatchSelected(2, "restraint", true);
controller.reviewSetup(reviewedDraft());
QEventLoop runLoop;
QObject::connect(&controller, &StructuralController::runFinished,
                 &runLoop, &QEventLoop::quit);
QTimer::singleShot(5000, &runLoop, &QEventLoop::quit);
controller.runAnalysis(solverUrl, outputRootUrl);
runLoop.exec();
controller.lastRun();
controller.findings();
QEventLoop commitLoop;
QObject::connect(&controller, &StructuralController::changed,
                 &commitLoop, [&] {
  if (!controller.busy() &&
      (controller.status() == "structural_archive_published" ||
       controller.status() == "structural_archive_publication_failed"))
    commitLoop.quit();
});
QTimer::singleShot(10000, &commitLoop, &QEventLoop::quit);
controller.commitLastRun();
commitLoop.exec();

const auto counters = countingBackend->counts();
require(counters.prepare_mesh == 1, "mesh is prepared once");
require(counters.compile_setup == 1, "reviewed snapshot compiles once");
require(counters.execute == 1,
        "solver, evidence, and findings execute through one stage once");
```

Then edit only the force and assert `prepare_mesh` remains one. Change only the
patch angle and assert `group_patches` increments while `prepare_mesh` remains
one. Repeated property reads must not change any counter.

- [x] **Step 2: Run the controller test and verify injection/counters are absent**

```bash
cmake --build --preset desktop-no-occt-debug --target prometheus_structural_controller_tests
```

Expected: compilation fails because `StructuralBackend` injection does not
exist.

- [x] **Step 3: Add the injectable non-QObject backend**

Create:

```cpp
struct DesktopStructuralRun final {
  prometheus::structural::SolverRunResult run;
  prometheus::structural::StructuralEvaluation evaluation;
  std::optional<prometheus::structural::StructuralArchive> archive;
  std::string archive_error;
};

class StructuralBackend {
public:
  virtual ~StructuralBackend() = default;
  virtual prometheus::structural::PreparedMesh prepareMesh(
      std::string_view bytes, double scale) const = 0;
  virtual std::vector<prometheus::structural::SurfacePatch> groupPatches(
      const prometheus::structural::PreparedMesh &mesh, double angle) const = 0;
  virtual prometheus::structural::CompiledStructuralSetup compileSetup(
      const prometheus::structural::StructuralSetup &setup) const = 0;
  virtual DesktopStructuralRun execute(
      const prometheus::structural::SolverRunOptions &options,
      const prometheus::structural::CompiledStructuralSetup &setup,
      std::optional<prometheus::structural::StructuralRefinementEvidence>
          refinement) const = 0;
};

[[nodiscard]] std::shared_ptr<const StructuralBackend>
makeLocalStructuralBackend();
```

`LocalStructuralBackend` delegates directly to the authoritative Qt-free
functions. The interface contains no alternate formulas or validators.

- [x] **Step 4: Store immutable stage outputs in `StructuralController`**

Replace separate recomputation-prone state with:

```cpp
std::shared_ptr<const StructuralBackend> backend_;
std::optional<prometheus::structural::PreparedMesh> prepared_mesh_;
std::vector<prometheus::structural::SurfacePatch> patches_;
std::optional<prometheus::structural::CompiledStructuralSetup> compiled_setup_;
std::optional<DesktopStructuralRun> completed_run_;
std::optional<prometheus::structural::StructuralArchiveVerification>
    restored_verification_;
```

Constructor injection is:

```cpp
explicit StructuralController(
    ProjectController *project = nullptr, QObject *parent = nullptr,
    std::shared_ptr<const StructuralBackend> backend =
        makeLocalStructuralBackend());
```

QML getters map stored values only. A reviewed-field edit clears
`compiled_setup_` and `completed_run_` but not `prepared_mesh_`. Patch-angle
change rebuilds only `patches_`. Mesh-byte or scale change clears all stages.

- [x] **Step 5: Port the useful feature-panel behavior into the one mainline panel**

Retain mainline's run, commit, stored-run, restore, and result visualization.
Add prepared-mesh display/highlighting and these stored properties:

```cpp
Q_PROPERTY(QQuick3DGeometry *meshGeometry READ meshGeometry NOTIFY changed)
Q_PROPERTY(QQuick3DGeometry *highlightGeometry READ highlightGeometry NOTIFY changed)
Q_PROPERTY(QVariantMap activeSurfacePatch READ activeSurfacePatch NOTIFY changed)
Q_PROPERTY(QVariantList materialCandidates READ materialCandidates NOTIFY changed)

Q_INVOKABLE void setActiveSurfacePatch(int patchId);
Q_INVOKABLE bool loadMaterialEvidence(const QUrl &source);
Q_INVOKABLE void selectMaterialCandidate(const QString &candidateId,
                                         const QString &applicability);
```

Material candidates loaded from the checked-in bounded evidence JSON must be
labeled `known`, `assumed`, or `unresolved`; selecting one populates form fields
but does not set `material_reviewed` or `scenario_confirmed` automatically.
Display selected area, compiled resultant, mesh quality, mesh/source identities,
and exact blocker codes. Do not restore the feature branch's separate Run or
Export path.

- [x] **Step 6: Add QML authority assertions**

Extend `qml_authority_tests.cpp` to require exactly one
`StructuralSetupPanel`, one `structuralController` property, and no references
to `StructuralSetupController`, `readyToExport`, or the removed feature-only
export action. Instantiate the QML with the mock controller and read every
property twice; the counting backend test remains unchanged.

- [x] **Step 7: Build and run desktop structural/QML tests**

```bash
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug --target prometheus_structural_controller_tests prometheus_qml_authority_tests prometheus_desktop
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure -R 'prometheus_structural_controller|prometheus_qml_authority'
```

Expected: one-pass counter assertions, setup invalidation, async execution,
archive commit/restore, stale-source handling, and QML authority tests pass.

- [x] **Step 8: Commit the consolidated desktop**

```bash
git add desktop/app desktop/ui
git diff --cached --check
git commit -m "Consolidate structural desktop execution"
```

### Task 9: Remove the retired parallel stack and retain validation fixtures

**Files:**
- Create: `cmake/AssertSingleStructuralAuthority.cmake`
- Delete: `desktop/app/structural_setup_controller.cpp`
- Delete: `desktop/app/structural_setup_controller.hpp`
- Delete: `desktop/app/tests/structural_setup_controller_tests.cpp`
- Delete: `desktop/structural/include/prometheus/structural/structural_finding.hpp`
- Delete: `desktop/structural/src/structural_finding.cpp`
- Delete: `desktop/structural/include/prometheus/structural/structural_case.hpp`
- Delete: `desktop/structural/src/structural_case.cpp`
- Delete: `desktop/structural/include/prometheus/structural/surface_setup.hpp`
- Delete: `desktop/structural/src/surface_setup.cpp`
- Delete: `desktop/structural/include/prometheus/structural/smoke_case.hpp`
- Delete: `desktop/structural/src/smoke_case.cpp`
- Delete: `desktop/structural/tools/export_structural_case.cpp`
- Delete: `desktop/structural/tools/verify_structural_case.cpp`
- Delete: `desktop/structural/tools/verify_structural_smoke.cpp`
- Delete: `desktop/structural/tests/structural_case_tools_fixture.cmake`
- Modify: `desktop/structural/include/prometheus/structural/surface_selection.hpp`
- Modify: `desktop/structural/src/surface_selection.cpp`
- Modify: `desktop/structural/CMakeLists.txt`
- Modify: `desktop/app/CMakeLists.txt`
- Modify: `scripts/run-structural-validation.ps1`
- Modify: `scripts/run-calculix-smoke.ps1`
- Modify: `docs/phase-03-structural-workflow.md`

- [x] **Step 1: Add a source-tree authority check before deleting files**

Add `cmake/AssertSingleStructuralAuthority.cmake` and register it as
`prometheus_structural_single_authority`. The script fails while any retired
source file exists or a retired symbol remains registered:

```cmake
set(retired_files
  desktop/app/structural_setup_controller.cpp
  desktop/app/structural_setup_controller.hpp
  desktop/app/tests/structural_setup_controller_tests.cpp
  desktop/structural/include/prometheus/structural/structural_finding.hpp
  desktop/structural/src/structural_finding.cpp
  desktop/structural/include/prometheus/structural/structural_case.hpp
  desktop/structural/src/structural_case.cpp
  desktop/structural/include/prometheus/structural/surface_setup.hpp
  desktop/structural/src/surface_setup.cpp
  desktop/structural/tools/export_structural_case.cpp
  desktop/structural/tools/verify_structural_case.cpp
)
foreach(path IN LISTS retired_files)
  if(EXISTS "${REPOSITORY_ROOT}/${path}")
    message(FATAL_ERROR "retired structural authority remains: ${path}")
  endif()
endforeach()
file(READ "${REPOSITORY_ROOT}/desktop/app/CMakeLists.txt" app_cmake)
file(READ "${REPOSITORY_ROOT}/desktop/structural/CMakeLists.txt" structural_cmake)
file(READ "${REPOSITORY_ROOT}/desktop/ui/Main.qml" main_qml)
foreach(symbol IN ITEMS StructuralSetupController
                        prometheus_export_structural_case
                        prometheus_verify_structural_case)
  string(FIND "${app_cmake}${structural_cmake}${main_qml}" "${symbol}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "retired structural symbol remains active: ${symbol}")
  endif()
endforeach()
```

The expected forbidden active-source patterns are:

```text
structural_setup_controller.cpp
structural_finding.cpp
StructuralSetupController
prometheus_export_structural_case
prometheus_verify_structural_case
```

- [x] **Step 2: Run the authority test and verify it fails while retired files remain in the tree**

```bash
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure -R prometheus_structural_single_authority
```

Expected: fail and name at least one retired authority.

- [x] **Step 3: Remove duplicate sources after their tests have moved**

Delete every path enumerated in this task. Move the feature branch's tested
surface-force distribution implementation into mainline's
`surface_selection.*`. Task 4 already placed canonical pre-execution evidence
in `CompiledStructuralSetup`, so remove the old case exporter/verifier and make
`run-structural-validation.ps1` call the authoritative runner/archive tools.

The retained tool set is exactly:

```text
prometheus_export_structural_smoke
prometheus_structural_mesh_probe
prometheus_run_calculix_job
prometheus_run_structural_benchmark
prometheus_run_structural_refinement
prometheus_replay_structural_run
prometheus_export_structural_archive
```

- [x] **Step 4: Make the smoke script execute the authoritative runner and parser**

`run-calculix-smoke.ps1` must generate the compiled setup/deck, invoke
`prometheus_run_calculix_job`, and fail unless the runner reports completed
validated evidence. It must not invoke `ccx` directly or call a second verifier
over the same active outputs.

- [x] **Step 5: Reconcile Phase 3 documentation with exact claims and limits**

Document one controller, one result compiler, v1 read/v2 write archives, the
single-computation invariant, the retained analytic and external gates, and
the still-open reviewed YUBI scenario/independent comparison. Preserve the
Phase 5 non-claim: manually entered component packages are not yet consumed by
structural analysis.

- [x] **Step 6: Run the authority, structural, and fixture tests**

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug --output-on-failure -R 'prometheus_structural|prometheus_project_intake_summary|prometheus_jpl_rover'
```

Expected: all selected tests pass and the authority scanner finds no retired
runtime.

- [x] **Step 7: Commit duplicate removal and documentation**

```bash
git add --all
git diff --cached --check
git commit -m "Remove parallel structural authority"
```

### Task 10: Run the amended release gate and record push readiness

**Files:**
- Modify if results changed: `docs/phase-03-structural-workflow.md`
- Modify if results changed: `docs/phase-04-persistence-and-portability.md`
- Modify: `docs/superpowers/plans/2026-08-15-mainline-structural-reconciliation.md` (check completed boxes only)

- [x] **Step 1: Verify the pulled Phase 5 backend on SQLite**

Run from `backend/`:

```bash
uv run pytest -q tests/test_manual_component_intake_v2.py tests/test_migrations_v2.py --tb=short
```

Expected: all selected Phase 5 intake and migration tests pass.

- [x] **Step 2: Verify PostgreSQL migration invariants when the configured local test database is available**

Run from `backend/` with the existing project test URL:

```bash
PROMETHEUS_TEST_POSTGRES_URL=postgresql+psycopg://127.0.0.1:55432/prometheus_program_01a_test uv run pytest -q tests/test_migrations.py tests/test_migrations_v2.py --tb=short
```

Expected: all PostgreSQL migration tests pass. If the configured service is not
running, report that external prerequisite rather than converting it into a
pass.

- [x] **Step 3: Run the complete headless build and test suite**

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --test-dir out/build/headless-debug --output-on-failure
```

Expected: zero failed tests.

- [x] **Step 4: Run the complete no-OCCT desktop build and test suite**

```bash
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure
```

Expected: zero failed tests, except that an environment-blocked loopback test
must be rerun outside the socket sandbox and reported separately with its exact
result.

- [x] **Step 5: Re-run the single-computation test in isolation**

```bash
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure -R prometheus_structural_controller
```

Expected: the counting backend reports one mesh preparation, one setup
compilation per reviewed snapshot, and one execution. The execution stage owns
exactly one result compilation and one finding compilation internally;
view/save/publication cause no backend-stage increments.

- [x] **Step 6: Validate repository and contract hygiene**

```bash
git diff --check origin/main...HEAD
cmake --list-presets
cmake --list-presets=build
cmake --list-presets=test
git status --short --branch
git log --oneline --decorate origin/main..HEAD
```

Expected: no whitespace errors, all presets parse, no uncommitted files, and a
reviewable local commit series. Do not push.

- [x] **Step 7: Record only measured verification results**

Update the Phase 3/4 status documents only when a command above produced new
evidence. Keep the YUBI real-scenario and physically separate clean-machine
trials open unless their external evidence was actually collected.

- [ ] **Step 8: Present the exact commit range, tests, remaining limits, and ask for explicit push authorization**

No `git push` occurs in this plan without a new user instruction.
