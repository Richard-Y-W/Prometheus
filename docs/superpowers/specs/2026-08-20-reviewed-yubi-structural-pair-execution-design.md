# Reviewed YUBI Structural Pair Execution Design

**Date:** 2026-08-20  
**Status:** Implemented and locally verified; native YUBI execution pending

## Problem

Prometheus can review a tetrahedral mesh, compile a linear-static CalculiX
setup, execute one coarse and one fine sample, compare declared observables,
write a structural archive, and replay that archive. The remaining YUBI gap is
not another finite-element implementation. It is a reproducible bridge from
the project owner's reviewed YUBI choices to the existing Qt-free structural
core.

The bridge must prevent four misleading outcomes:

- a command must not solve one mesh before discovering that the other mesh or
  its review record is invalid;
- a changed mesh, material record, patch assignment, or threshold must not be
  accepted under the earlier review;
- saving, reporting, or displaying a result must not rerun CalculiX; and
- a completed exploratory run must not be described as evidence that the
  manufactured YUBI bracket is safe or meets a Toyota requirement.

## Decision

Prometheus will add one strict reviewed-pair manifest and one Qt-free runner.
The runner will call the existing `prometheus_structural` library; it will not
implement displacement, stress, force distribution, convergence, or finding
logic independently.

The runner has two phases:

1. **Preflight:** read and validate the complete manifest, verify every supplied
   file identity, prepare both meshes, reconstruct both exact boundary
   selections, compile both structural setups, compile the global refinement
   criterion, and compile the reviewed boundary correspondence.
2. **Execution:** create a new output directory, run the coarse job once, run
   the fine job once, compile the refinement and finding objects once, and write
   one structural archive from the retained in-memory results.

No solver process starts unless the whole preflight phase succeeds. The output
directory must not already exist, which prevents an accidental second run from
overwriting or being confused with the first run.

## Alternatives Considered

### Desktop-only execution

The existing desktop can perform the calculation through the same structural
backend, but a click sequence is difficult to reproduce on the Windows solver
host and difficult to audit after the fact. It remains the visual review
surface, not the only execution record.

### YUBI-specific PowerShell or hard-coded C++ setup

A one-off script could reach CalculiX quickly, but it would create a second
meaning of the setup outside the reviewed structural types. That path would be
hard to reuse for the next real component. Rejected.

### Reviewed manifest plus generic runner

A small manifest compiler makes the approved inputs machine-checkable while
retaining one authoritative numerical path. The same runner can accept another
component later without adding another solver adapter. Selected.

## Reviewed YUBI Scenario

The first checked-in manifest records the choices approved on 2026-08-20:

- component geometry: Toyota YUBI `BRACKET_GRIPPER` from upstream commit
  `e8334ff04945ccf56c0576a56f6fab74b63daaa2`;
- source STEP SHA-256:
  `sha256:4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a`;
- material applicability: **assumed** 2024-T351 bare plate;
- elastic modulus: `73,773,903,036.9 Pa`;
- Poisson ratio: `0.33`;
- material evidence: the checked-in MIL-HDBK-5J candidate record, which is
  canceled reference data and does not identify the supplied bracket;
- restraint region: the UR5e/tool-flange attachment face, source CAD
  `Surface75`;
- load region: the gripper/upper-plate attachment face, source CAD
  `Surface76`;
- total force: `[0, -100, 0] N` in the reviewed mesh coordinate frame;
- applied moment: none;
- requirement: maximum displacement no greater than `0.0005 m`;
- requirement basis: project-owner-selected informational threshold for this
  workflow demonstration, not a Toyota or manufacturer requirement;
- patch angle: `15 degrees`;
- mesh-quality floor: tetrahedral minimum mean ratio `0.20`; and
- coarse-to-fine change limit: `0.10` for both global maximum displacement and
  global maximum von Mises stress.

The two supplied meshes are manual inputs generated with Gmsh 4.14.1 from the
pinned STEP geometry:

| Sample | Gmsh controls | Nodes | C3D4 elements | Load patch | Restraint patch |
| --- | --- | ---: | ---: | ---: | ---: |
| coarse | 1.0–3.0 mm, nominal 2.0 mm | 2,446 | 7,533 | 3 | 4 |
| fine | 0.5–1.5 mm, nominal 1.0 mm | 7,876 | 29,015 | 2 | 3 |

The manifest also records expected boundary-face count, minimum mean ratio,
selected area, centroid, and normal for each selected patch. These values do
not replace the exact mesh hashes. They make a future parser or patch-grouping
change fail preflight if the same numeric patch ID no longer denotes the
reviewed physical region.

## Manifest Contract

The first schema is
`urn:prometheus:schema:reviewed-structural-pair:1.0.0`. It contains exactly:

- schema and version;
- review status, date, role, and claim boundary;
- shared analysis, source geometry, material, load, restraint, requirement,
  scenario, and refinement fields;
- a coarse sample and a fine sample; and
- one distinct safe solver job name for each sample.

