# YUBI gripper Windows screening trial

## Purpose

Use a second independent, open-source mechanical project to close the bounded
Windows folder-screening gate. This trial exercises a clean medium-size STEP
assembly alongside a BOM, assembly guide, license, and source manifest. It is
not a simulation, strength result, or project-wide pass.

## Frozen source

- Project: [Toyota YUBI hardware](https://github.com/Toyota/yubi-hw)
- Revision: `e8334ff04945ccf56c0576a56f6fab74b63daaa2`
- License: CERN Open Hardware Licence Version 2 — Weakly Reciprocal
  (`CERN-OHL-W-2.0`)
- Assembly: `YUBI Gripper Assy_DYNAMIXEL.stp`
- Assembly SHA-256:
  `0db1eefe396d528c9705331f0f5d71a3b67d30d86d9b4edf8fb8be8d84efaac6`

The preparation script also verifies the BOM, assembly guide, and license
hashes. The external source and generated trial folder remain ignored build
inputs; Prometheus does not relicense or silently publish them.

## Reproduce

Prepare the exact mixed project folder:

```powershell
.\scripts\prepare-yubi-gripper-trial.ps1
```

Build Release, verify the assembly through the production OCCT importer, and
open the folder in the desktop application:

```powershell
.\scripts\run-yubi-gripper-trial.ps1
```

The expected screen is deliberately partial:

- the only STEP assembly is selected and imported automatically;
- the STEP, CSV, PDF, license, and manifest are all inventoried and hashed;
- the BOM and assembly guide remain visibly not evaluated;
- geometry findings and import warnings are scoped to supported checks;
- material, mass, loads, restraints, contacts, motion, and strength remain
  unknown or deferred;
- the UI never reports that the gripper or project passed.

## Observed Windows Release result

The 2026-08-15 trial used implementation `519e06c` and upstream revision
`e8334ff04945ccf56c0576a56f6fab74b63daaa2`.

| Observation | Result |
| --- | --- |
| Prepared folder | 5 files: STEP, CSV, PDF, license text, and JSON manifest |
| Assembly size | 7,613,253 bytes |
| Import hierarchy | 1 root and 90 leaf parts |
| Display mesh | 37,367 triangles |
| Static interference | Deferred; no clearance claim |
| Windows Release suite | 22/22 passed in 57.26 seconds |
| Desktop startup | Ordinary project-folder startup remained running |

The first import attempt crashed inside OCCT automatic solid reconstruction.
That is the first consequential Phase 2 finding: a valid independent project
exposed a transfer path not exercised by the motor fixture or OpenArm. The
importer now disables automatic shape healing after initializing every STEP
transfer session, preserves the received topology, and makes that limitation
visible. The exact same assembly then imported successfully with the counts
above.

Current friction and unknowns:

- BOM rows and the PDF assembly guide are inventoried but not understood.
- No catalog component is automatically matched to any CAD leaf.
- Material, mass, joints, loads, restraints, and operating conditions are
  unknown.
- There is no structural, fatigue, fastening, tolerance, manufacturability, or
  safety result.
- The trial still needs an outside user to measure setup time, selection
  clarity, and whether the unknown/deferred language is understandable.

## Evidence to retain

Record the importer root, leaf, and triangle counts; visible file states;
warnings; setup friction; time to first geometry; and any confusing language.
The Phase 1 completion record may cite this trial only after the Release
importer succeeds and the ordinary folder-startup path opens the project.
