# Structural Trust Seams Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make structural requests, Gmsh meshes, and CalculiX outputs fail closed before any real-project finding can be generated.

**Architecture:** Keep engineering logic in the Qt-free `prometheus_structural` library. Split mesh inspection, face-load compilation, raw result parsing, and run-evidence compilation into focused units; the PowerShell smoke remains orchestration and calls a Qt-free verifier built from the same library.

**Tech Stack:** C++20, CMake/CTest, Gmsh Abaqus text format, CalculiX 2.23 text outputs, PowerShell orchestration.

---

## File map

- `desktop/structural/include/prometheus/structural/types.hpp`: shared node, element, face, request, and issue types.
- `desktop/structural/include/prometheus/structural/mesh_validation.hpp` and `src/mesh_validation.cpp`: signed volume, mean-ratio quality, connectivity, and boundary coverage.
- `desktop/structural/include/prometheus/structural/gmsh_mesh.hpp` and `src/gmsh_mesh.cpp`: Gmsh/Abaqus parsing and surface-group retention.
- `desktop/structural/include/prometheus/structural/surface_setup.hpp` and `src/surface_setup.cpp`: face selections to fixed nodes and deterministic nodal forces.
- `desktop/structural/src/structural_request.cpp`: request identity, review, load, and mesh validation.
- `desktop/structural/include/prometheus/structural/calculix_result.hpp` and `src/calculix_result.cpp`: typed `.dat` and `.sta` parsing plus run-evidence compilation.
- `desktop/structural/src/calculix_deck.cpp`: deterministic safe heading emission.
- `desktop/structural/tools/verify_structural_smoke.cpp`: file-based smoke verifier.
- `desktop/structural/tests/structural_tests.cpp`: focused unit and seam regressions.
- `fixtures/structural/calculix-smoke/`: checked-in complete and deliberately
  incomplete run-evidence fixtures for the verifier target.
- `scripts/run-calculix-smoke.ps1`: capture and verify a real CalculiX run.
- `CMakeLists.txt`: make the Qt-free integrity library available to structural
  result compilation without duplicating SHA-256 logic.

### Task 1: Close request-validation gaps

**Files:**
- Modify: `desktop/structural/include/prometheus/structural/types.hpp`
- Modify: `desktop/structural/src/structural_request.cpp`
- Modify: `desktop/structural/tests/structural_tests.cpp`

- [ ] **Step 1: Write failing request tests**

Add cases after `missingNode` in `structural_tests.cpp`:

```cpp
  auto zeroForce = request;
  zeroForce.nodal_forces = {{4, {0.0, 0.0, 0.0}}};
  require(hasIssue(ps::validate_request(zeroForce), "zero_resultant_load"),
          "an all-zero load stays blocked");

  auto duplicateForce = request;
  duplicateForce.nodal_forces.push_back({4, {1.0, 0.0, 0.0}});
  require(hasIssue(ps::validate_request(duplicateForce), "duplicate_load_node"),
          "duplicate nodal forces stay blocked");

  auto malformedHash = request;
  malformedHash.geometry_sha256 =
      "sha256:4A6FBA05B237B725BE2CA4E5BA7F7617674B4BCAE4164FF32E88D9E75275017A";
  require(hasIssue(ps::validate_request(malformedHash),
                   "invalid_geometry_identity"),
          "uppercase SHA-256 stays blocked");

  auto injectedHeading = request;
  injectedHeading.component_name = "bracket\n*INCLUDE, INPUT=other.inp";
  require(hasIssue(ps::validate_request(injectedHeading),
                   "unsafe_heading_text"),
          "CalculiX keyword injection stays blocked");

  auto unknownMaterial = request;
  unknownMaterial.material_applicability = "unresolved";
  require(hasIssue(ps::validate_request(unknownMaterial),
                   "material_applicability_unresolved"),
          "an unresolved material cannot become reviewed");

  auto weakMesh = request;
  weakMesh.observed_minimum_mean_ratio = 0.09;
  weakMesh.minimum_mean_ratio_threshold = 0.10;
  require(hasIssue(ps::validate_request(weakMesh), "mesh_quality_below_limit"),
          "a mesh below its predeclared quality floor stays blocked");
```

- [ ] **Step 2: Run the structural test and verify RED**

Run:

```bash
cmake --fresh --preset headless-debug
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug -R '^prometheus_structural_tests$' --output-on-failure
```

