# Manual Structural Refinement Reconciliation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the production desktop a trustworthy manual coarse/fine structural workflow that runs each mesh once, derives findings from two identity-bound results, and publishes a replayable two-sample archive.

**Architecture:** Add a Qt-free refinement-pair compiler between validated CalculiX results and structural findings. The desktop retains one immutable coarse baseline, reviews and executes one finer mesh, then passes the two completed samples to that compiler; archive v3 stores both raw evidence packages and recomputes the comparison after a trust-boundary transition. Active viewing, archive creation, and project publication consume retained typed objects and never repeat solver execution or result interpretation.

**Tech Stack:** C++20, CMake/CTest, Qt 6/QML/QtConcurrent, nlohmann JSON with RFC 8785 canonicalization, SQLite-style content-addressed project storage, CalculiX process fixtures, and pytest for the unchanged Python service boundary.

---

## File and responsibility map

| File | Responsibility in this increment |
| --- | --- |
| `desktop/structural/include/prometheus/structural/structural_refinement.hpp` | Immutable criterion, completed sample, reviewed boundary correspondence, verified pair, and typed diagnostics. |
| `desktop/structural/src/structural_refinement.cpp` | Canonical shared-lineage identity, sample validation, mesh ordering, backend binding, and derived change calculation. |
| `desktop/structural/include/prometheus/structural/structural_setup.hpp` | Retain the reviewed typed setup inside `CompiledStructuralSetup`; downstream code must not reparse canonical setup JSON during an active session. |
| `desktop/structural/src/structural_setup.cpp` | Populate the retained reviewed setup exactly once during setup compilation. |
| `desktop/structural/include/prometheus/structural/structural_findings.hpp` | Public comparison summary and the finding compiler that accepts only a verified pair. |
| `desktop/structural/src/structural_findings.cpp` | Accepted versus indeterminate coverage and fine-result finding calculation. |
| `desktop/structural/include/prometheus/structural/structural_archive.hpp` | V3 archive writer and typed two-sample restore result. |
| `desktop/structural/src/structural_archive.cpp` | V3 serialization/replay plus isolated legacy v1/v2 readers. |
| `desktop/run_store/include/prometheus/run_store/project_v2.hpp` | Registered archive-v3 and embedded-project-run-v2 schema identities. |
| `desktop/run_store/src/run_store.cpp` | Closed manifest validation and project reference allowlists for v3. |
| `desktop/run_store/src/structural_archive_store.cpp` | Stream, embed, reconstruct, and geometry-bind fourteen v3 artifacts without assuming the legacy seven-file layout. |
| `desktop/app/structural_backend.hpp` | Separate one-sample execution from one-time pair finalization. |
| `desktop/app/structural_backend.cpp` | Invoke CalculiX once per sample, compile a pair once, compile findings once, and write one archive. |
| `desktop/app/structural_controller.hpp` | Sequential coarse/fine state and presentation-only Qt properties. |
| `desktop/app/structural_controller.cpp` | Baseline retention, fine-only invalidation, shared-input locking, asynchronous execution, restore, and publication. |
| `desktop/ui/StructuralSetupPanel.qml` | Manual coarse/fine controls, boundary-correspondence confirmation, honest status colors, and read-only comparison display. |
| `desktop/structural/tools/run_structural_benchmark.cpp` | Exercise the production pair and v3 archive boundary from the analytic benchmark gate. |
| `desktop/structural/tools/run_structural_refinement.cpp` | Exercise the same verified pair without an alternate evidence model. |
| `desktop/structural/tests/structural_tests.cpp` | Pair, findings, v3 replay, tamper, indeterminate, and legacy compatibility tests. |
| `desktop/run_store/tests/run_store_transaction_tests.cpp` | V3 packaging, publication, idempotence, reconstruction, and geometry-binding tests. |
| `desktop/app/tests/structural_controller_tests.cpp` | Production controller coarse/fine/publish flow and exact stage-call counts. |
| `desktop/app/tests/qml_authority_tests.cpp` | Ensure QML can declare a pre-run criterion and human review, but cannot author results, deltas, identities, or verdicts. |
| `desktop/integrity/src/canonical_json.cpp` | Reject chunk sizes that cannot be represented by `std::streamsize`. |
| `desktop/integrity/tests/canonical_json_tests.cpp` | Oversized chunk regression test. |
| `docs/phase-03-structural-workflow.md` | State the implemented manual pair, v3 claim, honest limitations, and automatic-mesher extension point. |
| `docs/phase-04-persistence-and-portability.md` | Distinguish legacy seven-artifact archives from the v3 fourteen-artifact relocation and embedded graph. |

## Task 1: Add immutable refinement-pair contracts and shared lineage

**Files:**
- Create: `desktop/structural/include/prometheus/structural/structural_refinement.hpp`
- Create: `desktop/structural/src/structural_refinement.cpp`
- Modify: `desktop/structural/include/prometheus/structural/structural_setup.hpp:73-78`
- Modify: `desktop/structural/src/structural_setup.cpp:381-398`
- Modify: `desktop/structural/CMakeLists.txt:1-15`
- Test: `desktop/structural/tests/structural_tests.cpp:1-380`

- [ ] **Step 1: Write failing typed-pair tests**

Add the new header include and a helper that detects a diagnostic code:

```cpp
#include "prometheus/structural/structural_refinement.hpp"

bool hasRefinementIssue(
    const ps::StructuralRefinementCompilation &compiled,
    const std::string_view code) {
  return std::ranges::any_of(compiled.issues(), [&](const auto &issue) {
    return issue.code == code;
  });
}
```

Create coarse and fine cantilever setups with one common reviewed analysis identity, run the existing solver fixture once for each, and compile the pair:

```cpp
auto coarseReviewed =
    ps::cantilever_benchmark(8, 2, 2).setup.reviewed_setup;
auto fineReviewed =
    ps::cantilever_benchmark(12, 3, 3).setup.reviewed_setup;
fineReviewed.analysis_id = coarseReviewed.analysis_id;
fineReviewed.component_name = coarseReviewed.component_name;
fineReviewed.geometry_sha256 = coarseReviewed.geometry_sha256;
const auto coarseSetup = ps::compile_structural_setup(coarseReviewed);
const auto fineSetup = ps::compile_structural_setup(fineReviewed);
const auto criterion =
    ps::compile_structural_refinement_criterion(0.10);
const ps::SolverRunOptions coarseOptions{
    fixture, processRoot, "typed_cantilever_coarse",
    std::chrono::seconds(5)};
const ps::SolverRunOptions fineOptions{
    fixture, processRoot, "typed_cantilever_fine",
    std::chrono::seconds(5)};
const auto coarseRun = ps::run_calculix(coarseOptions, coarseSetup);
const auto fineRun = ps::run_calculix(fineOptions, fineSetup);
const auto coarseSample = ps::compile_completed_structural_sample(
    ps::StructuralSampleRole::coarse, criterion, coarseOptions,
    coarseSetup, coarseRun);
const auto fineSample = ps::compile_completed_structural_sample(
    ps::StructuralSampleRole::fine, criterion, fineOptions,
    fineSetup, fineRun);
const auto correspondence =
    ps::review_structural_boundary_correspondence(
        coarseSetup, fineSetup, true, true);
const auto verified = ps::compile_structural_refinement(
    coarseSample, fineSample, correspondence);
require(verified.complete() &&
            verified.value()->status() ==
                ps::StructuralRefinementStatus::accepted &&
            verified.value()->coarse().run().validated_result->identity !=
                verified.value()->fine().run().validated_result->identity,
        "two completed ordered samples produce one accepted typed comparison");
```

Add negative assertions for invalid criterion values, reused mesh identity,
reversed coarse/fine order, changed material, changed force, changed
requirements, changed scenario, changed backend version, unconfirmed boundary
correspondence, and an incomplete result. Each case must assert one of these
stable codes:

