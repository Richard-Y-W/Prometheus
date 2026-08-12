# Milestone status

The earlier numbered milestones record how the rough electromechanical vertical was built. They are implementation checkpoints, not evidence that the general Prometheus product exists or that a physical design is correct. The capability-gated [master roadmap](program/00-master-roadmap.md) now governs forward work.

## Current truth boundary

- **Real CAD:** the native path imports real STEP/XDE with Open Cascade. The checked-in motor arm is a separate synthetic fixture used for conformance tests.
- **Synthetic research only:** component intake is exact lookup of `Prometheus Fixture Works / PM-36-GM`. Public research, remote datasheet acquisition, PDF parsing, and LLM extraction are not implemented.
- **Stable review and immutable publication:** v2 review identifies finalized revision-scoped claims and fingerprints, appends versioned events, and keeps publication separate. Publication stores exact RFC 8785 input bytes under an external SHA-256 identity; byte integrity is not an engineering result.
- **Execution gap:** the published execution-component package is not consumed by `EngineeringController`. Program 01B must remove fixed PM-36 values and close this loop.
- **Conformance calculations:** the current C++ motor-arm calculations exercise narrow deterministic paths. They do not provide arbitrary mechanical analysis or general engineering coverage.
- **No Python verdicts:** the unversioned Python research-confirm-plan-run path is retired. Python persists and packages evidence but is not an engineering decision authority.
- **No external solver:** no structural, thermal, electrical, CFD, or controls solver adapter exists.
- **No certification claim:** the repository does not claim physical validation, safety, certification, or project-wide correctness.

## Program 01A — amended implementation present; verification pending

The former Program 01A implementation added exact fixture provenance, typed values, atomic per-field review, reconstructed canonical packages, hash mismatch detection, structured failure codes, an explicit Qt review/publish flow, retired Python verdict routes, an archived React viewer, and OpenAPI exact-match testing. That work passed its superseded `contract_tested` gate and remains recorded as historical evidence.

The amended v2 implementation now provides bounded source ingestion, revision-scoped finalized claims, append-only review events, capability-specific publication and execution gates, immutable stored RFC 8785 objects, exact verified export, durable success/failure replay, SQLite/PostgreSQL backstops, and an independent C++ canonical-byte/hash verifier. The Qt flow uses claim IDs and visible draft versions, and the OCCT-disabled desktop path fails explicitly instead of synthesizing geometry.

Program 01A nevertheless remains in progress. The [pending amended completion record](program/01-trust-kernel/01a-amended-completion.md) may close only after the complete release matrix is observed. A sealed package is reviewed input only: it contains no engineering verdict, remains execution-blocked until Program 01B, and supplies no physical-validation or arbitrary-project claim. The [former completion record](program/01-trust-kernel/01a-integrity-and-contracts.md) is preserved, and the [v1-to-v2 migration guide](migration/program-01a-v1-to-v2.md) documents the breaking retirement. Program 01B has not started.

## Historical rough-V1 increments

### Milestones 0–1 — shell, contracts, and real STEP/XDE import

The repository introduced the C++20 core, Qt shell, versioned service boundary, JSON contracts, Open Cascade adapter, hierarchy-preserving STEP/XDE import, tessellation, safe malformed-file failure, asynchronous import, and atomic project reopen. These milestones established geometry handling, not engineering semantics for arbitrary assemblies.

### Milestone 2 — component persistence prototype

Normalized manufacturer, component, revision, parameter, source, evidence, job/event, and binding entities were added with Alembic migrations. Program 01A v2 later added the stable claim graph, evidence classes, append-only review, immutable objects, and explicit legacy classification. The provider remains exactly one synthetic fixture.

### Milestone 3 — fixed motor-arm conformance path

The desktop added a user-confirmed revolute joint, a structured motor-arm scenario, and fixed-input C++ torque-speed, holding, current, one-node thermal, and partial COG calculations. Findings retain methods and assumptions, but the component values remain compiled constants until Program 01B. Unknown masses and support geometry prevent complete COG or tipping conclusions.

### Milestones 4–6 — CAD inspection and sampled motion

The viewport added bounds, topology, SI measurement, X-Ray, standard views, static B-Rep common-volume interference, overlap classification, and a 19-sample revolute collision sweep. Sampled clearance is not continuous collision proof. Semantic classification is user input, not an inferred joint, fit, or load path.

### Milestones 7–12 — placement and transform interaction

The desktop added persistent translation and rotation, consistent render/OCCT transforms, undo/redo, world-axis interaction, drag previews, snapping, transient dimensions, cancellation, and world/local frames. These are CAD interaction capabilities; they do not supply missing materials, contacts, restraints, or requirements.

### Milestones 13–14 — bounds anchors and provisional connections

Bounds-center and face-center anchors support deterministic snap placement. Optional fixed/revolute/sliding/contact records preserve user-confirmed graph edges across reopen. Bounds anchors remain geometry aids and do not prove interface compatibility, fastening, retention, or force transfer.

### Milestone 15 — rough-V1 status baseline

The repository added a capability/status matrix and a rectangular support-polygon tipping primitive. The primitive has mathematical unit tests; the desktop still lacks complete reviewed per-part mass, support polygon, and loading inputs needed for an assembly tipping result.

### Milestone 16 — external CAD import stress test

The optional OpenArm 2.0 workflow records an on-demand hash-verified, licensed large-assembly import. It measures import and viewport behavior. It does not establish semantic parsing, interference coverage, or engineering correctness for that assembly. Large-file collision work is deliberately deferred and reported as such.

## Next gate

Program 01B must create two immutable reviewed motor packages with a decision-relevant difference, make C++ consume the selected package, prove the expected result changes, persist the package and execution manifest, reproduce after offline reopen, and show that Python never duplicates the calculation. Broader acquisition and solver work waits behind that gate.
