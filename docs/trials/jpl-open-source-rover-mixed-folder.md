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

Cross-platform preparation and verification:

```text
cmake -DPROMETHEUS_JPL_MODE=prepare -P scripts/jpl-rover-trial.cmake
cmake -DPROMETHEUS_JPL_MODE=verify -P scripts/jpl-rover-trial.cmake
```

Windows convenience entry points call the same CMake driver:

```powershell
.\scripts\prepare-jpl-rover-trial.ps1
.\scripts\run-jpl-rover-trial.ps1
```

The normal run verifies the production scanner and exits. Opening the desktop
is an explicit `-OpenDesktop` action so an automated run cannot hang on a GUI.

## Reproducible macOS gate — 2026-08-15

The gate was established from two fresh extractions of the same local
`git archive` at distinct absolute roots. Both production scans returned the
same inventory identity and every asserted count:

| Evidence | Result |
| --- | --- |
| Host | macOS 26.5.2 (`25F84`), arm64 |
| Toolchain | AppleClang 21.0.0.21000101; Qt 6.11.1; CMake 4.4.2 |
| Gate source state | `03c025a` |
| Scanner executable SHA-256 | `86f17b73553e3a56a2ecb6767bb3caf1a1e710f1a6fce2ab3e486fea7f02c484` |
| Inventory SHA-256 | `sha256:be8aebb1d1241579eeaa07ca64e582c88b7cf0617ff36c573e581d3e35689191` |
| Accounted files | 967 |
| STEP/STP candidates | 23 ready |
| Recognized but not evaluated | 58 |
| Unsupported | 886 |
| Unreadable | 0 |
| Exact duplicate copies | 191 |
| Automatically selected assembly | None |
| Distinct-root scan times | 2,895 ms and 3,109 ms |
| Consecutive cached verify scan times | 2,811 ms and 2,781 ms |

The first attempted preparation failed before promotion and exposed an error in
the earlier provenance note: the pinned tree contains `LICENSE.txt`, not
`LICENSE`. Direct inspection of the pinned Git object established its SHA-256
as `74227c34e68957a55d4d16091aeca5bcd240ec15883e5dee71f4b25139064413`.
The source contract and fixture were corrected before either inventory identity
was accepted.

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

The reproducible regression passes the bounded intake gate and fails the
broader semantic test in useful, explicit ways. It proves that this exact
967-file snapshot was completely accounted for and that Prometheus refused an
ambiguous automatic STEP choice. It does not prove that the rover CAD,
electronics, controls, mechanics, or complete system work. No checked-in STEP
file is authorized as the rover assembly, and no engineering analysis should
be run merely to make the project look supported.

An outside-user session is still required. That session should measure whether
the user can find `Loadable STEP`, understand why no assembly was selected, and
state that the rover has not been mechanically evaluated.
