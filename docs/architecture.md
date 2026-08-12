# Prometheus architecture

Prometheus is designed as a local project compiler around isolated engineering backends. The target architecture accepts heterogeneous project artifacts, preserves every source, constructs a human-reviewed system model, compiles requirements and scenarios into proof obligations, plans applicable analyses, and reports findings plus coverage. The current repository implements only a STEP/XDE electromechanical demonstrator and the fixture-backed Program 01A v2 trust boundary.

## Target compiler flow

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

The general artifact inventory, parser sandbox, semantic system graph, proof-obligation compiler, capability planner, solver runtime, and project coverage engine in this diagram are target subsystems. Contracts and roadmap entries do not make them current capabilities.

## Current Program 01A boundary

```text
Checked-in synthetic source bytes
        |
        v
Bounded descriptor-based ingestion -> immutable source artifact
        |
        v
V2 relational draft: slots, claims, evidence, selections, gates
        |
        v
Append-only claim review at expected draft version
        |
        v
Python input-package compiler and schema validation (no verdict)
        |
        v
Exact RFC 8785 bytes -> external SHA-256 identity
        |
        v
Immutable stored object + sealed revision binding
        |
        +-----------------------> exact verified API export
        |
        +-----------------------> independent C++ byte/hash/schema-ID verifier
```

This boundary establishes provenance and byte identity for one reviewed synthetic input package. It does not establish that the parameter values are physically true, that the model is complete or applicable, that execution occurred, or that an engineering project works.

## Draft and published authority

Before publication, the locked relational v2 draft is authoritative. It contains revision-scoped parameter slots, finalized candidate claims, explicit claim selections, evidence links, append-only review history, and capability gates. A claim fingerprint covers the claim and its evidence identity; review records that fingerprint so later drift cannot inherit an earlier decision.

Publication is a single state transition. Python reads and validates the selected draft, emits the closed execution-component value, canonicalizes it, verifies the resulting bytes, computes the external SHA-256 identity, installs or exactly matches an immutable object, and binds the revision. Python is the package compiler here, not an engineering-computation or project-verdict authority.

After publication, the stored byte object is authoritative. Relational projections may support search and display, but they may not reconstruct or replace a `sealed_v2` package. Export revalidates the stored metadata, canonical form, byte length, and hash before returning those exact bytes. The package has no internal `content_hash`; the revision binding, publication response, and quoted `ETag` carry the identity outside the hashed bytes.

That integrity statement is deliberately narrow. A sealed object is reviewed input, not a requirement, solver output, finding, pass/fail result, safety assessment, or physical-validation record.

## Claim and evidence graph

Candidate claims use server-generated, lowercase UUIDv4 identities scoped to a revision and parameter slot. Construction begins unfinalized, evidence links are added, then a one-way finalization stores the claim fingerprint. Same-revision foreign keys and database triggers backstop links among slots, claims, evidence, selections, review events, and gates. Publication includes exactly one selected claim per emitted slot and exactly one effective accepted review per selected claim.

Evidence uses a closed class with class-specific authority and provenance requirements:

- `manufacturer_document`: manufacturer or supplier authority, immutable artifact, and document identity;
- `private_upload`: private provider, user, or synthetic-fixture authority, immutable artifact, and local provenance;
- `user_measurement`: user authority, method, unit, observation time, and either an artifact or recorded observation;
- `derived_claim`: Prometheus-derivation authority, method, and at least one same-package parent claim or evidence identity;
- `validation_observation`: validation-activity or user authority, test provenance, observation time, and recorded observation.

Every evidence record separately declares physical-validation status, limitations, and optional source location. `extraction_confidence` describes extraction reliability only; it is not engineering confidence, review acceptance, source authority, or physical validation. The current fixture truthfully uses `private_upload`, `source_authority=synthetic_fixture`, and `physical_validation_status=unvalidated`.

## Review and capability gates

Review decisions identify `claim_id`, bind the stored claim fingerprint, require a note, and append at `expected_draft_version + 1`. A batch is atomic; stale versions and cross-revision claims fail without partial events. The local `reviewed_by` value is an audit label, not an authenticated user identity.

Gates belong to a named capability and one of two phases. Publication requires every declared publication gate for the selected capability to be satisfied. Execution gates do not prevent sealing reviewed input; they determine `execution_readiness`. The current `component_input.pm_36_gm` fixture has publication gates for component identity, source artifact, claim selection, and claim review, plus a blocked `package_consumer` execution gate. Therefore a valid publication can return `execution_readiness=blocked`. This is not a pass; it records that Program 01B has not connected the package to engineering execution.

## Computation and decision authority

Each future analysis declares one versioned authoritative computation backend: either a built-in C++ check or an isolated external numerical engine. Orchestration must not silently duplicate or alter that backend's calculation. A missing adapter, parser failure, unresolved boundary condition, failed process, invalid output, or nonconverged solve maps to `indeterminate` or `not_evaluated`; none can satisfy a proof obligation.

The independent C++ integrity library parses under the same bounded canonicalization policy, verifies canonical bytes and SHA-256, and checks the supported v2 schema identity without calling Python. It does not perform full engineering-schema validation or establish the truth of package contents.

The Qt-free C++ decision core is authoritative for project-summary reduction. It derives verdict and coverage from obligation counts and execution state, and it rejects unscoped satisfaction. The Python `ProjectSummaryV2` model validates and transports that shape but does not originate the verdict. The current fixed motor-arm checker does not consume the v2 package, so Program 01B remains the first package-to-decision gate.

## Geometry boundary

Real STEP/XDE import uses Open Cascade to retain B-Rep, hierarchy, names, persistent entity IDs, topology, bounds, volume, and tessellation. The checked-in motor-arm fixture is a separate synthetic conformance artifact. When Open Cascade is disabled, the Qt controller returns the explicit error `Open Cascade adapter is not enabled`; it does not report a synthetic import success.

Canonical CAD coordinates are right-handed, Z-up, and measured in metres. Qt Quick 3D applies an explicit presentation transform for its Y-up viewport; persisted coordinates are not rewritten. Placement overrides are SI translations plus extrinsic X-then-Y-then-Z rotations (`Rz * Ry * Rx`) about imported part-bounds centers.

Static interference uses bounding-box rejection followed by `BRepAlgoAPI_Common`. Revolute motion uses the same common-volume predicate at 19 deterministic samples and is sampled collision detection, not a continuous-clearance guarantee. Bounds-derived anchors are placement aids, not verified interfaces, joints, fasteners, fits, or load paths.

## Process and storage boundaries

- Qt/C++ desktop: review interaction, project state, geometry inspection, future analysis supervision, independent package-integrity support, and authoritative project-summary reduction.
- Python service: exact fixture ingestion, v2 draft persistence, claim-review transactions, input-package compilation, immutable publication, durable retry replay, and verified export; no engineering verdict.
- C++ integrity target: independent bounded JSON canonicalization, hash, and supported-schema-identity verification; no physics or finding generation.
- External solvers: future separately installed child processes behind versioned analysis and result packages. No adapter exists in this repository.
- Storage: SQLite for local use and PostgreSQL 17 as a conformance target, with immutable artifact/package bytes and relational metadata. The general portable project bundle remains Program 02 work.
- Contracts: checked-in JSON Schema and OpenAPI snapshots. Solver-library object types never enter persisted project meaning.

Planned engines and their narrow initial regimes are recorded in the [approved design](superpowers/specs/2026-08-11-prometheus-general-engineering-platform-design.md), not presented as current capabilities.
