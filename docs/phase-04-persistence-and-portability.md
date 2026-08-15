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

## Still required

- commit reviewed structural setup and run objects to the project transaction
  store;
- restore the complete supported workflow after close/reopen;
- persist full folder inventory identities and selected CAD/component mapping;
- invalidate setup, request, and results when their source geometry or evidence
  changes or disappears;
- snapshot projects at execution boundaries;
- export and import a complete project plus content-addressed sidecar;
- prove interrupted save recovery and clean-machine relocation; and
- define archive/quarantine behavior from encountered real formats.
