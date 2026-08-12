# Rough-V1 release status

The rough V1 is an electromechanical conformance demonstrator plus CAD interaction work. Its original Program 01A boundary is historical: amended v2 implementation now adds immutable stored input bytes and independent C++ integrity verification, while the amended completion record remains pending the full release matrix. The repository does not implement the complete Prometheus architecture and cannot determine whether an arbitrary engineering project works.

## Status matrix

| Area | Implemented | Boundary |
| --- | --- | --- |
| CAD | Real STEP/XDE import, B-Rep metadata, tessellation, persistent entity IDs, placement, exact static common-volume checks, and sampled revolute collision | The synthetic motor-arm fixture is a separate artifact. Sampled motion is not continuous-clearance proof; bounds anchors are not authoritative interfaces. |
| Component intake | Exact lookup of checked-in `Prometheus Fixture Works / PM-36-GM` synthetic JSON with source-byte hashing | No public search, remote acquisition, datasheet/PDF parsing, OCR, chart digitization, or LLM research provider exists. |
| Evidence review | Revision-scoped slots, finalized claims and fingerprints, typed source-linked evidence, append-only claim-ID decisions with required notes, optimistic draft versions, and separate publication | Human acceptance records a local decision; it does not physically validate the value, and reviewer labels are not authenticated. |
| Published input | V2 stores exact RFC 8785 execution-component bytes under an external SHA-256 identity, binds them to a sealed revision, verifies stored-byte export, and provides an independent C++ byte/hash/schema-ID verifier | The package is reviewed input and contains no finding or requirement verdict. Program 01B has not connected it to C++ engineering execution. |
| C++ checks | Fixed-input motor-arm torque, current, one-node thermal, selected COG primitives, and geometry conformance checks | The checker does not consume the published package until Program 01B. These recipes are not general mechanical or arbitrary engineering support. |
| Python | Fixture acquisition, candidate persistence, review API, package construction, and OpenAPI | The old confirm/plan/run endpoints are retired. Python is not an engineering decision authority and no production route imports the historical physics module. |
| Numerical solvers | None | No CalculiX, Elmer, ngspice, OpenFOAM, Modelica, MuJoCo, or other external analysis adapter is installed or implemented as a Prometheus capability. |
| Product claims | Historical former-gate evidence, current v2 implementation, and CAD/conformance demonstrations | Amended Program 01A remains pending final verification. No certification, safety assurance, physical validation, or project-wide correctness claim is made. |

## Geometry and assembly detail

The Qt/Open Cascade path imports real STEP/XDE separately from the local motor-arm fixture. It exposes hierarchy, instance names, persistent IDs, bounds, volume, surface area, face/edge counts, tessellation, selection, visibility, camera controls, SI measurement, placement transforms, undo/redo, bounds-anchor snapping, and user-confirmed semantic connection records.

Static interference first rejects separated bounding boxes and then evaluates B-Rep common volume. Joint motion checks 19 deterministic samples and excludes the connected pair. A clear sample set does not establish continuous clearance between samples. Material, mass, support geometry, contacts, fasteners, retention, and load paths remain unknown unless supplied and reviewed.

The optional OpenArm workflow is a licensed import stress test. Successful import of a large assembly measures parser and viewport behavior; it does not establish semantic understanding or engineering correctness.

## Component and execution detail

Fixture intake is deterministic lookup of `prometheus.pm-36-gm.fixture-2`. Unsupported or Unicode-lookalike fixture identities and caller attempts to substitute source identity fail without creating records. Each parameter slot selects a finalized typed claim with original text, unit, validity conditions, evidence links, and a stable fingerprint. Publication requires one effective accepted review for every selected claim and satisfied capability-specific publication gates; object installation, revision binding, and durable response storage commit together. The current execution-only package-consumer gate remains blocked.

The C++ motor-arm path still uses fixed PM-36 constants. The UI labels it as a conformance demo, and the React predecessor cannot launch it. Program 01B must provide two reviewed packages, remove those constants, prove result sensitivity to the bound revision, and reproduce the run after reopen.

## Failure semantics

Unsupported identity, incomplete review, invalid value shape, package validation failure, hash mismatch, retired route, and unavailable authoritative execution have distinct error codes. A missing parser, missing backend, invalid output, failed process, unresolved boundary condition, or nonconverged solve cannot become a pass or `satisfied` result.

## Verification entry points

`scripts/verify.ps1` runs backend tests, frontend tests/build/audit, headless C++ tests, the independent integrity target, and the Qt desktop with Open Cascade disabled. The optional native Open Cascade path requires the Windows UCRT64 toolchain. Amended results are not final until they are recorded in the [pending completion record](program/01-trust-kernel/01a-amended-completion.md); the [former record](program/01-trust-kernel/01a-integrity-and-contracts.md) remains historical.
