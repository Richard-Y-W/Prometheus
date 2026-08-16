# Finite Open Cascade Bounds and YUBI Viewport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for inline execution. Apply superpowers:test-driven-development to each behavior change and superpowers:verification-before-completion before reporting success. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import the pinned Toyota YUBI gripper with finite, honest scene bounds so the real assembly can be fitted and rendered, while retaining the original B-Rep as the only source for exact interference calculations.

**Architecture:** Centralize the importer’s bounds contract in Qt-free Open Cascade helpers. A closed, finite `Bnd_Box` remains preferred; an open, void, non-finite, or unordered box falls back to finite vertices from the importer’s existing tessellation. Store the resulting bounds in metres on `AssemblyNode`, reconstruct conservative broad-phase boxes in millimetres for `LeafShape`, and reuse the same contract after placements and revolute transforms. Record the bounds source during that same pass so interactive imports with any fallback-bound leaf enter the existing deferred/unknown interference state without reparsing or pre-running a boolean. Never clamp coordinates, heal topology, or derive a collision finding from tessellation.

**Tech Stack:** C++20, Open Cascade (`Bnd_Box`, `BRepBndLib`, `BRepMesh_IncrementalMesh`), CMake/CTest, Qt Quick 3D for the final viewport check, and the pinned Toyota YUBI STEP fixture at commit `e8334ff04945ccf56c0576a56f6fab74b63daaa2`.

---

## File and responsibility map

| File | Responsibility in this increment |
| --- | --- |
| `desktop/cad/tests/step_import_tests.cpp` | Make external-fixture mode fail when a renderable leaf exposes non-finite, unordered, or implausibly unbounded dimensions. Preserve the existing synthetic exact-interference, sweep, placement, and malformed-STEP checks. |
| `desktop/cad/step_import/step_importer.cpp` | Validate Open Cascade bounds, derive mesh fallback bounds, retain each leaf's bounds source, defer the interactive exact batch when fallback was required, produce conservative broad-phase boxes, and use the same contract for imported, placed, and swept shapes. |
| `docs/superpowers/specs/2026-08-16-finite-occt-bounds-and-yubi-viewport-design.md` | Approved semantic boundary; change only if measured implementation evidence reveals a contradiction. |

## Task 1: Add a failing pinned-YUBI bounds regression

**Files:**
- Modify: `desktop/cad/tests/step_import_tests.cpp:20-24`
- Test fixture: `out/external-demo/yubi-hw/STEP/gripper/YUBI Gripper Assy_DYNAMIXEL.stp`

- [ ] **Step 1: Add test-only bounds validation helpers**

Include `<array>`, `<cmath>`, and `<string_view>`. Add a helper that checks all six extrema, ordering, and a fixture-specific maximum span without imposing that envelope on production imports:

```cpp
bool valid_external_bounds(const prometheus::cad::AssemblyNode &node) {
  const auto &b = node.bounds;
  const std::array<double, 6> values{
      b.min_x, b.min_y, b.min_z, b.max_x, b.max_y, b.max_z};
  const bool finite = std::ranges::all_of(values, [](const double value) {
    return std::isfinite(value);
  });
  if (!finite || b.min_x > b.max_x || b.min_y > b.max_y ||
      b.min_z > b.max_z) {
    return false;
  }
  constexpr double pinned_yubi_maximum_span_m = 10.0;
  return b.max_x - b.min_x < pinned_yubi_maximum_span_m &&
         b.max_y - b.min_y < pinned_yubi_maximum_span_m &&
         b.max_z - b.min_z < pinned_yubi_maximum_span_m;
}
```

In `--import-only` traversal, validate every node with non-empty mesh positions. Print the persistent ID and six extrema on failure, then return a dedicated nonzero code. Keep the current nonzero roots/leaves/triangles requirement.

- [ ] **Step 2: Build only the Open Cascade importer test**

Run:

```bash
cmake --build out/build/macos-occt-release \
  --config Release --target prometheus_step_import_tests -j 6
```

Expected: the target builds. Locate the executable rather than assuming a single-config or multi-config layout:

```bash
find out/build/macos-occt-release -type f \
  -name prometheus_step_import_tests -perm -111 -print
```

- [ ] **Step 3: Observe the regression fail before production edits**

Run the located test executable with:

```bash
out/build/macos-occt-release/desktop/cad/prometheus_step_import_tests \
  --import-only \
  "out/external-demo/yubi-hw/STEP/gripper/YUBI Gripper Assy_DYNAMIXEL.stp"
```

Expected: nonzero exit with at least one external node reporting a bound near the observed Open Cascade sentinel (`2e+97 m`). Record the exact failure. Do not weaken the envelope or skip the offending solid to obtain green.

## Task 2: Implement one closed, finite bounds contract