Expected: FAIL at `an all-zero load stays blocked`.

- [ ] **Step 3: Add strict helpers and validation**

Add private helpers in `structural_request.cpp`:

```cpp
bool strict_sha256(const std::string_view value) {
  return value.size() == 71U && value.starts_with("sha256:") &&
         std::ranges::all_of(value.substr(7), [](const char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

bool safe_heading_text(const std::string_view value) {
  return !value.empty() && std::ranges::all_of(value, [](const unsigned char c) {
    return c >= 0x20U && c != 0x7fU;
  });
}
```

Replace the prefix/length hash check, validate `analysis_id` and
`component_name` with `safe_heading_text`, reject repeated `node_id` values,
sum the three force components across all records, and emit
`zero_resultant_load` when the finite resultant has zero magnitude.

Extend `StructuralRequest` with the reviewed provenance needed later:

```cpp
std::string material_designation;
std::string material_temper;
std::string material_product_form;
std::string material_applicability; // exactly "known" or "assumed"
std::string material_evidence_sha256;
std::string mesh_sha256;
std::vector<std::string> restraint_surface_groups;
std::vector<std::string> load_surface_groups;
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

Require nonempty safe material and group labels, strict evidence and mesh
SHA-256 values,
`known` or `assumed` applicability, finite positive mesh target size, thresholds
in `(0, 1]`, observed quality at least the threshold, finite positive reviewed
force and selected area, a finite unit direction within `1e-12`, compiled
nodal-force resultant matching the reviewed force within a scale-aware `1e-10`
relative tolerance, and a nonempty basis for each present limit. A reviewed
flag cannot override a missing provenance field.

- [ ] **Step 4: Run the test and verify GREEN**

Run the Step 2 command. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add desktop/structural/include/prometheus/structural/types.hpp \
  desktop/structural/src/structural_request.cpp \
  desktop/structural/tests/structural_tests.cpp
git commit -m "Harden structural request validation"
```

### Task 2: Retain and validate boundary surface groups

**Files:**
- Create: `desktop/structural/include/prometheus/structural/mesh_validation.hpp`
- Create: `desktop/structural/src/mesh_validation.cpp`
- Modify: `desktop/structural/include/prometheus/structural/types.hpp`
- Modify: `desktop/structural/include/prometheus/structural/gmsh_mesh.hpp`
- Modify: `desktop/structural/src/gmsh_mesh.cpp`
- Modify: `desktop/structural/CMakeLists.txt`
- Modify: `desktop/structural/tests/structural_tests.cpp`

- [ ] **Step 1: Add failing surface and mesh-integrity tests**

Extend the existing `rawMesh` fixture with two tetrahedra and named `CPS3`
groups. Assert:

```cpp
  require(mesh.surface_groups.size() == 2,
          "Gmsh surface ELSETs remain selectable");
  require(mesh.surface_groups.front().area_m2 > 0.0,
          "surface groups expose SI area");
  require(mesh.diagnostics.connected_components == 1 &&
              mesh.diagnostics.minimum_mean_ratio > 0.0,
          "mesh diagnostics expose connectivity and tetra quality");
```

Add separate fixtures and exception checks for an inverted tetrahedron, two
face-disconnected tetrahedra, a surface triangle not present on the volume
boundary, and a missing boundary triangle.

- [ ] **Step 2: Verify RED**

Run the focused structural test command from Task 1. Expected: compilation
failure because `surface_groups` and `diagnostics` do not exist.

- [ ] **Step 3: Define mesh types and inspection API**

Add to `types.hpp`:

```cpp
struct SurfaceTriangle final {
  int id{};
  std::array<int, 3> node_ids{};
};

struct SurfaceGroup final {
  std::string name;
  std::vector<SurfaceTriangle> triangles;
  std::vector<int> node_ids;
  double area_m2{};
  std::array<double, 3> centroid_m{};
  std::array<double, 3> representative_normal{};
};

struct MeshDiagnostics final {
  std::size_t connected_components{};
  double minimum_mean_ratio{};
  double maximum_mean_ratio{};
};
```

Declare in `mesh_validation.hpp`:

```cpp
[[nodiscard]] MeshDiagnostics validate_and_measure_mesh(
    const std::vector<Node> &nodes,
    const std::vector<Tetrahedron> &elements,
    const std::vector<SurfaceGroup> &surface_groups);
```

