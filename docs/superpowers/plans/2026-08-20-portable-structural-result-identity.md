# Portable Structural Result Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make an unchanged structural evidence archive replay across supported platforms despite harmless derived binary64 rounding, while continuing to reject changed artifacts, engineering decisions, and material numerical differences.

**Architecture:** Change new compiled CalculiX results from a metric-bearing v2 identity to an evidence-root v3 identity. During archive replay, match each stored identity against the new evidence-root candidate or an exactly reconstructed legacy v2 candidate, then compare only an explicit allowlist of derived numbers with the approved 64-epsilon relative bound. Preserve every stored legacy identity as lineage before recompiling comparisons and findings, so downstream hashes are neither rewritten nor detached.

**Tech Stack:** C++20, CMake/CTest, nlohmann/json, the Prometheus RFC 8785 canonical JSON and SHA-256 integrity library, and the existing Qt-free structural replay CLI.

---

## File map

Production files:

- `desktop/structural/src/calculix_result.cpp`: compile new evidence-root v3 result identities after all solver, coverage, and artifact checks pass.
- `desktop/structural/src/structural_archive.cpp`: reconstruct legacy v2 identities, compare allowlisted derived numbers, retain exact legacy lineage, and replay archive schemas v1 through v4 without executing a solver.

Test and evidence files:

- `desktop/structural/tests/structural_tests.cpp`: add identity conformance, legacy-v2 construction, one-ULP compatibility, beyond-bound rejection, and exact-field tamper coverage.
- `docs/superpowers/specs/2026-08-20-portable-structural-result-identity-design.md`: record implementation and the final portable replay evidence only after all gates pass.

No public header, CMake target, archive schema, UI, mesh, material, load, threshold, or solver adapter changes are required.

## Task 1: Root new result identity in exact source evidence

**Files:**

- Modify: `desktop/structural/tests/structural_tests.cpp:600-630`
- Modify: `desktop/structural/src/calculix_result.cpp:328-365`

- [ ] **Step 1: Add an independent v3 identity oracle to the structural test**

Add this test-only helper near the existing JSON helpers. It deliberately duplicates the public identity contract rather than calling production code, so changing a schema, compiler version, or bound field breaks the conformance test:

```cpp
std::string evidenceRootV3Identity(
    const ps::CompiledCalculixResult &result,
    const std::string_view geometryIdentity) {
  const nlohmann::json document{
      {"$schema",
       "urn:prometheus:schema:compiled-calculix-result:3.0.0"},
      {"schema_version", "3.0.0"},
      {"compiler_version", "calculix-evidence-compiler-v3"},
      {"compiled_setup_identity", result.compiled_setup_identity},
      {"request_geometry_sha256", geometryIdentity},
      {"backend",
       {{"executable_sha256", result.backend.executable_sha256},
        {"version", result.backend.version}}},
      {"artifacts",
       {{"deck", result.artifacts.deck.sha256},
        {"sta", result.artifacts.sta.sha256},
        {"dat", result.artifacts.dat.sha256},
        {"frd", result.artifacts.frd.sha256},
        {"stdout", result.artifacts.standard_output.sha256},
        {"stderr", result.artifacts.standard_error.sha256}}}};
  return prometheus::integrity::sha256_bytes(
      prometheus::integrity::canonicalize_json_bytes(document.dump()));
}
```

- [ ] **Step 2: Add the failing identity assertions**

Immediately after `compiledResult` is shown complete, add:

```cpp
  const auto expectedEvidenceIdentity =
      evidenceRootV3Identity(compiledResult, request.geometry_sha256);
  require(compiledResult.identity == expectedEvidenceIdentity,
          "compiled result identity is rooted in exact solver evidence");

  auto changedDerivedMetrics = compiledResult;
  changedDerivedMetrics.metrics->maximum_displacement_m = std::nextafter(
      changedDerivedMetrics.metrics->maximum_displacement_m,
      std::numeric_limits<double>::infinity());
  require(evidenceRootV3Identity(changedDerivedMetrics,
                                 request.geometry_sha256) ==
              expectedEvidenceIdentity,
          "derived metric rounding does not change evidence identity");
```

