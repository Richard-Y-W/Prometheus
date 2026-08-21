# Quantity- and Region-Specific Structural Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make structural refinement acceptance depend on predeclared physical quantities and regions, preserve unstable global peaks as honest diagnostics, and replay the evidence through archive v4 without repeating active solver work.

**Architecture:** Add a Qt-free observable compiler/evaluator between normalized CalculiX results and the existing refinement compiler. Typed criteria own canonical observable definitions; refinement derives per-observable comparisons and global-extremum diagnostics, findings require a converged matching scope, and archive v4 replays those derivations from both raw samples. Existing archives remain readable, real-project defaults remain global, and only the explicit validation command constructs or solves the approved 80/120 cantilever pair.

**Tech Stack:** C++20, CMake/CTest, nlohmann/json, the Prometheus RFC 8785/SHA-256 integrity library, Qt 6 controller tests, CalculiX 2.23, and Docker/Colima for native Linux ARM validation.

---

## File map

New focused files:

- desktop/structural/include/prometheus/structural/structural_observables.hpp: observable quantity, reduction, region, specification, compiled definition, evaluated value, and diagnostic contracts.
- desktop/structural/src/structural_observables.cpp: strict observable compilation and deterministic extraction from one normalized setup-bound result.
- desktop/structural/tests/structural_observable_tests.cpp: small-mesh contract, coverage, selection, and failure tests; never constructs the approved fine mesh.

Existing structural files:

- desktop/structural/include/prometheus/structural/structural_refinement.hpp and desktop/structural/src/structural_refinement.cpp: lock observable definitions into criterion identity and compare two evaluated samples.
- desktop/structural/include/prometheus/structural/structural_findings.hpp and desktop/structural/src/structural_findings.cpp: require a matching accepted global observable before issuing either existing global requirement finding; retain explicit unknowns.
- desktop/structural/include/prometheus/structural/structural_benchmarks.hpp and desktop/structural/src/structural_benchmarks.cpp: own the approved 80/120 pair and cantilever regional profile/reference.
- desktop/structural/include/prometheus/structural/structural_archive.hpp and desktop/structural/src/structural_archive.cpp: write and replay v4 while retaining v1-v3 compatibility.
- desktop/structural/tools/run_structural_benchmark.cpp and desktop/structural/tools/run_structural_refinement.cpp: select profiles, report regional acceptance separately from the clamp peak, and run each sample once.
- desktop/structural/tests/structural_tests.cpp and desktop/structural/tests/structural_tool_fixture.cmake: archive/finding integration, tamper tests, and a bounded tool smoke profile.
- desktop/structural/CMakeLists.txt: compile the new source and focused test.

Persistence and desktop files:

- desktop/run_store/include/prometheus/run_store/project_v2.hpp: register archive-v4 identity.
- desktop/run_store/src/structural_archive_store.cpp, desktop/run_store/src/run_store.cpp, and desktop/run_store/src/project_v2.cpp: accept v4 wherever the two-sample v3 graph is accepted, without adding a project-run graph version.
- desktop/run_store/tests/run_store_transaction_tests.cpp: v4 pack/publish/reconstruct coverage and v3 compatibility.
- desktop/app/structural_controller.cpp: create the conservative typed global default and display fine metrics from v3/v4 stored runs.
- desktop/app/tests/structural_controller_tests.cpp: require two executions, one finalization, v4 publication/restoration, and unchanged global scope.

Scripts and documentation:

- scripts/run-structural-validation.ps1 and scripts/run-structural-benchmarks.ps1: require v4 and scoped/global markers.
- docs/superpowers/specs/2026-08-20-quantity-region-specific-structural-convergence-design.md: mark implemented only after verification.

## Task 1: Preserve the approved mesh prerequisites without slowing unit tests

**Files:**

- Modify: desktop/structural/tests/structural_tests.cpp:506-532
- Retain pending changes: desktop/structural/include/prometheus/structural/structural_benchmarks.hpp
- Retain pending changes: desktop/structural/src/structural_benchmarks.cpp
- Retain pending changes: desktop/structural/src/structural_setup.cpp
- Retain pending changes: desktop/structural/tools/run_structural_benchmark.cpp
- Retain pending changes: desktop/structural/tools/run_structural_refinement.cpp

- [ ] **Step 1: Replace the expensive fine-mesh construction assertion**