```cpp
require(hasRefinementIssue(reusedMesh,
                           "refinement_mesh_identity_reused"),
        "one mesh cannot occupy both refinement roles");
require(hasRefinementIssue(reversed,
                           "refinement_mesh_not_finer"),
        "the fine role requires more elements and a smaller target size");
require(hasRefinementIssue(changedScenario,
                           "refinement_lineage_mismatch"),
        "shared reviewed physics must match exactly");
require(hasRefinementIssue(changedBackend,
                           "refinement_backend_mismatch"),
        "both samples must use one authoritative backend identity");
require(hasRefinementIssue(unreviewedBoundary,
                           "refinement_boundary_review_required"),
        "arbitrary mesh boundaries require explicit correspondence review");
```

- [ ] **Step 2: Build to verify the new contract is absent**

Run:

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
```

Expected: compilation fails because `structural_refinement.hpp`,
`CompiledStructuralSetup::reviewed_setup`, and the pair compiler APIs do not
exist.

- [ ] **Step 3: Define the complete public refinement interface**

Create `structural_refinement.hpp` with these final public names. Keep all
derived comparison constructors private:

```cpp
#pragma once

#include "prometheus/structural/calculix_runner.hpp"
#include "prometheus/structural/structural_setup.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace prometheus::structural {

enum class StructuralSampleRole { coarse, fine };
enum class StructuralRefinementStatus { accepted, indeterminate };

struct StructuralRefinementIssue final {
  std::string code;
  std::string message;
};

class StructuralRefinementCriterion final {
public:
  StructuralRefinementCriterion(const StructuralRefinementCriterion &) = default;
  StructuralRefinementCriterion &
  operator=(const StructuralRefinementCriterion &) = default;
  [[nodiscard]] double maximum_change_fraction() const noexcept;
  [[nodiscard]] const std::string &identity() const noexcept;

private:
  double maximum_change_fraction_{};
  std::string identity_;

  StructuralRefinementCriterion(double maximum_change_fraction,
                                std::string identity);
  friend StructuralRefinementCriterion
  compile_structural_refinement_criterion(double);
};

class CompletedStructuralSample final {
public:
  [[nodiscard]] StructuralSampleRole role() const noexcept;
  [[nodiscard]] const StructuralRefinementCriterion &criterion() const noexcept;
  [[nodiscard]] const SolverRunOptions &options() const noexcept;
  [[nodiscard]] const CompiledStructuralSetup &setup() const noexcept;
  [[nodiscard]] const SolverRunResult &run() const noexcept;
  [[nodiscard]] const std::string &lineage_identity() const noexcept;

private:
  StructuralSampleRole role_;
  StructuralRefinementCriterion criterion_;
  SolverRunOptions options_;
  CompiledStructuralSetup setup_;
  SolverRunResult run_;
  std::string lineage_identity_;

  CompletedStructuralSample(StructuralSampleRole role,
                            StructuralRefinementCriterion criterion,
                            SolverRunOptions options,
                            CompiledStructuralSetup setup,
                            SolverRunResult run,
                            std::string lineage_identity);
  friend std::shared_ptr<const CompletedStructuralSample>
  compile_completed_structural_sample(
      StructuralSampleRole, StructuralRefinementCriterion,
      SolverRunOptions, CompiledStructuralSetup, SolverRunResult);
};

using CompletedStructuralSamplePtr =
    std::shared_ptr<const CompletedStructuralSample>;

class ReviewedBoundaryCorrespondence final {
public:
  ReviewedBoundaryCorrespondence(
      const ReviewedBoundaryCorrespondence &) = default;
  ReviewedBoundaryCorrespondence &
  operator=(const ReviewedBoundaryCorrespondence &) = default;
  [[nodiscard]] const std::string &coarse_setup_identity() const noexcept;
  [[nodiscard]] const std::string &fine_setup_identity() const noexcept;
  [[nodiscard]] bool load_region_confirmed() const noexcept;
  [[nodiscard]] bool restraint_region_confirmed() const noexcept;
  [[nodiscard]] double coarse_load_area_m2() const noexcept;
  [[nodiscard]] double fine_load_area_m2() const noexcept;
  [[nodiscard]] double coarse_restraint_area_m2() const noexcept;
  [[nodiscard]] double fine_restraint_area_m2() const noexcept;

private:
  std::string coarse_setup_identity_;
  std::string fine_setup_identity_;
  bool load_region_confirmed_{};
  bool restraint_region_confirmed_{};
  double coarse_load_area_m2_{};
  double fine_load_area_m2_{};
  double coarse_restraint_area_m2_{};
  double fine_restraint_area_m2_{};

  ReviewedBoundaryCorrespondence(
      std::string coarse_setup_identity,
      std::string fine_setup_identity,
      bool load_region_confirmed,
      bool restraint_region_confirmed,
      double coarse_load_area_m2,
      double fine_load_area_m2,
      double coarse_restraint_area_m2,
      double fine_restraint_area_m2);
  friend ReviewedBoundaryCorrespondence
  review_structural_boundary_correspondence(
      const CompiledStructuralSetup &, const CompiledStructuralSetup &,
      bool, bool);
};

class StructuralRefinementCompilation;
class VerifiedStructuralRefinement;
[[nodiscard]] StructuralRefinementCompilation compile_structural_refinement(
    CompletedStructuralSamplePtr coarse,
    CompletedStructuralSamplePtr fine,
    const ReviewedBoundaryCorrespondence &boundary_correspondence);

class VerifiedStructuralRefinement final {
public:
  [[nodiscard]] const CompletedStructuralSample &coarse() const noexcept;
  [[nodiscard]] const CompletedStructuralSample &fine() const noexcept;
  [[nodiscard]] const ReviewedBoundaryCorrespondence &
  boundary_correspondence() const noexcept;
  [[nodiscard]] double displacement_change_fraction() const noexcept;
  [[nodiscard]] double stress_change_fraction() const noexcept;
  [[nodiscard]] double maximum_change_fraction() const noexcept;
  [[nodiscard]] StructuralRefinementStatus status() const noexcept;

private:
  CompletedStructuralSamplePtr coarse_;
  CompletedStructuralSamplePtr fine_;
  ReviewedBoundaryCorrespondence boundary_correspondence_;
  double displacement_change_fraction_{};
  double stress_change_fraction_{};
  double maximum_change_fraction_{};
  StructuralRefinementStatus status_{StructuralRefinementStatus::indeterminate};

  VerifiedStructuralRefinement(
      CompletedStructuralSamplePtr coarse,
      CompletedStructuralSamplePtr fine,
      ReviewedBoundaryCorrespondence boundary_correspondence,
      double displacement_change_fraction,
      double stress_change_fraction,
      double maximum_change_fraction,
      StructuralRefinementStatus status);
  friend class StructuralRefinementCompilation;
  friend StructuralRefinementCompilation compile_structural_refinement(
      CompletedStructuralSamplePtr, CompletedStructuralSamplePtr,
      const ReviewedBoundaryCorrespondence &);
};

using VerifiedStructuralRefinementPtr =
    std::shared_ptr<const VerifiedStructuralRefinement>;

class StructuralRefinementCompilation final {
public:
  [[nodiscard]] bool complete() const noexcept;
  [[nodiscard]] const VerifiedStructuralRefinementPtr &value() const noexcept;
  [[nodiscard]] const std::vector<StructuralRefinementIssue> &issues() const
      noexcept;

private:
  VerifiedStructuralRefinementPtr value_;
  std::vector<StructuralRefinementIssue> issues_;

  StructuralRefinementCompilation(
      VerifiedStructuralRefinementPtr value,
      std::vector<StructuralRefinementIssue> issues);
  friend StructuralRefinementCompilation compile_structural_refinement(
      CompletedStructuralSamplePtr, CompletedStructuralSamplePtr,
      const ReviewedBoundaryCorrespondence &);
};

[[nodiscard]] StructuralRefinementCriterion
compile_structural_refinement_criterion(double maximum_change_fraction);

[[nodiscard]] CompletedStructuralSamplePtr
compile_completed_structural_sample(
    StructuralSampleRole role,
    StructuralRefinementCriterion criterion,
    SolverRunOptions options,
    CompiledStructuralSetup setup,
    SolverRunResult run);

[[nodiscard]] ReviewedBoundaryCorrespondence
review_structural_boundary_correspondence(
    const CompiledStructuralSetup &coarse,
    const CompiledStructuralSetup &fine,
    bool load_region_confirmed,
    bool restraint_region_confirmed);

