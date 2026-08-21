# YUBI bracket structural trial result

**Recorded:** 2026-08-21
**Engineering evaluation:** `indeterminate`

## Purpose and claim boundary

This record reports one reviewed coarse/fine linear-static calculation on the
Toyota YUBI `BRACKET_GRIPPER` geometry. The Windows workflow completed both
CalculiX cases and replayed its archive, and a separately downloaded copy of
that archive passed the corrected local replay CLI. Those process results do
not make the bracket safe or make the engineering evaluation pass.

The reviewed question was whether maximum displacement remains at or below the
project-owner-selected `0.0005 m` informational threshold under the recorded
hypothetical setup. The predeclared refinement rule also requires global
maximum displacement and global maximum von Mises stress to change by no more
than 10% between the supplied meshes. Stress has no reviewed allowable and is
therefore a refinement diagnostic, not a pass/fail requirement.

## Reviewed model

| Input | Reviewed value | Evidence boundary |
| --- | --- | --- |
| Geometry | Toyota YUBI `BRACKET_GRIPPER`, upstream commit `e8334ff04945ccf56c0576a56f6fab74b63daaa2`, STEP SHA-256 `4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a` | Pinned geometry identity; not the complete gripper system |
| Material | Assumed 2024-T351 bare aluminum plate, `E = 73.7739030369 GPa`, Poisson ratio `0.33` | The YUBI BOM establishes only `A2024`; temper, product form, delivered stock, and material certificate remain unknown |
| Restraint | Fully fixed source CAD `Surface75`, treated as the UR5e/tool-flange attachment region | Idealized fixed boundary |
| Load | Total `[0, -100, 0] N` over source CAD `Surface76`, treated as the gripper/upper-plate attachment region; no applied moment | Exploratory project-owner-selected load, not a Toyota, manufacturer, or service requirement |
| Displacement limit | `0.0005 m` | Informational workflow threshold, not a validated functional or service limit |
| Refinement limit | `0.10` change fraction for both declared global observables | Locked before execution and not changed after observing the result |

The analysis assumes homogeneous isotropic linear elasticity, small
deformation, first-order tetrahedra, and the reviewed load/restraint
correspondence between meshes.

## Native execution and replay evidence