Keep the exact six division assertions. Delete the call that constructs cantilever_benchmark(120, 18, 18), then add:

    const auto validationFineElements =
        6ULL * static_cast<unsigned long long>(
                   validationMeshes.fine.length_divisions) *
        static_cast<unsigned long long>(
            validationMeshes.fine.width_divisions) *
        static_cast<unsigned long long>(
            validationMeshes.fine.height_divisions);
    require(validationFineElements == 233280ULL &&
                validationFineElements <= 480000ULL,
            "approved fine cantilever remains inside the mesher element bound");

The explicit Task 8 validation, not ordinary CTest, proves full setup compilation.

- [ ] **Step 2: Build and time the focused test**

Run:

    cmake --preset headless-debug
    cmake --build --preset headless-debug --target prometheus_structural_tests
    /usr/bin/time -p ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_tests$' --output-on-failure

Expected: PASS without constructing 233,280 tetrahedra in the test.

- [ ] **Step 3: Check and commit only the prerequisite diff**

Run:

    git diff --check
    git diff --stat
    git add desktop/structural/include/prometheus/structural/structural_benchmarks.hpp       desktop/structural/src/structural_benchmarks.cpp       desktop/structural/src/structural_setup.cpp       desktop/structural/tests/structural_tests.cpp       desktop/structural/tools/run_structural_benchmark.cpp       desktop/structural/tools/run_structural_refinement.cpp
    git commit -m "feat: pin cantilever validation meshes"

Expected: one isolated commit containing the six already-pending files.

## Task 2: Add the typed observable compiler and one-result evaluator

**Files:**

- Create: desktop/structural/include/prometheus/structural/structural_observables.hpp
- Create: desktop/structural/src/structural_observables.cpp
- Create: desktop/structural/tests/structural_observable_tests.cpp
- Modify: desktop/structural/CMakeLists.txt

- [ ] **Step 1: Create the exact public contract**

Create structural_observables.hpp with:

    #pragma once

    #include "prometheus/structural/calculix_result.hpp"
    #include "prometheus/structural/structural_setup.hpp"

    #include <array>
    #include <cstddef>
    #include <string>
    #include <vector>

    namespace prometheus::structural {

    enum class StructuralObservableQuantity {
      displacement_magnitude_m,
      von_mises_stress_pa
    };
    enum class StructuralObservableReduction { maximum };
    enum class StructuralObservableRegionKind {
      all_nodes,
      all_elements,
      element_centroid_box_m
    };

    struct StructuralElementCentroidBox final {
      std::array<double, 3> minimum_m{};
      std::array<double, 3> maximum_m{};
    };

    struct StructuralObservableRegion final {
      StructuralObservableRegionKind kind{};
      StructuralElementCentroidBox element_centroid_box_m{};
    };

    struct StructuralObservableSpec final {
      std::string id;
      StructuralObservableQuantity quantity{};
      StructuralObservableReduction reduction{};
      StructuralObservableRegion region;
      double maximum_change_fraction{};
    };

    struct StructuralObservableDefinition final {
      StructuralObservableSpec spec;
      std::string identity;
    };

    struct StructuralObservableValue final {
      StructuralObservableDefinition definition;
      double value{};
      std::size_t selected_rows{};
    };

    struct StructuralObservableIssue final {
      std::string code;
      std::string observable_id;
      std::string message;
    };

    struct StructuralObservableCompilation final {
      std::vector<StructuralObservableValue> values;
      std::vector<StructuralObservableIssue> issues;
      [[nodiscard]] bool complete() const noexcept {
        return !values.empty() && issues.empty();
      }
    };

    [[nodiscard]] std::vector<StructuralObservableDefinition>
    compile_structural_observable_definitions(
        std::vector<StructuralObservableSpec> specs);

    [[nodiscard]] std::vector<StructuralObservableSpec>
    global_structural_observable_specs(double maximum_change_fraction);

    [[nodiscard]] StructuralObservableCompilation
    evaluate_structural_observables(
        const std::vector<StructuralObservableDefinition> &definitions,
        const CompiledStructuralSetup &setup,
        const CompiledCalculixResult &result);

    } // namespace prometheus::structural

- [ ] **Step 2: Register the source and focused test**

Add src/structural_observables.cpp to prometheus_structural. Under BUILD_TESTING add:

    add_executable(prometheus_structural_observable_tests
      tests/structural_observable_tests.cpp
    )
    target_link_libraries(prometheus_structural_observable_tests PRIVATE
      prometheus_structural
    )
    add_test(
      NAME prometheus_structural_observable_tests
      COMMAND prometheus_structural_observable_tests
    )