**Files:**
- Modify: `desktop/cad/step_import/step_importer.cpp:52-73`
- Test: `desktop/cad/tests/step_import_tests.cpp`

- [ ] **Step 1: Add pure validation and conversion helpers**

Replace `add_bounds`, whose only condition is `!box.IsVoid()`, with helpers equivalent to:

```cpp
bool ordered_finite(const BoundingBox &bounds) {
  const std::array<double, 6> values{
      bounds.min_x, bounds.min_y, bounds.min_z,
      bounds.max_x, bounds.max_y, bounds.max_z};
  return std::ranges::all_of(values, [](const double value) {
           return std::isfinite(value);
         }) &&
         bounds.min_x <= bounds.max_x &&
         bounds.min_y <= bounds.max_y &&
         bounds.min_z <= bounds.max_z;
}

bool closed_brep_bounds(const TopoDS_Shape &shape, BoundingBox &metres) {
  Bnd_Box box;
  try {
    BRepBndLib::Add(shape, box);
    if (box.IsVoid() || box.IsOpen()) {
      return false;
    }
    Standard_Real min_x, min_y, min_z, max_x, max_y, max_z;
    box.Get(min_x, min_y, min_z, max_x, max_y, max_z);
    metres = {min_x / 1000.0, min_y / 1000.0, min_z / 1000.0,
              max_x / 1000.0, max_y / 1000.0, max_z / 1000.0};
    return ordered_finite(metres);
  } catch (const Standard_Failure &) {
    return false;
  }
}
```

Add `finite_mesh_bounds(const DisplayMesh &, BoundingBox &)`. Require at least one complete XYZ vertex, reject a position count not divisible by three, reject any non-finite coordinate, and compute ordered minima/maxima directly in metres.

Add `broad_phase_bounds(const BoundingBox &, double deflection_m, Bnd_Box &)`. Convert to millimetres only after revalidating finiteness, add both extrema, and enlarge the box by the non-negative tessellation deflection. This padding keeps the display-mesh fallback conservative relative to the requested linear mesh tolerance; it does not turn the mesh into collision evidence.

- [ ] **Step 2: Make `populate_geometry` reuse its existing mesh**

Preserve topology, volume, surface area, and tessellation behavior. Resolve bounds in this order:

```cpp
node.mesh = tessellate(shape, deflection * 1000.0);
if (!closed_brep_bounds(shape, node.bounds) &&
    !finite_mesh_bounds(node.mesh, node.bounds)) {
  return false;
}
```

The B-Rep remains untouched. Do not introduce coordinate clamps, automatic healing, or a fabricated origin box.

- [ ] **Step 3: Build and rerun the pinned regression**

Run the Task 1 build and external import command again.

Expected: the external import now exits zero; every renderable leaf has finite ordered bounds, and roots, leaves, and triangle counts remain nonzero. Capture the emitted counts for the verification record.

- [ ] **Step 4: Commit the red/green importer-boundary slice**

After the focused test is green:

```bash
git add desktop/cad/step_import/step_importer.cpp \
  desktop/cad/tests/step_import_tests.cpp
git commit -m "Recover finite bounds for open STEP shapes"
```

## Task 3: Keep broad-phase bounds finite after import, placement, and sweep

**Files:**
- Modify: `desktop/cad/step_import/step_importer.cpp:25-29,75-89,109-119`
- Test: `desktop/cad/tests/step_import_tests.cpp:32-46`

- [ ] **Step 1: Add failing transformed-bounds assertions to the synthetic test**

Retain the current behavioral assertions, and add finite-bound checks on the imported synthetic nodes before calculating the sweep pivot. Exercise `static_interferences` after a placement and `sweep_revolute` through both existing ranges; these calls are the regression that transformed shapes still produce valid broad-phase boxes.

Add a public fail-closed regression by passing a non-finite placement component
and requiring `static_interferences` to throw `std::invalid_argument`. A malformed
placement must never reach Open Cascade or produce an empty result that callers
could mistake for clear.

- [ ] **Step 2: Construct every initial `LeafShape` from reviewed node bounds**

In both the single-solid and multi-solid paths, stop calling raw `BRepBndLib::Add` a second time. Convert the already-resolved `AssemblyNode.bounds` into a conservative millimetre `Bnd_Box`:

```cpp
Bnd_Box bounds;
if (populate_geometry(detail, solids[i], deflection) &&
    broad_phase_bounds(detail.bounds, deflection, bounds)) {
  leaves.push_back({detail.persistent_id, solids[i], bounds});
} else {
  ++skipped_geometry;
}
```

Apply the corresponding conversion for a single leaf from `node.bounds`.

- [ ] **Step 3: Re-resolve transformed broad-phase bounds with the same contract**

Add a focused helper that first tries `closed_brep_bounds`; only on failure does it tessellate the transformed shape and call `finite_mesh_bounds`. Convert the result using `broad_phase_bounds`.

