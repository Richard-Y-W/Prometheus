# Validation plan

## Current automated coverage

This section records the former Program 01A implementation. The amended release gate is in progress and additionally requires shared Python/C++ RFC 8785 vectors, stable claims, immutable objects, SQLite/PostgreSQL concurrency and restart tests, the supported Python matrix, and a full OCCT-disabled desktop build.

The backend suite covers isolated database state, exact fixture identity and source attribution, typed values, Alembic upgrade/downgrade behavior, atomic per-field review, publication rollback, canonical hashing, schema references, persisted-input tamper detection, retired legacy routes, and checked-in OpenAPI equality.

The frontend suite checks historical-finding presentation and the explicit disabled-state explanations; its production build type-checks the archived viewer. C++ core tests cover the historical motor equations and selected geometry/project behavior in environments with CMake. The Program 01A Qt review-payload target covers exact decision-set validation when built with Qt.

Real Open Cascade STEP/XDE import and native geometry tests exist on the documented Windows toolchain. The optional OpenArm case is an import/rendering stress test, not an engineering validation case.

## Missing validation

- Program 01B has not shown that reviewed package values drive C++ results or reproduce after reopen.
- No public document acquisition/parser, general artifact inventory, semantic graph, requirements compiler, capability planner, or coverage engine exists.
- No external solver adapter, analytic cross-check corpus, cross-solver benchmark, manufactured solution, or physical validation result exists for the planned six domains.
- Qt review behavior and Open Cascade paths require native Windows verification; headless tests do not cover them.
- Browser pointer-event automation and packaged-product recovery tests remain absent.

Program 08 will set per-capability error thresholds and false-negative regression gates. Until then, contract-tested means the software rejects and preserves data as specified; it does not mean the physical model is validated.
