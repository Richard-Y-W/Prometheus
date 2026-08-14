# Prometheus General Engineering Compiler and Solver-Orchestration Design

- Status: Approved design
- Date: 2026-08-11
- Repository baseline: `a3e09c57539515f041c2a7b2dc59c97e63293cd0`

## 1. Purpose

Prometheus will be a local-first compiler, review environment, and solver-orchestration platform for heterogeneous engineering projects. A user supplies the available project artifacts. Prometheus inventories them, reconstructs a reviewable engineering model, identifies what the project is required to do, turns those requirements into proof obligations, prepares reviewed analyses, runs local numerical engines, and reports failures, scoped successes, unknowns, and coverage with reproducible provenance.

Prometheus is not limited to a motorized-mechanism vertical. Its architecture targets arbitrary engineering projects from the beginning. Its first credible release covers six broad analysis domains through narrow, validated workflows:

1. Geometry and kinematics.
2. Structural mechanics.
3. Thermal analysis.
4. Electrical circuits and power.
5. Computational fluid dynamics.
6. Controls and system dynamics.

Broad domain coverage does not imply universal analysis coverage. Every supported recipe declares its applicability, validation level, inputs, numerical engine, and limitations. Unsupported or unresolved work remains visible.

## 2. Product decisions

The approved program is based on these decisions:

- General project intake rather than a single engineering vertical.
- Local open-source numerical engines before commercial or cloud engines.
- Review-gated automation rather than unreviewed solver execution.
- A compiler-first architecture shared by all six domains.
- One Prometheus Qt/C++ application backend and multiple specialized numerical engines.
- A proprietary Prometheus core with an open adapter contract where useful.
- GPL numerical engines installed separately by users and invoked out of process, subject to commercial licensing review.
- C++ as the sole Prometheus decision authority.
- External engines as explicitly versioned numerical calculators, never independent sources of Prometheus verdicts.
- Validation and benchmark-corpus development beginning immediately and continuing throughout the program.

## 3. Product truth and verdict model

Prometheus cannot provide an unconditional claim that an arbitrary physical project “works.” Working behavior is meaningful only relative to requirements, scenarios, supplied evidence, modeled physics, numerical validity, and analysis coverage.

Allowed project summaries are:

- `requirements_violated`
- `no_violations_detected_within_scope`
- `insufficient_coverage`
- `analysis_blocked`

Individual proof obligations resolve to:

- `satisfied`
- `violated`
- `indeterminate`
- `not_applicable`
- `not_evaluated`

A missing parser, missing engine, unresolved entity, conflicting artifact, unreviewed boundary condition, failed execution, or nonconverged calculation must never produce `pass` or `satisfied`.

## 4. Capability roadmap

Prometheus advances through capability gates rather than calendar labels.

| Program | Outcome | Exit gate |
| --- | --- | --- |
| 00. Program reset | Truthful repository claims, safe fixture behavior, and an initial benchmark corpus | Fixture evidence cannot be represented as live research; baseline corpus exists |
| 01. Trust kernel | Immutable evidence and execution packages drive C++ findings | Changing only the bound Motor A/Motor B revision changes the result; offline reopen reproduces it |
| 02. Universal project intake | Every supplied artifact is hashed, classified, preserved, and visibly accounted for | Nothing is silently discarded; malformed artifacts fail safely |
| 03. Semantic engineering graph | Cross-file entities and relationships are reconstructed and reviewable | Inferred relationships have provenance and can be corrected |
| 04. Requirements, scenarios, and planning | Project intent becomes explicit proof obligations and reviewed analysis plans | Every analysis traces to an obligation; unsupported work remains visible |
| 05. Local solver runtime and SDK | Versioned C++ contracts execute isolated local numerical engines | A reference adapter proves execution, cancellation, validation, caching, and normalized results |
| 06. Six domain slices | One useful, bounded, benchmarked workflow exists in every approved domain | Each domain has known-pass, known-fail, and solver-benchmark cases |
| 07. Cross-domain orchestration | Validated quantities move between domains with explicit mapping and convergence | Representative electrical/thermal, fluid/thermal, and control/mechanical chains reproduce offline |
| 08. Validation and calibration | Predictions and coverage are measured against analytic, solver, and physical references | Release thresholds are met and no unresolved critical false-negative regression exists |
| 09. Product hardening and pilots | The local Windows product is recoverable, portable, secure, and usable by external engineers | Pilot users reproduce consequential findings without developer intervention |
| 10. Ecosystem | Additional engines, commercial adapters, cloud execution, signed component models, and third-party plugins | New capabilities do not require changes to the compiler’s core contracts |

