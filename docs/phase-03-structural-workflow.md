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

## Checkpoint 3: reviewable exterior boundary

The structural mesh adapter now derives the exterior triangular boundary from
the volume tetrahedra without trusting Gmsh's auxiliary surface elements. It:

- removes faces shared by exactly two tetrahedra;
- rejects faces shared by more than two tetrahedra as non-manifold;
- orients every retained face normal away from its owning tetrahedron;
- reports deterministic node IDs, centroid, unit normal, and area in SI units;
- rejects missing nodes, duplicate IDs, repeated tetrahedron nodes, zero-area
  faces, and zero-volume orientation cases.

For the exact local YUBI bracket mesh recorded above, the enhanced probe reports
4,616 exterior triangular faces with total exterior area `0.0090477 m^2`. This
is mesh topology evidence only. Prometheus has not inferred bolt faces, load
faces, restraint faces, material, or a structural scenario from those faces.

## Checkpoint 4: selectable patches and durable boundary selections

Exterior triangles can now be grouped into deterministic connected visual
patches using an explicit maximum neighboring-normal angle. The exact bracket
produces 437 patches at 5 degrees, 291 at 10 degrees, 263 at 15 degrees, 247 at
20 degrees, and 127 at 30 degrees. This sensitivity is intentional evidence
that patch IDs are transient selection aids rather than durable engineering
identities.

When the user reviews a selection, Prometheus resolves patch IDs into sorted
exact face-node triples, sorted node IDs, and total SI area. Duplicate,
overlapping, missing, or invalid patches fail closed. A reviewed total surface
force can be converted into consistent first-order triangular nodal forces;
the exact selected topology and total force remain the authoritative inputs.
The conversion is deterministic and tests require the resulting nodal vectors
to sum to the reviewed total vector.

No patch is automatically classified as a fastener, contact, load, or
restraint surface. Those meanings still require explicit review.

## Checkpoint 5: reviewed setup compiler

A separate Qt-free setup contract now retains the meaning and provenance that
must not be reduced to solver numbers prematurely:

- material designation, exact source SHA-256, applicability statement,
  Young's modulus, Poisson ratio, and review state;
- exact load and fully fixed restraint boundary selections plus the reviewed
  total load vector;
- displacement and/or von Mises limits with a source or explicit exploratory
  rationale;
- minimum and maximum SI mesh sizes, mesher identity, and review state;
- a non-empty scenario description and explicit final confirmation.

The setup compiler rejects unreviewed fields, invalid provenance, missing or
changed boundary topology, stale selection areas/node sets, overlapping load
and fixed faces, invalid mesh controls, and absent requirement rationale. Only
then does it create the narrow numerical `StructuralRequest`. No YUBI material,
load, restraint, or requirement has been supplied or inferred by this work.

## Checkpoint 6: isolated CalculiX process authority

The Qt-free structural library now owns a shell-free CalculiX process adapter
with explicit executable, working directory, safe job identity, and timeout.
It captures stdout and stderr independently, records elapsed time and exit code,
terminates timed-out work, and distinguishes launch failure, timeout, nonzero
exit, missing required output, invalid result data, and completed execution.
Completed status requires `.dat`, `.frd`, and `.sta` files plus successfully
parsed displacement and stress rows.

Cross-platform executable-boundary tests exercise successful output parsing,
nonzero exit, missing output, and forced timeout. The ordinary smoke script now
uses this production runner rather than invoking `ccx` directly. CalculiX 2.23
completed the existing SPOOLES smoke through the new runner in 1,249 ms and
returned the unchanged `2.228571e-8 m` displacement and `3428.571 Pa` von Mises
metrics. This remains execution wiring evidence, not bracket validation.

## Checkpoint 7: scoped structural findings

Completed metrics can now be compiled against only the displacement and/or von
Mises obligations declared by the reviewed request. Each finding records the
measured value, limit, signed margin, unit, and the bounded isotropic
linear-elastic C3D4 scope. Values at or below a limit are described as
`no_violation_detected_within_scope`; values above it are `violated`.

A failed, timed-out, missing-output, or invalid-result execution evaluates zero
obligations and creates no pass or violation findings. Tests exercise the same
completed metrics against loose known-pass limits and tighter known-fail limits,
and preserve an explicit limitation excluding safety, fatigue, buckling,
contact, fasteners, nonlinear behavior, and project-wide correctness.

## Next checkpoint

1. Present the selectable patches in the desktop and retain reviewed load and
   restraint selections as exact face/node identities.
2. Obtain reviewed elastic properties for the exact A2024 applicability state.
3. Define one bounded load and requirement without inferring them from bolt
   torque or servo identity.
4. Execute known-pass and known-fail cases, a refinement comparison, and an
   independent analytic benchmark before evaluating the real bracket.