- [ ] **Step 3: Run RED**

Run:

```sh
cmake --preset headless-debug
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_tests$' --output-on-failure
```

Expected: the structural test fails at `compiled result identity is rooted in exact solver evidence` because production still emits the v2 metric-bearing identity.

- [ ] **Step 4: Replace the production identity document**

In `compile_calculix_result_impl`, retain every existing validation and metric computation. Replace only the identity document with:

```cpp
  const auto identityDocument = integrity::canonicalize_json_bytes(
      Json{{"$schema",
            "urn:prometheus:schema:compiled-calculix-result:3.0.0"},
           {"schema_version", "3.0.0"},
           {"compiler_version", "calculix-evidence-compiler-v3"},
           {"compiled_setup_identity", result.compiled_setup_identity},
           {"request_geometry_sha256", request.geometry_sha256},
           {"backend",
            {{"executable_sha256", result.backend.executable_sha256},
             {"version", result.backend.version}}},
           {"artifacts",
            {{"deck", result.artifacts.deck.sha256},
             {"sta", result.artifacts.sta.sha256},
             {"dat", result.artifacts.dat.sha256},
             {"frd", result.artifacts.frd.sha256},
             {"stdout", result.artifacts.standard_output.sha256},
             {"stderr", result.artifacts.standard_error.sha256}}}}
          .dump());
  result.identity = integrity::sha256_bytes(identityDocument);
```

Do not include convergence, metrics, normalized rows, findings, or thresholds in this document. They remain replay checks in later tasks.

- [ ] **Step 5: Run GREEN and commit**

Run:

```sh
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_tests$' --output-on-failure
git diff --check
git add desktop/structural/src/calculix_result.cpp \
  desktop/structural/tests/structural_tests.cpp
git commit -m "feat: root structural results in exact evidence"
```

Expected: the complete focused test passes with new runs emitting evidence-root v3 identities.

## Task 2: Reconstruct legacy result identities and preserve lineage

**Files:**

- Modify: `desktop/structural/tests/structural_tests.cpp:110-350,1715-1765`
- Modify: `desktop/structural/src/structural_archive.cpp:130-210,1368-1495,1689-1790`

- [ ] **Step 1: Add the legacy-v2 identity oracle to tests**

Add a helper whose inputs match fields shared by archive-v2 roots and archive-v3/v4 samples:

```cpp
std::string legacyV2ResultIdentity(
    const std::string &setupIdentity,
    const std::string &geometryIdentity,
    const nlohmann::json &backend,
    const nlohmann::json &artifacts,
    const nlohmann::json &convergence,
    const nlohmann::json &metrics) {
  const nlohmann::json document{
      {"$schema",
       "urn:prometheus:schema:compiled-calculix-result:2.0.0"},
      {"schema_version", "2.0.0"},
      {"compiler_version", "calculix-evidence-compiler-v2"},
      {"compiled_setup_identity", setupIdentity},
      {"request_geometry_sha256", geometryIdentity},
      {"backend", backend},
      {"artifacts",
       {{"deck", artifacts.at("deck").at("sha256")},
        {"sta", artifacts.at("sta").at("sha256")},
        {"dat", artifacts.at("dat").at("sha256")},
        {"frd", artifacts.at("frd").at("sha256")},
        {"stdout", artifacts.at("stdout").at("sha256")},
        {"stderr", artifacts.at("stderr").at("sha256")}}},
      {"convergence", convergence},
      {"metrics", metrics}};
  return prometheus::integrity::sha256_bytes(
      prometheus::integrity::canonicalize_json_bytes(document.dump()));
}
```

Add a typed overload for the existing fixture builder:

