# Rough-V1 release status

The rough V1 is an electromechanical conformance demonstrator plus CAD interaction work. The former Program 01A gate added a fixture-backed component-input boundary, but the amended gate is reopened because that boundary is not yet immutable or independently verifiable across Python and C++. The repository does not implement the complete Prometheus architecture and cannot determine whether an arbitrary engineering project works.

## Status matrix

| Area | Implemented | Boundary |
| --- | --- | --- |
| CAD | Real STEP/XDE import, B-Rep metadata, tessellation, persistent entity IDs, placement, exact static common-volume checks, and sampled revolute collision | The synthetic motor-arm fixture is a separate artifact. Sampled motion is not continuous-clearance proof; bounds anchors are not authoritative interfaces. |
| Component intake | Exact lookup of checked-in `Prometheus Fixture Works / PM-36-GM` synthetic JSON with source-byte hashing | No public search, remote acquisition, datasheet/PDF parsing, OCR, chart digitization, or LLM research provider exists. |
| Evidence review | Typed values, source-linked evidence, one explicit review decision per field, review notes for ambiguous/rejected fields, and separate publication | Human acceptance records a decision; it does not physically validate the value. |
| Published input | Former v1 Python-canonicalized execution-component package, SHA-256 content hash, stable reconstructed export, reference checks, and persisted-tamper detection | Amended Program 01A must store exact RFC 8785 bytes and add independent C++ verification. The package is reviewed input and contains no finding or requirement verdict. |
| C++ checks | Fixed-input motor-arm torque, current, one-node thermal, selected COG primitives, and geometry conformance checks | The checker does not consume the published package until Program 01B. These recipes are not general mechanical or arbitrary engineering support. |
| Python | Fixture acquisition, candidate persistence, review API, package construction, and OpenAPI | The old confirm/plan/run endpoints are retired. Python is not an engineering decision authority and no production route imports the historical physics module. |
| Numerical solvers | None | No CalculiX, Elmer, ngspice, OpenFOAM, Modelica, MuJoCo, or other external analysis adapter is installed or implemented as a Prometheus capability. |
| Product claims | Historical former-gate contract tests and CAD/conformance demonstrations | Amended Program 01A remains in progress. No certification, safety assurance, physical validation, or project-wide correctness claim is made. |

## Geometry and assembly detail

The Qt/Open Cascade path imports real STEP/XDE separately from the local motor-arm fixture. It exposes hierarchy, instance names, persistent IDs, bounds, volume, surface area, face/edge counts, tessellation, selection, visibility, camera controls, SI measurement, placement transforms, undo/redo, bounds-anchor snapping, and user-confirmed semantic connection records.

Static interference first rejects separated bounding boxes and then evaluates B-Rep common volume. Joint motion checks 19 deterministic samples and excludes the connected pair. A clear sample set does not establish continuous clearance between samples. Material, mass, support geometry, contacts, fasteners, retention, and load paths remain unknown unless supplied and reviewed.

The optional OpenArm workflow is a licensed import stress test. Successful import of a large assembly measures parser and viewport behavior; it does not establish semantic understanding or engineering correctness.

## Component and execution detail

Fixture research is deterministic catalog lookup. Unsupported manufacturers, part numbers, or caller URLs fail without creating records. Each stored parameter retains a typed value shape, unit, original text, validity conditions, and evidence record. Publication requires accepted evidence for every parameter and occurs in the same transaction as package validation and hashing.

The C++ motor-arm path still uses fixed PM-36 constants. The UI labels it as a conformance demo, and the React predecessor cannot launch it. Program 01B must provide two reviewed packages, remove those constants, prove result sensitivity to the bound revision, and reproduce the run after reopen.

## Failure semantics

Unsupported identity, incomplete review, invalid value shape, package validation failure, hash mismatch, retired route, and unavailable authoritative execution have distinct error codes. A missing parser, missing backend, invalid output, failed process, unresolved boundary condition, or nonconverged solve cannot become a pass or `satisfied` result.

## Verification entry points

`scripts/verify.ps1` runs backend tests, frontend tests/build/audit, and headless C++ tests in the documented environment. Native Qt/Open Cascade and review-payload tests require the Windows UCRT64 toolchain. Exact Program 01A results and unavailable checks are recorded in [the milestone record](program/01-trust-kernel/01a-integrity-and-contracts.md).
