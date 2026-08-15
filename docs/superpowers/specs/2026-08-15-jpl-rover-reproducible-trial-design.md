# JPL Rover Reproducible Trial Design

## Problem

The repository records a Windows intake run for NASA JPL Open Source Rover at
revision `0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7`, but that record is not a
reproducible regression gate. The current PowerShell preparation script trusts
any pre-existing trial directory, and the runner accepts any successful scan
without checking the recorded 967-file inventory, the 23 ambiguous STEP
candidates, or the absence of an automatically selected assembly. The pinned
snapshot is also absent from the current Mac development machine.

This checkpoint must distinguish three claims:

1. the source snapshot is the pinned JPL revision;
2. the production scanner reproduced the expected inventory;
3. Prometheus correctly refused to select a primary assembly.

Passing this trial does not mean that Prometheus understands or mechanically
evaluates the rover.

## Scope

This checkpoint will:

- prepare the pinned JPL snapshot on macOS or Windows without placing its
  approximately 623 MB of source data in Git;
- reject partial, stale, or differently sourced snapshots;
- run the existing production `scanProjectFolder()` path;
- compare stable scanner output with a checked-in expectation record;
- keep scan duration observational rather than treating it as a fixed pass
  threshold;
- preserve the existing Windows entry points as thin wrappers;
- document the new gate beside the earlier Windows observation and add it to
  the Program 01D remaining-work list.

This checkpoint will not:

- add the external Rover repository to ordinary CI;
- choose one of the 23 STEP files as the rover assembly;
- parse the external Onshape assembly, PCB sources, BOM semantics, or manuals;
- claim mechanical, electrical, or project-wide correctness;
- modify the structural-analysis work running on another branch.

## Approaches Considered

### Keep the PowerShell scripts and add comparisons

This is the smallest Windows change, but it leaves the recorded trial
unrunnable on the current Mac and creates no shared behavior for future host
platforms.

### Maintain separate PowerShell and shell implementations

This makes both current development hosts runnable, but commit pins,
expectations, and replacement behavior can drift between two scripts.

### Use one CMake script with thin host wrappers

This is the selected approach. CMake and Git are already project prerequisites
on both hosts. One script can prepare the archive, invoke the selected desktop
build preset, run the production scanner, and compare JSON without introducing
a new package dependency. PowerShell remains a convenience wrapper rather than
an independent implementation.

## Design

### Pinned expectation record

`docs/trials/jpl-open-source-rover-expectations.json` will contain:

- the exact Git revision;
- the expected license-file SHA-256;
- a root-independent inventory SHA-256;
- total, ready, not-evaluated, unsupported, unreadable, and duplicate-copy
  counts;
- the expected empty primary STEP selection.

The inventory identity will hash the scanner's sorted relative path, byte
length, and content SHA-256 for every accounted artifact. Absolute paths and
elapsed time are excluded so macOS and Windows can reproduce the same identity.
The classification counts are asserted separately so a scanner-classification
change cannot hide behind unchanged source bytes.

### Portable preparation

`scripts/jpl-rover-trial.cmake` will own preparation and verification. It will:

1. locate the repository from the script path rather than the caller's current
   directory;
2. reuse or create the cached JPL Git clone under `out/external-demo`;
3. verify that the pinned commit object exists, fetching only when absent;
4. create a clean Git archive for that commit;
5. extract into a sibling staging directory;
6. validate the staged license hash before promotion;
7. replace the ignored trial directory only after staging succeeds;
8. write the pinned revision to a sidecar beside, rather than inside, the trial
   directory so the 967-file inventory is unchanged;
9. retain the last valid trial if clone, archive, extraction, or validation
   fails.

A valid prepared snapshot can be reused. Verification still scans and checks
its complete inventory identity, so an altered cache cannot pass merely because
its directory exists. An explicit refresh option will rebuild the snapshot.

### Production scan assertion

The existing `prometheus_project_intake_tests --scan-only` mode will add an
`inventory_sha256` field. The digest implementation will be exercised on small
temporary folders in the existing project-intake test binary before it is used
for the external trial.

In verify mode, the CMake driver will configure and build the caller-selected
desktop preset, execute that exact scanner binary, parse its compact JSON, and
fail on:

- a nonzero scanner exit;
- malformed or missing JSON;
- any expectation mismatch;
- any unreadable file;
- a nonempty primary STEP path.

The driver will print the verified summary and trial location. Opening the GUI
will remain a separate optional action so an automated trial cannot hang while
waiting for a desktop window to close.

### Host entry points

The documented direct entry point will be:

```text
cmake -DPROMETHEUS_JPL_MODE=verify -P scripts/jpl-rover-trial.cmake
```

The driver will default to `desktop-no-occt-debug` on macOS/Linux and
`windows-release` on Windows, while accepting an explicit preset override. The
existing `.ps1` files will call the same driver and may launch the desktop only
after verification succeeds.

## Failure Handling

- A network failure while the pinned commit is absent leaves any prior valid
  snapshot untouched and reports that preparation is incomplete.
- A wrong revision, wrong license hash, partial extraction, or inventory digest
  mismatch fails closed.
- Scanner crashes, unreadable files, malformed JSON, and unexpected automatic
  assembly selection fail the trial.
- A changed scanner classification requires an intentional expectation update
  backed by a reviewed trial result.
- The external trial remains opt-in and is not added to the ordinary test
  matrix; normal CI must not download 623 MB from a third party.

## Verification

Automated repository tests will establish that the inventory digest is sorted,
independent of the absolute root, and sensitive to path, size, or content
changes. A small CMake assertion module shared by the driver and an offline
script test will exercise both a matching JSON summary and a deliberate
mismatch without cloning the Rover repository.

The final local gate will then:

1. prepare the pinned snapshot on this Mac;
2. build the no-Open-Cascade desktop scanner;
3. reproduce all expected inventory fields;
4. record the measured macOS scan duration as an observation;
5. rerun from the cached snapshot to prove the ordinary path is repeatable.

The recorded result supports only deterministic project intake and an honest
ambiguity decision for this pinned repository revision.