[[nodiscard]] StructuralRefinementCompilation compile_structural_refinement(
    CompletedStructuralSamplePtr coarse,
    CompletedStructuralSamplePtr fine,
    const ReviewedBoundaryCorrespondence &boundary_correspondence);

} // namespace prometheus::structural
```

- [ ] **Step 4: Retain the typed reviewed setup and implement deterministic lineage**

Append this field to `CompiledStructuralSetup` so existing designated
initializers retain their order:

```cpp
StructuralSetup reviewed_setup;
```

Populate it at the end of `compile_structural_setup()`:

```cpp
return {.request = std::move(request),
        .canonical_setup_evidence = std::move(evidence),
        .calculix_deck = std::move(deck),
        .identity = integrity::sha256_bytes(identityDocument),
        .reviewed_setup = setup};
```

In `structural_refinement.cpp`, hash canonical JSON containing exactly the
shared reviewed physics: analysis/component/geometry; all material fields;
load label/vector/review; restraint label/review; all requirement fields;
scenario text/confirmation; and coordinate scale. Exclude mesh SHA-256,
topology, element/node counts, mesh sizes, quality, mesher identity, patch
angle, exact selected faces, and selected areas. Those excluded values either
must differ between discretizations or are bound by the separate human review.

Use this exact relative-change rule:

```cpp
double relative_change(const double coarse, const double fine) {
  const double scale = std::max(std::abs(coarse), std::abs(fine));
  return scale == 0.0 ? 0.0 : std::abs(fine - coarse) / scale;
}
```

`compile_structural_refinement()` must accumulate deterministic diagnostics and
return no value if any check fails. It must require coarse/fine roles, equal
criterion and lineage identities, different mesh and result identities, more
fine elements, a strictly smaller fine target size, exact backend executable
hash/version equality, true boundary confirmations bound to both setup
identities, and complete setup-bound results. Only then may it calculate the
two metric changes and privately construct the verified value.

Because the constructors are private, create the shared immutable objects from
their friend factories with an explicit `new CompletedStructuralSample(...)`
or `new VerifiedStructuralRefinement(...)` wrapped immediately in the declared
`std::shared_ptr<const ...>` alias. `std::make_shared` cannot call a private
constructor through the standard-library implementation.

- [ ] **Step 5: Register the source and run the focused test**

Add `src/structural_refinement.cpp` to `prometheus_structural`, then run:

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug --output-on-failure \
  -R '^prometheus_structural_tests$'
```

Expected: the structural test passes, including one accepted pair and every
stable rejection code. The solver fixture executes exactly twice for the valid
pair.

- [ ] **Step 6: Commit the typed domain boundary**

```bash
git add desktop/structural/CMakeLists.txt \
  desktop/structural/include/prometheus/structural/structural_refinement.hpp \
  desktop/structural/src/structural_refinement.cpp \
  desktop/structural/include/prometheus/structural/structural_setup.hpp \
  desktop/structural/src/structural_setup.cpp \
  desktop/structural/tests/structural_tests.cpp
git diff --cached --check
git commit -m "Add typed structural refinement pairs"
```

## Task 2: Compile findings only from a verified pair

**Files:**
- Modify: `desktop/structural/include/prometheus/structural/structural_findings.hpp`
- Modify: `desktop/structural/src/structural_findings.cpp`
- Test: `desktop/structural/tests/structural_tests.cpp:895-969`

- [ ] **Step 1: Write failing accepted, indeterminate, and equality tests**

Replace the caller-authored finding tests with assertions against the verified
pair produced in Task 1:

```cpp
const auto acceptedEvaluation =
    ps::compile_structural_findings(*verified.value());
require(acceptedEvaluation.declared_obligations == 2 &&
            acceptedEvaluation.evaluated_obligations == 2 &&
            acceptedEvaluation.comparison.has_value() &&
            acceptedEvaluation.comparison->status ==
                ps::StructuralRefinementStatus::accepted,
        "an accepted verified pair evaluates the fine obligations");
require(std::ranges::all_of(
            acceptedEvaluation.findings, [](const auto &finding) {
              return finding.evidence_sha256.size() == 4U;
            }),
        "each finding binds both setup and both result identities");

const auto strictCriterion =
    ps::compile_structural_refinement_criterion(0.01);
const auto strictCoarse = ps::compile_completed_structural_sample(
    ps::StructuralSampleRole::coarse, strictCriterion, coarseOptions,
    coarseSetup, coarseRun);
const auto strictFine = ps::compile_completed_structural_sample(
    ps::StructuralSampleRole::fine, strictCriterion, fineOptions,
    fineSetup, fineRun);
const auto unresolvedPair = ps::compile_structural_refinement(
    strictCoarse, strictFine, correspondence);
const auto unresolvedEvaluation =
    ps::compile_structural_findings(*unresolvedPair.value());
require(unresolvedPair.value()->status() ==
            ps::StructuralRefinementStatus::indeterminate &&
            unresolvedEvaluation.declared_obligations == 2 &&
            unresolvedEvaluation.evaluated_obligations == 0 &&
            unresolvedEvaluation.findings.empty(),
        "a valid pair above its criterion remains honestly indeterminate");
```

Retain the existing equality rule by building a verified test pair whose fine
metric equals its reviewed limit; assert `violated` and zero margin. Do not
mutate a comparison summary or pass flag to create this case.

- [ ] **Step 2: Run the focused test and observe the missing overload**

Run:

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
```

Expected: compilation fails because `StructuralEvaluation::comparison` and
`compile_structural_findings(const VerifiedStructuralRefinement&)` do not
exist.

- [ ] **Step 3: Add the derived public summary and verified-only overload**

Add this output-only summary. No production compiler accepts it as input:

```cpp
struct StructuralRefinementSummary final {
  StructuralRefinementStatus status{StructuralRefinementStatus::indeterminate};
  double displacement_change_fraction{};
  double stress_change_fraction{};
  double maximum_change_fraction{};
  double maximum_allowed_change_fraction{};
  std::vector<std::string> setup_sha256;
  std::vector<std::string> result_sha256;
};
```

Add `std::optional<StructuralRefinementSummary> comparison;` to
`StructuralEvaluation` and declare:

```cpp
[[nodiscard]] StructuralEvaluation compile_structural_findings(
    const VerifiedStructuralRefinement &refinement);
```

Keep the old aggregate and overload temporarily so v2 replay and the current
desktop continue to compile until Tasks 3-8 migrate them. Mark that declaration
with a comment stating that it is legacy-read/migration-only and remove it in
Task 8.

- [ ] **Step 4: Implement findings from the fine sample only**

The new overload must read the request and result through
`refinement.fine()`. It always fills declared coverage and the derived
comparison summary. If status is indeterminate, it returns with zero findings.
If accepted, it validates the fine result and reviewed limits, then emits the
existing displacement/stress findings. Construct evidence exactly as follows:

```cpp
std::vector<std::string> evidence{
    refinement.coarse().setup().identity,
    refinement.fine().setup().identity,
    refinement.coarse().run().validated_result->identity,
    refinement.fine().run().validated_result->identity};
std::ranges::sort(evidence);
evidence.erase(std::unique(evidence.begin(), evidence.end()), evidence.end());
```

The scope and limitations remain bounded linear-static C3D4 claims. Equality
to a limit remains a violation because only a strictly positive margin is a
non-violation.

- [ ] **Step 5: Run and commit the finding boundary**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug --output-on-failure \
  -R '^prometheus_structural_tests$'
git add desktop/structural/include/prometheus/structural/structural_findings.hpp \
  desktop/structural/src/structural_findings.cpp \
  desktop/structural/tests/structural_tests.cpp
git diff --cached --check
git commit -m "Compile findings from verified refinement"
```

Expected: accepted, indeterminate, known-pass, known-fail, and equality cases
pass without constructing a comparison summary from presentation data.

## Task 3: Add structural archive v3 with two-result replay

