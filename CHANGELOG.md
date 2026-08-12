# Changelog

## Unreleased

- Reopened Program 01A after the former fixture-backed boundary was found to reconstruct published content, review mutable field labels, use a Python-specific canonical form, and lack durable publication replay.
- Breaking at the amended release: v1 review, publication, and reconstructed package-export routes are retired with `410 Gone`; clients must adopt the stable-claim and immutable-object v2 boundary described in the [v1-to-v2 migration guide](docs/migration/program-01a-v1-to-v2.md).
- The amended completion gate remains pending. These entries move to a dated release section only after the full release matrix passes.