```cpp
std::string legacyV2ResultIdentity(
    const std::string &setupIdentity,
    const std::string &geometryIdentity,
    const ps::CompiledCalculixResult &result) {
  require(result.metrics && result.convergence,
          "legacy identity requires a complete result");
  const nlohmann::json backend{
      {"executable_sha256", result.backend.executable_sha256},
      {"version", result.backend.version}};
  const nlohmann::json artifacts{
      {"deck", {{"sha256", result.artifacts.deck.sha256}}},
      {"sta", {{"sha256", result.artifacts.sta.sha256}}},
      {"dat", {{"sha256", result.artifacts.dat.sha256}}},
      {"frd", {{"sha256", result.artifacts.frd.sha256}}},
      {"stdout", {{"sha256", result.artifacts.standard_output.sha256}}},
      {"stderr", {{"sha256", result.artifacts.standard_error.sha256}}}};
  const auto &storedConvergence = *result.convergence;
  const nlohmann::json convergence{
      {"step", storedConvergence.step},
      {"increment", storedConvergence.increment},
      {"attempt", storedConvergence.attempt},
      {"iterations", storedConvergence.iterations},
      {"total_time", storedConvergence.total_time},
      {"step_time", storedConvergence.step_time},
      {"increment_time", storedConvergence.increment_time}};
  const auto &storedMetrics = *result.metrics;
  const nlohmann::json metrics{
      {"maximum_displacement_m", storedMetrics.maximum_displacement_m},
      {"maximum_von_mises_pa", storedMetrics.maximum_von_mises_pa},
      {"displacement_rows", storedMetrics.displacement_rows},
      {"stress_rows", storedMetrics.stress_rows}};
  return legacyV2ResultIdentity(setupIdentity, geometryIdentity, backend,
                                artifacts, convergence, metrics);
}
```

- [ ] **Step 2: Make `createLegacyV2Archive` produce a genuine old identity**

Compute the legacy identity directly from the completed typed result. Use that identity in all three places that formerly used `validated.identity`:

```cpp
  const auto legacyIdentity = legacyV2ResultIdentity(
      setup.identity, request.geometry_sha256, validated);
```

The fixture must write `legacyIdentity` into:

- `validated_result_identity`;
- the active entry in `refinement.result_sha256`; and
- each finding's sorted `evidence_sha256` list.

Parse the generated manifest in the test and assert its stored result identity differs from the new `completed.validated_result->identity`. Change the successful v2 replay assertion to compare the verifier's lineage identity to the stored legacy identity.

- [ ] **Step 3: Run RED**

Run:

```sh
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_tests$' --output-on-failure
```

Expected: archive-v2 verification fails with `replay_result_identity_mismatch` because the replay compiler now emits a v3 identity.

- [ ] **Step 4: Add the production legacy identity reconstruction**

Add `legacy_v2_result_identity` beside `metrics_json` and `convergence_json`. Build the exact v2 document from the stored setup identity, root geometry identity, backend, six artifact hashes, convergence, and metrics. Select the named members explicitly so an extra object member can never become part of a valid historical identity:

