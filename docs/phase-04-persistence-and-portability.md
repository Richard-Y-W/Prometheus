# Phase 4 persistence and portability status

Phase 4 is active. Its objective is to let a supported project survive close,
reopen, interruption, relocation, and source changes without silently reusing
stale evidence.

## Checkpoint 1: verified structural-run relocation

A completed structural run can now be exported as an exact self-contained
directory with `prometheus_export_structural_archive MANIFEST DESTINATION`.
The operation:

- refuses an unverified or corrupt source archive;
- refuses to overwrite an existing destination;
- copies only the seven filenames declared by the closed archive contract;
- verifies all copied byte lengths, SHA-256 identities, canonical setup and
  manifest bytes, replayed DAT metrics, and recompiled findings;
- publishes only after verification through an atomic directory rename; and
- removes an unpublished temporary directory after failure.

The controller test relocates a completed child-process run and proves the
manifest identity remains unchanged. This is one evidence-bearing transport
primitive, not the Phase 4 exit gate.

## Checkpoint 2: project-anchored structural manifests

The project execution index now accepts a second, explicitly registered
committed-run contract for structural archives. It does not relabel structural
evidence as the older motor-analysis manifest. Before anchoring, the desktop
re-verifies the complete local archive and the transaction store independently
verifies the canonical manifest object, closed root and artifact set, safe
unique filenames, declared hashes, and registered media/schema identity.

Anchoring installs the immutable manifest in the content-addressed sidecar,
appends its exact reference and a `structural_run_anchored` event under the
existing exclusive writer lock, and atomically replaces the project index.
Repeating the operation is idempotent and records only a
`structural_run_invoked` display event. Close/reopen retains the reference and
resolves its exact canonical bytes. Unsafe manifests do not alter history.

This legacy checkpoint anchors the archive's root identity, including the
hashes and lengths of setup and solver artifacts. Checkpoint 3 supersedes it
for new desktop publications by embedding the complete artifact graph.

## Checkpoint 3: embedded structural archive graph

New structural publications now retain the complete verified run inside the
project's content-addressed sidecar. Each binary or text artifact is split into
bounded 700 KiB raw chunks and encoded as canonical immutable JSON objects. A
closed structural project-run manifest binds the original archive manifest,
all seven artifact identities, ordered chunk references, offsets, decoded
lengths, and complete reconstructed SHA-256 identities.

Publication independently decodes and hashes every supplied chunk graph before
installing it. It then installs the original archive manifest, chunks, and
project-run manifest under the existing exclusive transaction lock and commits
only the closed project-run reference to history. Missing, duplicated,
unreferenced, reordered, malformed, or forged chunks fail before the project
index changes. Retrying an interrupted or repeated publication is idempotent.

Reconstruction reads only verified content-addressed objects, rebuilds each
artifact, checks its exact length and SHA-256 identity, writes through a new
sibling temporary directory, and publishes by atomic rename. Tests cover a DAT
artifact spanning multiple chunks, byte-identical reconstruction of every
file, full structural offline replay after project close/reopen, forged and
missing chunk rejection, changed source rejection, interruption immediately
before project replacement, and successful recovery by retry.

The desktop packages and publishes this graph asynchronously. The older
manifest-only contract remains readable for prototype history but is no longer
used for new desktop structural runs.

## Checkpoint 4: project-history reconstruction and result restoration

The structural controller now enumerates embedded structural runs whenever a
project opens or saves. Motor replay ignores these distinct manifests instead
of misreporting them as failed motor analyses. From the structural panel, a
user can select an embedded run, reconstruct it asynchronously into a new
output directory, re-verify the archive and DAT replay, restore the submitted
volume mesh from the exact retained CalculiX deck, and rebuild the deformed
stress visualization without the original local run directory or CalculiX.

The generated-deck parser now distinguishes the `*NODE` definition from the
later `*NODE FILE` output request; a regression test proves a generated deck
round-trips its exact node and tetrahedron counts. The desktop integration test
publishes a child-process run, creates a fresh project/controller pair, lists
the embedded history, restores it through the ordinary controller action, and
receives a verified result geometry.

## Checkpoint 5: editable reviewed-setup restoration

Structural setup evidence now retains the exact surface-patch grouping angle in
addition to exact face/node identities. After reconstructing an embedded run,
the desktop rebuilds the volume boundary and deterministic patches at that
angle, proves the stored load and restraint faces map completely back to those
patches, restores every reviewed material, force, restraint, requirement, mesh,
scenario, and provenance field, recompiles the authoritative request, and
repopulates the form, findings, extrema, limitations, and result view.

The restored workflow is runnable only if the ordinary current validators still
accept it. Missing or changed selections cannot silently become defaults. The
desktop integration test proves a fresh controller restores both exact selected
surface roles and a `canRun` reviewed setup from embedded project history.

## Checkpoint 6: assembly dependency binding and stale restoration

Every new embedded structural project-run manifest now closes over the exact
project assembly SHA-256 present at publication. The transaction rechecks that
identity while holding the project writer lock, so a run packaged against a
different or concurrently changed assembly cannot enter current history. An
already committed historical run remains idempotently readable.

On reopen, structural history compares its bound assembly identity with the
current project snapshot. A changed assembly is labeled `STALE SOURCE`.
Historical setup, findings, raw evidence, and visualization remain restorable,
but request compilation is revoked, `canRun` is false, and an explicit
`source_artifact_changed` blocker requires geometry and surface review before a
new execution. Tests cover mismatched publication rejection and viewable but
non-runnable stale restoration after an assembly identity change.

## Checkpoint 7: complete portable project bundle

