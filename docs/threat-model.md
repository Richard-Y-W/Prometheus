# Threat model

## Current Program 01A boundary

Program 01A is reopened under the amended gate. The controls below describe the former v1 implementation that remains in the worktree; they do not yet provide immutable stored package bytes, stable claim-ID review, durable publication replay, or independent C++ canonical verification.

The checked-in PM-36 source artifact is project-controlled synthetic JSON. Fixture mode accepts one exact identity, rejects caller-supplied source URLs, verifies the source-byte hash, and creates no records for unsupported requests. Typed validation rejects non-finite values, invalid ranges, invalid curves, duplicate identities, and absent evidence references. Canonical hashing detects drift between a published revision and its exported execution package.

These controls do not provide signatures, user authorization, process isolation, or protection against an attacker who can replace both persisted data and hashes. The local development API uses HTTP. The current research provider does not fetch remote URLs, so it has no production SSRF surface; a future acquisition worker must not inherit that conclusion.

Real STEP input is untrusted. The current importer checks extension/signature and size at the Python prototype boundary, while the native Open Cascade parser still runs in the desktop process. Active parser interruption, memory/CPU isolation, archive containment, and parser crash containment are not implemented.

No external solver or cloud adapter exists. Therefore Program 01A neither uploads project data nor executes solver binaries.

## Required future controls

Programs 02, 05, and 09 must add scheme/host policy, redirect and download budgets, content/signature checks, safe artifact names, archive expansion limits, sandboxed parser workers, process CPU/memory/time limits, cancellation, project-scoped authorization, signed packaging, recovery, and audit records.

Source documents remain data, never instructions. Downloaded macros, CAD scripts, embedded binaries, supplied source code, and solver decks must not execute during parsing. Any future external or cloud transfer requires explicit disclosure of artifacts, destination, credentials, retention, and licensing terms.