```cpp
std::string legacy_v2_result_identity(
    const std::string &setupIdentity,
    const std::string &geometryIdentity,
    const Json &backend,
    const Json &artifacts,
    const Json &convergence,
    const Json &metrics) {
  const Json document{
      {"$schema",
       "urn:prometheus:schema:compiled-calculix-result:2.0.0"},
      {"schema_version", "2.0.0"},
      {"compiler_version", "calculix-evidence-compiler-v2"},
      {"compiled_setup_identity", setupIdentity},
      {"request_geometry_sha256", geometryIdentity},
      {"backend",
       {{"executable_sha256",
         json_string(backend, "executable_sha256")},
        {"version", json_string(backend, "version")}}},
      {"artifacts",
       {{"deck", json_string(artifacts.at("deck"), "sha256")},
        {"sta", json_string(artifacts.at("sta"), "sha256")},
        {"dat", json_string(artifacts.at("dat"), "sha256")},
        {"frd", json_string(artifacts.at("frd"), "sha256")},
        {"stdout", json_string(artifacts.at("stdout"), "sha256")},
        {"stderr", json_string(artifacts.at("stderr"), "sha256")}}},
      {"convergence",
       {{"step", convergence.at("step")},
        {"increment", convergence.at("increment")},
        {"attempt", convergence.at("attempt")},
        {"iterations", convergence.at("iterations")},
        {"total_time", convergence.at("total_time")},
        {"step_time", convergence.at("step_time")},
        {"increment_time", convergence.at("increment_time")}}},
      {"metrics",
       {{"maximum_displacement_m",
         metrics.at("maximum_displacement_m")},
        {"maximum_von_mises_pa", metrics.at("maximum_von_mises_pa")},
        {"displacement_rows", metrics.at("displacement_rows")},
        {"stress_rows", metrics.at("stress_rows")}}}};
  return integrity::sha256_bytes(
      integrity::canonicalize_json_bytes(document.dump()));
}
```

Use this exact artifact mapping:

```cpp
Json{{"deck", json_string(artifacts.at("deck"), "sha256")},
     {"sta", json_string(artifacts.at("sta"), "sha256")},
     {"dat", json_string(artifacts.at("dat"), "sha256")},
     {"frd", json_string(artifacts.at("frd"), "sha256")},
     {"stdout", json_string(artifacts.at("stdout"), "sha256")},
     {"stderr", json_string(artifacts.at("stderr"), "sha256")}}
```

Before calling this helper, add or retain exact-key and type checks for backend, convergence, artifacts, and metrics. Convergence must contain exactly step, increment, attempt, iterations, total time, step time, and increment time; the four counters must be integers and the three times must be finite numbers. Metrics must contain exactly both maxima and both row counts; maxima must be finite numbers and row counts must be nonnegative integers. Canonicalize the identity document and hash it with `integrity::sha256_bytes`.

- [ ] **Step 5: Match candidates without trusting a version label**

In `replay_v3_sample`:

1. Compile raw evidence once to obtain the v3 evidence-root candidate.
2. Reconstruct the legacy v2 candidate from the stored sample.
3. Require `validated_result_identity` to equal either candidate.
4. Keep setup, backend, convergence, artifacts, row counts, and numeric checks after identity selection.
5. After all checks pass, assign the archived identity back to `replayedResult.identity` before calling `compile_completed_structural_sample`.

The identity condition must be:

```cpp
  const auto legacyIdentity = legacy_v2_result_identity(
      setupIdentity, geometryIdentity, backend, value.at("artifacts"),
      value.at("convergence"), value.at("metrics"));
  if (resultIdentity != replayedResult.identity &&
      resultIdentity != legacyIdentity)
    reject("replay_result_identity_mismatch",
           "v3 solver evidence matches neither supported result identity");
```

In `verify_v2_archive`, require the stored identity to equal the reconstructed legacy candidate. Do not reinterpret an archive-v2 root as a new identity format. After its replay checks pass, restore `replayedResult.identity = resultIdentity` before rebuilding its findings.

- [ ] **Step 6: Run GREEN and commit**

Run:

```sh
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_tests$' --output-on-failure
git diff --check
git add desktop/structural/src/structural_archive.cpp \
  desktop/structural/tests/structural_tests.cpp
git commit -m "feat: replay legacy structural result identities"
```

Expected: the genuine archive-v2 fixture verifies and reports its archived v2 identity, while new runs retain v3 identities.

## Task 3: Bound numeric replay for sample metrics and legacy findings

**Files:**

- Modify: `desktop/structural/tests/structural_tests.cpp:80-350,1715-1790`
- Modify: `desktop/structural/src/structural_archive.cpp:1-220,1368-1495,1689-1980`

- [ ] **Step 1: Add one-ULP and outside-bound archive-v1 tests**

