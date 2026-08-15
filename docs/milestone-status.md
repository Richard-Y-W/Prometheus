# Milestone status

The earlier numbered milestones record how the rough electromechanical vertical was built. They are implementation checkpoints, not evidence that the general Prometheus product exists or that a physical design is correct. The capability-gated [master roadmap](program/00-master-roadmap.md) now governs forward work.

## Current truth boundary

- **Real CAD:** the native path imports real STEP/XDE with Open Cascade. The checked-in motor arm is a separate synthetic fixture used for conformance tests.
- **Synthetic research only:** component intake is exact lookup of the checked-in Motor A, Motor B, or `Prometheus Fixture Works / PM-36-GM` fixture. Public research, remote datasheet acquisition, PDF parsing, and LLM extraction are not implemented.
- **Stable review and immutable publication:** v2 review identifies finalized revision-scoped claims and fingerprints, appends versioned events, and keeps publication separate. Publication stores exact RFC 8785 input bytes under an external SHA-256 identity; byte integrity is not an engineering result.
- **Bounded execution loop:** the exact Motor A/B package and reviewed scenario drive `motor_arm_builtin_v1` through the shared Qt-free C++ execution library. The desktop and replay CLI use that same implementation; project-local immutable run objects reproduce offline under the recorded numeric identity.
- **Conformance calculations:** four fixed motor-arm obligations cover torque-speed, continuous holding torque, driver current, and a one-node thermal estimate. They do not provide arbitrary mechanical analysis or general engineering coverage.
- **No Python verdicts:** the unversioned Python research-confirm-plan-run path and the historical Python motor module are retired. Python persists and packages evidence but is not an engineering decision authority.
- **No external solver:** no structural, thermal, electrical, CFD, or controls solver adapter exists.
- **No certification claim:** the repository does not claim physical validation, safety, certification, or project-wide correctness.

## Program 01A — complete under the amended contract-tested gate

The former Program 01A implementation added exact fixture provenance, typed values, atomic per-field review, reconstructed canonical packages, hash mismatch detection, structured failure codes, an explicit Qt review/publish flow, retired Python verdict routes, an archived React viewer, and OpenAPI exact-match testing. That work passed its superseded `contract_tested` gate and remains recorded as historical evidence.

The amended v2 implementation now provides bounded source ingestion, revision-scoped finalized claims, append-only review events, capability-specific publication and execution gates, immutable stored RFC 8785 objects, exact verified export, durable success/failure replay, SQLite/PostgreSQL backstops, and an independent C++ canonical-byte/hash verifier. The Qt flow uses claim IDs and visible draft versions, and the OCCT-disabled desktop path fails explicitly instead of synthesizing geometry.

Program 01A closed on 2026-08-12 after the [amended completion record](program/01-trust-kernel/01a-amended-completion.md) captured a successful nine-job release matrix, exact local verification, and a restart-based exact-byte audit. A sealed package is still reviewed input only and contains no engineering verdict. Program 01B added two new execution-ready packages without changing the older blocked PM-36 package or converting byte integrity into physical validation. The [former completion record](program/01-trust-kernel/01a-integrity-and-contracts.md) is preserved, and the [v1-to-v2 migration guide](migration/program-01a-v1-to-v2.md) documents the breaking retirement.

## Program 01B — complete under the bounded contract-tested gate

Program 01B closed on 2026-08-13 at implementation and CI commit `dd5b915ae0fa23f0d48fb7e4f8df4a9834c9816d`. The [completion record](program/01-trust-kernel/01b-package-driven-execution-completion.md) binds that claim to a successful nine-job matrix, the local 13/3/19/20 native suites, the complete SQLite/PostgreSQL suites, exact Motor A/B object identities, and offline desktop/CLI replay.

The two synthetic packages' normalized engineering vectors differ only in the decision-relevant continuous-torque value; their package graphs retain distinct fixture and claim identities. Under one unchanged reviewed scenario, Motor A failed the continuous holding obligation and Motor B passed it; all non-holding calculation outputs remained equal. Package, scenario, request, result, manifest, numeric profile, assumptions, limitations, consumed claims, and scoped coverage remain visible. Geometry remains a separate capability instead of being merged into an unqualified project verdict.

This gate establishes one synthetic package-driven built-in backend. It is not arbitrary project verification, physical validation, universal intake, general mechanical analysis, or external solver execution. Center of gravity remains explicitly uncovered, and the simplified model provides no structural, fatigue, fastening, tolerance, manufacturing, safety, or certification conclusion.

## Historical rough-V1 increments

### Milestones 0–1 — shell, contracts, and real STEP/XDE import

The repository introduced the C++20 core, Qt shell, versioned service boundary, JSON contracts, Open Cascade adapter, hierarchy-preserving STEP/XDE import, tessellation, safe malformed-file failure, asynchronous import, and atomic project reopen. These milestones established geometry handling, not engineering semantics for arbitrary assemblies.

### Milestone 2 — component persistence prototype

Normalized manufacturer, component, revision, parameter, source, evidence, job/event, and binding entities were added with Alembic migrations. Program 01A v2 later added the stable claim graph, evidence classes, append-only review, immutable objects, and explicit legacy classification. The provider remains exactly one synthetic fixture.

### Milestone 3 — fixed motor-arm conformance path

The desktop added a user-confirmed revolute joint, a structured motor-arm scenario, and fixed-input C++ torque-speed, holding, current, one-node thermal, and partial COG calculations. Findings retained methods and assumptions, but component values were compiled constants at that milestone; Program 01B later removed them from the production path. Unknown masses and support geometry still prevent complete COG or tipping conclusions.

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

Program 01C closed on 2026-08-15 at implementation commit `519e06c` after the
[completion record](program/01-trust-kernel/01c-windows-screening-completion.md)
captured a fresh Windows OCCT Release build, 22/22 native suites, and a pinned
independent YUBI trial importing one root, 90 leaves, and 37,367 triangles.
The trial found and removed an OCCT automatic-shape-healing crash while keeping
the disabled repair visible as a limitation.

Program 01D multi-project evidence is current. It must complete three written,
materially different project trials, include at least one outside user, rank
observed import/semantic/UX failures, and select one real component and bounded
structural question. Universal intake, semantic reconstruction, general
planning, and broader solver work remain later roadmap gates.
