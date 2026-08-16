# Phase 4 clean-machine portability and recovery trial

Status: required external evidence. Do not mark Phase 4 complete from local or
CI results alone.

## Purpose

Prove that a supported Prometheus project survives conversion, close/reopen,
portable export, transfer to another supported Windows machine, source-change
detection, and recovery without developer repair or silent reuse of stale
results. This does not certify a design, physically validate CalculiX, or
establish general CAD/PDF/BOM support.

## Required environment

- Machine A creates the bundle.
- Machine B is a supported clean Windows machine that has never opened it.
- Prefer an operator outside the core implementation work.
- Record the exact Prometheus commit, Release artifact SHA-256, OS, CPU/RAM,
  UTC start/end time, source revisions/hashes, and every diagnostic code.

Do not copy build trees, source worktrees, developer configuration, or
absolute-path sidecars to Machine B. Transfer only the normal application
distribution and exported portable bundle.

## Project matrix

Run the complete workflow with the bounded YUBI structural project. Run the
applicable intake/open/close/portable checks for the OpenArm and JPL Rover
shapes in
[Program 01D](../program/01-trust-kernel/01d-multi-project-evidence.md).
Do not invent an assembly for JPL when none is authoritative.

| Shape | Required evidence |
| --- | --- |
| YUBI gripper | Saved structural setup, findings, raw archive, inventory, limitations, and unknowns reopen on Machine B. |
| OpenArm 2.0 | Large hierarchy and inventory reopen without losing degraded or unsupported states. |
| JPL Rover | Mixed-folder inventory remains complete and assembly ambiguity remains explicit. |

For each trial retain screenshots of the opened project, inventory, restored
setup, findings, and blockers; bundle/project/source SHA-256 identities; exact
reproduction steps for failures; and whether developer intervention occurred.

## A. Current v2 close/reopen

1. Open the folder and account for every file.
2. Open or create its `.prometheus` project through the normal UI.
3. For YUBI, record the assembly hash, selected surfaces, material, loads,
   restraints, mesh settings, findings, limitations, and archive manifest hash.
4. Close Prometheus completely, relaunch, and reopen the project.
5. Compare every recorded field and identity.
6. Confirm historical findings remain viewable and a runnable setup appears
   only when ordinary validators accept the restored state.

Pass: supported state and immutable identities agree exactly. Missing or stale
inputs remain blockers rather than becoming defaults.

## B. Legacy v1 conversion

1. Back up and hash a representative v1 project.
2. Open it and confirm it is display-only and requires **Save As v2**.
3. Attempt to save over the source and confirm rejection.
4. Save to a new destination, close, reopen, and compare placements,
   connections, interferences, reviewed joint, geometry findings, and opaque
   legacy engineering display state.
5. Confirm the source bytes are unchanged and no historical calculation was
   fabricated as a v2 run.

Pass: supported state survives, unsupported legacy state remains visibly
non-authoritative, and the original bytes never change.

## C. Portable export and clean-machine restore

1. On Machine A choose **Export portable project bundle** into a new folder.
2. Record the bundle manifest, project index, CAD source, and transfer hashes.
3. Transfer the complete folder to Machine B without modification.
4. On Machine B choose **Restore portable project bundle**, select a new
   destination parent, and let Prometheus open the restored project.
5. Compare inventory, assembly hash, component mappings, structural setup,
   findings, raw outputs, archive identities, limitations, and unknowns.
6. Close and reopen it on Machine B and repeat the comparison.
7. Confirm it uses bundle-relative data and no Machine A-only path.

Pass: exact supported state and evidence reopen without developer repair, and
the destination appears only after complete verification.

## D. Tamper and stale-source behavior

Use disposable copies.

1. Change selected CAD bytes. Confirm new structural execution is revoked,
   historical results remain viewable, and stale assembly evidence is named.
2. Restore exact CAD bytes. Identity may become current, but cleared review
   selections must not be silently reconstructed.
3. Change an unbound PDF/BOM/source file. Confirm inventory reports it without
   invalidating a result that never consumed it.
4. Remove or alter a declared bundle object. Restore must reject it and publish
   no destination.
5. Add an undeclared bundle file and confirm the same fail-closed result.

Pass: invalidation follows actual dependencies rather than folder proximity,
and tampered bundles never produce partial restored projects.

## E. Previous-index recovery

Use a disposable project that has completed at least one save.

1. Record current and `.project-index.previous` hashes.
2. Confirm **Recover damaged Prometheus project** refuses to roll back a valid
   current index.
3. Corrupt only the current index and recover again.
4. Confirm the retained predecessor is validated, restored, and opened.
5. Corrupt both indexes. Confirm recovery fails without inventing or
   overwriting state.

Pass: valid state cannot be rolled back, damaged state recovers only from a
valid predecessor, and failed recovery is non-mutating.

## Gate decision

Phase 4 closes only when all applicable procedures pass on the supported
Release build, the complete YUBI workflow opens on Machine B, limitations and
failures are committed as evidence, no developer-only repair was needed, and
CI plus local tests are green at the tested commit. A failure returns to the
smallest relevant implementation checkpoint; it does not justify proceeding
to Phase 5 as though portability were established.
