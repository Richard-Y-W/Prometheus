# Component model

The Program 01A v2 component model is a revision-scoped claim graph for one synthetic fixture. It is not yet the general semantic model for arbitrary assemblies.

## Draft graph

A v2 draft relates:

- one manufacturer, component identity, and component revision;
- named parameter slots with quantity, dimension, and `required_for_execution` state;
- one or more immutable candidate claims per slot and one explicit current selection;
- typed evidence records and claim-to-evidence links;
- append-only claim-review events bound to the reviewed claim fingerprint;
- capability-specific publication and execution gates;
- persisted missing-information and limitation identities.

Candidate claims are constructed in two steps: the service inserts an unfinalized value, completes its evidence links, then performs one permitted finalization transition that stores the fingerprint. A finalized claim cannot be edited. Database constraints enforce same-revision ownership and prevent a selection or review from naming an unfinished claim.

The API identifies review subjects by `claim_id`, not by a mutable parameter name. Each review batch supplies `expected_draft_version`; a successful atomic batch appends events and increments the revision once. Rejected and ambiguous events remain in history when a later event supersedes them. The reviewer's string is a local audit label, not authenticated identity, and acceptance does not establish physical truth.

## Evidence classes

All evidence records carry an identity, owning revision, source authority, physical-validation status, limitations, and optional extraction/location metadata. Each class adds non-interchangeable requirements:

| Class | Required authority and content |
| --- | --- |
| `manufacturer_document` | `manufacturer` or `supplier`; artifact hash and document identity |
| `private_upload` | `private_provider`, `synthetic_fixture`, or `user`; artifact hash and local provenance |
| `user_measurement` | `user`; method, unit, observation time, and an artifact or recorded observation |
| `derived_claim` | `prometheus_derivation`; method and at least one parent claim or evidence identity |
| `validation_observation` | `validation_activity` or `user`; test provenance, observation time, and recorded observation |

`source_locator` and `excerpt` occur together; a page requires both. Parent and artifact references must resolve within the emitted package graph. `extraction_confidence`, when present, means confidence in extraction only. It cannot stand in for source authority, review status, applicability, engineering uncertainty, or physical validation.

The PM-36 source is classified as a `private_upload` from `synthetic_fixture` authority with `unvalidated` physical status. Its values support conformance tests only; they are not manufacturer claims.

## Capability gates

Gates are scoped to a capability. Publication gates establish that the component identity, source artifact, selected claims, and effective accepted reviews needed for the package are present. Execution gates state whether a downstream consumer is available. A gate for another capability has no effect on the selected capability.

All publication gates must be satisfied before sealing. An unresolved execution gate may coexist with successful publication and makes `execution_readiness=blocked`. The current fixture is intentionally in that state because no v2 package consumer or solver execution exists. A sealed package in this state remains input, not a finding or pass.

## Publication authority switch

The relational graph is authoritative while the revision is `v2_draft`. Publication compiles the reviewed selection into the closed execution-component contract, canonicalizes it as RFC 8785 bytes, computes an external SHA-256 identity, and stores those exact bytes. The state change binds the revision to the object as `sealed_v2` and makes the revision and child graph immutable.

After that transition, the stored object is authoritative. Export revalidates and returns its exact bytes; it never rebuilds the package from relational rows. The object hash is not a member of the package because including it would make the identity self-referential. Package integrity establishes byte identity only, not model completeness, applicability, physical validation, or engineering correctness.

Migrated v1 publications without authoritative stored bytes are labeled `legacy_unsealed`. They remain queryable historical metadata and cannot become v2 exports or idempotent v2 successes without creating a new draft and review history.

## Execution component and summary

The execution-component package contains component identity, source-artifact references, parameter slots, selected claims, evidence, effective accepted reviews, gates, missing information, limitations, and an input-only authority declaration. It contains no requirement, solver result, finding, pass, or failure verdict.

Python compiles and validates this input package but does not issue an engineering decision. The independent C++ verifier checks canonical byte identity and supported schema identity; it does not validate physical truth. The Qt-free C++ decision core remains authoritative for project-summary verdict, coverage, and execution state. Program 01B must make reviewed package values drive the C++ checker before publication affects an engineering calculation.

The legacy certification-tier vocabulary is historical evidence state, not safety or design certification. Typed component ports, coordinate frames, interfaces, and general compatibility rules remain future semantic-graph capabilities.
