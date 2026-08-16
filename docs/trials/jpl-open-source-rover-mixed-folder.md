# NASA JPL Open Source Rover mixed-folder trial

## Purpose

Exercise Prometheus against a genuinely heterogeneous engineering repository,
not a curated single-assembly demo. This is the third independent project in
Phase 2 and specifically tests inventory scale, duplicate visibility, format
boundaries, and ambiguous CAD selection.

## Frozen source

- Project: [NASA JPL Open Source Rover](https://github.com/nasa-jpl/open-source-rover)
- Revision: `0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7`
- `LICENSE.txt` SHA-256:
  `74227c34e68957a55d4d16091aeca5bcd240ec15883e5dee71f4b25139064413`
- Prepared as a clean `git archive` snapshot without repository internals

The repository combines mechanical instructions, DXFs, STL files, electrical
PCB sources, 23 STEP/STP component models, CSV parts data, PDFs, images,
generated manufacturing files, configuration, and source code. It does not
contain one obvious top-level neutral-format rover assembly.

## Reproduce

```powershell
.\scripts\prepare-jpl-rover-trial.ps1
.\scripts\run-jpl-rover-trial.ps1
```

The run script builds Windows Release, runs the production folder scanner in
measurement mode, and opens the unchanged snapshot through the ordinary
project-folder startup path.

## Observed Windows Release result

| Observation | Result |
| --- | --- |
| Snapshot size | Approximately 623 MB |
| Accounted files | 967 |
| STEP/STP candidates | 23 ready |
| Recognized but not evaluated | 58 |
| Unsupported | 886 |
| Unreadable | 0 |
| Exact duplicate copies | 191 |
| Production scan time | 17,998 ms |
| Automatically selected assembly | None |
| Desktop after intake | Running and responsive |

Prometheus correctly refused to guess among 23 STEP component files. This
avoids silently presenting a Raspberry Pi, connector, or PCB component model
as the rover assembly. Every file remained accounted for and no unreadable
artifact disappeared.

## Consequential findings

1. **Selection is semantically ambiguous.** The repository's mechanical rover
   model is hosted in an external online CAD system, while checked-in STEP
   files are primarily electrical component models. Filename and file size are
   insufficient evidence of project authority.
2. **The original inventory was operationally correct but hard to use.** The
   23 load controls were distributed through a 967-row list. This trial caused
   Prometheus to add path/category/state search and All, Loadable STEP,
   Recognized, Unsupported, and Duplicate filters with a visible match count.
3. **Unsupported dominates the first screen.** Images, PCB fabrication files,
   archives, and tool-specific files are honestly visible, but 886 red rows
   can obscure the 58 artifacts that may matter to an engineer.
4. **Duplicates are material.** The 191 exact duplicate copies show that
   content identity is useful, but later UX should group copies without
   removing their paths or provenance.
5. **Intake is not semantic understanding.** Prometheus cannot connect the
   parts lists, instructions, electrical design, external Onshape assembly,
   and checked-in component models into one rover graph.

## Current decision

The trial passes the bounded intake test and fails the broader usability and
semantic test in useful, explicit ways. No checked-in STEP file is authorized
as the rover assembly, and no engineering analysis should be run merely to
make the project look supported. Phase 2 should use this evidence to prioritize
artifact filtering/grouping and authoritative assembly declaration; it should
not trigger a universal parser project.

An outside-user session is still required. That session should measure whether
the user can find `Loadable STEP`, understand why no assembly was selected, and
state that the rover has not been mechanically evaluated.