**Files:**
- Modify: `desktop/structural/include/prometheus/structural/structural_archive.hpp`
- Modify: `desktop/structural/src/structural_archive.cpp`
- Test: `desktop/structural/tests/structural_tests.cpp:62-135,970-1020`

- [ ] **Step 1: Add failing v3 write, replay, indeterminate, and tamper tests**

Write both jobs into the same empty study directory with distinct safe job
names. Add these assertions:

```cpp
const auto acceptedArchive = ps::write_structural_refinement_archive(
    *verified.value(), acceptedEvaluation);
require(acceptedArchive.schema_version == "3.0.0" &&
            acceptedArchive.coarse_result_identity ==
                coarseRun.validated_result->identity &&
            acceptedArchive.validated_result_identity ==
                fineRun.validated_result->identity,
        "new writes bind both active validated results under v3");
const auto replay =
    ps::verify_structural_archive(acceptedArchive.manifest_path);
require(replay.valid && replay.schema_version == "3.0.0" &&
            replay.refinement &&
            replay.refinement->status() ==
                ps::StructuralRefinementStatus::accepted &&
            replay.evaluation &&
            replay.evaluation->evaluated_obligations == 2,
        "v3 replay reconstructs both results and findings");

const auto relocated = ps::export_structural_archive(
    acceptedArchive.manifest_path, processRoot / "relocated-v3");
require(ps::verify_structural_archive(relocated.manifest_path).valid &&
            relocated.manifest_sha256 == acceptedArchive.manifest_sha256,
        "v3 export copies all fourteen declared artifacts and replays exactly");

const auto unresolvedArchive = ps::write_structural_refinement_archive(
    *unresolvedPair.value(), unresolvedEvaluation);
const auto unresolvedReplay =
    ps::verify_structural_archive(unresolvedArchive.manifest_path);
require(unresolvedReplay.valid && unresolvedReplay.evaluation &&
            unresolvedReplay.evaluation->evaluated_obligations == 0 &&
            unresolvedReplay.evaluation->findings.empty(),
        "an above-threshold study remains replayable and indeterminate");
```

For each tamper case, copy the archive, canonicalize the modified manifest,
and assert verification failure after changing one of:

```text
comparison.maximum_change_fraction
comparison.status
samples.coarse.validated_result_identity
samples.fine.compiled_setup_identity
samples.coarse.artifacts.dat bytes
samples.fine.artifacts.sta bytes
```

Keep explicit v1 and v2 fixtures and assert they remain readable only as
schema versions `1.0.0` and `2.0.0`.

- [ ] **Step 2: Run the test and verify v3 is absent**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
```

Expected: compilation fails because the v3 writer and two-sample verification
field do not exist.

- [ ] **Step 3: Extend the archive handle and verification result**

Use this public shape:

```cpp
struct StructuralArchive final {
  std::filesystem::path manifest_path;
  std::string manifest_sha256;
  std::string schema_version;
  std::string validated_result_identity;
  std::string coarse_result_identity;
};

struct StructuralArchiveVerification final {
  bool valid{};
  std::string code;
  std::string detail;
  std::optional<CalculixMetrics> metrics;
  int declared_obligations{};
  int evaluated_obligations{};
  std::string schema_version;
  std::string validated_result_identity;
  std::optional<CalculixDat> normalized;
  std::optional<StructuralSetup> reviewed_setup;
  std::optional<CompiledStructuralSetup> compiled_setup;
  std::optional<StructuralEvaluation> evaluation;
  VerifiedStructuralRefinementPtr refinement;
};

[[nodiscard]] StructuralArchive write_structural_refinement_archive(
    const VerifiedStructuralRefinement &refinement,
    const StructuralEvaluation &evaluation);
```

The existing single-result fields continue to represent the fine sample so the
result viewport and older restore code retain one active display result.

- [ ] **Step 4: Write the closed v3 manifest from retained objects**

Use schema
`urn:prometheus:schema:structural-run-archive:3.0.0` and archive kind
`linear_static_refinement_study`. The exact root members are:

```text
$schema, schema_version, archive_kind, analysis_id, component_name,
geometry_sha256, criterion, boundary_correspondence, samples, comparison,
coverage, findings, limitation
```

`samples` contains exactly `coarse` and `fine`. Each sample contains exactly:

```text
role, compiled_setup_identity, validated_result_identity, mesh,
execution, backend, convergence, artifacts, metrics
```

Each nested `artifacts` object contains exactly `setup`, `deck`, `dat`, `frd`,
`sta`, `stdout`, and `stderr`. Use the sample's safe job name to create unique
flat filenames in one study directory:

```text
<job>.reviewed-structural-setup.json
<job>.inp
<job>.dat
<job>.frd
<job>.sta
<job>.stdout.txt
<job>.stderr.txt
```

Require both samples to use the same canonical working directory and require
the manifest not to exist before writing. Rehash every mutable solver file and
compare it with the retained artifact identity. Writing setup/stdout/stderr
bytes is packaging only; do not call `compile_calculix_result()`,
`compile_structural_refinement()`, or `compile_structural_findings()`.

Serialize `criterion`, `boundary_correspondence`, `comparison`, coverage, and
findings from the verified objects. Accept an indeterminate evaluation when it
has zero evaluated obligations and no findings; reject any mismatch between the
comparison status and evaluation coverage.

- [ ] **Step 5: Replay both samples and regenerate the comparison**

Add `verify_v3_archive()` before the existing v2/v1 dispatch. It must:

1. enforce every closed key set and bounded artifact reference;
2. load and identity-check fourteen artifacts;
3. deserialize and compile both setup files;
4. rebuild two `CalculixRunEvidence` values and call
   `compile_calculix_result()` once per sample;
5. rebuild the locked criterion and boundary review from persisted reviewed
   inputs;
6. call `compile_completed_structural_sample()` for both roles;
7. call `compile_structural_refinement()` once;
8. call `compile_structural_findings()` once; and
9. require all regenerated derived JSON to equal the manifest.

Return the fine normalized fields/setup/metrics in the compatibility display
fields and return the verified pair in `refinement`. V1 and v2 dispatch must
not populate that pair or claim two-result replay.

Update `export_structural_archive()` to use the same schema-specific declared
artifact iterator as verification: seven flat references for v1/v2 and
fourteen nested sample references for v3. Copy only declared safe filenames,
write the unchanged canonical manifest, replay the destination, and publish by
atomic rename only after successful verification.

- [ ] **Step 6: Run structural replay tests**

```bash
cmake --build --preset headless-debug --target \
  prometheus_structural_tests prometheus_replay_structural_run \
  prometheus_export_structural_archive
ctest --test-dir out/build/headless-debug --output-on-failure \
  -R '^prometheus_structural_tests$'
```

Expected: accepted and indeterminate v3 archives replay; every tamper fails;
legacy v1/v2 fixtures retain their original version labels.

- [ ] **Step 7: Commit v3 replay**

```bash
git add desktop/structural/include/prometheus/structural/structural_archive.hpp \
  desktop/structural/src/structural_archive.cpp \
  desktop/structural/tests/structural_tests.cpp
git diff --cached --check
git commit -m "Add replayable structural archive v3"
```

## Task 4: Store and reconstruct the fourteen-artifact v3 graph

**Files:**
- Modify: `desktop/run_store/include/prometheus/run_store/project_v2.hpp:20-40`
- Modify: `desktop/run_store/src/project_v2.cpp`
- Modify: `desktop/run_store/src/run_store.cpp:445-560`
- Modify: `desktop/run_store/src/structural_archive_store.cpp:18-25,140-335`
- Test: `desktop/run_store/tests/run_store_transaction_tests.cpp:138-208,238-294,572-760`

- [ ] **Step 1: Add failing v3 project-store tests**

Add a synthetic v3 fixture with two seven-artifact sample objects and one
shared geometry identity. Pack, publish, repeat-publish, reconstruct, and
byte-compare all fourteen files. Assert these registered identities:

```cpp
require(packed.archive_manifest.reference.schema_id ==
            run_store::structural_manifest_schema_id_v3 &&
            packed.archive_manifest.reference.schema_version == "3.0.0" &&
            packed.project_manifest.reference.schema_id ==
            run_store::structural_project_run_schema_id_v2,
        "v3 archives use the two-sample embedded graph contract");