Copy the valid archive-v1 directory twice without using the verified export path. In the first copy, parse the manifest, move `metrics.maximum_displacement_m` by one representable value, canonicalize, and rewrite only the copied manifest:

```cpp
  auto within = nlohmann::json::parse(fixtureBytes(withinManifest));
  const auto original =
      within["metrics"]["maximum_displacement_m"].get<double>();
  within["metrics"]["maximum_displacement_m"] = std::nextafter(
      original, std::numeric_limits<double>::infinity());
  writeFixtureBytes(
      withinManifest,
      prometheus::integrity::canonicalize_json_bytes(within.dump()));
  require(ps::verify_structural_archive(withinManifest).valid,
          "legacy v1 accepts one-ULP derived metric replay drift");
```

In the second copy set the same field to `original * (1.0 + 1.0e-10)`. Require an invalid result whose code is exactly `replay_numeric_mismatch`.

Create one more copied v1 manifest with a coherent displacement requirement and finding:

```cpp
  const double limit = 0.001;
  const double measured =
      findingDocument["metrics"]["maximum_displacement_m"].get<double>();
  findingDocument["requirements"]["displacement_limit_m"] = limit;
  findingDocument["coverage"] = {
      {"declared_obligations", 1}, {"evaluated_obligations", 1}};
  findingDocument["findings"] = nlohmann::json::array({
      {{"obligation", "maximum_displacement"},
       {"disposition", "no_violation_detected_within_scope"},
       {"measured", measured},
       {"limit", limit},
       {"margin", limit - measured},
       {"unit", "m"},
       {"scope",
        "isotropic linear-elastic C3D4 model under the confirmed scenario"}}});
```

First verify that coherent v1 finding. Then move its `measured` field by one ULP, recompute `margin`, and require verification to remain valid. A coherent `1.0e-10` relative change must fail with `replay_numeric_mismatch`.

- [ ] **Step 2: Add coherent one-ULP and outside-bound archive-v2 tests**

For each copied v2 manifest:

1. save the old `validated_result_identity`;
2. change `metrics.maximum_displacement_m`;
3. recompute the legacy v2 identity with the test oracle;
4. replace the old identity in `validated_result_identity`, `refinement.result_sha256`, and every finding `evidence_sha256` array;
5. update the `maximum_displacement` finding's `measured` value and calculate `margin = limit - measured`;
6. sort each finding evidence array; and
7. canonicalize and rewrite the copied manifest.

Use `std::nextafter` for the passing copy and `original * (1.0 + 1.0e-10)` for the rejected copy. Require the latter to fail with `replay_numeric_mismatch`. This proves a caller cannot evade replay merely by recomputing the old metric-bearing hash.

- [ ] **Step 3: Run RED**

Run:

```sh
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_tests$' --output-on-failure
```

Expected: the one-ULP copies fail under the current exact comparisons.

- [ ] **Step 4: Implement the approved finite-number equivalence primitive**

Add `<limits>` to `structural_archive.cpp`, then add:

```cpp
constexpr double derivedReplayMultiplier = 64.0;

bool derived_number_equivalent(const double stored,
                               const double replayed) {
  if (!std::isfinite(stored) || !std::isfinite(replayed)) return false;
  if (stored == replayed) return true;
  const double scale = std::max(std::abs(stored), std::abs(replayed));
  return std::abs(stored - replayed) <=
         derivedReplayMultiplier * std::numeric_limits<double>::epsilon() *
             scale;
}
```

Add a diagnostic formatter using `std::numeric_limits<double>::max_digits10`, the classic locale, and `std::setprecision`. Add `reconcile_derived_number(storedDocument, replayedDocument, pointer, fieldPath)` that:

- rejects a missing or nonnumeric field under the existing contract/finding mismatch path;
- rejects NaN and infinity;
- rejects an outside-bound value with code `replay_numeric_mismatch` and detail containing the field path plus stored and replayed round-trip values; and
- copies the accepted stored value into the replayed JSON document so a final whole-document equality test still enforces every non-allowlisted field exactly.

