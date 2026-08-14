# Prometheus master roadmap

The [approved general-engineering design](../superpowers/specs/2026-08-11-prometheus-general-engineering-platform-design.md) defines Prometheus as a local project compiler and solver-orchestration environment. The roadmap advances by evidence-bearing capability gates. A broad intake goal does not permit Prometheus to claim that an arbitrary project works when its parsers, semantic model, requirements, boundary conditions, solvers, or validation coverage are incomplete.

## Current project-value sequence

The compiler architecture remains the long-term direction, but infrastructure
layers are no longer separate prerequisites to useful mechanical work. The
current sequence is:

1. **One real folder to one useful screen:** account for every file, load an
   unambiguous STEP assembly, report supported static geometry results, and
   show unknowns.
2. **Several materially different projects:** repeat the same interaction on
   at least three projects and record where import, semantics, checks, or UX
   fail.
3. **One real structural analysis:** select one component from an imported
   project, review material, loads, and restraints, and run CalculiX end to end.
4. **Extract shared contracts from evidence:** generalize only boundaries used
   by the working project workflows and at least two analysis adapters.
5. **Harden observed failures:** add persistence, parser isolation, recovery,
   portability, and broader matrices at explicit release checkpoints.

Ordinary implementation checkpoints target 60–90 minutes. Each must unlock a
new real project, a new trustworthy failure class, or materially reduce setup
time. Focused tests run for every checkpoint; complete CI, documentation,
portability, threat-model, and failure-injection gates run periodically rather
than after every small capability.

## Program sequence

