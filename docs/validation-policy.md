# Validation policy

Software verification, contract testing, analytic comparison, cross-solver comparison, manufacturer comparison, physical validation, and system validation are different evidence levels. A passing unit test verifies code behavior under its test inputs; it does not validate a physical model or certify a design.

## Result states

Project obligations resolve to `satisfied`, `violated`, `indeterminate`, `not_applicable`, or `not_evaluated`. Project summaries distinguish requirements violated, no violations detected within stated scope, insufficient coverage, and analysis blocked.

A parser failure, missing critical input, unresolved assumption, unsupported regime, missing backend, process failure, timeout, invalid result contract, or nonconverged solve cannot produce `satisfied`. Errors remain errors; absent execution remains `not_evaluated`.

## Evidence and review

Critical inputs require source provenance, units, applicability conditions, and explicit review. Program 01A requires a per-field decision before publication, but human acceptance is not physical validation. LLM confidence and extraction probability are not engineering confidence.

Synthetic fixtures test contracts and failure behavior. They cannot support a claim about a real manufacturer's component. Real STEP import tests geometry handling; it does not establish material, mass, interface, or load-path truth.

## Computation authority

Each analysis names one versioned authoritative computation backend. Prometheus C++ controls applicability, execution approval, result validation, acceptance criteria, coverage, and findings. Python may acquire and package candidate evidence but may not issue production engineering verdicts. No external solver adapter exists in the current repository.

## Reproducibility record

Every production analysis must retain source artifact hashes, schema versions, reviewed component revisions, requirements, scenarios, assumptions, boundary conditions, backend and adapter identities, executable hashes, settings, raw outputs, diagnostics, convergence state, normalized results, cache state, and timestamps. A content hash detects drift only within its declared canonical payload; it is not a signature or access-control mechanism.

## Claim limit

Prometheus makes no certification or project-wide correctness claim. `No violations detected within scope` is permitted only when the report names that scope, analysis coverage, validation levels, unresolved unknowns, and conditions under which the result no longer applies.
