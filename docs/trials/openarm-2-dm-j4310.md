# OpenArm 2.0 + DM-J4310 first real-component trial

## Purpose

Use one licensed open-source mechanical assembly and one documented catalog
component to drive Prometheus Phase 1 and Phase 2 with observable product
evidence. This is an intake and screening trial, not a safety or certification
claim.

## Selected sources

- Project: [Enactic OpenArm Hardware](https://github.com/enactic/openarm_hardware)
- Project license: CERN Open Hardware Licence Version 2 — Strongly Reciprocal
  (`CERN-OHL-S-2.0`)
- Assembly: OpenArm 2.0 full STEP, verified locally as
  `0f97a5fb308d5b09353aa67bbd32d7c08e55dc5629bcbe20c12f71c82ef13c94`
- Component: DAMIAO `DM-J4310-2EC V1.1`, identified by the official OpenArm 2.0
  motor documentation as the J5–J8 actuator type
- Component evidence: the manual linked by OpenArm, verified locally as
  `aad2c3ae95932393c87b4d149a4009c8e93af727df8d787472de807e2703d68e`

The upstream source and manual remain authoritative. Prometheus does not
relicense them and does not publish their values merely because they were
downloaded.

## Prepare the reproducible folder

Run:

```powershell
.\scripts\prepare-openarm-dm4310-trial.ps1
```

To prepare, build, and open the complete mixed-folder workflow in one command,
run:

```powershell
.\scripts\run-openarm-component-trial.ps1
```

This sets `PROMETHEUS_STARTUP_PROJECT_FOLDER`; it does not bypass intake by
opening the STEP file directly.

### CAD interaction controls

- Drag a visible part directly to move it on the current camera plane.
- Hold `Ctrl` and click parts to add or remove them from the selection.
- Drag on empty viewport space to box-select visible part centers.
- Drag the translation gizmo or use its nudges to move the selected group while
  preserving relative offsets.
- Group movement commits as one undo/redo action and triggers one geometry
  recomputation.
- Use `W`, `A`, `S`, and `D` to pan the camera and `Q`/`E` to move out/in.
- Use `T` for translate mode, `R` for rotate mode, and `F` to fit the view.
- Hold `Alt` while dragging to temporarily bypass placement snapping.

The script verifies both external artifacts and creates the ignored folder
`out/trials/openarm-2-dm-j4310`. It includes the STEP assembly, component
manual, and a source-only manifest. The manifest explicitly records that no
component claim, CAD binding, or simulation input has been reviewed yet.

Open that folder in Prometheus. The expected first-screen behavior is:

- all three files are visible and hashed;
- the STEP file is ready and selected as the only loadable assembly;
- the PDF and JSON are visible as not evaluated;
- no specification is inferred from filenames, geometry, or prose;
- the large-assembly collision limitation remains visible;
- no whole-project pass is shown.

When the source manifest and referenced manual hashes match, intake also shows
one **candidate component evidence** card. After the assembly loads, select the
intended CAD entity, reopen Project inventory, and choose **Bind candidate to
selected part**. The properties panel then displays the candidate identity.
This is an explicit session binding only: it does not mark specifications as
reviewed, create an execution package, or enable analysis. Saving a Prometheus
project preserves the binding through the existing project snapshot path.

The generated source manifest includes eleven page-9 numeric candidates:
rated voltage/current/torque/speed, peak current/torque, maximum no-load speed,
reduction ratio, outer diameter, height, and approximate mass. Original text
and units are retained alongside SI-normalized candidate values. Intake shows
their count, but every claim remains `unreviewed`; none is an execution input.
Choose **Review claims** to inspect the original value/unit, normalized SI
candidate, exact source file, and page for each property. Accept, reject, and
reset decisions are independent and session-only in this prototype. They are
discarded by a folder rescan and do not publish a component package; this is
intentional until real-project trials establish the durable review UX.

## Questions this trial should answer

1. Can a user find the intended J5–J8 motor geometry in the imported assembly?
2. Can the user bind it to an exact reviewed DM-J4310 component revision?
3. Can Prometheus screen the motor envelope and mounting region for supported
   static interference?
4. After loads and operating conditions are reviewed, can one bounded motor
   recipe assess torque adequacy without pretending to simulate the whole arm?

## Evidence to record

- preparation and load time;
- file inventory and hashes;
- assembly warnings, units, hierarchy, and selectable-part count;
- whether the intended motor is identifiable without developer knowledge;
- any naming or geometry ambiguity;
- unsupported checks and missing inputs;
- screenshots or logs of the first consequential finding;
- all user confusion and manual workarounds.

## Exit decision

This trial succeeds when an ordinary user can open the folder, understand what
was and was not evaluated, identify the intended motor, and state the next
review action. Component publication and motor execution remain separate later
gates and require reviewed evidence and scenario inputs.
