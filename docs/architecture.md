# Prometheus architecture

Prometheus is a Windows-first C++20/Qt 6 desktop application plus a Python component-research service. The canonical JSON contracts are solver-, toolkit-, and transport-independent.

## Ownership

- `desktop/core`: units, semantic assembly, projects, scenarios, plans, native checks, normalized findings, cache dependencies.
- `desktop/cad`: Open Cascade STEP/XDE, B-Rep, tessellation, and collision representations.
- `desktop/simulation`: fidelity planner and solver adapters; MuJoCo remains optional.
- `services/component-research`: acquisition, extraction, evidence validation, immutable component publishing, and scenario drafts.
- `schemas`: permanent interchange and project meaning.

The earlier React UI remains under `frontend/` as a verified reference prototype during migration. The Python `backend/` now also owns the normalized Milestone 2 research store and `/v1` research API; it is not the target physics boundary. Physics remains in C++ and the Python API narrows toward evidence acquisition and component compilation. See [ADR-0001](adr/0001-language-split.md).

## Dependency direction

QML UI → desktop application services → solver-independent core → adapters. External libraries must not leak their object types into canonical schemas or persisted projects. The desktop communicates with research over versioned JSON/OpenAPI via HTTPS, or localhost HTTP in explicit development mode.

Canonical CAD coordinates remain right-handed Z-up in metres. The Qt Quick 3D presentation layer applies an explicit −90° X-axis transform because its viewport convention is Y-up; imported coordinates and persisted transforms are not rewritten.

Static interference follows a two-stage native pipeline: Open Cascade bounding boxes reject separated pairs, then `BRepAlgoAPI_Common` confirms non-zero shared solid volume. A box overlap is shown only as measurement broad-phase information and never promoted to a physical interference finding. Unclassified confirmed overlap is warning-level until assembly semantics identify it as prohibited or intentional.

Revolute motion checks reuse the same exact common-volume predicate at 19 deterministic joint samples. They execute through a Qt background future, rotate the moving B-Rep around the persisted joint pivot/axis, and exclude the connected joint counterpart. Results record the first sampled collision angle, maximum sampled overlap volume, sample count, and checker method.

Placement overrides are canonical SI translations and degree-valued Euler rotations keyed by persistent XDE entity ID. Rotation is extrinsic X then Y then Z (`Rz * Ry * Rx`) about the center of the imported part bounds, followed by translation. Qt renders this with nested transform nodes while OCCT applies the same ordered rigid transforms to the source B-Rep. The same override list is consumed by the viewport, rotated bounds and measurements, static Boolean checks, joint sweeps, and project persistence. Static collision recomputation runs asynchronously after every accepted placement. Connection classifications are stored separately from computed overlaps because computed overlap membership can change with placement. An in-memory command history supplies placement undo/redo; it deliberately is not serialized as project state.

## Evidence boundary

Engineering values carry provenance. Unknown is representable. Candidate LLM extraction is never published directly; deterministic validation and review precede an immutable revision. Numerical checkers consume confirmed structured inputs, never raw prose.

## Current milestone

Milestones 0–2 provide the native shell, real OCCT STEP/XDE ingestion, browser-independent Qt Quick 3D rendering, normalized research persistence, explicit evidence review, immutable revision publication, and part-to-revision binding. Research extraction is fixture-backed; this is an intentional offline provider behind the same service boundary, not a claim of live public-source acquisition. Joint/scenario/check execution is the next active product slice.