require(packed.chunks.size() >= 14U,
        "every v3 artifact is represented in the object graph");
```

Also assert rejection of an unknown sample role, duplicate filename across
roles, missing fine artifact, wrong v3 geometry identity, a project-manifest
artifact count other than fourteen, and a forged reconstructed archive whose
geometry differs from the project manifest.

- [ ] **Step 2: Run the transaction target and observe v3 rejection**

```bash
cmake --build --preset headless-debug --target \
  prometheus_run_store_transaction_tests
ctest --test-dir out/build/headless-debug --output-on-failure \
  -R '^prometheus_run_store_transaction$'
```

Expected: the new fixture fails because v3 and the embedded project-run-v2
contract are not registered.

- [ ] **Step 3: Register versioned archive and project-run identities**

Add:

```cpp
inline constexpr std::string_view structural_manifest_schema_id_v3 =
    "urn:prometheus:schema:structural-run-archive:3.0.0";
inline constexpr std::string_view structural_project_run_schema_id_v1 =
    "urn:prometheus:schema:structural-project-run:1.0.0";
inline constexpr std::string_view structural_project_run_schema_id_v2 =
    "urn:prometheus:schema:structural-project-run:2.0.0";
inline constexpr std::string_view structural_project_run_schema_id =
    structural_project_run_schema_id_v1;
```

Update supported object-reference validation to accept exactly archive v1/v2/v3
and embedded project-run v1/v2 with matching schema versions and media types.

- [ ] **Step 4: Make artifact discovery schema-specific and closed**

Replace the global seven-key assumption with a helper that returns flattened
references:

```cpp
struct DeclaredStructuralArtifact final {
  std::string role;
  std::string file;
  std::uint64_t byte_length{};
  std::string sha256;
};

std::vector<DeclaredStructuralArtifact> declaredArtifacts(
    const Json &archive, const bool version3);
```

For v1/v2, roles remain `setup`, `deck`, `dat`, `frd`, `sta`, `stdout`, and
`stderr`. For v3, roles are `coarse/setup` through `coarse/stderr` followed by
`fine/setup` through `fine/stderr`. Reject missing/extra keys, unsafe or
duplicate filenames, invalid hashes, and non-unsigned byte lengths before any
file read.

Use that same ordered list for streaming package construction. Keep the current
one-pass `sha256_file_chunks()` call: each source artifact must be read once to
both verify its identity and construct chunk objects.

Write project-run schema v1 for archive v1/v2 and schema v2 for archive v3.
The v2 project manifest retains the same root members as v1 but requires
exactly fourteen ordered artifact records. This versioning prevents an older
seven-artifact consumer from misreading the graph.

- [ ] **Step 5: Dispatch graph validation and reconstruction by project-run version**

`validate_embedded_structural_graph()` must accept project-run v1 only with
archive v1/v2 and seven records, and project-run v2 only with archive v3 and
fourteen records. Compare the archive's top-level geometry identity with both
the embedded assembly identity and, when requested, the current project
assembly.

`reconstruct_structural_archive()` already consumes an artifact array; add
closed project-run version checks before reconstruction and require the decoded
record count for that version. Preserve atomic sibling-temporary publication
and remove the temporary directory after any failure.

- [ ] **Step 6: Run and commit the run-store graph**

```bash
cmake --build --preset headless-debug --target \
  prometheus_run_store_transaction_tests prometheus_run_store_project_tests
ctest --test-dir out/build/headless-debug --output-on-failure \
  -R '^(prometheus_run_store_transaction|prometheus_run_store_project)$'
git add desktop/run_store/include/prometheus/run_store/project_v2.hpp \
  desktop/run_store/src/project_v2.cpp \
  desktop/run_store/src/run_store.cpp \
  desktop/run_store/src/structural_archive_store.cpp \
  desktop/run_store/tests/run_store_transaction_tests.cpp
git diff --cached --check
git commit -m "Store two-sample structural archives"
```

Expected: v1/v2 seven-file graphs and v3 fourteen-file graphs publish,
reconstruct, and reject cross-version or geometry-detached combinations.

## Task 5: Separate one-sample backend execution from pair finalization

**Files:**
- Modify: `desktop/app/structural_backend.hpp`
- Modify: `desktop/app/structural_backend.cpp`
- Test: `desktop/app/tests/structural_controller_tests.cpp:20-75`

- [ ] **Step 1: Add failing backend call-count and indeterminate tests**

Extend `StageCounts` with `execute_sample` and `finalize_refinement`. Add
counting overrides for the new APIs. In a focused backend fixture, call sample
execution twice and finalization once, then assert:

```cpp
require(counts.execute_sample == 2 &&
            counts.finalize_refinement == 1,
        "one refinement study executes exactly two samples and finalizes once");
require(completed.comparison && completed.archive &&
            completed.evaluation.comparison.has_value(),
        "backend finalization returns the typed pair, evaluation, and v3 archive");
```

In a separate counting-backend fixture, declare a strict criterion before its
coarse execution, execute its coarse and fine samples once each, and finalize
once. Assert a present v3 archive, `comparison_indeterminate`, zero evaluated
obligations, and zero findings. Do not rewrap results under a criterion chosen
after either execution.

- [ ] **Step 2: Run the desktop target and observe missing backend methods**

```bash
cmake --build --preset desktop-no-occt-debug --target \
  prometheus_structural_controller_tests
```

Expected: compilation fails because one-sample execution and pair finalization
are not exposed.

- [ ] **Step 3: Add the backend result contracts**

Add:

```cpp
struct DesktopStructuralSampleResult final {
  prometheus::structural::CompletedStructuralSamplePtr sample;
  std::optional<prometheus::structural::SolverRunResult> failed_run;
  std::string error;

  [[nodiscard]] const prometheus::structural::SolverRunResult &run() const {
    return sample ? sample->run() : failed_run.value();
  }
};

struct DesktopStructuralRefinementResult final {
  prometheus::structural::VerifiedStructuralRefinementPtr comparison;
  prometheus::structural::StructuralEvaluation evaluation;
  std::optional<prometheus::structural::StructuralArchive> archive;
  std::string archive_error;
  std::vector<prometheus::structural::StructuralRefinementIssue> issues;
};
```

Add these virtual methods alongside the legacy `execute()` method until the
controller migrates in Task 6:

```cpp
[[nodiscard]] virtual DesktopStructuralSampleResult executeSample(
    prometheus::structural::SolverRunOptions options,
    prometheus::structural::CompiledStructuralSetup setup,
    prometheus::structural::StructuralSampleRole role,
    prometheus::structural::StructuralRefinementCriterion criterion)
    const = 0;

[[nodiscard]] virtual DesktopStructuralRefinementResult finalizeRefinement(
    prometheus::structural::CompletedStructuralSamplePtr coarse,
    prometheus::structural::CompletedStructuralSamplePtr fine,
    const prometheus::structural::ReviewedBoundaryCorrespondence &
        boundary_correspondence) const = 0;
```

- [ ] **Step 4: Implement the one-authority backend path**

`executeSample()` calls `run_calculix()` exactly once. If the run is not
completed, move it into `failed_run` and return no sample. If completed, call
`compile_completed_structural_sample()` exactly once and move the options,
setup, criterion, and run into the immutable sample. The successful result is
then read through `DesktopStructuralSampleResult::run()`; it is not copied into
a second desktop result object.

`finalizeRefinement()` calls `compile_structural_refinement()` once. On typed
issues, return no comparison/evaluation/archive. On success, call
`compile_structural_findings()` once and
`write_structural_refinement_archive()` once. Archive both accepted and valid
indeterminate comparisons. Do not call the solver or result compiler in this
method.

- [ ] **Step 5: Run and commit the backend split**

```bash
cmake --build --preset desktop-no-occt-debug --target \
  prometheus_structural_controller_tests
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure \
  -R '^prometheus_structural_controller$'
git add desktop/app/structural_backend.hpp \
  desktop/app/structural_backend.cpp \
  desktop/app/tests/structural_controller_tests.cpp
