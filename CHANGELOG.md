# Changelog

## Unreleased

No entries.

## 2026-08-12 — Program 01A amended trust boundary

- Reopened Program 01A after the former fixture-backed boundary was found to reconstruct published content, review mutable field labels, use a Python-specific canonical form, and lack durable publication replay.
- Breaking at the amended release: v1 review, publication, and reconstructed package-export routes are retired with `410 Gone`; clients must adopt the stable-claim and immutable-object v2 boundary described in the [v1-to-v2 migration guide](docs/migration/program-01a-v1-to-v2.md).
- Closed the amended `contract_tested` gate at implementation commit `2491df33ad6ae9032ea71f7994a3f137599e2dba` after all nine required GitHub jobs passed. The [completion record](docs/program/01-trust-kernel/01a-amended-completion.md) keeps the package-integrity claim separate from engineering execution and physical validation.
