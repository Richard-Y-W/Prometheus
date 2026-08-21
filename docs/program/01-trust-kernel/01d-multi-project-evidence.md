# Program 01D multi-project evidence

Status: active. Technical runs exist for all three project shapes, and the JPL
Rover intake is now a pinned deterministic regression. The outside-user
session required by the gate is still outstanding.

## Trial matrix

| Trial | Project shape | Measured result | Primary finding |
| --- | --- | --- | --- |
| [YUBI gripper](../../trials/yubi-gripper-windows-screening.md) | Clean medium STEP assembly plus BOM/PDF | 5 files; 1 root; 90 leaves; 37,367 triangles | OCCT automatic solid repair can access-violate on real CAD |
| [OpenArm 2.0](../../trials/openarm-2-dm-j4310.md) | Large deep assembly plus component evidence | 3 files; 607 leaves; 230,433 triangles; 9,013 ms import | One invalid raw topology node must not terminate an otherwise useful import |
| [JPL Open Source Rover](../../trials/jpl-open-source-rover-mixed-folder.md) | 623 MB mixed mechanical/electrical/docs/code repository | 967 files; 23 STEP candidates; inventory `sha256:be8aebb1…89191`; 2.8–3.1 s current macOS scans | Pinned repeatable accounting and correct refusal to guess; no system understanding |

## Reproducible Rover evidence

On 2026-08-15, two distinct archive roots produced the same exact inventory
identity, counts, duplicate total, and empty primary STEP path. Two subsequent
normal cached verification runs also passed. The preparation gate caught and
blocked an incorrect recorded license filename/hash before promotion; the
pinned Git tree established the corrected `LICENSE.txt` identity. This closes
the automated Rover accounting item only. It does not satisfy the independent
human session or establish any rover engineering finding.

## Ranked observed failures and friction

1. **Process safety during third-party CAD transfer.** Automatic OCCT shape
   healing caused a native access violation on YUBI. It is now disabled after
   each transfer session initializes.
2. **Partial-topology containment.** Raw OpenArm topology could throw during
   bounding. Individual unusable nodes now degrade with a warning while valid
   hierarchy and meshes remain available.
3. **Assembly authority.** JPL Rover has 23 checked-in STEP component models
   but no authoritative checked-in rover assembly. Prometheus correctly leaves
   the primary assembly empty.
4. **Large-inventory navigation.** A flat 967-row list hid actionable STEP
   candidates. Path search and state/duplicate filters were added from this
   evidence.
5. **Duplicate overload.** JPL contains 191 exact duplicate copies. They must
   remain accounted for, but a future grouped presentation should reduce noise
   without erasing paths.
6. **Missing semantics.** BOMs, manuals, CAD names, external assembly links,
   materials, joints, loads, and requirements are not connected automatically.
   This remains explicit instead of triggering speculative universal parsing.
7. **Source records can be wrong.** The first reproducible preparation rejected
   the previously recorded Rover license identity. Source provenance must be
   checked against the pinned object, not copied forward from a prior note.

## Selected first structural slice

The evidence selects the YUBI `BRACKET_GRIPPER` mounting component:

- upstream path: `STEP/gripper/BRACKET_GRIPPER.stp`;
- source revision: `e8334ff04945ccf56c0576a56f6fab74b63daaa2`;
- SHA-256:
  `4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a`;
- size: 118,191 bytes;
- BOM designation: machined `A2024`, used to attach the gripper to UR5e.

The first bounded question is:

> Under one explicitly reviewed gripper load case and explicit UR5e-side bolt
> restraints, does the selected bracket remain below reviewed displacement and
> stress limits in an isotropic linear-static CalculiX model?

The BOM's `A2024` text is candidate material identity only. It does not supply
reviewed elastic constants, temper, allowable stress, load magnitude,
restraint faces, mesh adequacy, or safety factor. Phase 3 must require those
inputs and keep the analysis blocked until they are reviewed.

## Remaining exit evidence

The repository now contains a neutral [participant task](../../trials/outside-user-screening-task-sheet.md),
blank [factual observation form](../../trials/outside-user-screening-observation-form.md),
[nonintervention protocol](../../trials/outside-user-screening-facilitator-protocol.md),
and Windows-first packaging and manifest-verification scripts. The package
intentionally embeds only the hash of the reviewed Rover expectation, not its
counts or answer-bearing JSON. The five-case tamper fixture passed under a
temporary hash-verified PowerShell 7.6.4 runtime on macOS. Windows-native
execution and the deployable ZIP still require the manual Windows checkpoint,
followed by a clean-account/VM package check.

One person outside the core development work must run at least one trial
without live developer intervention. Give them only the normal application and
the selected trial folder, then record:

1. setup time and time to inventory;
2. time to visible assembly or to a correct ambiguity decision;
3. whether they can find a loadable STEP using the filters;
4. whether they understand unsupported versus not evaluated;
5. whether they mistakenly believe the whole project passed;
6. their exact points of confusion and attempted workarounds.

Program 01D must not close until that report exists. Technical implementation
can prepare the bounded Phase 3 input workflow in parallel, but no outside-user
result may be invented or inferred from developer testing.