- [ ] **Step 3: Write failing definition and region tests**

Use cantilever_benchmark(4, 2, 2). Build one complete synthetic normalized row per reviewed node and element. Compile:

    const auto definitions =
        ps::compile_structural_observable_definitions({
            {.id = "test.maximum_displacement",
             .quantity =
                 ps::StructuralObservableQuantity::displacement_magnitude_m,
             .reduction = ps::StructuralObservableReduction::maximum,
             .region = {
                 .kind =
                     ps::StructuralObservableRegionKind::all_nodes},
             .maximum_change_fraction = 0.10},
            {.id = "test.section_stress",
             .quantity =
                 ps::StructuralObservableQuantity::von_mises_stress_pa,
             .reduction = ps::StructuralObservableReduction::maximum,
             .region = {
                 .kind = ps::StructuralObservableRegionKind::
                     element_centroid_box_m,
                 .element_centroid_box_m = {
                     .minimum_m = {0.25, 0.0, -0.05},
                     .maximum_m = {0.50, 0.1, 0.05}}},
             .maximum_change_fraction = 0.10}});

Assert two values, all-node coverage, and a nonempty strict element subset. Assert identity changes across valid ID, quantity/region, box-bound, and threshold changes. Because `maximum` is the only supported reduction, assert that an out-of-range reduction enum is rejected rather than inventing a second reduction solely for testing. Reject an empty list, duplicate/unsafe IDs, wrong quantity-region pairs, non-finite/reversed boxes, and thresholds outside (0, 1].

- [ ] **Step 4: Run RED**

Run:

    cmake --build --preset headless-debug       --target prometheus_structural_observable_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_observable_tests$' --output-on-failure

Expected: build/link failure because the implementation is absent.

- [ ] **Step 5: Implement strict definition compilation**

Use canonical schema urn:prometheus:schema:structural-observable-definition:1.0.0. IDs contain 1-128 lowercase characters from a-z, 0-9, dot, underscore, or hyphen. Bounds are finite and inclusive. Threshold is in (0, 1]. Emit `refinement_observable_duplicate` for duplicate IDs, `refinement_region_invalid` for bad bounds, and `refinement_observable_invalid` for every other definition error. global_structural_observable_specs(x) returns exactly:

    return {
        {.id = "global.maximum_displacement",
         .quantity =
             StructuralObservableQuantity::displacement_magnitude_m,
         .reduction = StructuralObservableReduction::maximum,
         .region = {
             .kind = StructuralObservableRegionKind::all_nodes},
         .maximum_change_fraction = maximum_change_fraction},
        {.id = "global.maximum_von_mises_stress",
         .quantity =
             StructuralObservableQuantity::von_mises_stress_pa,
         .reduction = StructuralObservableReduction::maximum,
         .region = {
             .kind = StructuralObservableRegionKind::all_elements},
         .maximum_change_fraction = maximum_change_fraction}};

- [ ] **Step 6: Implement deterministic evaluation**

Build ID maps from the reviewed mesh. Reject duplicate or unknown result IDs. Require integration point 1 for selected C3D4 elements. Compute each tetrahedron centroid from its four reviewed node coordinates and use inclusive bounds. Require at least one selected row. Reject non-finite or negative magnitudes/stresses. Consume result.normalized only: do not read DAT, call its parser, or invoke a solver.

- [ ] **Step 7: Add exact fail-closed coverage cases**

Require:

    missing selected row       -> refinement_observable_row_missing
    duplicate selected row     -> refinement_observable_row_duplicate
    unknown node or element    -> refinement_observable_entity_unknown
    empty centroid region      -> refinement_region_empty
    NaN, Inf, negative value   -> refinement_observable_nonfinite
    wrong integration point    -> refinement_observable_row_missing

- [ ] **Step 8: Run GREEN and commit**

    cmake --build --preset headless-debug       --target prometheus_structural_observable_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_observable_tests$' --output-on-failure
    git add desktop/structural/CMakeLists.txt       desktop/structural/include/prometheus/structural/structural_observables.hpp       desktop/structural/src/structural_observables.cpp       desktop/structural/tests/structural_observable_tests.cpp
    git commit -m "feat: evaluate typed structural observables"

Expected: focused test PASS.

## Task 3: Compile two samples into scoped refinement evidence

**Files:**