- [ ] **Step 5: Add explicit document comparators**

Implement these internal helpers; do not introduce a generic recursive tolerant JSON comparison:

```cpp
void require_metrics_replay(const Json &stored, Json replayed,
                            std::string_view fieldPrefix);
void require_findings_replay(const Json &stored, Json replayed,
                             std::string_view fieldPrefix);
```

`require_metrics_replay` reconciles only:

- `/maximum_displacement_m`; and
- `/maximum_von_mises_pa`.

Its final exact equality retains `displacement_rows` and `stress_rows` as exact integers. `require_findings_replay` first requires equal array lengths, then reconciles only `/<index>/measured` and `/<index>/margin`; its final exact equality retains obligations, dispositions, limits, units, scopes, evidence identities, assumptions, and array ordering.

- [ ] **Step 6: Use the metric comparator in every schema path**

Replace metric equality in:

- `replay_v3_sample`, using a prefix such as `samples.coarse.metrics` or `samples.fine.metrics`;
- `verify_v2_archive`, using `metrics`; and
- the archive-v1 replay path, using `metrics`.

Keep backend, convergence, setup, artifacts, and row counts exact. For archive v1, build an expected metrics JSON from `parsedMetrics` and call the same helper rather than comparing doubles inline.

- [ ] **Step 7: Use the finding comparator in v1 and v2**

Add a small `legacy_v1_findings_json` serializer with exactly the historical seven members: obligation, disposition, measured, limit, margin, unit, and scope. Compare it through `require_findings_replay`.

For v2, replace `root.at("findings") != findings_json(*evaluation)` with `require_findings_replay`. Keep coverage, refinement, limitation, disposition, requirements, and evidence identities exact.

- [ ] **Step 8: Run GREEN and commit**

Run:

```sh
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_tests$' --output-on-failure
git diff --check
git add desktop/structural/src/structural_archive.cpp \
  desktop/structural/tests/structural_tests.cpp
git commit -m "fix: bound portable structural metric replay"
```

Expected: one-ULP v1/v2 fixtures pass, outside-bound fixtures report `replay_numeric_mismatch`, and existing artifact/identity tamper tests remain rejected.

## Task 4: Reconcile v3/v4 comparisons and findings without weakening decisions

**Files:**

- Modify: `desktop/structural/tests/structural_tests.cpp:1320-1715`
- Modify: `desktop/structural/src/structural_archive.cpp:250-450,1510-1685`

- [ ] **Step 1: Add a coherent alternate-platform v4 fixture helper**

Start from the small generated `v4Archive`, export it to a copy, and parse its manifest. Convert both samples to legacy identities with the test oracle. For each converted sample:

- replace `validated_result_identity`;
- replace the matching entry in `comparison.result_sha256`; and
- replace the old identity in every finding `evidence_sha256` array, then sort the array.

For the coarse displacement sample, set the stored maximum displacement to `std::nextafter(value, infinity())`, recompute that sample's legacy identity again, and update the corresponding identity references again. Update the displacement observable and displacement global-extremum `coarse_value`. Recompute their changes with the production formula:

```cpp
double testRelativeChange(const double coarse, const double fine) {
  const double scale = std::max(std::abs(coarse), std::abs(fine));
  return scale == 0.0 ? 0.0 : std::abs(fine - coarse) / scale;
}
```

The status, entity IDs, row counts, thresholds, participation flag, positions, coverage, unknowns, and finding disposition stay unchanged. Canonicalize the copied manifest.

- [ ] **Step 2: Add RED assertions for coherent v4 replay**

Require the one-ULP coherent archive to verify and retain both archived legacy result identities in its reconstructed refinement. Create another coherent copy with coarse displacement set to `original * (1.0 + 1.0e-10)`, recompute the same dependent fields and identity references, and require code `replay_numeric_mismatch`.

