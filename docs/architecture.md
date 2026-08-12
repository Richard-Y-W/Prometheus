# Prometheus architecture

Prometheus is designed as a local project compiler around isolated engineering backends. The target architecture accepts heterogeneous project artifacts, preserves every source, constructs a human-reviewed system model, compiles requirements and scenarios into proof obligations, plans applicable analyses, and reports findings plus coverage. The current repository implements only the STEP/XDE electromechanical demonstrator and the Program 01A trust boundary.

## Compiler flow

```text
Original project files
        |
        v
Content-addressed artifact store
        |
        v
Sandboxed parser workers
        |
        v
Artifact graph and evidence claims
        |
        v
Human-reviewed semantic system model
        |
        v
Requirements x scenarios -> proof obligations
        |
        v
Capability registry and analysis planner
        |
        v
Human-reviewed assumptions and boundary conditions
        |
        v
One declared versioned computation backend per analysis
        |
        v
C++ result validation, findings, unknowns, coverage, and manifest
```

The artifact store, general parser sandbox, semantic graph, proof-obligation compiler, capability planner, solver runtime, and coverage engine in this diagram are target subsystems. They are not present merely because their contracts or roadmap exist.

## Authority boundary

The Qt/C++ application owns confirmed engineering state, units and coordinate frames, review gates, proof obligations, applicability, acceptance criteria, result validation, coverage, and findings. Each analysis declares one authoritative computation backend: either a built-in C++ check or a versioned external numerical engine. The orchestration layer does not independently reproduce that calculation.

Python may inventory documents and produce candidate evidence. It may not approve evidence, choose final boundary conditions, determine production applicability, or issue an engineering verdict. The previous Python analysis endpoints are retired; `backend/app/physics.py` remains only as a historical equation reference pending Program 01B parity review. See [ADR-0006](adr/0006-authoritative-analysis-backends.md).

A missing adapter, parser failure, unresolved boundary condition, failed process, invalid output, or nonconverged solve maps to `indeterminate` or `not_evaluated`. None can satisfy a proof obligation.

## Current component boundary

The versioned service reads one exact checked-in synthetic source artifact. A candidate revision contains typed parameter values and pending evidence. The API requires one explicit review decision per parameter, publishes in a single transaction, and hashes a canonical execution-component package. The Qt client does not generate acceptance decisions from the parameter list and does not combine review with publication.

The package is reviewed input, not an analysis result. It contains no verdict. Program 01B must remove fixed PM-36 values from `EngineeringController` and make the C++ checker consume the bound package before this boundary reaches execution.

## Geometry boundary

Real STEP/XDE import uses Open Cascade to retain B-Rep, hierarchy, names, persistent entity IDs, topology, bounds, volume, and tessellation. The checked-in motor-arm fixture is a separate synthetic conformance artifact.

Canonical CAD coordinates are right-handed, Z-up, and measured in metres. Qt Quick 3D applies an explicit presentation transform for its Y-up viewport; persisted coordinates are not rewritten. Placement overrides are SI translations plus extrinsic X-then-Y-then-Z rotations (`Rz * Ry * Rx`) about imported part-bounds centers.

Static interference uses bounding-box rejection followed by `BRepAlgoAPI_Common`. Revolute motion uses the same common-volume predicate at 19 deterministic samples and therefore is sampled collision detection, not a continuous-clearance guarantee. Bounds-derived anchors are placement aids, not verified interfaces, joints, fasteners, fits, or load paths.

## Process and storage boundaries

- Qt/C++ desktop: current CAD state, human review UI, project state, built-in conformance checks, and future analysis supervision.
- Python service: exact fixture acquisition, candidate evidence persistence, review API, and canonical package construction; no engineering verdicts.
- External solvers: future separately installed child processes behind versioned analysis/result packages.
- Storage: current SQLite metadata plus JSON project state; the general content-addressed portable bundle remains Program 02 work.
- Contracts: canonical JSON Schema and OpenAPI; solver-library object types never enter persisted project meaning.

No external solver adapter exists in the repository. Planned engines and their narrow initial regimes are recorded in the [approved design](superpowers/specs/2026-08-11-prometheus-general-engineering-platform-design.md), not presented as current capabilities.