Extend `VolumeMesh` with `surface_groups` and `diagnostics`.

- [ ] **Step 4: Implement volume and boundary checks**

In `mesh_validation.cpp`, use this scale-aware volume floor:

```cpp
const double length_scale = bounding_box_diagonal(nodes);
const double volume_floor_m3 =
    std::max(1.0e-30, std::pow(length_scale, 3) * 1.0e-15);
```

Then:

- index nodes by ID;
- calculate each signed determinant and reject volume at or below
  `volume_floor_m3` (negative volume gets `inverted_tetrahedron`; nonnegative
  volume below the floor gets `degenerate_tetrahedron`);
- calculate mean ratio as
  `12 * pow(3 * volume, 2.0 / 3.0) / sum(squared_edge_lengths)`;
- index each sorted tetra face and count occurrences;
- traverse tetrahedra joined by a shared face and require one component;
- require each surface triangle to match a face occurring exactly once;
- require every boundary face to appear exactly once in a surface group;
- compute each group's unique nodes, area-weighted centroid, and normalized
  area-vector.

Use the error strings asserted by Step 1 so every failure is distinguishable.

- [ ] **Step 5: Parse `CPS3` and `ELSET`**

Extend `gmsh_mesh.cpp`'s section state with `triangles`, parse case-insensitive
`TYPE=CPS3`, require a nonempty `ELSET=...` value, collect triangles by group,
then call `validate_and_measure_mesh()` before returning.

- [ ] **Step 6: Build and verify GREEN**

Run the focused structural test. Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add desktop/structural/include/prometheus/structural/types.hpp \
  desktop/structural/include/prometheus/structural/gmsh_mesh.hpp \
  desktop/structural/include/prometheus/structural/mesh_validation.hpp \
  desktop/structural/src/gmsh_mesh.cpp \
  desktop/structural/src/mesh_validation.cpp \
  desktop/structural/tests/structural_tests.cpp \
  desktop/structural/CMakeLists.txt
git commit -m "Retain validated structural surface groups"
```

### Task 3: Compile selected faces into deterministic loads and restraints

**Files:**
- Create: `desktop/structural/include/prometheus/structural/surface_setup.hpp`
- Create: `desktop/structural/src/surface_setup.cpp`
- Modify: `desktop/structural/CMakeLists.txt`
- Modify: `desktop/structural/tests/structural_tests.cpp`

- [ ] **Step 1: Write a failing face-compilation test**

Use a two-triangle load group sharing two nodes and assert the API:

```cpp
  const auto setup = ps::compile_surface_setup(
      mesh, {"FixedFace"}, {"LoadFace"}, 120.0, {0.0, 0.0, -2.0});
  require(setup.fully_fixed_node_ids == std::vector<int>({1, 2, 3}),
          "restraint groups compile unique sorted nodes");
  require(setup.nodal_forces.size() == setup.loaded_node_ids.size(),
          "shared load nodes compile once");
  require(std::abs(setup.resultant_force_n[2] + 120.0) < 1e-10,
          "area-weighted nodal loads preserve the reviewed resultant");
```

Add rejection tests for unknown groups, zero selected area, empty selections,
zero/nonfinite magnitude, and zero/nonfinite direction.

- [ ] **Step 2: Verify RED**

Run the focused structural test. Expected: missing header/API compilation error.

- [ ] **Step 3: Define and implement the compiler**

Declare:

```cpp
struct CompiledSurfaceSetup final {
  std::vector<int> fully_fixed_node_ids;
  std::vector<int> loaded_node_ids;
  std::vector<NodalForce> nodal_forces;
  double selected_load_area_m2{};
  std::array<double, 3> resultant_force_n{};
};

[[nodiscard]] CompiledSurfaceSetup compile_surface_setup(
    const VolumeMesh &mesh,
    const std::vector<std::string> &restraint_groups,
    const std::vector<std::string> &load_groups,
    double magnitude_n,
    const std::array<double, 3> &direction);
```

Normalize direction, compute traction from total selected area, add
`traction * triangle_area / 3` to each triangle vertex in an ordered map, and
emit sorted, unique records.

- [ ] **Step 4: Verify GREEN and commit**

Run the focused test, then:

```bash
git add desktop/structural/include/prometheus/structural/surface_setup.hpp \
  desktop/structural/src/surface_setup.cpp \
  desktop/structural/tests/structural_tests.cpp \
  desktop/structural/CMakeLists.txt