Also create a finding-only copy of the unmodified v4 archive. Move the first finding's `measured` value with `std::nextafter`, recompute `margin = limit - measured`, and require it to verify. A `1.0e-10` relative measured-value change with a coherently recomputed margin must fail with `replay_numeric_mismatch`.

Create two copies of the generated `acceptedArchive` v3 fixture. In the first, move `comparison.displacement_change_fraction` by one ULP and set `comparison.maximum_change_fraction` to the maximum of the stored displacement and stress changes. Require it to verify. In the second, change the displacement fraction by `1.0e-10` relative, recompute the maximum, and require `replay_numeric_mismatch`. These copies isolate the archive-v3 comparison serializer while the v4 fixture exercises the shared legacy sample path.

- [ ] **Step 3: Run RED**

Run:

```sh
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_tests$' --output-on-failure
```

Expected: the sample checks from Task 3 succeed, then exact v4 comparison or finding replay rejects the one-ULP archive.

- [ ] **Step 4: Add explicit v3 and v4 comparison comparators**

Implement:

```cpp
void require_v3_comparison_replay(const Json &stored, Json replayed);
void require_v4_comparison_replay(const Json &stored, Json replayed);
```

The v3 helper reconciles only:

- `/displacement_change_fraction`;
- `/stress_change_fraction`; and
- `/maximum_change_fraction`.

The v4 helper requires matching observable and global-extremum array sizes. For every observable index it reconciles only:

- `/observables/<index>/coarse_value`;
- `/observables/<index>/fine_value`; and
- `/observables/<index>/change_fraction`.

For every global-extremum index it reconciles only:

- `/global_extrema/<index>/coarse_value`;
- `/global_extrema/<index>/fine_value`; and
- `/global_extrema/<index>/change_fraction`.

After reconciling those paths, each helper requires the full JSON documents to be equal. Therefore setup/result/definition identities, status, entity IDs, locations, selected-row counts, thresholds, participation, and within-threshold decisions remain exact.

- [ ] **Step 5: Wire comparison and finding replay**

In `verify_v3_archive`, call `require_v3_comparison_replay` and `require_findings_replay`, then retain the separate exact coverage and limitation checks.

In `verify_v4_archive`, call `require_v4_comparison_replay` and `require_findings_replay`, then retain exact coverage, unknowns, and limitation checks. Do not reconcile requirement limits or observable thresholds.

If the reconstructed platform value changes an extremum entity, accepted/indeterminate status, finding disposition, coverage count, or unknown reason, the final exact comparison must still return the existing `replay_finding_mismatch` or `replay_refinement_mismatch` path.

- [ ] **Step 6: Strengthen representative fail-closed assertions**

Retain every existing v4 tamper test and assert representative failure codes:

- changed criterion threshold without a matching identity: `archive_contract_invalid`;
- changed selected-row count: `replay_finding_mismatch`;
- changed global entity or participation: `replay_finding_mismatch`;
- changed refinement status: `replay_finding_mismatch`; and
- changed unknown code: `replay_finding_mismatch`.

Do not add tolerance to positions, limits, thresholds, convergence, or setup evidence.

- [ ] **Step 7: Run GREEN and commit**

Run:

```sh
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug \
  -R '^prometheus_structural_tests$' --output-on-failure
git diff --check
git add desktop/structural/src/structural_archive.cpp \
  desktop/structural/tests/structural_tests.cpp
git commit -m "fix: replay derived structural values portably"
```

Expected: coherent one-ULP v4 sample/comparison/finding drift verifies, materially changed numbers fail with `replay_numeric_mismatch`, and all decision-level tampering remains rejected.

## Task 5: Prove the existing ARM64 archive is portable without rerunning CalculiX

**Files:**

- Modify: `docs/superpowers/specs/2026-08-20-portable-structural-result-identity-design.md`

- [ ] **Step 1: Record immutable solver-artifact hashes before replay**

