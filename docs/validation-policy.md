# Validation policy

Software verification, contract testing, analytic comparison, cross-solver comparison, manufacturer comparison, physical validation, and system validation are different evidence levels. A passing unit test verifies code behavior under its test inputs; it does not validate a physical model or certify a design.

## Result states

Project obligations resolve to `satisfied`, `violated`, `indeterminate`, `not_applicable`, or `not_evaluated`. The C++ decision core reduces obligation counts and execution state into a project verdict and coverage. It may report `satisfied_within_scope` only when at least one applicable obligation exists, none is violated or unresolved, and execution completed. A confirmed violation takes precedence; unresolved or absent work keeps the verdict indeterminate.

A parser failure, missing critical input, unresolved assumption, unsupported regime, missing backend, process failure, timeout, invalid result contract, or nonconverged solve cannot produce `satisfied`. Errors remain errors; absent execution remains `not_evaluated`.

## Evidence and review

Critical inputs require source provenance, units, applicability conditions, limitations, and explicit review. V2 review binds a revision-scoped claim ID and fingerprint through an append-only event at an expected draft version. Human acceptance records a local decision and accountability label; it does not establish source truth, applicability, safety, or physical validation. The current reviewer label is not authenticated.

Evidence class, source authority, and physical-validation status are independent. `extraction_confidence` estimates extraction reliability only and must never be presented as generic engineering confidence. Derived evidence retains its parents; measurements and validation observations retain their method, time, and provenance.

Synthetic fixtures test contracts and failure behavior. They cannot support a claim about a real manufacturer's component. Real STEP import tests geometry handling; it does not establish material, mass, interface, contact, restraint, or load-path truth.

## Package integrity

Draft relational state is authoritative before publication. A `sealed_v2` revision is bound to one immutable stored RFC 8785 object, and export must verify and return those exact bytes. The SHA-256 object identity and strong `ETag` are external to the package.

This is a byte-integrity claim, not an engineering claim. The execution-component package is reviewed input and contains no requirement verdict, solver result, finding, pass, failure, certification, or physical-validation conclusion. A successful publication may truthfully carry `execution_readiness=blocked`.

Legacy v1 publications without authoritative bytes remain `legacy_unsealed`. They cannot be reconstructed and presented as sealed v2 objects or silently upgraded by migration.

## Computation authority

Each analysis names one versioned authoritative computation backend. A built-in lightweight check may be C++; a numerical analysis may name an isolated external solver. Orchestration must not independently reproduce or silently alter the authoritative calculation.

Python compiles and validates the reviewed input package but may not issue a production engineering verdict. The independent C++ integrity library verifies canonical bytes, hash, and supported schema identity but does not validate physical truth. The Qt-free C++ decision core owns the normalized project-summary verdict and coverage. No external solver adapter exists in the current repository, and the fixed C++ motor checker does not consume the v2 package until Program 01B.

## Reproducibility record

Every future production analysis must retain source artifact hashes, schema versions, reviewed component revisions, requirements, scenarios, assumptions, boundary conditions, backend and adapter identities, executable hashes, settings, raw outputs, diagnostics, convergence state, normalized results, cache state, and timestamps. Program 01A currently retains only the bounded fixture source, reviewed input graph, sealed package object, revision binding, and durable publication response.

A SHA-256 identity detects drift within its declared bytes and metadata. It is not a signature, authorization mechanism, proof of origin, or defense against an attacker who can replace both application and database controls.

## Claim limit

Prometheus makes no certification or project-wide correctness claim. `satisfied_within_scope` is permitted only when the report names the assessment scope, analysis coverage, validation levels, unresolved unknowns, and invalidating conditions. Program 01A cannot emit that result from package publication because no engineering execution occurs in this program.