- Modify: desktop/structural/include/prometheus/structural/structural_refinement.hpp
- Modify: desktop/structural/src/structural_refinement.cpp
- Modify: desktop/structural/tests/structural_observable_tests.cpp
- Modify: desktop/structural/tests/structural_tests.cpp

- [ ] **Step 1: Write failing pair-comparison tests**

Require these records:

    enum class StructuralObservableConvergenceStatus {
      accepted,
      indeterminate
    };

    struct StructuralObservableComparison final {
      StructuralObservableDefinition definition;
      double coarse_value{};
      double fine_value{};
      std::size_t coarse_selected_rows{};
      std::size_t fine_selected_rows{};
      double change_fraction{};
      StructuralObservableConvergenceStatus status{
          StructuralObservableConvergenceStatus::indeterminate};
    };

    struct StructuralGlobalExtremumDiagnostic final {
      StructuralObservableQuantity quantity{};
      double coarse_value{};
      double fine_value{};
      int coarse_entity_id{};
      int fine_entity_id{};
      std::array<double, 3> coarse_position_m{};
      std::array<double, 3> fine_position_m{};
      double change_fraction{};
      double comparison_threshold{};
      bool participated_in_acceptance{};
      bool within_threshold{};
    };

Use small synthetic samples. Put the 12% peak in an element outside the regional box, keep rows inside the box within 2%, and regenerate `CalculixMetrics` from those same normalized rows. Require the regional comparison to accept while the retained global stress diagnostic has `participated_in_acceptance == false` and `within_threshold == false`. Require either declared observable above its threshold to make the overall status indeterminate.

- [ ] **Step 2: Run RED**

    cmake --build --preset headless-debug       --target prometheus_structural_observable_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_observable_tests$' --output-on-failure

Expected: FAIL because typed criteria/comparisons are absent.

- [ ] **Step 3: Extend the immutable criterion**

Add:

    [[nodiscard]] const std::vector<StructuralObservableDefinition> &
    observables() const noexcept;
    [[nodiscard]] bool legacy_global_extrema_only() const noexcept;

and:

    [[nodiscard]] StructuralRefinementCriterion
    compile_structural_refinement_criterion(
        std::vector<StructuralObservableSpec> observable_specs);

Canonicalize the overload as urn:prometheus:schema:structural-refinement-criterion:2.0.0 with the complete definition array. Keep the existing double overload solely for exact v3 reproduction; it retains its v1 identity.

- [ ] **Step 4: Derive typed comparisons once**

For v2 criteria, evaluate coarse once and fine once. Pair by definition identity, apply the existing zero-safe relative-change formula, and accept only if every definition meets its own threshold. Translate evaluator issues into StructuralRefinementIssue without changing codes.

Derive global displacement and stress diagnostics from the existing normalized rows and reviewed mesh without reparsing output. Break equal-value ties by the smallest entity ID, record the node position or element centroid, and require the value to agree with `CalculixMetrics`. A diagnostic participates only if a definition uses its matching global region. Its comparison threshold comes from the matching quantity definition even when that definition is regional.

Expose:

    [[nodiscard]] const std::vector<StructuralObservableComparison> &
    observable_comparisons() const noexcept;
    [[nodiscard]] const std::vector<StructuralGlobalExtremumDiagnostic> &
    global_extremum_diagnostics() const noexcept;

Retain legacy scalar getters for v3 replay. New production/tool code must not use stress_change_fraction as scoped acceptance.

- [ ] **Step 5: Verify the legacy path and commit**

    cmake --build --preset headless-debug       --target prometheus_structural_observable_tests                prometheus_structural_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_(observable_)?tests$' --output-on-failure
    git add desktop/structural/include/prometheus/structural/structural_refinement.hpp       desktop/structural/src/structural_refinement.cpp       desktop/structural/tests/structural_observable_tests.cpp       desktop/structural/tests/structural_tests.cpp
    git commit -m "feat: compile scoped structural refinement"

Expected: both tests PASS, including v3 criterion identities.

## Task 4: Match findings to converged scope and retain unknowns

**Files:**

- Modify: desktop/structural/include/prometheus/structural/structural_findings.hpp
- Modify: desktop/structural/src/structural_findings.cpp
- Modify: desktop/structural/tests/structural_observable_tests.cpp
- Modify: desktop/structural/tests/structural_tests.cpp

- [ ] **Step 1: Write failing coverage tests**

Add:

    struct StructuralUnevaluatedObligation final {
      std::string obligation;
      std::string code;
      std::string detail;
    };