Reject non-finite placement and sweep inputs before creating Open Cascade
transforms. Use the helper in `apply_placements` after all rotations and
translation; throw if finite bounds cannot be established. Use the same helper
for each moved sweep shape and throw if it fails. Never invoke `IsOut` or an
exact boolean with a void/open bound, and never translate a bounds failure into
an empty result that could be interpreted as clear.

- [ ] **Step 4: Run the full importer behavior test**

Run:

```bash
out/build/macos-occt-release/desktop/cad/prometheus_step_import_tests
```

Expected: exit zero. This proves the known synthetic overlap remains exactly detected, the known clear/collision sweeps keep their expected results, the placement clears the overlap, and malformed STEP still fails closed.

Run the pinned YUBI external import once more and confirm the same finite counts as Task 2.

- [ ] **Step 5: Commit transformed-bound propagation**

```bash
git add desktop/cad/step_import/step_importer.cpp \
  desktop/cad/tests/step_import_tests.cpp
git commit -m "Keep transformed STEP broad phases finite"
```

## Task 3A: Defer pathological exact work without a second import pass

**Files:**
- Modify: `desktop/cad/step_import/step_importer.cpp:42-190`
- Modify: `desktop/cad/tests/step_import_tests.cpp:34-35`
- Test fixture: `out/external-demo/yubi-hw/STEP/gripper/YUBI Gripper Assy_DYNAMIXEL.stp`

- [ ] **Step 1: Make external-fixture mode request normal automatic interference**

Change the external test call from explicitly deferred input:

```cpp
StepImporter{}.import_file(argv[2], 0.0015, false)
```

to the normal interactive request:

```cpp
StepImporter{}.import_file(argv[2], 0.0015, true)
```

Require the returned warnings to contain both `interference was deferred` and
`tessellation-derived bounds`. Continue requiring finite bounds, 1 root, 90
leaves, and 37,367 triangles. This distinguishes risk-based automatic deferral
from a caller that requested no exact work.

- [ ] **Step 2: Observe the pre-policy timeout**

Build the test, then run the pinned fixture under a ten-second parent-process
timeout:

```bash
cmake --build out/build/macos-occt-release \
  --config Release --target prometheus_step_import_tests -j 6
python3 -c 'import subprocess,sys
try:
    completed=subprocess.run(sys.argv[1:],timeout=10)
except subprocess.TimeoutExpired:
    print("YUBI automatic exact import exceeded 10 seconds")
    raise SystemExit(124)
raise SystemExit(completed.returncode)' \
  out/build/macos-occt-release/desktop/cad/prometheus_step_import_tests \
  --import-only \
  "out/external-demo/yubi-hw/STEP/gripper/YUBI Gripper Assy_DYNAMIXEL.stp"
```

Expected before the policy change: exit 124 with `YUBI automatic exact import
exceeded 10 seconds`. The live desktop sample already located the worker inside
`BRepAlgoAPI_Common`; do not increase the timeout to make that boolean part of
interactive loading.

- [ ] **Step 3: Retain the bounds source without repeating bounds work**

Add a private source enum and store it on each collision leaf:

```cpp
enum class BoundsSource { unavailable, closed_brep, tessellation };

struct LeafShape {
  std::string id;
  TopoDS_Shape shape;
  Bnd_Box bounds;
  BoundsSource bounds_source{BoundsSource::unavailable};
};
```

Change `populate_geometry` to return `BoundsSource`. It must tessellate once,
try the closed B-Rep box once, and otherwise reuse `node.mesh`:

```cpp
BoundsSource populate_geometry(AssemblyNode &node, const TopoDS_Shape &shape,
                               double deflection) {
  if (shape.IsNull()) return BoundsSource::unavailable;
  try {
    node.mesh = tessellate(shape, deflection * 1000.0);
    const auto bounds_source = closed_brep_bounds(shape, node.bounds)
                                   ? BoundsSource::closed_brep
                                   : finite_mesh_bounds(node.mesh, node.bounds)
                                         ? BoundsSource::tessellation
                                         : BoundsSource::unavailable;
    if (bounds_source == BoundsSource::unavailable)
      return bounds_source;
    GProp_GProps volume;
    BRepGProp::VolumeProperties(shape, volume);
    node.volume_m3 = volume.Mass() / 1e9;
    GProp_GProps surface;
    BRepGProp::SurfaceProperties(shape, surface);
    node.surface_area_m2 = surface.Mass() / 1e6;
    TopTools_IndexedMapOfShape faces, edges;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    node.face_count = faces.Extent();
    node.edge_count = edges.Extent();
    return bounds_source;
  } catch (const Standard_Failure &) {
    return BoundsSource::unavailable;
  }
}
```