git commit -m "Compile reviewed structural face selections"
```

### Task 4: Bind parsed results to solver completion and mesh coverage

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `desktop/structural/CMakeLists.txt`
- Modify: `desktop/structural/include/prometheus/structural/calculix_result.hpp`
- Modify: `desktop/structural/src/calculix_result.cpp`
- Modify: `desktop/structural/tests/structural_tests.cpp`

- [ ] **Step 1: Write failing run-evidence tests**

Define a successful `.sta` fixture ending at step time `1.0` and a stdout
fixture containing the exact completion marker captured from CalculiX 2.23.
Assert that `compile_calculix_result()` returns complete metrics for the valid
request and an issue-only indeterminate result, one at a time, for:

- nonzero process status;
- missing completion marker;
- final `.sta` time below `1.0`;
- missing, duplicate, or unexpected displacement node;
- missing, duplicate, or unexpected element/integration-point stress row;
- nonfinite components.

- [ ] **Step 2: Verify RED**

Run the focused structural test. Expected: missing run-evidence API.

- [ ] **Step 3: Introduce typed rows and evidence**

Move `desktop/integrity` before `desktop/structural` in the root CMake graph,
include integrity whenever structural is enabled, and link
`prometheus_structural` to `prometheus_integrity`. Replace count-only parsing
with:

```cpp
struct DisplacementRow final {
  int node_id{};
  std::array<double, 3> displacement_m{};
};

struct StressRow final {
  int element_id{};
  int integration_point{};
  std::array<double, 6> stress_pa{};
};

struct CalculixDat final {
  std::vector<DisplacementRow> displacements;
  std::vector<StressRow> stresses;
};

struct CalculixRunEvidence final {
  int process_exit_code{};
  std::string solver_executable_sha256;
  std::string solver_version;
  std::string deck_bytes;
  std::string standard_output;
  std::string standard_error;
  std::string status_bytes;
  std::string data_bytes;
};

struct CalculixResultIssue final {
  std::string code;
  std::string message;
};

struct CalculixArtifactHashes final {
  std::string deck_sha256;
  std::string status_sha256;
  std::string data_sha256;
  std::string standard_output_sha256;
  std::string standard_error_sha256;
};

struct CompiledCalculixResult final {
  std::optional<CalculixMetrics> metrics;
  std::vector<CalculixResultIssue> issues;
  CalculixArtifactHashes artifacts;
  [[nodiscard]] bool complete() const {
    return metrics.has_value() && issues.empty();
  }
};

[[nodiscard]] CompiledCalculixResult compile_calculix_result(
    const StructuralRequest &request,
    const CalculixRunEvidence &evidence);
```

- [ ] **Step 4: Implement exact final-step compilation**

Require a strict executable SHA-256 and nonempty version. Recompute the deck
from `request` and require byte equality with `evidence.deck_bytes`; hash all
raw artifacts with the shared integrity library. Parse section times from
`.dat`, retain only the final requested time, parse numeric `.sta` rows, require
final step 1/time 1 within `1e-9`, compare row IDs with request node/element
IDs, and calculate maxima only after all evidence checks pass. Catch raw-parser
errors at this boundary and return a coded issue with no metrics; no failed
evidence path may return zero-valued metrics.

- [ ] **Step 5: Verify GREEN and commit**

Run the focused test, then:

```bash
git add desktop/structural/include/prometheus/structural/calculix_result.hpp \
  desktop/structural/src/calculix_result.cpp \
  desktop/structural/tests/structural_tests.cpp