Add vector<StructuralUnevaluatedObligation> unknowns to StructuralEvaluation. Require:

    global displacement + global stress accepted -> 2 findings, 0 unknowns
    global displacement + regional stress accepted -> 1 finding, 1 unknown
    any required observable indeterminate -> 0 findings, 2 unknowns

The unmatched stress entry is exactly:

    {"maximum_von_mises_stress",
     "matching_converged_scope_missing",
     "The global stress obligation has no accepted all-elements stress observable."}

For an above-threshold pair, use `refinement_observable_not_converged` for each declared obligation's unknown. Invalid solver evidence is rejected before a `VerifiedStructuralRefinement` exists and therefore cannot enter the finding compiler.

- [ ] **Step 2: Run RED**

    cmake --build --preset headless-debug       --target prometheus_structural_observable_tests                prometheus_structural_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_(observable_)?tests$' --output-on-failure

Expected: FAIL because overall refinement currently authorizes both findings.

- [ ] **Step 3: Implement exact scope matching**

Displacement requires an accepted displacement_magnitude_m, maximum, all_nodes comparison. Stress requires an accepted von_mises_stress_pa, maximum, all_elements comparison. Regional stress never matches the current global stress requirement. Preserve inclusive limit behavior and four setup/result evidence identities.

Set declared_obligations equal to findings plus unknowns and evaluated_obligations equal to findings. Unknowns are not informational findings.

- [ ] **Step 4: Update the summary contract**

For v4, StructuralRefinementSummary copies:

    std::vector<StructuralObservableComparison> observables;
    std::vector<StructuralGlobalExtremumDiagnostic> global_extrema;

Retain legacy scalar summary fields for v3 only. V4 finding scope names all reviewed mesh nodes or all reviewed C3D4 elements. Do not emit a regional stress finding in this phase.

- [ ] **Step 5: Run GREEN and commit**

    cmake --build --preset headless-debug       --target prometheus_structural_observable_tests                prometheus_structural_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_(observable_)?tests$' --output-on-failure
    git add desktop/structural/include/prometheus/structural/structural_findings.hpp       desktop/structural/src/structural_findings.cpp       desktop/structural/tests/structural_observable_tests.cpp       desktop/structural/tests/structural_tests.cpp
    git commit -m "feat: bind structural findings to converged scope"

## Task 5: Write and replay structural archive v4

**Files:**

- Modify: desktop/structural/include/prometheus/structural/structural_archive.hpp
- Modify: desktop/structural/src/structural_archive.cpp
- Modify: desktop/structural/tests/structural_tests.cpp

- [ ] **Step 1: Write failing v4 round-trip and tamper tests**

For a typed-global accepted pair require archive schema 4.0.0 and a valid replay with empty unknowns. For a regional-stress pair require one finding and one unknown. Copy and tamper separate manifests for quantity, one box bound, threshold, selected-row count, coarse value, fine value, change, participation flag, global peak entity/location/value, unknown code, and status; each replay must fail. Keep existing v1-v3 fixtures passing.

- [ ] **Step 2: Run RED**

    cmake --build --preset headless-debug --target prometheus_structural_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_tests$' --output-on-failure

Expected: FAIL because typed criteria have no v4 writer/verifier.

- [ ] **Step 3: Freeze the closed v4 contract**

Add:

    constexpr auto archiveSchemaV4 =
        "urn:prometheus:schema:structural-run-archive:4.0.0";

Exact root members:

    $schema, schema_version, archive_kind, analysis_id, component_name,
    geometry_sha256, criterion, boundary_correspondence, samples, comparison,
    coverage, findings, unknowns, limitation

criterion contains identity and observables. Each definition contains identity, id, quantity, reduction, region, and maximum_change_fraction. comparison contains status, observables, global_extrema, setup_sha256, and result_sha256. Each comparison contains definition identity, coarse/fine values, selected-row counts, change, threshold, and status.

- [ ] **Step 4: Split writer paths without changing v3**

Move the current body unchanged into write_v3_structural_refinement_archive. Dispatch legacy criteria to v3 and typed criteria to v4. V4 requires:

    declared_obligations == findings.size() + unknowns.size()
    evaluated_obligations == findings.size()
    accepted: matched findings and/or explicit unknowns
    indeterminate: zero findings and one unknown per declared obligation

Serialize existing derived objects only. The writer must not run CalculiX, parse DAT, or compile refinement/findings again.

- [ ] **Step 5: Implement strict v4 replay**