- Workflow: [YUBI structural trial run
  32503165787](https://github.com/Richard-Y-W/Prometheus/actions/runs/32503165787),
  attempt 1
- Job: `Windows reviewed YUBI pair`, job ID `96837321279`
- Repository commit:
  `6195ec6275097bdb37c921c646b99e3084169cc0`
- Prerequisite automatic matrix: [run
  32499570880](https://github.com/Richard-Y-W/Prometheus/actions/runs/32499570880),
  9/9 jobs successful on the same commit
- Prerequisite structural validation: [run
  32502246569](https://github.com/Richard-Y-W/Prometheus/actions/runs/32502246569),
  successful on the same commit
- Solver: `CalculiX Version 2.23, Copyright(C) 1998-2025 Guido Dhondt`
- Solver executable SHA-256:
  `913abf828a2d706f3e8c9da89d7a0eddd68ce817f8dabf1098cb013dfe3f94f6`
- Archive schema: `4.0.0`
- Workflow artifact: `yubi-structural-trial-32503165787-1`, artifact ID
  `9454345968`, 3,123,911 compressed bytes
- GitHub artifact digest:
  `sha256:42030c8fd85c944b7c313c12bdae0ce4b3c5bedf6a34440c904391ec82891b25`
- Structural archive manifest SHA-256:
  `7794c99815e7ccfed597e860fa16d60a566a19fd25d801f7ad8137ba030b12a7`
- Trial-summary SHA-256:
  `f3d246f98359c29d698da868be2a436d753de73a4191eba3faee3e7fee37505a`

The retained runner log contains exactly two CalculiX version banners, one for
`yubi_bracket_coarse` and one for `yubi_bracket_fine`. Both solver processes
exited zero. Archive creation, upload, download, and replay did not execute
another solver case.

The downloaded archive was replayed independently with the corrected local
CLI:

```text
status=verified max_displacement_m=7.70501e-09 max_von_mises_pa=143224 obligations=0/1
```

## Identities and retained artifacts

- Reviewed-pair manifest file SHA-256:
  `f74aac395c4eb055c961a95e221f81776aa936b2df33d350c063c8b1fbe6beee`
- Reviewed-pair semantic identity:
  `sha256:930f52d5b47b52cc4778c40c550b174efaabaf89d0520dcb8b0393d4d2bb2273`
- Refinement criterion identity:
  `sha256:8b29233af215d87579c5daaf68f6d49fed789626744680c8c81b55159eb51dbd`

| Identity | Coarse | Fine |
| --- | --- | --- |
| Mesh source SHA-256 | `0f1e3dcd8d6a7e80ae7dd580e45e6fbca42db3d15e204b1f686fea80e4462a0e` | `20295abe4b4748072f35362234e99a66a5e4651a31a5ae66f4c2179706ea51a4` |
| Compiled setup | `sha256:4c11ff130f3245e82856b9f71cb4c6f9754e6fa044ac62d16d6572900a0120d6` | `sha256:faa8bf058880bacf2350559e128b3ef2d0b0d56664ae8a3adcfeb98748142976` |
| Validated-result identity (archive field) | `sha256:16a354e513aa489592ae8086b0600b0064c99eb2b8cf519e28455dd90123f7fa` | `sha256:2c52671b39862c5ea33cdd97564db615c18bb0e55bdfe9a84d7458d17a23cb66` |
| Deck SHA-256 | `c3c57b3c3f4e2383c515c4e75d5fc67e15025dda568960a7b46207b455c15c94` | `62f30f136dd936ae26a7f7a2af403989eeabfd7e4154fbbb9053d48875ae387c` |
| DAT SHA-256 | `48edab505e58d0fd661e56e3cb2d365bcb7283ea35b7fe496a174a66bbca1338` | `5221fdcbee15fb3aed0c125110b80c58735cb6cf39f83a92c65d106e2a68a8cd` |
| FRD SHA-256 | `7c436abbbc2aaa80f97309cb9a0bb019dcf3b43e0dfd1dece32e92f1051551fc` | `0e0f5c774ac8ccde6d2c8c965fb0de16c4949de9f638252f655e73a29489a74e` |
| Reviewed-setup artifact SHA-256 | `29184b68d3a59da128b74533707ccf3e850112d465277a9bdaa58fc5389bf139` | `6f2f7052355c7fb15e9a4efc18d8e2cf2955bd4983d2d96ee9132e9c98fe7290` |
| STA SHA-256 | `253efd59462bae0a6fce7b06552a063c82349747e482366b56da923c68b7ca9d` | `253efd59462bae0a6fce7b06552a063c82349747e482366b56da923c68b7ca9d` |
| Standard-error SHA-256 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| Standard-output SHA-256 | `92e26d66100449691be49b3c70935ec523f64cf0a0168fc06536d8c10dcb4151` | `dd935219c83c3a9d0313718da3e44d0f84125814081a62b5e17742876bebd7b5` |

## Solver completion and mesh evidence

| Evidence | Coarse | Fine |
| --- | ---: | ---: |
| Nodes | 2,446 | 7,876 |
| `C3D4` elements | 7,533 | 29,015 |
| Exterior boundary faces | 4,616 | 12,076 |
| Observed minimum mean ratio | `0.3147872535447022` | `0.24813546919245444` |
| Required minimum mean ratio | `0.20` | `0.20` |
| Solver exit code | 0 | 0 |
| Solver elapsed time | 702 ms | 2,532 ms |
| Final convergence row | step 1, increment 1, attempt 1, iteration 1 | step 1, increment 1, attempt 1, iteration 1 |
| Displacement rows | 2,446 | 7,876 |
| Stress rows | 7,533 | 29,015 |

Solver completion establishes that both declared cases ran and produced the
required result coverage. It does not establish coarse/fine agreement or
satisfaction of an engineering requirement.

## Coarse/fine comparison

| Observable | Coarse | Fine | Change fraction | Predeclared maximum | Status |
| --- | ---: | ---: | ---: | ---: | --- |
| Global maximum displacement | `7.162286152439026e-9 m` | `7.705012145689252e-9 m` | `0.0704380451306449` (`7.04380451306449%`) | `0.10` | Accepted for refinement |
| Global maximum von Mises stress | `123153.84050992897 Pa` | `143224.12817969918 Pa` | `0.1401320289035979` (`14.01320289035979%`) | `0.10` | Indeterminate |

The displacement observable changed by less than 10%, but the required stress
diagnostic changed by more than 10%. The locked pair criterion therefore
reported `refinement=indeterminate`, `evaluation=indeterminate`, zero findings,
and `0/1` evaluated obligations. The fine displacement is numerically below the
informational `0.0005 m` threshold, but Prometheus did not convert that value
into a finding because the complete predeclared refinement gate did not pass.

The archive declares one `maximum_displacement` obligation and records zero as
evaluated. Its retained unknown is
`refinement_observable_not_converged`: at least one required refinement
observable exceeded its predeclared change threshold. No obligation is covered
by a finding in this trial.

The numerical metrics, comparison fractions, evaluation, and coverage exactly
match the solver evidence retained from the earlier run
[32452481678](https://github.com/Richard-Y-W/Prometheus/actions/runs/32452481678).
That earlier workflow failed during archive replay and is not a successful
release checkpoint. Run 32503165787 produced the corrected portable result
identities above and passed both workflow replay and independent local replay.

## What this result does not establish

This trial does not identify the manufactured bracket's material condition,
service load, allowable stress, safety factor, or rated capacity. It does not
evaluate bolt preload, fastener stress, contact, friction, slip, applied
moments, plasticity, large deformation, fatigue, buckling, manufacturing
variation, assembly tolerances, defects, or the behavior of the complete YUBI
gripper. It is not a safety result and not evidence that the arbitrary YUBI
project works.

Resolving this specific numerical uncertainty would require a newly reviewed
finer mesh or another predeclared convergence study; the current evidence does
not authorize another solver run or a relaxed threshold.
