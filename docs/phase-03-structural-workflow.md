# Phase 3 structural workflow status

Status: active. Phase 2's outside-user session is deferred but remains open as
documented in the README; it has not been counted as completed evidence.

## Selected real slice

The selected component is Toyota YUBI `BRACKET_GRIPPER` at upstream revision
`e8334ff04945ccf56c0576a56f6fab74b63daaa2`, exact STEP SHA-256
`4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a`.
The BOM calls it machined `A2024` and describes it as the gripper-to-UR5e
attachment. That text is candidate identity, not reviewed material properties.

Prepare the exact blocked slice with:

```powershell
.\scripts\prepare-yubi-structural-slice.ps1
```

The generated manifest deliberately leaves material/temper, elastic
properties, load, restraint faces, requirements, mesh controls, and scenario
confirmation unreviewed. The application must not turn this folder into a
structural pass.

## Checkpoint 1: structural request and solver smoke

Implemented:

- Qt-free typed nodes, first-order tetrahedra, nodal forces, full-node
  restraints, material values, reviewed requirements, and review gates;
- validation for exact geometry identity, unsupported schema, non-finite or
  invalid material values, missing mesh references, inverted or zero-volume
  tetrahedra, zero-resultant or duplicate loads, unsafe heading text, weak
  reviewed mesh quality, fewer than three non-collinear fixed nodes, missing
  limit bases, and unconfirmed review state;
- deterministic SI CalculiX input generation using `C3D4` and an explicitly
  pinned `SPOOLES` equation solver;
- typed parsing of raw `.dat` displacement and stress identities;
- fail-closed result compilation that binds the exact regenerated deck,
  executable hash and version, process status, completion and error streams,
  final `.sta` convergence state, and complete final-step node and element row
  coverage before calculating displacement or von Mises metrics;
- cryptographic hashes for every raw artifact consumed by that compiler.

Installed development backends:

- CalculiX `2.23` from `mingw-w64-ucrt-x86_64-calculix-ccx`;
- Gmsh `4.15.2` from `mingw-w64-ucrt-x86_64-gmsh`.

The initial PaStiX default crashed with Windows access violation `0xC0000005`.
Prometheus therefore pins SPOOLES; the same smoke deck completed successfully.
This is backend compatibility evidence, not validation of the YUBI bracket.

Artifacts from the prior successful real Windows smoke:

| Artifact | SHA-256 |
| --- | --- |
| CalculiX input | `e8d0d9a76022c5df81ef4b986162fd6ac89214d3523afcc2c15911f1bbc40495` |
| Raw `.dat` | `da87ea3100779139f78576f86101edd811d530a0152f1f624c202fdc9514bfb2` |
| Raw `.frd` | `582cc2bfd1de886a8a38a014c5d947a71740e34064b635199c21bc4267b6c8cb` |

The smoke result was `2.228571e-8 m` maximum displacement. Its single
integration-point stress state compiled to `3428.571 Pa` von Mises. These
values prove parser/execution wiring only; no independent benchmark tolerance
has been claimed yet.

The checked-in `fixtures/structural/calculix-smoke/complete` case is explicitly
synthetic and only tests parser and coverage wiring. It is not solver-execution
evidence. Run the real reproducible gate on Windows with:

```powershell
.\scripts\run-calculix-smoke.ps1
```

The command deletes stale outputs for this one generated job, captures the
actual solver executable hash, version, exit status, stdout, and stderr, and
passes the generated deck plus `.sta` and `.dat` bytes through the same Qt-free
result compiler. A missing file, solver error marker, incomplete step, stale or
modified deck, non-finite value, or missing/duplicate/unexpected result identity
causes a nonzero gate result; file existence alone cannot pass.

## Structural trust-seam verification — 2026-08-15

Local release-checkpoint environment: macOS 26.5.2 build 25F84 on arm64,
Apple Clang 21.0.0, CMake 4.4.2, and Qt 6.11.1.

- `cmake --fresh --preset headless-debug`, build, and CTest: 15/15 passed.
- `cmake --fresh --preset desktop-no-occt-debug` and build: passed.
- Desktop CTest inside the managed sandbox: 22/23 passed. The remaining
  `prometheus_exact_package_download` test could not create its loopback test
  listener there; an isolated rerun with loopback access passed 1/1.
- `git diff --check`: passed.

The synthetic smoke fixture consumed by both headless and desktop CTest has
these exact SHA-256 values:

| Synthetic artifact | SHA-256 |
| --- | --- |
| Input deck | `e8d0d9a76022c5df81ef4b986162fd6ac89214d3523afcc2c15911f1bbc40495` |
| Status | `3a8a499e47d474d5c4fa30bde12ead2550ef84b8c5ca5b3ba14fcc79f24bcd5d` |
| Data | `57d38a55374169fb2b707d2ba804e6a78fa03c6f381955338c242191c374d49b` |
| Standard output | `713f43615dc704c1dce8bfc34d71f31ca6fb512df922e8543da3e7edabfcd5cb` |
| Standard error | `01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b` |

The prior real Windows smoke used CalculiX 2.23. The hardened Windows command
has not yet been rerun after adding full result-evidence verification, so that
external gate remains pending. No YUBI solver run has been executed, and no
YUBI pass, fail, or structural finding exists.

## Checkpoint 2: real bracket meshing

The exact bracket has now completed the meshing-only portion of checkpoint 2:

- Gmsh command uses explicit 1–3 mm characteristic lengths;
- generated local mesh SHA-256:
  `020dd2649c8a0ce1bc2e486b5c20c2a5aa9d91d7f5579990f6a61eb022533944`;
