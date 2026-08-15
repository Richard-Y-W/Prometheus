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
  invalid material values, missing mesh references, zero-volume tetrahedra,
  missing loads, fewer than three non-collinear fixed nodes, missing limits,
  and unconfirmed review state;
- deterministic SI CalculiX input generation using `C3D4` and an explicitly
  pinned `SPOOLES` equation solver;
- deterministic parsing of raw `.dat` displacement and stress rows into
  maximum displacement and von Mises stress;
- missing output sections fail instead of becoming zero-valued results.

Installed development backends:

- CalculiX `2.23` from `mingw-w64-ucrt-x86_64-calculix-ccx`;
- Gmsh `4.15.2` from `mingw-w64-ucrt-x86_64-gmsh`.

The initial PaStiX default crashed with Windows access violation `0xC0000005`.
Prometheus therefore pins SPOOLES; the same smoke deck completed successfully.
This is backend compatibility evidence, not validation of the YUBI bracket.

Successful smoke artifacts:

| Artifact | SHA-256 |
| --- | --- |
| CalculiX input | `e8d0d9a76022c5df81ef4b986162fd6ac89214d3523afcc2c15911f1bbc40495` |
| Raw `.dat` | `da87ea3100779139f78576f86101edd811d530a0152f1f624c202fdc9514bfb2` |
| Raw `.frd` | `582cc2bfd1de886a8a38a014c5d947a71740e34064b635199c21bc4267b6c8cb` |

The smoke result was `2.228571e-8 m` maximum displacement. Its single
integration-point stress state compiled to `3428.571 Pa` von Mises. These
values prove parser/execution wiring only; no independent benchmark tolerance
has been claimed yet.

Run the reproducible smoke with:

```powershell
.\scripts\run-calculix-smoke.ps1
```

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

## Next checkpoint

1. Mesh the exact bracket through an isolated Gmsh invocation and retain the
   exact mesh plus diagnostics.
2. Present mesh statistics and face groups for human restraint/load selection.
3. Obtain reviewed elastic properties for the exact A2024 applicability state.
4. Define one bounded load and requirement without inferring them from bolt
   torque or servo identity.
5. Execute known-pass and known-fail cases, a refinement comparison, and an
   independent analytic benchmark before evaluating the real bracket.