git diff --cached --check
git commit -m "Add two-stage structural backend"
```

## Task 6: Wire the sequential controller and prove valid publication

**Files:**
- Modify: `desktop/app/structural_controller.hpp`
- Modify: `desktop/app/structural_controller.cpp`
- Modify: `desktop/app/tests/structural_controller_tests.cpp`

- [ ] **Step 1: Replace the bypass test with a failing production-controller flow**

Create coarse and fine `.inp` fixtures representing the same tetrahedral
volume at two resolutions. The fine fixture must have a distinct byte hash,
more C3D4 elements, and reviewed target size below the coarse value. Drive the
actual controller in this order:

```cpp
constexpr std::string_view coarseMesh = R"(*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
*ELEMENT, TYPE=C3D4, ELSET=Volume
1, 1, 2, 3, 4
)";
constexpr std::string_view fineMesh = R"(*NODE
1, 0, 0, 0
2, 10, 0, 0
3, 0, 10, 0
4, 0, 0, 10
5, 2.5, 2.5, 2.5
*ELEMENT, TYPE=C3D4, ELSET=Volume
1, 1, 2, 3, 5
2, 1, 4, 2, 5
3, 1, 3, 4, 5
4, 2, 4, 3, 5
)";
writeQtFixture(coarsePath, coarseMesh);
writeQtFixture(finePath, fineMesh);
```

Use the same four exterior physical faces in both reviews; node 5 is interior.
Then drive the actual controller:

```cpp
controller.loadMesh(coarseUrl, 0.001, 1.0);
controller.setPatchSelected(coarseLoadPatch, "load", true);
controller.setPatchSelected(coarseFixedPatch, "restraint", true);
auto coarseDraft = projectBoundDraft();
coarseDraft["refinement_maximum_change_fraction"] = 0.10;
controller.reviewSetup(coarseDraft);
controller.runAnalysis(solverUrl, outputRootUrl);
waitForRun(controller);
require(controller.hasRefinementBaseline() &&
            controller.status() ==
                "execution_completed_evaluation_pending",
        "one completed coarse solve becomes a retained baseline, not a pass");

controller.loadMesh(fineUrl, 0.001, 1.0);
controller.setPatchSelected(fineLoadPatch, "load", true);
controller.setPatchSelected(fineFixedPatch, "restraint", true);
auto fineDraft = projectBoundDraft();
fineDraft["mesh_target_size_m"] = 0.001;
fineDraft["boundary_load_correspondence_reviewed"] = true;
fineDraft["boundary_restraint_correspondence_reviewed"] = true;
controller.reviewSetup(fineDraft);
controller.runAnalysis(solverUrl, outputRootUrl);
waitForRun(controller);
require(controller.status() == "comparison_accepted" &&
            controller.lastRun().value("archived").toBool() &&
            controller.findings().size() == 2,
        "the second controller run creates accepted findings and a v3 archive");

controller.commitLastRun();
waitForPublication(controller);
require(project.committedRunCount() == 1 &&
            controller.lastRun().value("project_anchored").toBool(),
        "the valid production controller path publishes its retained archive");
```

Delete the existing test block that directly calls `run_calculix()`,
`compile_structural_refinement_evidence()`, `write_structural_archive()`, and
run-store publication outside the controller.

Assert final counts: two mesh preparations, two groupings, two setup
compilations, two sample executions, one refinement finalization, and no count
increase after property reads, `commitLastRun()`, export, or publication.

- [ ] **Step 2: Add failing invalidation and retry tests**

Test these transitions without another coarse execution:

```cpp
const auto coarseExecutionCount = countingBackend->counts().execute_sample;
controller.setPatchSelected(fineExtraPatch, "load", true);
require(controller.hasRefinementBaseline() &&
            !controller.lastRun().value("archived").toBool() &&
            countingBackend->counts().execute_sample == coarseExecutionCount,
        "fine-only edits retain the baseline and invalidate only fine state");
```

Also assert: a failed fine run retains the baseline; a non-finer fine setup is
blocked before execution; a direct fine draft cannot change locked material,
force, requirements, geometry, or scenario; and
`discardRefinementBaseline()` clears both stages.

- [ ] **Step 3: Run the test and observe the missing controller state**

```bash
cmake --build --preset desktop-no-occt-debug --target \
  prometheus_structural_controller_tests
```

Expected: compilation fails because baseline properties and discard behavior
do not exist, and the current controller still executes a single unrefined run.

- [ ] **Step 4: Add the controller's public presentation contract**

Add these properties and invokable:

```cpp
Q_PROPERTY(QString refinementStage READ refinementStage NOTIFY changed)
Q_PROPERTY(bool hasRefinementBaseline READ hasRefinementBaseline NOTIFY changed)
Q_PROPERTY(bool sharedInputsLocked READ sharedInputsLocked NOTIFY changed)
Q_PROPERTY(QVariantMap baselineRun READ baselineRun NOTIFY changed)
Q_PROPERTY(QVariantMap refinementComparison READ refinementComparison NOTIFY changed)

QString refinementStage() const { return refinement_stage_; }
bool hasRefinementBaseline() const { return baseline_sample_ != nullptr; }
bool sharedInputsLocked() const { return baseline_sample_ != nullptr; }
QVariantMap baselineRun() const { return baseline_run_; }
QVariantMap refinementComparison() const { return refinement_comparison_; }
Q_INVOKABLE void discardRefinementBaseline();
```

Add retained state:

```cpp
QString refinement_stage_{"coarse"};
QVariantMap baseline_run_;
QVariantMap refinement_comparison_;
std::optional<prometheus::structural::StructuralRefinementCriterion>
    refinement_criterion_;
prometheus::structural::CompletedStructuralSamplePtr baseline_sample_;
std::optional<prometheus::structural::ReviewedBoundaryCorrespondence>
    boundary_correspondence_;
std::optional<DesktopStructuralRefinementResult> completed_refinement_;
```

Retain `completed_run_` only until all old restore code has migrated in this
task; remove it before committing.

- [ ] **Step 5: Implement baseline-aware setup and invalidation**

Before a baseline exists, `reviewSetup()` compiles the current coarse setup and
locks `refinement_maximum_change_fraction`. Reject a missing/invalid criterion
with `refinement_criterion_invalid`.

After a baseline exists, construct the fine setup from current fine mesh,
boundary selections, and mesh controls, but copy these fields from
`baseline_sample_->setup().reviewed_setup` regardless of QVariant input:

```cpp
fine.analysis_id = coarse.analysis_id;
fine.component_name = coarse.component_name;
fine.geometry_sha256 = coarse.geometry_sha256;
fine.material = coarse.material;
fine.load.total_force_n = coarse.load.total_force_n;
fine.requirement = coarse.requirement;
fine.scenario_description = coarse.scenario_description;
fine.scenario_confirmed = coarse.scenario_confirmed;
```

The fine load/restraint selections and their review flags remain current-mesh
inputs. After fine setup compilation, build
`ReviewedBoundaryCorrespondence` from the two compiled setups and the two
explicit draft confirmation fields. A mesh/patch/edit clears only fine
comparison/result/correspondence state when a baseline exists. Material,
requirement, force, geometry, or scenario changes are unavailable while locked;
`discardRefinementBaseline()` is the only route back to a changed coarse setup.

- [ ] **Step 6: Execute coarse and fine into one immutable study directory**

The coarse run creates one unique study directory under the chosen output root
and uses a safe `prometheus_structural_coarse` job name. On completion, retain
the returned sample, show raw extrema, set stage `fine`, and set status
`execution_completed_evaluation_pending`.

Fine runs reuse the retained study directory and use a unique safe name such
as `prometheus_structural_fine_<8 hex chars>` so a failed attempt never becomes
input to a retry. After `executeSample()`, call `finalizeRefinement()` in the
same worker. Accepted status produces findings and green success;
indeterminate status produces zero findings, an archive, and amber status.
Invalid/failing fine results retain the baseline.

Update `commitLastRun()` to consume only
`completed_refinement_->archive`. Keep the manifest-handle equality check and
geometry binding. Publication still runs asynchronously and must not call any
backend engineering method.

On v3 restore, use verification's fine setup/normalized fields for the result
viewport, populate the baseline/comparison presentation maps from the verified
pair, and retain stale-source behavior. V1/v2 restore remains displayable under
its existing narrower fields. Update structural history enumeration to accept
both `structural_project_run_schema_id_v1` and
`structural_project_run_schema_id_v2`; do not silently skip newly published v3
studies.

- [ ] **Step 7: Run and commit the production path**

```bash
cmake --build --preset desktop-no-occt-debug --target \
  prometheus_structural_controller_tests
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure \
  -R '^prometheus_structural_controller$'