- 2,451 nodes and 7,566 final `C3D4` volume elements;
- explicit `0.001` coordinate scale converts the source millimetres to SI;
- imported SI bounds: `[-0.0175, -0.042, 0]` to
  `[0.035, 0.042, 0.008]` metres;
- Gmsh line and surface elements are excluded from the structural volume mesh.

The local mesh hash includes Gmsh's absolute output-path heading and is not a
portable semantic identity. The source geometry hash, Gmsh version/arguments,
explicit scale, and later canonical node/element bytes must define the
reproducible mesh identity. Run this checkpoint with:

```powershell
.\scripts\mesh-yubi-structural-slice.ps1
```

Meshing success does not authorize solver execution or demonstrate mesh
convergence.

## Checkpoint 3: reviewed setup and bounded material evidence

The desktop now has an explicit Structural workspace. It renders the retained
boundary mesh and lets the reviewer assign named groups as restraints or loads,
choose a material candidate and mark its applicability `known` or `assumed`,
enter force and requirement values, review the exact mesh controls, and confirm
the complete scenario. All readiness decisions remain in the C++ controller;
QML cannot enable export by inventing a local readiness condition.

The initial 2024 aluminum candidate ledger is
[`fixtures/evidence/aluminum-2024-candidates-v1.json`](../fixtures/evidence/aluminum-2024-candidates-v1.json),
with its source analysis in
[`docs/evidence/2024-aluminum-material-candidates.md`](evidence/2024-aluminum-material-candidates.md).
The Toyota BOM supports only `A2024`. Generic producer and canceled-handbook
values remain candidates and do not identify the supplied YUBI bracket. The
prepared structural slice therefore records material applicability as
`unresolved` and keeps every review flag false.

## Checkpoint 4: predeclared analytic validation gate

The benchmark contract was recorded before solver execution in
[`fixtures/structural/tension-bar/expectations.json`](../fixtures/structural/tension-bar/expectations.json).
It defines a 1.0 m by 0.1 m by 0.1 m tension bar, `F = 1000 N`, `E = 70 GPa`,
`nu = 0.30`, and these analytic values:

```text
loaded-face axial displacement = F L / (A E) = 1.4285714286e-6 m
central axial stress           = F / A       = 100000 Pa
```

The contract also fixes coarse, medium, and fine target sizes of 0.100 m,
0.050 m, and 0.025 m. Fine loaded-face displacement and volume-weighted
central axial stress must each be within 5 percent of the analytic values.
Medium-to-fine loaded-face displacement change, divided by the absolute fine
value, must not exceed 2 percent. Those tolerances are not adjusted after a
run.

`prometheus_export_structural_case` re-parses a canonical reviewed case,
recompiles its selected faces from the exact mesh, and emits an immutable
case/mesh/deck package. `prometheus_verify_structural_case` independently
rechecks the package, compiles complete CalculiX evidence, derives the scoped
benchmark metrics, and emits deterministic `pass`, `fail`, or `indeterminate`
findings. A result equal to its limit fails. Missing convergence, coverage,
refinement, or a requested limit basis is indeterminate.

The execution manifest declares whether the result profile is generic
structural findings or the analytic tension-bar check. Benchmark-only central-
band math is never applied silently to a YUBI or other arbitrary component.
The verifier hash-binds the retained `.frd` bytes alongside the deck, `.sta`,
`.dat`, streams, refinement evidence, case, mesh, and execution manifest.

A macOS preflight with the official Gmsh 4.15.2 ARM SDK exercised the real INP
export for all three declared sizes. The strict parser retained the named
physical ELSETs and the exporter accepted 87 nodes/203 tetrahedra (coarse),
190 nodes/435 tetrahedra (medium), and 1,072 nodes/3,558 tetrahedra (fine).
Their minimum mean ratios were 0.4195,
0.06560, and 0.4038, respectively, all above the predeclared 0.05 floor. This
is mesh/export wiring evidence only; it contains no CalculiX result.

Run the real gate on the reviewed Windows environment:

```powershell
.\scripts\run-structural-validation.ps1
```

For release checkpoints, `.github/workflows/structural-validation.yml` exposes
the same command as an explicit manual Windows job. It installs only the UCRT64
C++/Gmsh/CalculiX dependencies and uploads the bounded evidence directory even
when the gate fails; it is not part of every-push CI.

The runner preserves all three meshes, decks, `.sta`, `.dat`, `.frd`, captured
streams, backend identities, elapsed times, package manifests, and result
manifests under `out/validation/structural/tension-bar`. It rejects stale,
byte-identical, or non-increasing mesh levels and records the exact mesher
parameters plus exporter/verifier executable identities. It computes analytic
comparisons only from normalized result rows, then re-evaluates the unchanged
fine solve against the predeclared `1.6e-6 m` known-pass limit and `1.2e-6 m`
known-fail limit.

The checked-in `package-smoke` data exercises this package seam in CTest and
proves that modified package bytes fail closed. It is synthetic data, not an
executed solver benchmark. The Windows analytic run remains required before a
YUBI finding is permitted.

Local implementation verification on 2026-08-15 passed all 16 headless tests
and built the desktop without Open Cascade. In the managed sandbox, 24 of 25
desktop tests passed; the sole loopback-listener test passed in its isolated
rerun with local socket access. These are software checks, not Windows solver
evidence.

## Next checkpoint

1. Run the predeclared analytic gate on Windows and retain its compact summary.
2. Rerun the hardened real CalculiX smoke on that same backend installation.
3. Produce stable named boundary groups for the exact YUBI bracket.
4. Obtain explicit human review of material applicability, load/restraint
   groups, limits, mesh controls, and the complete bounded scenario.
5. Execute the bracket only if the analytic and review gates pass; otherwise
   record the result as indeterminate without relaxing a tolerance.
