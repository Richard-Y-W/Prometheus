# Prometheus master roadmap

The [approved general-engineering design](../superpowers/specs/2026-08-11-prometheus-general-engineering-platform-design.md) defines Prometheus as a local project compiler and solver-orchestration environment. The roadmap advances by evidence-bearing capability gates. A broad intake goal does not permit Prometheus to claim that an arbitrary project works when its parsers, semantic model, requirements, boundary conditions, solvers, or validation coverage are incomplete.

## Program sequence

| Program | Capability gate | Exit evidence |
| --- | --- | --- |
| [01 — Trust kernel](#program-01--trust-kernel) | Reviewed evidence becomes immutable execution input and drives C++ decisions | Two packages that differ in one reviewed value produce the expected different C++ result and reproduce offline |
| [02 — Universal project intake](#program-02--universal-project-intake) | Every supplied artifact is hashed, classified, preserved, and visibly accounted for | No file disappears silently; malformed and opaque artifacts retain explicit states |
| [03 — Semantic engineering graph](#program-03--semantic-engineering-graph) | Cross-file engineering entities and relationships become reviewable | Inferred edges carry provenance and can be corrected without rewriting sources |
| [04 — Requirements, scenarios, and planning](#program-04--requirements-scenarios-and-planning) | Project intent compiles into proof obligations and reviewed analysis plans | Every planned analysis traces to an obligation; unsupported questions stay visible |
| [05 — Local solver runtime and SDK](#program-05--local-solver-runtime-and-sdk) | Isolated numerical engines execute through versioned contracts | A reference adapter proves version checks, cancellation, result validation, caching, and failure mapping |
| [06 — Six bounded domain slices](#program-06--six-bounded-domain-slices) | One narrow workflow exists for geometry, structures, thermal, circuits, CFD, and controls | Each slice passes analytic or solver benchmarks plus known-pass and known-fail project cases |
| [07 — Cross-domain orchestration](#program-07--cross-domain-orchestration) | Reviewed quantities move between domains with explicit mappings | Representative coupled chains converge and reproduce from their manifests |
| [08 — Validation and calibration](#program-08--validation-and-calibration) | Prediction error and coverage are measured against reference evidence | Release thresholds hold and no unresolved critical false-negative regression remains |
| [09 — Product hardening and pilots](#program-09--product-hardening-and-pilots) | The Windows product is recoverable, portable, secure, and usable outside the development team | Pilot engineers reproduce consequential findings without developer intervention |
| [10 — Ecosystem](#program-10--ecosystem) | Third-party engines and component models extend stable contracts | A new capability ships without changing the compiler's core meaning |

Program 00, repository truth and benchmark-corpus maintenance, runs continuously. Programs 02 through 05 establish shared contracts sequentially. Program 06 domain adapters may proceed in parallel only after the Program 05 SDK is stable.

## Program 01 — Trust kernel

Program 01 prevents candidate evidence, stale state, or failed computation from becoming an engineering finding.

- **[01A — Integrity and contracts](01-trust-kernel/01a-amended-implementation-plan.md) — implementation present, verification pending:** the v2 code now provides stable claims, append-only review, immutable RFC 8785 objects, durable publication replay, database backstops, and independent C++ byte verification. The [amended completion record](01-trust-kernel/01a-amended-completion.md) remains pending until the complete release matrix is observed. These controls establish reviewed-input integrity only; the package is not an engineering result.
- **01B — Package-to-C++ execution:** remove fixed production PM-36 values, parse the bound package in C++, persist package and run manifests, and prove Motor A/Motor B sensitivity plus offline reproduction.
- **01C — Safe evidence acquisition:** preserve source bytes, add sandboxed deterministic parsers and optional candidate extraction, and retain explicit licensing and review state.
- **01D — End-to-end review and reproduction:** complete portable project manifests, native Windows CI, and reopen/reproduction acceptance tests.

Amended 01A is the current gate. The former v1 record remains [historical evidence](01-trust-kernel/01a-integrity-and-contracts.md), and its mutation/export endpoints are retired under the [migration guide](../migration/program-01a-v1-to-v2.md). Program 01B has not started and begins only after the amended release matrix verifies the v2 input boundary. 01C cannot begin until the repository proves that reviewed values, rather than fixture constants, drive the C++ result.

## Program 02 — Universal project intake

Build the content-addressed artifact store, parser-worker sandbox, format detection, quarantine states, archive limits, and portable project bundle. “Universal” means every file is accounted for; it does not mean every proprietary format is semantically understood.

## Program 03 — Semantic engineering graph

Represent components, geometry, BOM rows, materials, ports, joints, nets, firmware symbols, control signals, fluid regions, requirements, evidence, and measurements. Every inferred relationship retains its source, method, confidence, alternatives, and review state.

## Program 04 — Requirements, scenarios, and planning

Compile reviewed intent into measurable proof obligations. Add the capability registry and a planner that states what it can answer, what inputs are missing, which assumptions require review, and which questions remain unsupported.

## Program 05 — Local solver runtime and SDK

Define immutable analysis and result packages, isolated child-process execution, resource limits, cancellation, version checks, self-tests, caching, diagnostics, and normalized failure states. Each analysis names one authoritative computation backend under [ADR-0006](../adr/0006-authoritative-analysis-backends.md).

## Program 06 — Six bounded domain slices

Deliver narrow, benchmarked recipes for geometry and kinematics, structural mechanics, thermal analysis, electrical circuits and power, CFD, and controls/system dynamics. Domain breadth is not evidence of unrestricted physics coverage.

## Program 07 — Cross-domain orchestration

Move dimensioned, reviewed quantities between independently validated recipes. Initial chains include electrical-to-thermal, fluid-to-thermal, and control-to-mechanical mappings with explicit iteration and convergence records.

## Program 08 — Validation and calibration

Maintain analytic, cross-solver, manufactured-solution, and physical-reference corpora. Measure false-negative and false-positive behavior by capability and validation level; do not collapse software test coverage into physical validation.

## Program 09 — Product hardening and pilots

Add parser isolation, recovery, signed packaging, installer/update controls, access policy, audit logs, performance budgets, accessibility, and external pilot workflows. Release gates depend on recovery and reproduction evidence, not feature count.

## Program 10 — Ecosystem

Stabilize adapter and component-package extension points, licensing boundaries, signed third-party artifacts, commercial solver connectors, and optional cloud execution. Extensions inherit the same applicability, validation, and failure-state rules as built-in capabilities.