git add desktop/app/structural_controller.hpp \
  desktop/app/structural_controller.cpp \
  desktop/app/tests/structural_controller_tests.cpp
git diff --cached --check
git commit -m "Wire sequential structural refinement controller"
```

Expected: `controller.commitLastRun()` succeeds for the two-run path, fine
retry/invalidation retains the baseline, and publication does not change stage
counts.

## Task 7: Expose the manual coarse/fine workflow without QML authority

**Files:**
- Modify: `desktop/ui/StructuralSetupPanel.qml`
- Modify: `desktop/app/tests/qml_authority_tests.cpp`
- Test: `desktop/app/tests/structural_controller_tests.cpp`

- [ ] **Step 1: Add failing QML contract and forbidden-field tests**

Extend `StructuralControllerProbe` with the five properties and discard
invokable from Task 6. Assert the panel loads and contains the pre-run criterion
and human boundary review controls. Retain a source-text assertion that none of
these computed evidence keys appear:

```cpp
for (const auto *forbidden : {
         "refinement_complete",
         "refinement_criteria_satisfied",
         "refinement_change_fraction",
         "refinement_result_sha256",
         "validated_result_identity"}) {
  require(!qmlBytes.contains(forbidden),
          "QML cannot author structural result or refinement evidence");
}
require(qmlBytes.contains("refinement_maximum_change_fraction") &&
            qmlBytes.contains("boundary_load_correspondence_reviewed") &&
            qmlBytes.contains("boundary_restraint_correspondence_reviewed"),
        "QML may declare a pre-run criterion and explicit human review");
```

- [ ] **Step 2: Run the QML test and observe missing controls**

```bash
cmake --build --preset desktop-no-occt-debug --target \
  prometheus_qml_authority_tests
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure \
  -R '^prometheus_qml_authority$'
```

Expected: the new required properties/controls are absent.

- [ ] **Step 3: Add stage-aware controls and locked inputs**

Add a coarse-stage numeric field with default `0.10` and range `(0, 1]`:

```qml
Label { text: "Maximum coarse-to-fine change"; color: mutedColor }
TextField {
    id: refinementMaximumChange
    objectName: "refinementMaximumChange"
    text: "0.10"
    enabled: !structuralController.sharedInputsLocked
    validator: DoubleValidator { bottom: 0; top: 1 }
}
```

Submit that field only as
`refinement_maximum_change_fraction`. Add two fine-stage checkboxes that state
the exact human claim:

```qml
CheckBox {
    id: loadCorrespondenceReviewed
    visible: structuralController.hasRefinementBaseline
    text: "Fine load faces represent the same physical region as baseline"
}
CheckBox {
    id: restraintCorrespondenceReviewed
    visible: structuralController.hasRefinementBaseline
    text: "Fine restraint faces represent the same physical region as baseline"
}
```

Submit their two review fields. Disable shared material, force, requirement,
geometry, and scenario editors while `sharedInputsLocked`; keep fine surface
selection and mesh controls editable. Add a “Discard baseline and start over”
button that calls the controller invokable.

- [ ] **Step 4: Present honest state and derived comparison values**

Change the mesh button and run button labels by stage: “Load coarse mesh,”
“Run coarse baseline,” “Load fine mesh,” and “Run fine comparison.” Show the
baseline element count and selected load/restraint areas from `baselineRun`.
Show fine equivalents, displacement change, stress change, maximum change,
criterion, and accepted/indeterminate state from `refinementComparison`.
Result identities remain controller/archive evidence rather than QML-submitted
or QML-interpreted fields.

Use green only when `status === "comparison_accepted"`. Use amber for
`execution_completed_evaluation_pending` and `comparison_indeterminate`; use
red only for execution/contract failures. Remove the sentence claiming the
panel cannot supply a typed refinement workflow.

- [ ] **Step 5: Run desktop presentation tests**

```bash
cmake --build --preset desktop-no-occt-debug --target \
  prometheus_qml_authority_tests prometheus_structural_controller_tests \
  prometheus_desktop
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure \
  -R '^(prometheus_qml_authority|prometheus_structural_controller)$'
```

Expected: both tests pass; QML carries only the criterion and human-review
inputs, while all deltas, identities, statuses, and findings come from the
controller.

- [ ] **Step 6: Commit the manual UI**

```bash
git add desktop/ui/StructuralSetupPanel.qml \
  desktop/app/tests/qml_authority_tests.cpp \
  desktop/app/tests/structural_controller_tests.cpp