Programs 00 and 08 are continuous workstreams. Programs 02 through 05 establish shared contracts sequentially. The six Program 06 adapters may proceed in parallel only after the solver SDK is stable.

## 5. Compiler-first architecture

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
C++-controlled isolated numerical engines
        |
        v
C++ result validation and acceptance criteria
        |
        v
Findings, unknowns, coverage, and reproducible manifest
```

### 5.1 Authority model

The Prometheus C++ core is the sole authority for:

- Confirmed engineering state.
- Units and coordinate systems.
- Proof obligations and analysis planning.
- Applicability and input validation.
- Human-review gates and execution approval.
- Numerical-result validation and convergence acceptance.
- Acceptance criteria, coverage, and normalized findings.

Each numerical analysis has one explicitly versioned numerical engine. The engine computes raw numerical results but cannot declare that the project passes or fails. Raw outputs are immutable evidence interpreted by the C++ core.

Python workers may parse documents, inspect source code, and produce candidate evidence or relationships. They may not approve claims, silently change confirmed values, select final engineering assumptions, determine applicability, apply acceptance criteria, or issue findings.

### 5.2 Technology boundaries

- **Qt/C++ application:** project UI, confirmed semantic graph, proof obligations, planning, review gates, built-in deterministic checks, solver-job construction, job supervision, result validation, coverage, and findings.
- **Python workers:** isolated document processing, source-code analysis, candidate evidence extraction, and optional LLM assistance.
- **C++ solver adapters:** versioned executables or plugins that translate immutable analysis packages into engine-specific jobs and return raw outputs and diagnostics.
- **Numerical engines:** separately installed local programs or permissively licensed libraries.
- **Storage:** immutable content-addressed files, SQLite metadata and indexes, and canonical versioned JSON contracts.

No solver-specific object type may become part of a persisted Prometheus project contract.

## 6. Core subsystems

### 6.1 Project bundle and artifact store

Every supplied file receives a cryptographic hash, media type, detected format, version information, origin, size, parser status, and immutable source location. Original artifacts are never rewritten. Derived representations always identify their source hash, parser, parser version, and settings.

### 6.2 Sandboxed parser workers

CAD, PDFs, spreadsheets, source code, schematics, logs, and solver files are processed outside the desktop process. Workers have CPU, memory, output-size, archive-depth, and time limits. They have no network access unless explicitly authorized and cannot execute supplied macros, scripts, or binaries.

### 6.3 Artifact graph

The graph connects artifacts and engineering entities, including CAD bodies, BOM rows, components, materials, interfaces, joints, contacts, electrical nets, firmware symbols, control signals, fluid regions, requirements, scenarios, evidence claims, and observations.

Representative relationships include:

- `contains`
- `represents`
- `references`
- `connected_to`
- `controlled_by`
- `powered_by`
- `constrained_by`
- `measured_by`
- `satisfies`
- `contradicts`
- `supersedes`

Every inferred edge records evidence, inference method and version, confidence, alternatives, review state, reviewer, and timestamp.

### 6.4 Semantic system model

The reviewed system model contains components, geometry, materials, joints, contacts, connections, nets, fluid regions, control blocks, interfaces, coordinate frames, units, and uncertainties. Candidate and confirmed state remain distinct.

### 6.5 Proof-obligation compiler

Requirements and scenarios compile into questions with measurable acceptance conditions. A requirement records its subject, behavior, quantity, comparator, limit, unit, scenario, criticality, source, and review state. If explicit requirements are absent, Prometheus may propose clearly labeled screening obligations from approved failure-mode policies.

### 6.6 Capability registry and planner

Every built-in check and solver recipe declares:

- Questions answered.
- Required entities and inputs.
- Optional inputs and uncertainty support.
- Applicable physical regimes and model assumptions.
- Supported boundary conditions.
- Numerical engine and adapter version.
- Expected outputs and resource class.
- Validation level.
- Conditions that invalidate the result.

The planner matches proof obligations to capabilities and explains selections, omissions, inapplicability, unsupported work, and missing information.

### 6.7 Review gates

Three reviews are required:

1. Semantic review of identities, materials, interfaces, and connections.
2. Intent review of requirements, scenarios, limits, and safety factors.
3. Analysis review of loads, restraints, contacts, meshes, turbulence models, circuit assumptions, and control approximations.

Missing questions are prioritized by consequence, number of obligations unlocked, answerability, acquisition cost, and sensitivity.

### 6.8 Solver runtime

Numerical engines execute in isolated jobs, not inside the Qt UI process. Each job receives an immutable analysis package and emits a result package. The C++ runtime owns capability detection, version checks, self-tests, launch, cancellation, timeouts, resource limits, logs, caching, and safe resume.

### 6.9 Result normalization and coverage

The C++ core converts validated raw outputs into findings while preserving raw results, convergence state, mesh and time-step information, warnings, assumptions, adapter version, engine version, and evidence lineage.

Coverage reports requirements satisfied, requirements violated, indeterminate work, unsupported work, artifact gaps, semantic gaps, scenario coverage, domain coverage, and the highest-value missing information.

## 7. Universal project intake

Arbitrary intake means that every supplied artifact is accepted into the inventory or explicitly rejected for a recorded reason. It does not mean every proprietary format is understood immediately.

Every artifact receives one of four comprehension states:

1. Semantically parsed.
2. Partially parsed with documented gaps.
3. Opaque but preserved.
4. Failed or quarantined.

Initial semantic support prioritizes portable formats:

- STEP and IGES CAD; STL and related meshes as geometry-only sources.
- CSV/XLSX BOMs and engineering tables.
- PDF, HTML, Markdown, text, images, and scanned documents.
- JSON, YAML, and XML configuration.
- Source-code trees and build manifests.
- Open schematic and PCB formats such as KiCad.
- SPICE netlists.
- Modelica models.
- Solver decks and results.
- Test logs, time-series data, and measurement metadata.

Proprietary CAD, EDA, and simulation artifacts remain opaque until an installed native application, converter, or plugin exports a supported representation.

### 7.1 Portable project layout

```text
project.prometheus/
  manifest.json
  objects/sha256/
  derived/
  graph/
    semantic-graph.json
    review-decisions.json
  evidence/
    claims-and-sources.json
  runs/
