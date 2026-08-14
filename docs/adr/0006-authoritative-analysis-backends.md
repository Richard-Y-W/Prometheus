# ADR-0006: Authoritative analysis backends

- Status: accepted
- Date: 2026-08-11

## Context

Prometheus must combine built-in checks with specialized numerical engines without creating two disagreeing implementations of the same calculation. It must also keep numerical output separate from the engineering decision that interprets requirements, applicability, convergence, uncertainty, and coverage.

## Decision

Prometheus C++ owns confirmed engineering state, review gates, applicability, result validation, acceptance criteria, coverage, and findings. Python may acquire artifacts and produce candidate evidence, but it may not approve evidence, choose production assumptions, or issue an engineering verdict.

Each numerical analysis declares exactly one versioned authoritative computation backend. A lightweight check may use a built-in C++ implementation. A structural, thermal, circuit, fluid, or controls analysis may instead use an external solver selected by the capability registry. The adapter and orchestration layers do not reproduce or silently alter that backend's calculation.

External solvers return numerical results and diagnostics. The C++ decision layer checks the declared capability, inputs, boundary conditions, execution state, convergence, and result contract before it produces a finding. A missing backend, failed process, invalid result, unresolved boundary condition, or nonconverged analysis resolves to `indeterminate` or `not_evaluated`, never `satisfied`.

## Consequences

- Every result manifest identifies the backend, backend version and executable hash, adapter version, inputs, outputs, diagnostics, and validation state.
- Solver-specific objects remain outside persisted Prometheus contracts.
- Python workers can accelerate evidence acquisition without becoming an alternate physics or verdict path.
- Adding a solver requires an applicability contract, validation corpus, failure mapping, and reproducibility test; installing an executable alone does not add a supported capability.

Program 01A establishes the evidence and execution-package boundary. Program 01B is the first gate that must prove a reviewed package changes a C++ result and reproduces after offline reopen.