Reuse the 14-artifact reconstruction. Parse definitions into StructuralObservableSpec, recompile the criterion and identity, reconstruct both normalized results, compile refinement/findings once, and require regenerated criterion, comparison, coverage, findings, unknowns, and limitation JSON to equal the manifest exactly. Keep explicit v1-v3 dispatch.

Verification detail:

    v4 two-sample setup, solver evidence, scoped comparison, unknowns, and findings replay verified

- [ ] **Step 6: Run tests and commit**

    cmake --build --preset headless-debug       --target prometheus_structural_tests                prometheus_structural_observable_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_(observable_)?tests$' --output-on-failure
    git add desktop/structural/include/prometheus/structural/structural_archive.hpp       desktop/structural/src/structural_archive.cpp       desktop/structural/tests/structural_tests.cpp
    git commit -m "feat: replay scoped structural archive v4"

## Task 6: Carry archive v4 through the store and desktop

**Files:**

- Modify: desktop/run_store/include/prometheus/run_store/project_v2.hpp
- Modify: desktop/run_store/src/structural_archive_store.cpp
- Modify: desktop/run_store/src/run_store.cpp
- Modify: desktop/run_store/src/project_v2.cpp
- Modify: desktop/run_store/tests/run_store_transaction_tests.cpp
- Modify: desktop/app/structural_controller.cpp
- Modify: desktop/app/tests/structural_controller_tests.cpp

- [ ] **Step 1: Write failing run-store tests**

Add structural_manifest_schema_id_v4. Generalize `create_structural_archive_v3_fixture` into `create_structural_refinement_archive_fixture(root, directoryName, schemaId, schemaVersion)`. Reuse its exact fourteen files and sample records. Build the root as follows so the only v3/v4 fixture difference is the registered contract and the v4-only unknowns member:

    nlohmann::json document{
        {"$schema", schemaId},
        {"schema_version", schemaVersion},
        {"archive_kind", "linear_static_refinement_study"},
        {"analysis_id", "embedded-refinement-analysis"},
        {"component_name", "component"},
        {"geometry_sha256", geometrySha256},
        {"criterion", nlohmann::json::object()},
        {"boundary_correspondence", nlohmann::json::object()},
        {"samples",
         {{"coarse", sample("coarse", 'a', 'b')},
          {"fine", sample("fine", 'c', 'd')}}},
        {"comparison", nlohmann::json::object()},
        {"coverage", nlohmann::json::object()},
        {"findings", nlohmann::json::array()},
        {"limitation", "bounded"}};
    if (schemaVersion == "4.0.0")
      document["unknowns"] = nlohmann::json::array();

Require pack, publish, committed-reference parsing, reconstruction, and relocation. Claim v4 over canonical v3 bytes and require `structural_manifest_contract_invalid`.

Keep structural-project-run:2.0.0. V3 and v4 both have two samples and fourteen artifacts; do not add project-run v3.

- [ ] **Step 2: Run RED**

    cmake --build --preset headless-debug       --target prometheus_run_store_transaction_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_run_store_transaction$' --output-on-failure

Expected: FAIL because v4 is unregistered.

- [ ] **Step 3: Extend only two-sample allowlists**

Where code tests version3 or archiveV3, use:

    const bool twoSampleArchive =
        (schemaId == structural_manifest_schema_id_v3 &&
         schemaVersion == "3.0.0") ||
        (schemaId == structural_manifest_schema_id_v4 &&
         schemaVersion == "4.0.0");

Use it for refinement archive kind, fourteen artifacts, sample traversal, project-run v2 compatibility, committed reference validation, pack, publish, reconstruct, and relocate. Reject v4 under project-run v1.

- [ ] **Step 4: Write failing controller tests**

Require:

    criterion.observables().size() == 2U
    completed.archive->schema_version == "4.0.0"
    acceptedBackend->counts().execute_sample == 2
    acceptedBackend->counts().finalize_refinement == 1

Require all_nodes and all_elements regions. Repeated lastRun, storedRuns, save, and restore reads must not increment execution/finalization counts.

- [ ] **Step 5: Create the conservative controller default**

Replace the scalar factory call with:

    const auto maximumChange =
        draft_.value("refinement_maximum_change_fraction").toDouble();
    refinement_criterion_ = ps::compile_structural_refinement_criterion(
        ps::global_structural_observable_specs(maximumChange));

Stored-run display reads samples.fine.metrics for schema 3.0.0 or 4.0.0; v1/v2 retain root metrics. Do not add a QML regional editor.

