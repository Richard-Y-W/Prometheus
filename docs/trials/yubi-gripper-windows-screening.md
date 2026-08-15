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

## Evidence to retain

Record the importer root, leaf, and triangle counts; visible file states;
warnings; setup friction; time to first geometry; and any confusing language.
The Phase 1 completion record may cite this trial only after the Release
importer succeeds and the ordinary folder-startup path opens the project.