The desktop can now export the open project into a new portable directory. The
bundle contains a canonical project index with a relative CAD source path, the
exact source CAD bytes, and only the immutable sidecar objects reachable from
the project's package, scenario, motor-run, and structural-run history. A
canonical bundle manifest closes over the project index, CAD source, and every
reachable object's registered identity. Existing destinations are never
overwritten, and a temporary export is published by directory rename only
after complete verification.

The verifier rejects changed project or CAD bytes, unsafe relative source
paths, missing or conflicting objects, undeclared reachable objects, and
declared objects that are not reachable from the project. The transaction test
moves the complete directory, opens the relocated project, reconstructs an
embedded multi-chunk structural run, and byte-compares all retained artifacts.
It also proves that a changed source CAD file prevents export without
publishing a partial destination.

This is automated clean-location evidence on the supported Windows build. A
physical clean-machine user trial remains part of the Phase 4 exit gate.

## Checkpoint 8: immutable accounted-folder inventory

Every successful folder scan can now produce a canonical, content-addressed
inventory snapshot. It retains each relative path, byte length, SHA-256 identity
when readable, classification, analysis state, and the explicit explanation for
that state. Unsupported, not-yet-evaluated, symlinked, and unreadable files stay
visible; the snapshot does not convert accounting into semantic understanding.

When the scanned folder contains the open project's exact current CAD source,
the desktop anchors the snapshot to project history. This works whether the
project already existed at scan time or is saved afterward. An unrelated or
changed folder cannot be attached merely because it was scanned. Repeated scans
of identical content reuse the immutable snapshot, and the inventory panel shows
the anchored identity. Inventory roots are distinct from analysis runs, so run
counts and replay views remain accurate.

The portable-bundle graph now treats anchored inventory snapshots as reachable
project evidence. Its relocation test opens the moved project and reads back the
exact canonical inventory bytes alongside the embedded structural run. This
checkpoint transports the accounting record, not every non-CAD source byte.

## Checkpoint 9: inventory change classification and scoped invalidation

A rescan now compares the new canonical accounting record with the latest
anchored snapshot by relative path, byte length, SHA-256 identity, and analysis
state. The inventory panel reports added, missing, changed, and state-changed
files. Identical snapshots are not recommitted.

The comparison distinguishes dependency from mere co-location. A changed PDF,
BOM candidate, source file, or other currently unbound artifact creates a new
inventory snapshot and visible change record without revoking structural runs
that never consumed it. A changed or missing selected CAD source immediately
marks the assembly identity stale, refuses to anchor the changed snapshot as a
valid project state, clears the runnable structural request, and adds an
explicit surface-review blocker. Completed historical results remain viewable.
Restoring the exact CAD bytes can restore the source identity, but the cleared
structural request still requires ordinary geometry and surface review before a
new run.

Tests prove non-CAD evidence changes preserve current assembly state and run
counts, while a CAD identity change emits the invalidation boundary and retains
the last two valid inventory snapshots rather than laundering changed source
bytes into project history.

## Checkpoint 10: immutable pre-execution project snapshots

Every genuinely new motor or embedded structural publication now stores a
canonical immutable snapshot of the complete project index immediately before
the run enters history. The snapshot binds its exact project-index SHA-256,
execution kind, and pending run-manifest identity. It therefore preserves the
active component/package bindings, scenario, CAD and geometry state, reviewed
engineering state, prior inventory roots, and earlier run history that formed
the execution boundary.

The snapshot and run are committed by the same locked atomic project-index
replacement. Failure before replacement leaves neither reference in project
history; a successful retry reuses installed immutable objects. Retrying an
already committed run adds no second snapshot. Snapshot roots are not presented
as analysis runs in the desktop, and motor/structural history controllers ignore
their distinct registered schema.

Portable-bundle verification checks the embedded project bytes and hash, proves
the pending run is committed, and recursively retains every immutable object
reachable from the captured pre-execution state. Tests inspect a motor snapshot
and prove it contains the package binding and scenario but no not-yet-published
run, while the relocated structural bundle carries its execution snapshot.

## Checkpoint 11: bounded inert source-evidence archive

Successful project-folder intake now prepares a closed content-addressed archive
alongside the inventory snapshot. Readable non-CAD files are split into canonical
700 KiB chunks and independently reconstructed and hashed before publication.
The current prototype policy retains at most 32 MiB per file and 128 MiB per
inventory. Files beyond either limit, unreadable/symlinked files, Prometheus's
own project/sidecar state, and the separately transported selected CAD source
remain identity-only with an explicit `external_only` reason.

Recognized documents, tables, and structured data are retained as inert portable
evidence. Unknown formats and source code are retained with a `quarantined`
disposition: Prometheus never executes or automatically previews them. On
explicit reconstruction, ordinary retained files are written below `retained/`;
quarantined files are written below `quarantine/` with the neutral
`.prometheus-quarantined` suffix.

The inventory snapshot, archive manifest, and chunks publish under one project
writer lock and one atomic index replacement. The manifest must account for
every inventory path and exactly reproduce each retained identity; missing,
extra, reordered, forged, or unreferenced chunks are rejected. Interrupted
publication leaves the prior index untouched and retry reuses verified immutable
objects.

Portable bundles recursively include the evidence archive graph. The relocation
test moves the whole bundle, verifies it, reconstructs the retained PDF
byte-for-byte, reconstructs unknown executable-shaped bytes only under the
neutral quarantine name, and confirms the CAD source is not duplicated into the
generic evidence output.

## Still required

- add user-facing backup/restore and migration flows beyond the existing atomic
  interrupted-write recovery primitives;
- bind non-CAD inventory/evidence dependencies and invalidate only their correct
  downstream setup, request, or result state when they change or disappear;
- prove relocation on a separate supported clean machine; and
- refine archive/quarantine policy from additional real-project formats and
  user evidence.
