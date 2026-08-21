# Reviewed YUBI bracket structural pair

This directory freezes the two manually supplied meshes and the review record
for one exploratory linear-static calculation on Toyota YUBI's
`BRACKET_GRIPPER` geometry.

The calculation asks a narrow question: under the recorded 100 N local
negative-Y load, fixed-face idealization, and assumed elastic material, does
the refined model's maximum displacement violate the project-owner-selected
0.50 mm informational threshold? A result applies only to that model and does
not establish the material, rated load, allowable stress, or safety of a
manufactured YUBI bracket.

## Immutable inputs

- Upstream: `https://github.com/Toyota/yubi-hw`
- Commit: `e8334ff04945ccf56c0576a56f6fab74b63daaa2`
- STEP path: `STEP/gripper/BRACKET_GRIPPER.stp`
- STEP SHA-256:
  `4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a`
- Coarse mesh SHA-256:
  `0f1e3dcd8d6a7e80ae7dd580e45e6fbca42db3d15e204b1f686fea80e4462a0e`
- Fine mesh SHA-256:
  `20295abe4b4748072f35362234e99a66a5e4651a31a5ae66f4c2179706ea51a4`
- Material-evidence SHA-256:
  `cc0d48a14b9802f43aa599a994b35bbcb02cda4708119bcacbd41b3bf219fbe3`

The STEP file is not republished here. The manifest records its pinned source
identity. The two mesh files are derived from that STEP with Gmsh 4.14.1 and
are distributed with Toyota's pinned CERN-OHL-W-2.0 license notice. The raw
license bytes in `YUBI-HARDWARE-LICENSE.txt` have SHA-256
`6d3e25e377354ac09d62d33c38520d0fc2a0d7be84b346643de91a5d46d98902`.

## Reviewed assumptions

- Material: assumed 2024-T351 bare plate, using `73.7739030369 GPa` and
  Poisson ratio `0.33` from the selected canceled-handbook reference record.
- Restraint: fully fixed source CAD `Surface75`, treated as the UR5e/tool-flange
  attachment region.
- Load: a total `[0, -100, 0] N` distributed over source CAD `Surface76`,
  treated as the gripper/upper-plate attachment region.
- Applied moment: none.
- Requirement: maximum displacement no greater than `0.0005 m`, informational
  only.
- Refinement: both global maximum displacement and global maximum von Mises
  stress must change by no more than 10% between the supplied meshes.

The material record does not identify the actual bracket temper, product form,
thickness, heat lot, or certificate. The load and displacement threshold do
not come from Toyota.

## Execution

On the supported Windows solver host, run:

```powershell
.\scripts\run-yubi-structural-slice.ps1
```

The script compiles both setups before execution, runs CalculiX once for the
coarse mesh and once for the fine mesh, writes one structural archive, and
replays that archive once at the release boundary. It does not regenerate the
meshes or rerun the analytic structural suite.

## Recorded native checkpoint

[GitHub Actions run 32503165787](https://github.com/Richard-Y-W/Prometheus/actions/runs/32503165787)
executed this exact pair at commit
`6195ec6275097bdb37c921c646b99e3084169cc0`. Both solver cases completed and
the downloaded v4 archive passed independent replay. Maximum displacement
changed by `7.04380451306449%`, while maximum von Mises stress changed by
`14.01320289035979%`. Because the stress diagnostic exceeded the locked 10%
criterion, the engineering evaluation remains `indeterminate`, with zero
findings and `0/1` evaluated obligations.

The exact run identities, artifact hashes, results, and limitations are in the
[YUBI bracket structural trial result](../../../docs/trials/yubi-bracket-structural-result.md).

## Excluded claims

This model does not evaluate bolt preload, fastener stress, contact, friction,
slip, applied moments, plasticity, large deformation, fatigue, buckling,
manufacturing variation, assembly tolerance, or project-wide correctness. It
contains no reviewed stress allowable, so it cannot produce a stress pass or
fail. If either required global observable changes by more than 10%, the
engineering evaluation is indeterminate; the threshold is not adjusted after
the result is known.