```

The local filesystem holds immutable objects. SQLite stores metadata, graph edges, review state, and indexes. A dedicated graph database is not required for the initial product.

### 7.2 Entity resolution

Identity resolution uses the strongest evidence first:

1. Immutable IDs and exact hashes.
2. Reference designators, exact part numbers, and CAD metadata.
3. Explicit cross-references.
4. Names, geometry signatures, connection patterns, and context.
5. LLM-assisted candidates.

An LLM-proposed relationship remains unconfirmed until deterministic evidence or human review accepts it. Conflicting revisions create explicit conflict sets rather than silent winner selection.

## 8. Proof obligations and planning

The scenario model captures initial conditions, loads, disturbances, environment, commands, sequence, duration, duty cycle, component states, fault conditions, and expected outputs.

Proof obligations follow this state machine:

```text
candidate
  -> confirmed
  -> planned
     -> blocked
     -> not_applicable
     -> not_evaluated
     -> ready
        -> running
           -> satisfied | violated | indeterminate
```

Every completed obligation traces to a requirement or clearly labeled screening policy, scenario, affected entities, analysis plan, reviewed assumptions, execution package, numerical output, and finding.

The same reviewed project snapshot must always generate the same plan. A change invalidates only the dependent graph region and runs.

## 9. Reference numerical-engine portfolio

Prometheus exposes one application and report. The engines below are internal numerical tools, not independent Prometheus backends.

| Domain | Initial engine | First supported workflow | Initial exclusion boundary |
| --- | --- | --- | --- |
| Geometry and kinematics | Open Cascade and Project Chrono | Assembly DOF, prescribed motion, rigid-body dynamics, contact, and reaction loads | Flexible-body and granular claims |
| Structural | CalculiX | Linear-static, isotropic, small-deformation solid mechanics | Nonlinear materials, fatigue, fracture, and complex contact |
| Thermal | CalculiX initially; Elmer later | Steady and transient solid conduction with reviewed heat sources and convection | Automatic radiation, phase change, and unrestricted conjugate heat transfer |
| Electrical and power | ngspice | DC operating point, transient response, parameter sweeps, and voltage/current/power limits | Signal integrity, RF, and electromagnetic-field claims |
| CFD | OpenFOAM through WSL2 | Steady incompressible internal flow, pressure drop, and reviewed forced convection | Transient, compressible, combustion, and multiphase flow |
| Controls and system dynamics | OpenModelica plus C++ linear checks | Reviewed time response, poles, settling, overshoot, and stability margins | Automatic proof of nonlinear or hybrid-system stability |

Gmsh is the initial separately installed meshing engine. Open Cascade remains the canonical B-Rep geometry engine.

Relevant primary documentation:

- Project Chrono: <https://github.com/projectchrono/chrono>
- CalculiX: <https://www.calculix.de/>
- Elmer FEM: <https://github.com/ElmerCSC/elmerfem>
- ngspice: <https://ngspice.sourceforge.io/docs/ngspice-manual.pdf>
- OpenFOAM on Windows: <https://openfoam.org/download/windows/>
- OpenModelica command-line interface: <https://openmodelica.org/doc/OpenModelicaUsersGuide/v1.25.7/introduction.html>
- Gmsh: <https://gmsh.info/doc/texinfo/gmsh.html>

### 9.1 Installation and licensing model

Prometheus detects compatible local engines, reports required and detected versions, links to official installation instructions, and runs an engine-specific self-test before enabling a capability. It never silently downloads, upgrades, or substitutes an engine.

GPL engines are initially installed separately by users and invoked as independent processes. The proprietary distribution, adapter boundaries, packaging, and documentation require qualified licensing review before release. The architecture does not assume that a process boundary by itself resolves every licensing obligation.

Before commercial distribution, the repository’s own license and every bundled or invoked dependency’s obligations must be resolved and documented as part of the release gate.

Every run records the executable version and hash, environment, adapter version, inputs, outputs, and diagnostics. OpenFOAM manifests additionally record the WSL distribution and Linux package environment.

## 10. Cross-domain orchestration

Solvers never call one another directly. The C++ dependency graph validates and records every exchange.

Coupling advances through:

1. Independent analyses.
2. Sequential scalar transfers.
3. Sequential field transfers.
4. Iterative coupling.
5. Transient co-simulation.

Every exchange records quantity type, unit, representation, source result, source and target entities, coordinate frame, time basis, spatial mapping, conservation method, uncertainty, applicability interval, and review state.

Initial coupled paths are:

- Electrical losses to thermal heat sources to temperature-dependent ratings.
- CFD pressure and heat-transfer fields to structural and solid-thermal boundaries.
- Controller outputs to mechanism motion to structural loads.
- Structural deformation to clearance/collision checks and, later, fluid geometry.

Iterative loops declare update order, initial state, relaxation, tolerances, conservation checks, maximum iterations, residual history, and divergence detection. Failure to converge is indeterminate.

Prometheus implements simple C++-controlled file and scalar transfers first. FMI may later support dynamic model exchange and co-simulation. preCICE may later support spatial transient coupling after independent OpenFOAM and CalculiX adapters are validated.

Primary references:

- FMI: <https://fmi-standard.org/about/>
- OpenModelica FMI support: <https://openmodelica.org/doc/OpenModelicaUsersGuide/latest/fmitlm.html>
- preCICE: <https://precice.org/docs>

## 11. Product workflow

The product presents one engineering-review process:

1. **Create project:** snapshot supplied files and display artifact coverage and privacy concerns.
2. **Resolve project:** review prioritized identity, version, material, interface, and connection questions.
3. **Confirm intent:** review requirements, scenarios, design factors, limits, environments, and fault cases.
4. **Review analysis plan:** inspect ready, blocked, unsupported, inapplicable, and engine-dependent obligations.
5. **Execute locally:** monitor dependencies, resource use, warnings, convergence, caching, cancellation, and resume.
6. **Review findings:** organize results by requirement, scenario, subsystem, and severity rather than by engine.
7. **Understand coverage:** expose violations, scoped successes, unknowns, unsupported work, and the highest-value next information.
8. **Compare revisions:** show changed artifacts, graph state, assumptions, invalidated runs, and findings.

Analysis depth profiles are:

- **Screen:** fast deterministic checks and cached results.
- **Standard:** supported reviewed solver workflows.
- **Deep:** sensitivity studies, convergence refinement, and coupled analyses.

Depth changes cost and completeness, not truth standards.

## 12. Verification, validation, and trust

Prometheus distinguishes solving a declared mathematical problem correctly from selecting a physically adequate model and from covering the important project questions.

Capability trust labels are:

- `experimental`
- `contract_tested`
- `analytically_verified`
- `solver_benchmarked`
- `physically_correlated`
- `externally_validated`

The project report cannot imply more confidence than its least-validated decisive analysis.

### 12.1 Validation pyramid

1. Contract and error-state tests.
2. Closed-form or manufactured-solution comparisons.
3. Official numerical-engine benchmark reproduction.
4. Mesh, time-step, tolerance, sensitivity, conservation, and convergence studies.
5. Independent solver comparison.
6. Instrumented physical correlation.
7. Blind external-project validation.

### 12.2 Benchmark corpus

The corpus includes synthetic fixtures, canonical benchmark problems, unrelated public projects, failed/corrected pairs, instrumented physical builds, clean negative cases, and confidential pilot projects evaluated locally.

Seeded defects include geometry and constraint errors, collisions, incorrect materials, excessive stress/deformation, thermal failures, voltage/current failures, unstable controls, invalid fluid boundaries, solver nonconvergence, unit/frame corruption, conflicting revisions, and missing requirements.

Measured subsystem metrics include parser containment, entity-resolution precision/recall, requirement extraction misses, planner applicability recall, invalid-check rejection, numerical error, conservation, convergence, reproducibility, finding false positives/false negatives, coverage accuracy, setup time, review burden, and rerun time.

### 12.3 Release validation gate

The six-domain release requires:

- At least one `solver_benchmarked` supported workflow per domain.
- Known-pass and known-fail projects for decisive workflows.
- Parser, execution, and convergence failures that cannot become passes.
- Cross-domain unit, conservation, and mapping tests.
- Accurate unsupported and indeterminate coverage.
- Reproducible external-engine runs on the Windows runtime.
- Independent review of assumptions and boundary generation.
- No unresolved critical false-negative regression.

## 13. Security and privacy invariants

- Original project artifacts are immutable.
- Parsers and engines run with explicit resource and filesystem boundaries.
- Archive expansion, symlinks, path traversal, formulas, macros, and executables are treated as hostile inputs.
- Uploaded code is analyzed but never executed as part of ingestion.
- Candidate LLM extraction is optional and cannot make engineering decisions.
- Secrets are detected before any authorized external-provider request.
- Proprietary-data transmission requires an explicit user action and recorded provider policy.
- Raw engine output, logs, and failure diagnostics remain available for audit.

## 14. Documentation and execution structure

The existing `Prometheus_Phase_2_Evidence_to_Execution_Build_Prompt.md` is source material for Program 01. It must not be executed as one monolithic task.

The implementation program is documented as:

```text
docs/program/
  00-master-roadmap.md
  01-trust-kernel/
    01a-integrity-and-contracts.md
    01b-data-driven-execution.md
    01c-evidence-acquisition.md
    01d-native-integration.md
  02-universal-project-intake.md
  03-semantic-engineering-graph.md
  04-requirements-scenarios-planner.md
  05-local-solver-runtime-and-sdk.md
  06-domain-adapters/
    06a-geometry-kinematics.md
    06b-structural.md
    06c-thermal.md
    06d-electrical-power.md
    06e-cfd.md
    06f-controls-dynamics.md
  07-cross-domain-orchestration.md
  08-validation-and-calibration.md
  09-product-hardening-and-pilots.md
  10-solver-component-ecosystem.md