For aggregate nodes, compare the enum with `unavailable` where the old code
used `bool node_geometry_ok`. For single- and multi-solid leaves, copy the
already returned source into `LeafShape`; do not call `closed_brep_bounds` or
`tessellate` again.

- [ ] **Step 4: Route fallback-bound batches into the existing deferred state**

After `read_node` has populated `leaves`, count fallback-bound leaves once:

```cpp
const auto fallback_leaf_count = std::ranges::count_if(
    leaves, [](const LeafShape &leaf) {
      return leaf.bounds_source == BoundsSource::tessellation;
    });
const bool defer_for_fallback = fallback_leaf_count > 0;
```

Run the existing exact pair loop only when
`compute_interferences && !defer_for_fallback`. When the caller requested exact
work but fallback was observed, emit:

```cpp
result.warnings.push_back(
    "Static interference was deferred because " +
    std::to_string(fallback_leaf_count) +
    " imported shapes required tessellation-derived bounds");
```

When `compute_interferences` is false, retain the existing large-assembly
warning. Do not emit both messages. Do not change `static_interferences`; that
method remains the explicit exact attempt and continues to use original B-Reps.

- [ ] **Step 5: Verify automatic deferral and unchanged small-fixture exact work**

Run the timeout command from Step 2 again.

Expected after the policy change: exit 0 in approximately the established
two-second parse/tessellation time with:

```text
Imported roots=1 leaves=90 triangles=37367 interferences=deferred
```

Run:

```bash
out/build/macos-occt-release/desktop/cad/prometheus_step_import_tests
```

Expected: exit 0. The three-leaf synthetic motor arm still completes automatic
exact interference and finds its one known overlap; its placement, sweep,
non-finite-input, and malformed-STEP checks remain green.

- [ ] **Step 6: Commit the bounded policy change**

```bash
git add desktop/cad/step_import/step_importer.cpp \
  desktop/cad/tests/step_import_tests.cpp
git commit -m "Defer exact checks for fallback STEP geometry"
```

## Task 4: Verify the real macOS viewport and record bounded evidence

**Files:**
- Verify: `out/build/macos-occt-release/desktop/app/prometheus_desktop`
- Verify fixture: `out/external-demo/yubi-hw/STEP/gripper/YUBI Gripper Assy_DYNAMIXEL.stp`

- [ ] **Step 1: Rebuild the production desktop**

Run:

```bash
cmake --build out/build/macos-occt-release \
  --config Release --target prometheus_desktop -j 6
```

Expected: the desktop and importer rebuild successfully from tracked source. Confirm there is no uncommitted temporary import-deferral hook.

- [ ] **Step 2: Replace the stale black-window process**

Stop only the known Prometheus process launched for this YUBI inspection. Launch the rebuilt desktop normally and load the exact pinned assembly. If exact interference is still materially slow, use the product’s existing explicit deferred-interference flow if available; do not label deferred work as clear and do not add an undocumented production bypass in this task.

- [ ] **Step 3: Inspect the rendered evidence**

Use `Fit`, select the same previously problematic solid, and verify:

- the assembly is visible rather than a black viewport;
- displayed dimensions are finite and metre-scale, not `2e+97 m`;
- the source filename is `YUBI Gripper Assy_DYNAMIXEL.stp`;
- hierarchy and mesh remain populated; and
- interference remains explicitly deferred or reports only completed exact results.

Capture a screenshot for the user. State explicitly that visibility validates framing and tessellation only, not structural adequacy or project correctness.

## Task 5: Run the bounded completion gate

**Files:**
- Verify only; update documentation only if observed counts or limitations differ from the approved design.

- [ ] **Step 1: Run focused Open Cascade CTest**

Run:

```bash
ctest --test-dir out/build/macos-occt-release \
  -C Release -R prometheus_step_import_tests --output-on-failure
```

Expected: the registered synthetic importer test passes.

- [ ] **Step 2: Run unchanged headless and desktop-no-OCCT suites**

Run:

```bash
cmake --build --preset headless-debug -j 6
ctest --preset headless-debug --output-on-failure
cmake --build --preset desktop-no-occt-debug -j 6
ctest --preset desktop-no-occt-debug --output-on-failure
```

Expected: all non-network tests pass. If the known managed-sandbox loopback HTTP test fails, rerun that exact test outside the socket sandbox and report both outcomes; do not hide or relabel it.

- [ ] **Step 3: Check source and commit hygiene**

Run:

```bash
git diff --check
git status --short --branch
git log --oneline --decorate -5
```

Expected: no whitespace errors or unintended files. The branch may remain ahead of origin because pushing requires a separate user request.

- [ ] **Step 4: Final evidence report**

Report the exact YUBI root/leaf/triangle counts, focused and broader test counts, selected finite dimensions, screenshot outcome, any explicit deferred analysis, commits created, and whether the branch is pushed. Do not claim an engineering pass from a successful render.
