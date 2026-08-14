# Windows-First Project Screening Design

## Decision

Prometheus will stop advancing one infrastructure layer at a time. The next
work is a vertical product slice: select a real mechanical-project folder in
the Windows desktop, account for every discovered file, load an unambiguous
STEP assembly, run the geometry checks already supported by Open Cascade, and
show both findings and questions that were not evaluated.

Windows is the primary product target. The supported mechanical profile is the
existing MSYS2 UCRT64 build with Qt 6 and Open Cascade enabled. macOS remains a
fast development host and Linux/macOS checks remain useful, but neither is a
release gate for this prototype slice. A Windows build of the real OCCT-enabled
desktop is required at the next release checkpoint.

This design implements the first two outcomes of the compressed roadmap and
creates the evidence needed for the third:

1. Load and completely inventory one real project folder.
2. Produce useful geometry findings and honest unknowns.
3. Repeat the workflow on materially different projects before generalizing.
4. Add one external structural solver only after a component, material, load,
   and restraint can be selected from a real project.
5. Harden only failures observed in those project trials.

## Alternatives considered

### Universal intake foundation first

Define versioned artifact schemas, a content-addressed store, sandboxed parser
workers, archive policies, and persistence before exposing folder intake. This
is the former roadmap direction. It is architecturally comprehensive but does
not quickly prove that engineers receive useful conclusions, so it is rejected
for the prototype.

### Folder-to-finding vertical slice — selected

Add a small Qt/C++ folder scanner and connect it directly to the existing
desktop and OCCT adapter. The scanner records every file, but only STEP is
semantically loaded in this slice. Other formats remain visible as
`not_evaluated`. This proves the product interaction and preserves truthful
coverage without committing to universal contracts prematurely.

### Structural solver first

Integrate CalculiX immediately and ask the user for geometry, material, loads,
and restraints manually. This could demonstrate FEA sooner, but it would not
solve the current blank-project problem and would encourage another fixture
workflow. It follows the project-screening slice instead.

## User workflow

1. The user selects **Open Project Folder** in the desktop.
2. Prometheus scans the folder recursively without following directory
   symlinks. Hidden files are included.
3. Every discovered file receives a relative path, byte count, SHA-256 digest
   when readable, broad category, and explicit analysis state.
4. The inventory opens immediately after scanning:
   - STEP/STP files are marked `ready`.
   - Recognized but unsupported engineering and document formats are marked
     `not_evaluated` with a reason.
   - Other files are marked `unsupported`.
   - Symlinks and unreadable files remain present with an explicit state.
5. If exactly one readable STEP/STP file exists, Prometheus loads it
   automatically. If there are multiple candidates, the inventory lets the
   user choose one; Prometheus does not guess which assembly is authoritative.
6. After a successful OCCT import, Prometheus runs the initial static-geometry
   screen and makes the results available from the main workspace.
7. The results show detected solid intersections or a bounded no-intersection
   result only when that check actually ran. They separately list unevaluated
   questions such as motion, material, mass, loads, restraints, and structural
   strength.

## Components

### Project intake scanner

`desktop/app/project_intake.*` owns a narrow, session-only representation of
the selected folder. The pure scan function performs recursive enumeration,
single-pass SHA-256 hashing, classification, deterministic relative-path
ordering, and summary counts. `ProjectIntakeController` runs that function off
the UI thread and exposes the result to QML.

The inventory is deliberately not added to the strict v2 project schema or the
immutable run store. Persistence will be designed after real project trials
show which metadata is needed. Source files are never modified or copied.

### Inventory panel

`desktop/ui/ProjectInventoryPanel.qml` shows the project root, total files,
ready files, not-evaluated files, unsupported/unreadable files, and a row for
each artifact. A STEP row can be loaded explicitly. The existing main window
adds the folder chooser and exposes `ProjectIntakeController` as a context
property.

### Initial mechanical screen

`EngineeringController` retains authority for geometry findings. Its initial
screen consumes only facts already produced by `CadController`:

- exact static B-Rep intersections when OCCT computed them;
- a bounded informational no-intersection result when computation completed
  with no intersections;
- no result, plus an explicit unknown, when static intersection computation
  was deferred;
- motion unknown unless a reviewed joint sweep was run;
- material, mass, load/restraint, and structural-strength unknowns in this
  slice.

Unknowns are session-level coverage statements, not persisted engineering
findings and never converted into a pass. Existing classified intersection
behavior and sampled-joint results remain unchanged.

## Error behavior

- A nonexistent or non-directory selection fails without changing the last
  successful inventory.
- An unreadable or changing file remains visible and has no digest; it does not
  abort accounting for other files.
- An empty folder succeeds with zero accounted files and a visible explanation.
- Multiple STEP files never trigger an automatic choice.
- A missing OCCT adapter, malformed STEP file, deferred collision check, or
  failed import never produces a geometry pass.
- Folder selection is disabled while a scan is running. This prototype does
  not add cancellation or concurrent scan scheduling.

## Testing and verification

The implementation follows test-first development with a deliberately small
test surface:

1. A scanner test covers nested and hidden files, deterministic ordering,
   hashes, classification, symlink visibility, empty folders, and invalid
   roots.
2. An engineering-controller test covers detected intersections, a genuine
   zero-intersection screen, deferred static computation, and visible unknowns.
3. An offscreen QML test proves that the inventory panel instantiates and its
   STEP action calls the existing CAD import boundary.
4. The existing OCCT motor-arm fixture proves folder-to-import-to-finding
   behavior locally.
5. The existing OpenArm project is used as a large-assembly trial and must show
   deferred interference as unknown rather than pass.
6. After focused local tests pass, the feature branch runs one Windows
   checkpoint. Routine edits do not rerun the former nine-job release matrix.

## Timebox and deferrals

Implementation checkpoints target 60–90 minutes. Work is included only if it
unlocks a new real project, a new trustworthy failure class, or meaningfully
reduces setup time. The following remain deferred until project evidence
justifies them:

- artifact-store migration and immutable folder snapshots;
- parser process isolation and archive expansion;
- PDF, spreadsheet, BOM, proprietary CAD, and source-code semantics;
- automatic assembly selection among multiple CAD files;
- generalized semantic graph and planner contracts;
- CalculiX packaging, meshing, and structural result normalization;
- cross-platform release matrices, installer recovery, exhaustive failure
  injection, and threat-model expansion.

## Acceptance criteria for this slice

- Selecting a folder accounts for every discovered file without silently
  dropping unsupported formats.
- Every readable regular file has a SHA-256 digest and byte count.
- Exactly one STEP file loads automatically; multiple STEP files require a user
  choice.
- A successfully imported small assembly produces truthful static-interference
  findings or a bounded zero-intersection result.
- Deferred or unavailable analysis is prominent as unknown and never appears
  as a pass.
- The workflow is reachable in the existing desktop without using a CLI.
- Focused native tests pass locally and the Windows-first build path is
  exercised at the checkpoint.