The parser rejects duplicate JSON members, unknown members, unsupported schema
versions, non-finite or out-of-range numbers, unsafe text, malformed SHA-256
values, absolute paths, parent traversal, symlinks, and files outside the
manifest directory. Referenced files must be regular files with exact declared
hashes and bounded sizes.

The material-evidence file is hash-verified. Its selected `candidate_id` must
exist, be unique, and match the manifest's designation, temper, product form,
elastic modulus, and Poisson ratio. This check binds the approved numbers to
the reviewed candidate without claiming that the candidate applies to the
manufactured bracket.

For each sample, preflight:

1. parses and strictly validates the supplied Gmsh/Abaqus C3D4 mesh;
2. checks mesh identity, node count, element count, boundary-face count, and
   observed minimum mean ratio;
3. groups exterior faces at the declared patch angle;
4. resolves the exact load and restraint patch IDs;
5. compares each selected patch's area, centroid, and normal with the reviewed
   values using a small cross-platform floating-point tolerance;
6. builds a `StructuralSetup` using the computed mesh and exact selections;
7. compiles that setup through `compile_structural_setup()`; and
8. retains the compiled object for execution.

The pair is rejected unless mesh identities differ, the fine sample contains
more tetrahedra, its target size is smaller, its coordinate scale matches, its
shared lineage inputs match, and both load/restraint correspondence decisions
are explicitly true.

The manifest identity is the SHA-256 of its RFC 8785 canonical semantic JSON.
Whitespace-only edits therefore do not change the identity; any reviewed field
change does.

## Execution and Single-Computation Invariant

The CLI is:

```text
prometheus_run_reviewed_structural_pair \
  REVIEWED_PAIR_JSON CCX OUTPUT_DIRECTORY [TIMEOUT_SECONDS]
```

After preflight, the runner calls `run_calculix()` once with the coarse compiled
setup and once with the fine compiled setup. It then calls the existing sample,
refinement, finding, and archive compilers. It does not parse solver output or
calculate an engineering metric itself.

The following actions consume retained outputs and never rerun CalculiX:

- structural archive creation;
- console summary generation;
- GitHub artifact upload;
- desktop opening of either supplied mesh; and
- offline archive replay.

Offline replay deliberately reparses immutable evidence at an import or release
trust boundary. That verification is not an additional solve and is not part of
the interactive save path.

## Result Semantics

The CLI returns success when both solver evidence sets validate and the archive
is written, even if the engineering evaluation is indeterminate. Its output
states one of:

- `no_violation_detected_within_scope`;
- `violated`; or
- `indeterminate`.

An above-threshold coarse/fine change is an honest `indeterminate` result, not a
process failure and not a pass. A malformed manifest, changed artifact, invalid
mesh, unresolved patch, failed solver, incomplete row coverage, nonconvergence,
or archive error returns a nonzero process status.

Because the YUBI manifest declares only the displacement requirement, it cannot
produce a stress pass or fail. Global von Mises stress remains a required
refinement diagnostic. If that global maximum changes by more than 10%, the
current conservative pair criterion leaves the displacement evaluation
indeterminate as approved; the threshold will not be relaxed after seeing the
result.

## Windows Trial Workflow

A dedicated manual GitHub Actions workflow will:

1. use `windows-2022` and the existing pinned UCRT64 CalculiX package;
2. build only the reviewed-pair runner, replay tool, and focused tests;
3. run the checked-in YUBI manifest once;
4. replay the emitted archive once as a release-boundary integrity check; and
5. upload the exact archive, runner log, and replay log.

The workflow does not regenerate the meshes and does not rerun the analytic
structural suite. The current structural core already has a successful Windows
analytic/refinement/replay record at commit
`00adcb6ca59e368eacabf8a66ed8acc71cb4a865`. If implementation changes any
existing numerical, solver, refinement, or archive code rather than adding the
new adapter, the full structural validation workflow must run again.

## Claim Boundary

This trial evaluates one linear-static, small-deformation, isotropic-elastic
model on the pinned YUBI bracket geometry under an assumed material and an
exploratory load case. It excludes bolt preload, fastener stress, contact,
friction, slip, applied moments, nonlinear response, plasticity, fatigue,
buckling, manufacturing variation, assembly tolerances, and project-wide
correctness.

A displacement finding, if produced, applies only to the reviewed model,
meshes, load, restraint, threshold, and solver evidence in the archive. It does
not establish the actual YUBI material, rated load, allowable stress, safety
factor, or component safety.

## Focused Verification

Ordinary tests will use small deterministic meshes and the existing fake solver
process. They will establish:

- valid pair preflight compiles two setup objects with the reviewed force and
  criterion;
- duplicate or unknown JSON members fail;
- changed artifact hashes, material candidate values, expected patch geometry,
  or fine-mesh ordering fail before execution;
- a successful tool-fixture run produces exactly two distinct job artifacts,
  one v4 archive, and a replayable result; and
- a preflight failure produces no solver output directory.

The real 29,015-element fine mesh and native CalculiX solve remain outside the
ordinary test suite. They run only through the explicit manual YUBI workflow.