```

### 14.1 Revised Phase 2 sequence

Program 01A first corrects misleading fixture provenance, automatic review acceptance, documentation claims, generic quantity/evidence/execution contracts, and failure states.

Program 01B then creates immutable synthetic Motor A and Motor B packages, removes production PM-36 constants from the engineering controller, drives C++ calculations from the bound package, and proves offline reproduction. The motor arm is a conformance fixture, not a product specialization.

Program 01C adds safe acquisition, source preservation, deterministic parsing, optional LLM candidate extraction, review, and immutable publication.

Program 01D completes the review-gated Qt workflow, portable manifests, Windows native CI, and end-to-end reopen/reproduction tests.

Program 01C cannot begin until 01B closes the trusted data-to-execution loop.

### 14.2 Execution discipline

Every program follows:

```text
Approved specification
  -> implementation plan
  -> tests and benchmark fixtures
  -> implementation
  -> complete verification
  -> independent review
  -> milestone report and exact commit
  -> explicit approval for the next program
```

A milestone report records implemented behavior, verification evidence, unsupported behavior, security and licensing implications, validation level, known false-positive and false-negative risks, exact commit, and reproduction instructions.

## 15. Explicit non-goals and invariants

- Do not add unrelated CAD convenience features while the trust kernel and compiler foundation remain open.
- Do not maintain duplicate production physics implementations in different languages.
- Do not allow a numerical engine or LLM to issue a project verdict.
- Do not silently infer missing numerical values or boundary conditions.
- Do not equate file acceptance with semantic understanding.
- Do not equate solver convergence with physical validity.
- Do not hide unsupported files, requirements, scenarios, domains, or analyses.
- Do not introduce coupled simulation before independent adapters are validated.
- Do not freeze domain-specific types into the solver-independent project contracts.
- Do not begin a new program merely because the preceding code compiles; its capability gate must pass.

## 16. First broad-release definition

The first credible broad release is achieved when an external engineer can import a heterogeneous project, account for every artifact, review the reconstructed graph, confirm requirements and scenarios, understand the analysis plan, execute at least one validated workflow in each of the six domains, inspect traceable findings, see unsupported and indeterminate coverage prominently, change an artifact, rerun only affected work, and export a reproducible offline review package.

This release is a broad engineering screening and solver-orchestration product. It is not a certification system and does not claim universal coverage of arbitrary engineering physics.
