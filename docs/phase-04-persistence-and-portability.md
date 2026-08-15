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

## Still required

- persist full folder inventory identities and selected CAD/component mapping;
- invalidate setup, request, and results when their source geometry or evidence
  changes or disappears;
- snapshot projects at execution boundaries;
- export and import a complete project plus content-addressed sidecar;
- prove clean-machine relocation; and
- define archive/quarantine behavior from encountered real formats.