Run this before the final build and retain the output in the session log:

```sh
shasum -a 256 \
  out/validation/structural-linux-arm64/scoped-cantilever/cantilever_coarse.inp \
  out/validation/structural-linux-arm64/scoped-cantilever/cantilever_coarse.dat \
  out/validation/structural-linux-arm64/scoped-cantilever/cantilever_coarse.frd \
  out/validation/structural-linux-arm64/scoped-cantilever/cantilever_coarse.sta \
  out/validation/structural-linux-arm64/scoped-cantilever/cantilever_fine.inp \
  out/validation/structural-linux-arm64/scoped-cantilever/cantilever_fine.dat \
  out/validation/structural-linux-arm64/scoped-cantilever/cantilever_fine.frd \
  out/validation/structural-linux-arm64/scoped-cantilever/cantilever_fine.sta
shasum -a 256 \
  out/validation/structural-linux-arm64/scoped-cantilever/prometheus-structural-run.json
```

Do not invoke `ccx`, `prometheus_run_structural_refinement`, or either structural benchmark runner during this task.

- [ ] **Step 2: Run each complete local suite once**

Run:

```sh
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure

cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --preset desktop-no-occt-debug --output-on-failure
```

Expected: every headless and desktop-no-OCCT test passes. Do not repeat the full suites unless a concrete failure requires a targeted fix.

- [ ] **Step 3: Replay the unchanged Linux ARM64 archive with the macOS binary**

Run:

```sh
out/build/desktop-no-occt-debug/desktop/structural/prometheus_replay_structural_run \
  out/validation/structural-linux-arm64/scoped-cantilever/prometheus-structural-run.json
```

Expected: exit code 0 and one line beginning with:

```text
status=verified max_displacement_m=
```

The coherent legacy-v4 unit test already proves that reconstructed refinement retains archived v2 sample identities. This command proves the real archive reaches the same verified outcome on macOS. It only reads and compiles evidence; it has no solver executable argument and must not launch CalculiX.

- [ ] **Step 4: Prove the archive and solver artifacts did not change**

Run the same two `shasum -a 256` commands from Step 1. Compare all nine hashes with the recorded pre-replay output.

Expected: every INP, DAT, FRD, STA, and manifest hash is byte-for-byte unchanged.

- [ ] **Step 5: Perform the bounded implementation audit**

Run:

```sh
rg -n 'compiled-calculix-result:3\.0\.0|calculix-evidence-compiler-v3' \
  desktop/structural/src/calculix_result.cpp \
  desktop/structural/tests/structural_tests.cpp
rg -n 'derivedReplayMultiplier|replay_numeric_mismatch|legacy_v2_result_identity' \
  desktop/structural/src/structural_archive.cpp
rg -n 'compile_calculix_result' desktop/structural/src/structural_archive.cpp
git diff --check
git status --short
```

Confirm from the final diff that:

- `replay_v3_sample` has one `compile_calculix_result` call per sample;
- archive-v2 has one `compile_calculix_result` call;
- archive-v1 parses its DAT once;
- no solver runner call was added to verification;
- no threshold, requirement, setup, or artifact field entered the numeric allowlist; and
- no archive-v5, UI, mesh, or backend change entered the diff.

- [ ] **Step 6: Record the implementation evidence and commit**

Change the design status to `Implemented and locally verified`. Add a short validation record containing:

- the focused structural test result;
- full headless and desktop-no-OCCT totals;
- the successful macOS replay of the unchanged ARM64 archive;
- the one-ULP pass and `1.0e-10` rejection coverage;
- the unchanged nine hashes; and
- the explicit statement that CalculiX was not executed.

Then run:

```sh
git diff --check
git add docs/superpowers/specs/2026-08-20-portable-structural-result-identity-design.md
git commit -m "docs: record portable structural replay evidence"
git status --short --branch
```

Expected: the worktree is clean, the branch remains local and ahead of its remote, and no push occurs without a separate user request.