- [ ] **Step 6: Run GREEN and commit**

    cmake --build --preset headless-debug       --target prometheus_run_store_transaction_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_run_store_transaction$' --output-on-failure
    cmake --preset desktop-no-occt-debug
    cmake --build --preset desktop-no-occt-debug       --target prometheus_structural_controller_tests
    ctest --test-dir out/build/desktop-no-occt-debug       -R '^prometheus_structural_controller$' --output-on-failure
    git add desktop/run_store/include/prometheus/run_store/project_v2.hpp       desktop/run_store/src/structural_archive_store.cpp       desktop/run_store/src/run_store.cpp       desktop/run_store/src/project_v2.cpp       desktop/run_store/tests/run_store_transaction_tests.cpp       desktop/app/structural_controller.cpp       desktop/app/tests/structural_controller_tests.cpp
    git commit -m "feat: publish structural archive v4"

## Task 7: Apply the cantilever profile and bound routine tools

**Files:**

- Modify: desktop/structural/include/prometheus/structural/structural_benchmarks.hpp
- Modify: desktop/structural/src/structural_benchmarks.cpp
- Modify: desktop/structural/tools/run_structural_benchmark.cpp
- Modify: desktop/structural/tools/run_structural_refinement.cpp
- Modify: desktop/structural/tests/structural_tool_fixture.cmake
- Modify: desktop/structural/tests/structural_observable_tests.cpp
- Modify: scripts/run-structural-validation.ps1
- Modify: scripts/run-structural-benchmarks.ps1

- [ ] **Step 1: Write failing profile tests**

Add:

    [[nodiscard]] std::vector<StructuralObservableSpec>
    cantilever_validation_observable_specs();

    [[nodiscard]] BenchmarkComparison compare_cantilever_validation(
        const VerifiedStructuralRefinement &refinement);

Require:

    cantilever.maximum_displacement:
      displacement magnitude, maximum, all_nodes, threshold 0.10

    cantilever.section_von_mises:
      von Mises, maximum, element centroid box
      minimum {0.100, 0.000, -0.050}
      maximum {0.125, 0.100,  0.050}
      threshold 0.10

Analytic comparison uses 0.0002 m tip displacement and 5.4 MPa stress at x=0.100 m, with 15% and 25% tolerances. It reads fine typed values by exact ID, not the global stress metric.

- [ ] **Step 2: Run RED, implement, and run GREEN**

    cmake --build --preset headless-debug       --target prometheus_structural_observable_tests
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_observable_tests$' --output-on-failure

Expected before implementation: FAIL. Implement the profile/comparison and rerun for PASS.

- [ ] **Step 3: Update tool profiles and output**

Axial uses global_structural_observable_specs(0.10). Cantilever/refinement uses cantilever_validation_observable_specs(). Print:

    observable.cantilever.maximum_displacement.change_fraction=
    observable.cantilever.section_von_mises.change_fraction=
    global.maximum_von_mises_stress.change_fraction=
    global.maximum_von_mises_stress.participated_in_acceptance=false
    global.maximum_von_mises_stress.status=not_converged_in_this_study
    archive_schema_version=4.0.0

Cantilever requires both typed observations and the regional analytic comparison. It requires one global displacement finding and one unmatched-global-stress unknown; it does not require a global stress finding.

- [ ] **Step 4: Add bounded smoke mode**

Usage becomes:

    prometheus_run_structural_refinement CCX OUTPUT_DIRECTORY [--smoke]

Default uses 80/12/12 and 120/18/18. Smoke uses 20/3/3 and 40/6/6, prints validation_profile=smoke_non_authoritative, and only prints refinement=smoke_passed. Update structural_tool_fixture.cmake to pass --smoke and require that marker. CTest never runs the expensive pair.

- [ ] **Step 5: Update Windows scripts**

Keep both PowerShell scripts on the default pair. Require archive_schema_version=4.0.0, both observable markers, and the global stress diagnostic before reporting the gate complete.

- [ ] **Step 6: Run tool tests and commit**

    cmake --build --preset headless-debug       --target prometheus_run_structural_benchmark                prometheus_run_structural_refinement                prometheus_replay_structural_run                prometheus_structural_solver_fixture
    ctest --test-dir out/build/headless-debug       -R '^prometheus_structural_(tool_fixture|observable_tests|tests)$'       --output-on-failure
    git add desktop/structural/include/prometheus/structural/structural_benchmarks.hpp       desktop/structural/src/structural_benchmarks.cpp       desktop/structural/tools/run_structural_benchmark.cpp       desktop/structural/tools/run_structural_refinement.cpp       desktop/structural/tests/structural_tool_fixture.cmake       desktop/structural/tests/structural_observable_tests.cpp       scripts/run-structural-validation.ps1       scripts/run-structural-benchmarks.ps1
    git commit -m "feat: validate cantilever by declared region"