git commit -m "Require complete CalculiX run evidence"
```

### Task 5: Make the real smoke traverse the result compiler

**Files:**
- Create: `desktop/structural/include/prometheus/structural/smoke_case.hpp`
- Create: `desktop/structural/src/smoke_case.cpp`
- Create: `desktop/structural/tools/verify_structural_smoke.cpp`
- Create: `fixtures/structural/calculix-smoke/complete/prometheus_tetra_smoke.inp`
- Create: `fixtures/structural/calculix-smoke/complete/prometheus_tetra_smoke.sta`
- Create: `fixtures/structural/calculix-smoke/complete/prometheus_tetra_smoke.dat`
- Create: `fixtures/structural/calculix-smoke/complete/prometheus_tetra_smoke.stdout.txt`
- Create: `fixtures/structural/calculix-smoke/complete/prometheus_tetra_smoke.stderr.txt`
- Modify: `desktop/structural/CMakeLists.txt`
- Modify: `scripts/run-calculix-smoke.ps1`
- Modify: `docs/phase-03-structural-workflow.md`

- [ ] **Step 1: Add a failing CLI test mode**

Register a CTest named `prometheus_structural_smoke_verifier_fixture` and make
`verify_structural_smoke.cpp` accept:

```text
prometheus_verify_structural_smoke OUTPUT_DIRECTORY PROCESS_EXIT_CODE \
  SOLVER_EXECUTABLE_SHA256 SOLVER_VERSION STDOUT_FILE STDERR_FILE
```

Point the test at `fixtures/structural/calculix-smoke/complete`, initially
return 9, then run:

```bash
cmake --build --preset headless-debug --target prometheus_verify_structural_smoke
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_smoke_verifier_fixture$' --output-on-failure
```

Expected: FAIL with exit code 9, proving CTest executes the new target.

- [ ] **Step 2: Implement the verifier**

Move the exact analytic request into a shared function in
`desktop/structural/include/prometheus/structural/smoke_case.hpp` and
`src/smoke_case.cpp` so exporter and verifier cannot drift. Read the generated
deck, `.sta`, `.dat`, and captured streams; call `compile_calculix_result()`;
print a compact summary only when `complete()` is true; otherwise print every
issue to stderr and return 9. The checked-in fixture is parser/coverage evidence
and must be labeled synthetic; the real Windows command remains the solver
evidence gate.

- [ ] **Step 3: Wire the PowerShell runner**

Capture solver streams without losing the exit status:

```powershell
$stdout = Join-Path $output "$job.stdout.txt"
$stderr = Join-Path $output "$job.stderr.txt"
$process = Start-Process -FilePath 'ccx' -ArgumentList $job `
  -WorkingDirectory $output -Wait -PassThru -NoNewWindow `
  -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$solver = (Get-Command ccx).Source
$solverHash = 'sha256:' + (Get-FileHash -LiteralPath $solver -Algorithm SHA256).Hash.ToLowerInvariant()
$solverVersion = (& $solver -v 2>&1 | Out-String).Trim()
& (Join-Path $repo 'out/build/windows-release/desktop/structural/prometheus_verify_structural_smoke.exe') `
  $output $process.ExitCode $solverHash $solverVersion $stdout $stderr
if ($LASTEXITCODE -ne 0) { throw 'Structural smoke result verification failed.' }
```

- [ ] **Step 4: Run unit tests locally and the real smoke on Windows**

Local:

```bash
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
```

Windows:

```powershell
.\scripts\run-calculix-smoke.ps1
```

Expected: the verifier prints the established displacement/stress metrics and
returns zero. If the actual `.sta` formatting differs from the checked-in
fixture, update the parser and fixture from the captured raw file; do not bypass
the status check.

- [ ] **Step 5: Commit**

```bash
git add desktop/structural/tools/verify_structural_smoke.cpp \
  desktop/structural/include/prometheus/structural/smoke_case.hpp \
  desktop/structural/src/smoke_case.cpp desktop/structural/CMakeLists.txt \
  fixtures/structural/calculix-smoke scripts/run-calculix-smoke.ps1 \
  docs/phase-03-structural-workflow.md
git commit -m "Verify the CalculiX smoke result seam"
```

### Task 6: Run the structural-seam gate

**Files:**
- Modify if required: `CMakePresets.json`
- Modify: `docs/phase-03-structural-workflow.md`

- [ ] **Step 1: Run format and diff hygiene**

```bash
git diff --check
```

- [ ] **Step 2: Run the complete headless suite**

```bash
cmake --fresh --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
```

- [ ] **Step 3: Run the desktop no-OCCT suite**

```bash
cmake --fresh --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --preset desktop-no-occt-debug --output-on-failure
```

- [ ] **Step 4: Record exact results and remaining limits**

Update the phase document with command, platform, test count, solver version,
artifact hashes, and the explicit statement that no YUBI finding has run.

- [ ] **Step 5: Commit**

```bash
git add docs/phase-03-structural-workflow.md CMakePresets.json
git commit -m "Record structural trust seam verification"
```