| Program | Capability gate | Exit evidence |
| --- | --- | --- |
| [01 — Trust kernel](#program-01--trust-kernel) | Reviewed evidence becomes immutable execution input and drives C++ decisions | Two packages that differ in one reviewed value produce the expected different C++ result and reproduce offline |
| [02 — Intake generalization](#program-02--intake-generalization) | Generalize the proven folder inventory into persisted, isolated intake | No file disappears silently; malformed and opaque artifacts retain explicit states |
| [03 — Semantic engineering graph](#program-03--semantic-engineering-graph) | Cross-file engineering entities and relationships become reviewable | Inferred edges carry provenance and can be corrected without rewriting sources |
| [04 — Requirements, scenarios, and planning](#program-04--requirements-scenarios-and-planning) | Project intent compiles into proof obligations and reviewed analysis plans | Every planned analysis traces to an obligation; unsupported questions stay visible |
| [05 — First solver and runtime extraction](#program-05--first-solver-and-runtime-extraction) | A real CalculiX workflow runs before a general SDK is extracted | A project component completes one reviewed linear-static analysis; shared runtime boundaries follow a second adapter |
| [06 — Six bounded domain slices](#program-06--six-bounded-domain-slices) | One narrow workflow exists for geometry, structures, thermal, circuits, CFD, and controls | Each slice passes analytic or solver benchmarks plus known-pass and known-fail project cases |
| [07 — Cross-domain orchestration](#program-07--cross-domain-orchestration) | Reviewed quantities move between domains with explicit mappings | Representative coupled chains converge and reproduce from their manifests |
| [08 — Validation and calibration](#program-08--validation-and-calibration) | Prediction error and coverage are measured against reference evidence | Release thresholds hold and no unresolved critical false-negative regression remains |
| [09 — Product hardening and pilots](#program-09--product-hardening-and-pilots) | The Windows product is recoverable, portable, secure, and usable outside the development team | Pilot engineers reproduce consequential findings without developer intervention |
| [10 — Ecosystem](#program-10--ecosystem) | Third-party engines and component models extend stable contracts | A new capability ships without changing the compiler's core meaning |

Program 00, repository truth, and per-capability benchmark maintenance run
continuously. Programs 02 through 05 are no longer mandatory sequential
foundation work. Their shared contracts are extracted from the working
mechanical slices above.

## Program 01 — Trust kernel

Program 01 prevents candidate evidence, stale state, or failed computation from becoming an engineering finding.

- **[01A — Integrity and contracts](01-trust-kernel/01a-amended-implementation-plan.md) — complete under the amended `contract_tested` gate:** the v2 code provides stable claims, append-only review, immutable RFC 8785 objects, durable publication replay, database backstops, and independent C++ byte verification. The [amended completion record](01-trust-kernel/01a-amended-completion.md) cites the successful database, Python, frontend, and native release matrix. These controls establish reviewed-input integrity only; the package is not an engineering result.
- **01B — Package-to-C++ execution — complete under the bounded `contract_tested` gate:** the shared C++ execution path consumes exact reviewed Motor A/B packages, persists immutable run objects, changes the holding outcome under one unchanged scenario, and reproduces exact results offline. The [completion record](01-trust-kernel/01b-package-driven-execution-completion.md) binds the claim to the implementation SHA and successful release matrix.
- **01C — Windows-first real-project screening — current:** select a project folder, account for every file, load an unambiguous STEP assembly, run the supported static geometry screen, and expose unevaluated mechanical questions. The [lean design](../superpowers/specs/2026-08-14-windows-first-project-screening-design.md) deliberately keeps the inventory session-only until project trials establish the persistence requirements.
- **01D — Multi-project evidence:** run the workflow on at least three materially different mechanical projects, rank the observed failures, and choose the first structural component from that evidence.

Program 01C is the current prototype gate. Local evidence covers the motor-arm
folder and the large OpenArm import boundary; a Windows OCCT-enabled checkpoint
and additional independent projects remain required before it closes. The
former v1 record remains [historical evidence](01-trust-kernel/01a-integrity-and-contracts.md),
and its mutation/export endpoints are retired under the
[migration guide](../migration/program-01a-v1-to-v2.md). Program 01B proved
that reviewed values drive one bounded C++ result; it did not establish
arbitrary-project verification or external solver execution.

## Program 02 — Intake generalization

Generalize the thin folder inventory only after multi-project evidence exists.
Persist source identities, isolate parsers that actually proved necessary, add
archive and quarantine policies for formats encountered in trials, and export
a portable bundle. File accounting never implies semantic understanding.

## Program 03 — Semantic engineering graph

Begin with components, geometry, materials, joints, loads, restraints, and
requirements used by the mechanical project trials. Add other entity families
when a real domain slice needs them. Every inferred relationship retains its
source and review state.

## Program 04 — Requirements, scenarios, and planning

Start with a thin reviewed requirement/scenario form attached to actual
mechanical checks. Extract a general capability registry and planner only after
multiple checks expose a repeated planning boundary.

## Program 05 — First solver and runtime extraction

Integrate one bounded CalculiX linear-static component workflow through a small
isolated process adapter. Keep its inputs, assumptions, raw output, convergence,
and failure states explicit. After a second numerical adapter exists, extract
the genuinely shared runtime/SDK boundary. Each analysis still names one
authoritative backend under [ADR-0006](../adr/0006-authoritative-analysis-backends.md).

## Program 06 — Six bounded domain slices

Advance geometry, kinematics, and structural mechanics first. Thermal,
electrical, CFD, and controls remain backlog items until mechanical project
evidence or pilot demand justifies them. Domain breadth is not an MVP gate.

## Program 07 — Cross-domain orchestration

Move dimensioned, reviewed quantities between independently validated recipes. Initial chains include electrical-to-thermal, fluid-to-thermal, and control-to-mechanical mappings with explicit iteration and convergence records.

## Program 08 — Validation and calibration

Add an analytic, solver, or physical reference with each capability from its
first implementation. A later program may aggregate calibration and release
thresholds, but validation is never postponed until after domain breadth.

## Program 09 — Product hardening and pilots

Add parser isolation, recovery, signed packaging, installer/update controls, access policy, audit logs, performance budgets, accessibility, and external pilot workflows. Release gates depend on recovery and reproduction evidence, not feature count.

## Program 10 — Ecosystem

Stabilize adapter and component-package extension points, licensing boundaries, signed third-party artifacts, commercial solver connectors, and optional cloud execution. Extensions inherit the same applicability, validation, and failure-state rules as built-in capabilities.