Expected: tool/unit tests PASS; counting backend remains exactly two executions and one finalization.

## Task 8: Run bounded release verification and one real ARM study

**Files:**

- Modify after evidence passes: docs/superpowers/specs/2026-08-20-quantity-region-specific-structural-convergence-design.md
- Create under ignored output: out/validation/structural-linux-arm64/scoped-cantilever/

- [ ] **Step 1: Run hygiene and preset checks**

    git diff --check
    cmake --list-presets
    git status --short --branch

Expected: no diff errors and valid presets.

- [ ] **Step 2: Run the full headless suite once**

    cmake --preset headless-debug
    cmake --build --preset headless-debug
    ctest --test-dir out/build/headless-debug --output-on-failure

Expected: all headless tests PASS. Do not rerun passing suites unless a later edit touches them.

- [ ] **Step 3: Run desktop no-OCCT once**

    cmake --preset desktop-no-occt-debug
    cmake --build --preset desktop-no-occt-debug
    ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure

Expected: all desktop tests PASS. If only the known environment-blocked loopback test fails, rerun that exact test once outside the socket sandbox; it must pass there.

- [ ] **Step 4: Rebuild native Linux ARM structural targets**

    docker run --rm --platform linux/arm64       -v "$PWD:/workspace" -w /workspace       prometheus-structural-validation-arm64:ccx-2.23       /bin/sh -lc 'cmake -S . -B out/build/linux-arm64-structural -G Ninja         -DPROMETHEUS_BUILD_DESKTOP=OFF -DBUILD_TESTING=ON &&         cmake --build out/build/linux-arm64-structural --target           prometheus_run_structural_refinement           prometheus_replay_structural_run           prometheus_structural_observable_tests &&         ctest --test-dir out/build/linux-arm64-structural           -R "^prometheus_structural_observable_tests$" --output-on-failure'

Expected: build and focused test PASS natively on ARM64.

- [ ] **Step 5: Execute the approved pair exactly once**

    docker run --rm --platform linux/arm64       -v "$PWD:/workspace" -w /workspace       prometheus-structural-validation-arm64:ccx-2.23       /bin/sh -lc 'CCX_PATH=$(command -v ccx);         out/build/linux-arm64-structural/desktop/structural/prometheus_run_structural_refinement           "$CCX_PATH" out/validation/structural-linux-arm64/scoped-cantilever'

Expected approximately:

    observable.cantilever.maximum_displacement.change_fraction=0.02182
    observable.cantilever.section_von_mises.change_fraction=0.0235
    global.maximum_von_mises_stress.change_fraction=0.1034
    global.maximum_von_mises_stress.participated_in_acceptance=false
    global.maximum_von_mises_stress.status=not_converged_in_this_study
    refinement=passed
    archive_schema_version=4.0.0

Last printed digits may differ only if rebuilt binary/backend evidence differs. A material change triggers investigation, not wider tolerance.

- [ ] **Step 6: Replay v4 without another solve**

    docker run --rm --platform linux/arm64       -v "$PWD:/workspace" -w /workspace       prometheus-structural-validation-arm64:ccx-2.23       out/build/linux-arm64-structural/desktop/structural/prometheus_replay_structural_run       out/validation/structural-linux-arm64/scoped-cantilever/prometheus-structural-run.json

Expected: status=verified and schema_version=4.0.0. No new INP, DAT, FRD, or STA and no CalculiX process.

- [ ] **Step 7: Mark the design implemented and record evidence**

Change design status to Implemented and locally verified. Record exact test counts, solver/backend SHA-256, archive SHA-256, observable values, diagnostic values, and the limitation that the clamp-edge peak did not converge in this study.

- [ ] **Step 8: Commit evidence and verify clean state**

    git add docs/superpowers/specs/2026-08-20-quantity-region-specific-structural-convergence-design.md
    git commit -m "docs: record scoped convergence evidence"
    git diff --check HEAD~1 HEAD
    git status --short --branch

Expected: tracked worktree clean. Do not push until the user explicitly requests it.