git diff --cached --check
git commit -m "Expose manual coarse-fine structural flow"
```

## Task 8: Migrate tools and remove caller-authored refinement authority

**Files:**
- Modify: `desktop/structural/include/prometheus/structural/structural_findings.hpp`
- Modify: `desktop/structural/src/structural_findings.cpp`
- Modify: `desktop/structural/include/prometheus/structural/structural_benchmarks.hpp`
- Modify: `desktop/structural/src/structural_benchmarks.cpp`
- Modify: `desktop/structural/include/prometheus/structural/structural_archive.hpp`
- Modify: `desktop/structural/src/structural_archive.cpp`
- Modify: `desktop/structural/tools/run_structural_benchmark.cpp`
- Modify: `desktop/structural/tools/run_structural_refinement.cpp`
- Modify: `desktop/structural/tests/structural_tool_fixture.cmake`
- Modify: `desktop/structural/tests/structural_tests.cpp`
- Modify: `desktop/app/structural_backend.hpp`
- Modify: `desktop/app/structural_backend.cpp`
- Modify: `desktop/app/tests/structural_controller_tests.cpp`

- [ ] **Step 1: Add a source gate for the retired APIs**

Extend the existing retired-source test to fail if active C++ contains any of:

```text
struct StructuralRefinementEvidence
compile_structural_refinement_evidence(
compile_structural_findings(request,
write_structural_archive(
DesktopStructuralRun execute(
```

The v2 verifier may use an internal type named `LegacyV2RefinementRecord`; it
must not expose or accept the retired production aggregate.

- [ ] **Step 2: Run the gate and verify the old paths are still present**

```bash
cmake --build --preset headless-debug --target prometheus_structural_tests
ctest --test-dir out/build/headless-debug --output-on-failure \
  -R 'retired|structural'
```

Expected: failure lists the old finding/evidence/writer/backend entry points.

- [ ] **Step 3: Give benchmark meshes one stable analysis lineage**

Change axial and cantilever benchmark factories so mesh division counts do not
change the reviewed analysis identity. Use:

```text
analytic-axial-tension-bar-refinement-v1
analytic-cantilever-refinement-v1
```

Mesh SHA-256, target size, topology, compiled setup identity, and result
identity still differ between coarse and fine. Analytic comparison tolerances
remain unchanged.

- [ ] **Step 4: Migrate both tools to the production pair API**

Run both samples in one output directory with distinct job names, compile one
criterion, two completed samples, one reviewed correspondence record, one
verified pair, and one evaluation. The benchmark tool then writes v3:

```cpp
const auto criterion =
    ps::compile_structural_refinement_criterion(0.10);
const auto coarseSample = ps::compile_completed_structural_sample(
    ps::StructuralSampleRole::coarse, criterion, coarseOptions,
    coarseReference.setup, coarse.run);
const auto fineSample = ps::compile_completed_structural_sample(
    ps::StructuralSampleRole::fine, criterion, fineOptions,
    fineReference.setup, fine.run);
const auto boundaryReview =
    ps::review_structural_boundary_correspondence(
        coarseReference.setup, fineReference.setup, true, true);
const auto compiled = ps::compile_structural_refinement(
    coarseSample, fineSample, boundaryReview);
if (!compiled.complete())
  throw std::runtime_error(compiled.issues().front().code + ": " +
                           compiled.issues().front().message);
const auto evaluation =
    ps::compile_structural_findings(*compiled.value());
const auto archive = ps::write_structural_refinement_archive(
    *compiled.value(), evaluation);
```

Print the independently derived displacement, stress, and maximum changes.
Update `structural_tool_fixture.cmake` to expect the v3 manifest at the common
benchmark output root and replay it with the existing CLI.

- [ ] **Step 5: Remove every public legacy production entry point**

Delete `StructuralRefinementEvidence`, the old findings overload,
`compile_structural_refinement_evidence()`, the v2 archive writer, and
`StructuralBackend::execute()`. Remove their tests rather than preserving a
second path.

Keep v2 read compatibility inside `structural_archive.cpp` by parsing an
internal `LegacyV2RefinementRecord` and reproducing the original v2 finding
claim locally. That compatibility routine may validate the stored active
result and claimed second identity, but it must not return a
`VerifiedStructuralRefinement` or feed v2 data into the new finding compiler.
Add a generated canonical v2 fixture so this reader remains tested after the
writer is gone.

- [ ] **Step 6: Run all structural and desktop focused gates**

```bash
cmake --build --preset headless-debug --target \
  prometheus_structural_tests prometheus_run_structural_benchmark \
  prometheus_run_structural_refinement prometheus_replay_structural_run
ctest --test-dir out/build/headless-debug --output-on-failure \
  -R 'structural|retired'
cmake --build --preset desktop-no-occt-debug --target \
  prometheus_structural_controller_tests prometheus_qml_authority_tests
ctest --test-dir out/build/desktop-no-occt-debug --output-on-failure \
  -R '^(prometheus_structural_controller|prometheus_qml_authority)$'
```

Expected: tool fixtures use v3; legacy readers still accept v1/v2; the source
gate finds no retired production entry point.

- [ ] **Step 7: Commit authority cleanup**

```bash
git add desktop/structural desktop/app/structural_backend.hpp \
  desktop/app/structural_backend.cpp \
  desktop/app/tests/structural_controller_tests.cpp
git diff --cached --check
git commit -m "Retire caller-authored refinement evidence"
```

## Task 9: Harden chunk bounds, update claims, and run one release checkpoint

**Files:**
- Modify: `desktop/integrity/src/canonical_json.cpp`
- Modify: `desktop/integrity/tests/canonical_json_tests.cpp`
- Modify: `docs/phase-03-structural-workflow.md`
- Modify: `docs/phase-04-persistence-and-portability.md`
- Modify: `docs/superpowers/specs/2026-08-16-manual-structural-refinement-reconciliation-design.md`
- Modify: `docs/superpowers/plans/2026-08-15-mainline-structural-reconciliation.md`

- [ ] **Step 1: Add the failing oversized-stream-chunk test**

Add:

```cpp
requireThrows(
    [&] {
      (void)integrity::sha256_file_chunks(
          fixture, std::numeric_limits<std::size_t>::max(),
          [](const std::string_view) {});
    },
    "streamsize",
    "chunk sizes outside the stream interface fail before allocation or read");
```

Include `<limits>` in the test.

- [ ] **Step 2: Run the integrity test and observe the unsafe size path**

```bash
cmake --build --preset integrity-debug --target prometheus_integrity_tests
ctest --test-dir out/build/integrity-debug --output-on-failure \
  -R '^prometheus_integrity$'
```

Expected: the new test fails because only zero chunk size is rejected.

- [ ] **Step 3: Reject unrepresentable stream sizes before allocation**

Before allocating the chunk buffer, add:

```cpp
if (chunkBytes == 0U)
  throw std::invalid_argument("file hash chunk size must be positive");
if (chunkBytes >
    static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
  throw std::invalid_argument(
      "file hash chunk size exceeds std::streamsize");
```

Include `<limits>` in `canonical_json.cpp`. Run the integrity command again;
expected: pass.

- [ ] **Step 4: Update Phase 3 claims to match the implemented boundary**

Revise `docs/phase-03-structural-workflow.md` so its product path explicitly
contains two reviewed meshes/results. State that:

- coarse and fine meshes are manually supplied in this increment;
- the criterion is locked before the baseline solve;
- shared physics is copied and locked while fine surfaces are independently
  reviewed;
- a valid above-threshold study is archived as indeterminate with zero
  findings;
- v3 replay reconstructs both raw result packages;
- v1/v2 remain legacy read contracts;
- archive/publication do not repeat active engineering calculation; and
- automatic mesh generation can plug into the prepared-mesh boundary but is
  not implemented.

Remove the current statements that only accepted v2 archives are written or
that the desktop has no typed refinement flow. Keep the YUBI and arbitrary
CAD-face correspondence limitations beside the new claims.

Revise `docs/phase-04-persistence-and-portability.md` so Checkpoints 1 and 3
say schema v1/v2 retains seven artifacts, whereas v3 relocation and new desktop
embedding retain fourteen artifacts across coarse and fine samples. State that
offline v3 verification replays both results and the derived comparison;
active publication still performs integrity hashing/chunking without repeating
solver or finding computation.

Change the design spec status to `Implemented; release verification pending`
before the release commands. In the older reconciliation plan, add a checked
remediation note linking this plan and explaining that the independent review's
two Important findings are closed by the typed controller path and replayed
two-result archive; do not rewrite its historical task sequence.

- [ ] **Step 5: Run the native release checkpoint once**

Configure, build, and test headless plus desktop-no-OCCT:

```bash
cmake --preset headless-debug
cmake --build --preset headless-debug
ctest --preset headless-debug --output-on-failure
cmake --preset desktop-no-occt-debug
cmake --build --preset desktop-no-occt-debug
ctest --preset desktop-no-occt-debug --output-on-failure
```

Expected: all headless and desktop tests pass. If the managed sandbox blocks
the existing loopback HTTP listener test, rerun only that exact test outside
the socket sandbox and record both facts; do not weaken or skip the test.

- [ ] **Step 6: Run the unchanged backend and PostgreSQL release checks**

```bash
/private/tmp/prometheus-program-01a-uv/bin/uv run --extra dev \
  pytest -q --tb=short
PROMETHEUS_TEST_POSTGRES_URL=postgresql+psycopg://127.0.0.1:55432/prometheus_program_01a_test \
  /private/tmp/prometheus-program-01a-uv/bin/uv run \
  pytest -q tests/test_migrations.py tests/test_migrations_v2.py --tb=short
```

Expected: the full backend suite and both PostgreSQL migration suites pass.
These commands are run once at the release checkpoint because this increment
does not modify the Python service.

- [ ] **Step 7: Run repository hygiene and request independent review**

```bash
git diff --check
cmake --list-presets
cmake --list-presets=build
cmake --list-presets=test
git status --short --branch
```

Expected: no whitespace errors, all preset lists parse, and only the intended
documentation/integrity changes remain before the final commit. Request an
independent code review focused on: reachable controller publication, private
comparison construction, v3 replay from two raw results, legacy isolation,
fine-only invalidation, and exact stage counts. Resolve every Critical or
Important finding and rerun its focused regression before proceeding.

- [ ] **Step 8: Record verified status and commit the release checkpoint**

After all release commands and review pass, change the design spec status from
`Implemented; release verification pending` to `Implemented and locally
verified`. Do not claim Windows execution until GitHub Actions or a supported
Windows machine runs it.

```bash
git add desktop/integrity/src/canonical_json.cpp \
  desktop/integrity/tests/canonical_json_tests.cpp \
  docs/phase-03-structural-workflow.md \
  docs/phase-04-persistence-and-portability.md \
  docs/superpowers/specs/2026-08-16-manual-structural-refinement-reconciliation-design.md \
  docs/superpowers/plans/2026-08-15-mainline-structural-reconciliation.md
git diff --cached --check
git commit -m "Verify manual structural refinement flow"
git status --short --branch
```

Expected: the worktree is clean. Stop before push and report the exact local
commit range, test counts, any sandbox exception, remaining Windows CI status,
and the continuing YUBI/outside-user limitations.
