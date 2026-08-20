# Prometheus Product Vision and Phased Deployment Plan

## Document status

- **Purpose:** Define the product Prometheus is intended to become, record the capabilities already established, and provide an evidence-driven path from the current prototype to a usable deployed product.
- **Current implementation reference:** `main` as of 2026-08-20 (uncommitted at time of writing; see [docs/phase-07-requirements-and-planning.md](phase-07-requirements-and-planning.md) for the latest checkpoint evidence).
- **Current maturity:** Phase 1 (real-folder screening) and Phase 5 (reviewed component and evidence intake) are complete against their exit gates. Phase 2 (multi-project evidence), Phase 3 (CalculiX structural workflow), and Phase 4 (persistence/portability) have substantial checkpoint evidence recorded in their own `docs/phase-0N-*.md` files but remain open pending outside-user/physical-hardware trial evidence. Phase 6 (semantic engineering graph) and Phase 7 (requirements and thin planning) are underway in small, evidence-scoped checkpoints rather than closed. The prototype has a strong trust boundary, real STEP/XDE import, a package-driven C++ motor-arm analysis backend that now also consumes manually entered (non-fixture) components, hash-verified CAD-to-component binding with supersede detection, a persisted CAD-to-CAD joint graph edge, a real reviewed-requirement/uncovered-work model for the structural workflow, and multiple materially different open-source project inputs under trial.
- **First deployable target:** A bounded Windows mechanical-design screening application. It is not initially a universal engineering authority.
- **Long-term target:** A local CAD-centered engineering project compiler and solver-orchestration environment that can assemble reviewed real-world component data, model operating scenarios, run appropriate bounded analyses, and reveal failures, unknowns, and incomplete coverage before an expensive physical prototype is built.

---

## 1. The product goal

Prometheus should help a user move from an engineering concept to a reviewable virtual prototype.

The intended end-to-end experience is:

1. Create a new project or open an existing engineering project folder.
2. Import or construct the CAD assembly while preserving parts, hierarchy, transforms, interfaces, and source identities.
3. Search for, import, or manually define real components such as motors, bearings, fasteners, batteries, controllers, sensors, pumps, gears, and structural parts.
4. Preserve the original sources for each component specification, including datasheets, manufacturer records, measurements, and user-provided evidence.
5. Normalize component properties into typed engineering quantities with units, applicability conditions, tolerances, provenance, and explicit human review.
6. Place and bind those components into the CAD assembly.
7. Define how the design is intended to operate: motion, loads, restraints, environments, duty cycles, power states, requirements, and failure limits.
8. Determine which supported analyses can answer each engineering question.
9. Run authoritative geometry, kinematic, structural, thermal, electrical, fluid, and controls analyses when the required information and validated solver capability exist.
10. Report what failed, what passed within a declared scope, what remains unknown, what was not evaluated, and why.
11. Preserve exact inputs, solver identities, raw outputs, findings, assumptions, limitations, and coverage so the result can be reopened and reproduced.
12. Help the user revise the design and compare alternatives before manufacturing a physical prototype.

The product is not supposed to produce a vague whole-project green checkmark. It should behave like an engineering review partner that can say:

- this motor is undersized for the reviewed holding load;
- these parts interfere at a specific configuration;
- this component exceeds the declared displacement or stress limit in the supported model;
- this wire, driver, or battery is outside the reviewed electrical envelope;
- this thermal model predicts an excessive temperature under the specified duty cycle;
- this question cannot be answered because material, contact, load, requirement, or solver coverage is missing;
- this result is only valid for the stated scenario, assumptions, numerical model, and evidence.

### 1.1 What “add any available component” should mean

The long-term usability goal is that users can add components beyond a fixed built-in catalog. That does not mean Prometheus can safely understand every product page or datasheet automatically on day one.

A trustworthy component intake path should eventually support:

- built-in validated component records;
- manufacturer APIs or structured catalogs;
- local PDFs and datasheets;
- spreadsheets and BOM rows;
- STEP models and other supported CAD representations;
- manually entered specifications;
- measured values;
- supplier information;
- optional machine-assisted extraction that always remains a candidate until reviewed.

Every accepted property needs:

- a typed quantity and unit;
- the original value and unit;
- its source artifact and exact source identity;
- a locator into the source where practical;
- applicability and operating conditions;
- uncertainty or tolerance where known;
- evidence class and validation state;
- explicit user review;
- a stable identity that downstream calculations can reference.

Unknown, conflicting, or unavailable properties must remain visible. Prometheus must never invent a convenient value simply to make a simulation run.

### 1.2 What “simulate the build” should mean

Simulation is not one universal operation. Prometheus should orchestrate bounded authoritative analyses, each with declared inputs, models, applicability, diagnostics, and validation evidence.

Examples include:

- CAD hierarchy and geometry integrity;
- static interference and bounded clearance checks;
- sampled or continuous motion collision;
- rigid-body kinematics;
- torque, speed, current, and duty-cycle checks;
- linear-static structural analysis;
- buckling, fatigue, contact, and nonlinear structural analysis when later supported;
- steady-state and transient thermal analysis;
- circuit and power-budget analysis;
- fluid and CFD workflows;
- controls and system dynamics;
- coupled electrical-to-thermal, controls-to-mechanical, or deformation-to-clearance workflows.

Each analysis must distinguish a completed calculation from a validated claim. A solver returning numbers is not, by itself, proof that the model represents the physical design.

---

## 2. Product principles that should not be compromised

### 2.1 Honest scope over universal claims

Prometheus may report a scoped success only when the relevant requirement, scenario, model, evidence, applicability, solver execution, and validation coverage support it. Missing or unsupported work remains visible.

### 2.2 Evidence before assertion

Engineering values must be traceable to reviewed evidence. Machine extraction, web research, and inference may propose candidates but may not silently publish facts.

### 2.3 One authoritative calculation path

Each engineering recipe names one versioned authoritative backend. UI code, Python services, persistence code, and replay tools must not independently reimplement the calculation and create competing answers.

### 2.4 Fail closed

Missing data, corrupt objects, stale projects, invalid units, solver failure, nonconvergence, unsupported models, or unavailable backends can never become a pass.

### 2.5 Reproducibility

Important analyses should preserve the reviewed inputs, scenario, backend identity, numerical profile, raw outputs, findings, assumptions, limitations, and coverage needed to reproduce and audit the result.

### 2.6 Human review at consequential boundaries

The user must confirm evidence selections, inferred relationships, loads, restraints, requirements, scenarios, and analysis applicability before those choices become authoritative.

### 2.7 Validation begins with each capability

Every new analysis needs benchmarks, known-pass cases, known-fail cases, tolerances, and explicit applicability limits when it is introduced. Validation is not postponed until a distant final phase.

### 2.8 Real projects drive generalization

Prometheus should implement the smallest workflow that answers a real engineering question, observe how it fails across multiple projects, and generalize only the repeated boundaries.

---

## 3. What has already been completed

## 3.1 Historical CAD and interaction foundation

The repository already contains significant CAD capability:

- a C++20 decision core and Qt/QML desktop shell;
- real STEP/XDE import through Open Cascade;
- preservation of assembly hierarchy, names, geometry, transforms, and persistent entity identities;
- tessellated rendering;
- bounds, topology, volume, surface-area, face, and edge inspection;
- part selection, isolation, placement, translation, and rotation;
- undo/redo and coordinate-frame interactions;
- measurement and snapping aids;
- user-confirmed provisional connections;
- exact static solid-intersection checks for supported smaller assemblies;
- sampled revolute collision sweeps;
- explicit deferral instead of false clearance claims for unsupported large collision workloads;
- save/reopen behavior for portions of the project model.

These capabilities establish a real CAD foundation. They do not automatically establish material, load, contact, fastening, requirement, or engineering semantics.

## 3.2 Program 01A: trustworthy evidence and publication

Program 01A established a bounded trust kernel:

- exact synthetic fixture identities that cannot impersonate arbitrary research;
- typed engineering values;
- revision-scoped candidate claims;
- stable claim identities and fingerprints;
- explicit per-claim human review;
- append-only review events;
- separate review and publication actions;
- immutable RFC 8785 canonical package bytes;
- SHA-256 content identity;
- exact stored-byte export and verification;
- SQLite and PostgreSQL integrity backstops;
- durable success and failure replay;
- independent C++ canonical-byte and hash verification;
- retirement of misleading legacy Python decision routes;
- explicit separation between a reviewed input package and an engineering result;
- extensive schema, migration, concurrency, failure-injection, and cross-platform testing.

This work is strong and should largely be preserved. It is infrastructure for trusting inputs, not proof that arbitrary component data is correct.

## 3.3 Program 01B: package-driven C++ execution

Program 01B closed one complete evidence-to-execution loop:

- two reviewed synthetic motor packages differ in one decision-relevant continuous-torque value;
- exact package bytes are independently verified and consumed by C++;
- a shared Qt-free C++ execution library owns the motor calculation;
- Motor A fails and Motor B passes the holding obligation under the same reviewed scenario;
- non-holding outputs remain consistent;
- package, scenario, request, result, and run manifest are stored immutably;
- the desktop and replay CLI use the same authoritative calculation path;
- saved runs reopen and reproduce offline under the recorded numeric identity;
- the duplicate Python production calculation was removed;
- failure handling covers corrupt, missing, stale, unsupported, interrupted, and mismatched execution states.

This is meaningful technical proof. It remains a synthetic four-obligation conformance backend, not a general mechanical solver.

## 3.4 Current real-folder screening prototype

The latest feature branch adds a product-oriented pivot:

- opening an arbitrary local project folder;
- recursive accounting for nested, hidden, supported, unsupported, and unreadable files;
- readable-file byte size and SHA-256 identity;
- classification of STEP, other CAD, document, BOM/table, source, and structured-data artifacts;
- visible unsupported files rather than silent omission;
- automatic loading when exactly one readable STEP assembly exists;
- explicit user selection when multiple STEP assemblies exist;
- a desktop project inventory panel;
- bounded geometry results separated from mechanical unknowns;
- explicit `not_evaluated` status for motion, material/mass, loads/restraints, contacts/supports, and structural strength;
- a leaner roadmap organized around observable project-value gates.

This is the correct immediate direction, but it remains a prototype gate rather than a completed product capability.

---

## 4. What was done particularly well

### 4.1 Trust and truthfulness

Prometheus has unusually strong protection against turning incomplete work into false confidence. Verdict, coverage, and execution state are separate dimensions. A known failure can coexist with incomplete coverage, which reflects real engineering practice.

### 4.2 Cross-language integrity

Python and C++ independently agree on exact canonical package identity. Published inputs are stored as immutable bytes instead of being silently reconstructed from mutable database rows.

### 4.3 Calculation authority

The production motor calculation is centralized in a shared C++ library. Desktop, replay, QML, Python, and persistence code cannot legitimately create alternative production answers.

### 4.4 Reproducible execution

The package-to-request-to-result-to-manifest chain is a valuable foundation for future external solvers. It makes the origin of a finding inspectable and supports offline replay.

### 4.5 Failure handling

The repository handles many corruption, concurrency, transaction, portability, and replay failure modes that would otherwise become expensive later.

### 4.6 Real CAD rather than a fabricated mockup

The Open Cascade path imports actual STEP/XDE content. The project is not merely a web interface wrapped around synthetic geometry.

### 4.7 Recent roadmap correction

The current plan correctly moves real project folders, repeated project trials, and one external structural solver ahead of universal parsers, a generalized planner, a solver SDK, six-domain breadth, and ecosystem work.

---

## 5. Where the project overreached

Program 01B was overengineered relative to the visible capability and current product maturity. One bounded gate became 27 commits, 164 changed files, and roughly 36,700 inserted lines. The integrity work may be useful later, but too much production hardening happened before enough real-project evidence existed.

The correct response is not to delete the trust kernel. It is to freeze it and simplify future scope.

### Preserve

- explicit review;
- typed values and units;
- stable provenance;
- immutable published inputs;
- C++ calculation authority;
- honest coverage and execution states;
- fail-closed behavior;
- exact run artifacts and replay;
- tests protecting consequential trust boundaries.

### Freeze temporarily

- additional generic contract families;
- more synthetic backends;
- universal schema work;
- a general solver SDK;
- a large capability planner;
- plugin/ecosystem architecture;
- new cross-domain orchestration;
- further exhaustive failure injection not tied to an observed release risk.

### Simplify only with evidence

- unreachable legacy compatibility paths;
- duplicate historical models;
- abstractions with only one implementation and no near-term second use;
- persisted fields that no workflow reads;
- repeated documentation that does not add a distinct claim;
- redundant tests that protect no additional risk.

Before removing a complex subsystem, identify the invariant it protects and prove that the invariant remains covered.

---

## 6. Current gaps

Prometheus does not yet:

- understand arbitrary PDFs, datasheets, spreadsheets, BOMs, source code, schematics, proprietary CAD, or simulation files;
- search public component catalogs safely;
- connect a BOM row to a CAD part or datasheet;
- infer materials, joints, fasteners, contacts, loads, restraints, or operating conditions;
- compile user requirements into supported proof obligations;
- automatically select an analysis safely;
- run a real structural, general thermal, circuit, CFD, or controls solver;
- prove continuous collision clearance, fatigue life, fastening integrity, tolerance stack-up, manufacturability, safety, or certification;
- determine whether an arbitrary engineering project works;
- ship as a signed, recoverable, clean-machine Windows product.

The fresh Windows OCCT-enabled Release build compiled successfully and all 22
native suites passed on 2026-08-15. The earlier symlink and unresolved-state
fixture failures are no longer present in the current baseline. The independent
YUBI trial also exposed an OCCT automatic shape-healing access violation; the
production importer now preserves raw transferred topology without that repair
and reports the limitation explicitly.

---

## 7. Phased plan to a usable deployed product

## Phase 0: Consolidate the current branch

### Objective

Turn the large feature branch into a stable, understood baseline without destroying useful trust work.

### Work

- diagnose the three symlink-substitution test failures;
- decide whether Windows Developer Mode, test privileges, reparse-point handling, or an explicit privileged security job is required;
- diagnose the unresolved-state project save failure independently;
- run full CI for the seven post-01B folder-screening commits;
- verify a clean-clone OCCT-enabled Windows build;
- audit dead code, duplicated paths, generated artifacts, and legacy compatibility;
- confirm that the synthetic motor workflow is isolated from general project intake;
- update completion/status documentation with current evidence;
- integrate the accepted branch into `main` only after its required gates pass.

### Exit gate

- clean repository state;
- reproducible clean build;
- green required CI;
- no unexplained required-test failure;
- no unresolved critical or high trust issue;
- accepted baseline integrated into `main`.

## Phase 1: Close the Windows real-folder screening gate — complete

### Objective

Let a user open one real mechanical project folder and receive a useful, honest first screen.

### Work

- verify complete file accounting across nested, hidden, unsupported, unreadable, duplicate, large, and linked entries;
- make file states visibly distinct: inventoried, parsed, evaluated, unsupported, failed, and deferred;
- improve progress and error reporting for large folders;
- automatically import one unambiguous STEP assembly;
- require explicit selection when several STEP files exist;
- preserve import warnings, units, hierarchy, and geometry-repair information;
- show supported static geometry checks;
- show material, mass, load, restraint, contact, motion, and strength questions as unknown or not evaluated;
- give the user a meaningful next action for each important unknown;
- keep session-only data session-only until project trials establish its persistence requirements.

### Exit gate

- one real Windows project opens without developer intervention;
- every discovered file has a visible state;
- supported geometry results reproduce;
- unsupported questions remain visible;
- no UI language implies the entire project passed.

## Phase 2: Test three materially different real projects — current

### Objective

Replace speculative architecture with observed project evidence.

### Project set

1. A small clean STEP assembly.
2. A large assembly with deep hierarchy and performance pressure.
3. A messy mixed folder containing multiple CAD files, BOMs, PDFs, spreadsheets, source files, or ambiguous revisions.

At least one trial should involve someone outside the core development team.

### Record for each project

- installation and setup time;
- time to visible assembly;
- inventory completeness;
- import warnings and failures;
- unit and hierarchy correctness;
- assembly-selection friction;
- unsupported formats encountered;
- missing semantics;
- UI confusion;
- memory and performance;
- crash or recovery behavior;
- the first result that would have saved time or prevented a mistake.

### Exit gate

- three written trial reports;
- ranked product failures and usability friction;
- one selected real component and engineering question for the first solver workflow;
- evidence-based priorities for BOM, datasheet, and material intake.

## Phase 3: Implement one real CalculiX structural workflow — technical workflow complete; real-component evidence open

### Objective

Answer the first meaningful real-project question: whether one selected component satisfies a reviewed stress or displacement requirement under a bounded linear-static model.

### Initial supported model

- one selected solid component;
- isotropic linear-elastic material;
- reviewed Young's modulus and Poisson ratio;
- a small set of explicit load and restraint types;
- generated tetrahedral mesh;
- isolated CalculiX process;
- displacement and stress output;
- convergence and solver diagnostics;
- scoped findings and coverage.

### User flow

1. Select the component.
2. Review or enter its material.
3. Select geometry and define loads.
4. Select geometry and define restraints.
5. Enter or select the requirement limit.
6. Preview the mesh and analysis request.
7. Confirm the scenario.
8. Run CalculiX.
9. Inspect raw diagnostics and visualized results.
10. Review findings, assumptions, limitations, and uncovered questions.
11. Save and replay the run.

### Validation

- analytic tension-bar benchmark;
- cantilever displacement benchmark;
- known-pass case;
- known-fail case;
- mesh-refinement comparison;
- unit conversion tests;
- missing or inadequate restraint detection;
- nonconvergence and solver-failure classification;
- independent CalculiX comparison.

### Exit gate

- one real component executes end to end;
- results agree with reference evidence within declared tolerances;
- missing setup never becomes a pass;
- exact solver inputs and raw outputs are retained;
- findings remain scoped to the supported model.

## Phase 4: Persist and transport real projects

### Objective

Allow work to survive interruption, relocation, and source changes without silently reusing stale results.

### Work

- persist file inventory identities;
- preserve selected assembly and component mappings;
- store reviewed materials, requirements, loads, restraints, and scenarios;
- detect changed or missing source artifacts;
- mark dependent state stale;
- create immutable project snapshots at execution boundaries;
- create a portable Prometheus project bundle;
- add safe backup, recovery, and migration;
- define archive and quarantine behavior for formats actually encountered.

### Exit gate

- close/reopen retains the complete supported workflow;
- changed source files invalidate the correct downstream results;
- interrupted saves recover the last valid state;
- a project bundle opens on another supported clean machine.

## Phase 5: Add reviewed component and evidence intake — exit gate met

See [docs/phase-05-component-intake.md](phase-05-component-intake.md) for
checkpoint-by-checkpoint evidence. Structured CSV/BOM import and
named/linked acquisition remain open as additional intake paths but are not
exit-gate blockers.

### Objective

Allow users to add components beyond the synthetic catalog while retaining trustworthy specifications.

### First intake paths

- manual typed component entry;
- named/linked component acquisition: a manufacturer part name or a URL to a product page or datasheet, fetched and retained as an exact source artifact, with a bounded extractor proposing typed candidate parameters for review;
- structured CSV or BOM import;
- local manufacturer datasheet attachment;
- a deliberately small set of deterministic PDF/table extractors based on actual pilot documents;
- optional candidate extraction that never bypasses review;
- local component library reuse.

### Required behavior

- preserve exact source files and hashes;
- record licenses and acquisition state;
- extract candidates with source locators;
- represent conflicting candidates;
- require explicit selection and review;
- retain original and normalized values;
- handle unit conversions visibly;
- require applicability and operating conditions;
- allow unknown values;
- publish immutable reviewed component packages;
- bind a component package to a CAD entity;
- invalidate dependent analyses when a binding or reviewed value changes.

### Exit gate

- a user adds a component not compiled into Prometheus;
- its critical specifications trace to reviewed sources;
- the component binds to a CAD part;
- a supported analysis consumes the reviewed values;
- conflicting or missing values remain visible and block unsupported claims.

## Phase 6: Build the first semantic engineering graph — checkpoint 2 landed

See [docs/phase-06-semantic-engineering-graph.md](phase-06-semantic-engineering-graph.md)
for evidence. Two edges have landed so far: checkpoint 1 promotes the
CAD-to-component binding into a real, persisted, append-only graph edge, and
checkpoint 2 does the same for a confirmed CAD-to-CAD revolute joint. The
other seven entity families remain future work, added only as real
workflows need them.

### Objective

Connect the artifacts needed by the real mechanical workflows.

### Initial entity families

- CAD assembly and part;
- BOM row;
- component and revision;
- source document and evidence claim;
- material;
- joint and contact;
- load and restraint;
- requirement and scenario;
- analysis request and finding.

### Rules

- inferred relationships retain provenance and confidence;
- user confirmation is required at consequential boundaries;
- corrections append review state instead of rewriting source evidence;
- the graph begins with entities demonstrated by real projects;
- unsupported entities remain opaque artifacts rather than being forced into a false schema.

### Exit gate

- the pilot projects can express their required relationships;
- every consequential edge is reviewable and traceable;
- component, CAD, requirement, and analysis identities connect end to end.

## Phase 7: Add requirements, scenarios, and thin planning — checkpoint 1 landed

See [docs/phase-07-requirements-and-planning.md](phase-07-requirements-and-planning.md)
for evidence. Checkpoint 1 replaces the structural workflow's two hardcoded
requirement fields with a real reviewed requirement list (quantity,
comparator, limit, unit, applicability, criticality, source), preserves
requirements outside CalculiX's coverage as visible uncovered work instead of
making them unrepresentable, and wires the previously-unused
`decision::summarize` coverage/verdict rollup into the structural findings
so a run can never be reported as satisfied while real uncovered work
remains. Capability matching and analysis recommendation remain unproven
beyond a fixed single-capability check — that needs a second capability
before it can be called real. The motor-arm subsystem is untouched.

### Objective

Make analyses answer explicit engineering questions.

### Work

- add a reviewed requirement form with quantity, comparator, limit, unit, applicability, criticality, scenario, and source;
- compile supported requirements into proof obligations;
- match obligations to available validated capabilities;
- show required missing inputs;
- recommend an analysis only when its applicability is explainable;
- require confirmation before execution;
- preserve unsupported requirements as visible uncovered work;
- show why each finding satisfies, violates, or cannot answer its obligation.

### Exit gate

- every production analysis traces to a reviewed requirement or explicit exploratory question;
- unsupported questions remain visible;
- users can understand and override the proposed plan.

## Phase 8: Validate the complete bounded mechanical product

### Objective

Move supported workflows beyond contract testing toward credible engineering validation.

### Evidence maintained per capability

- analytic or authoritative benchmark;
- known-pass case;
- known-fail case;
- applicability limits;
- numerical tolerance;
- mesh or discretization sensitivity;
- solver identity;
- independent comparison;
- physical observation where practical;
- false-positive and false-negative assessment;
- regression fixtures.

### Exit gate

- release tolerances are documented and enforced;
- no unresolved critical known false-negative regression exists;
- the UI exposes validation level and applicability;
- product claims match the evidence.

## Phase 9: Windows alpha packaging

### Objective

Let invited users install and operate Prometheus without a development environment.

### Work

- produce a release build;
- bundle Qt, Open Cascade, CalculiX, and required runtimes;
- include complete third-party notices;
- build and sign the installer and executable;
- define application, cache, log, and project storage locations;
- support uninstall without deleting user projects;
- test standard-user permissions;
- test spaces, Unicode, long paths, network-disabled operation, and external drives;
- sandbox untrusted parsers and solver processes;
- add crash recovery and privacy-conscious diagnostics;
- provide a user-reviewed support bundle;
- automate packaged install, launch, benchmark, save, reopen, and uninstall tests;
- document supported Windows versions and hardware.

### Exit gate

- clean-machine installation succeeds;
- no compiler, Python environment, or developer Qt installation is required;
- packaged smoke tests pass;
- interrupted work recovers;
- security review has no unresolved critical or high finding;
- limitations are visible in the product.

## Phase 10: External pilot and usability refinement

### Objective

Prove that engineers outside the development team can obtain useful, correctly understood findings.

### Pilot

- recruit three to five trusted mechanical engineers;
- use projects they already understand;
- observe unassisted setup and operation;
- compare results against their existing workflow;
- record misunderstood statuses, incorrect setup, missed failures, useful findings, saved time, and trust concerns.

### Metrics

- installation success;
- project-open success;
- STEP import success;
- time to first evaluated finding;
- analysis completion rate;
- save/reopen success;
- recovery success;
- percentage of findings whose scope users correctly understand;
- reported false positives and false negatives;
- support burden;
- willingness to use Prometheus again.

### Exit gate

- multiple engineers reproduce consequential findings without developer intervention;
- no recurring critical misunderstanding of scope or coverage remains;
- setup time is acceptable;
- at least one workflow demonstrably saves engineering time or catches an expensive error.

## Phase 11: Production v1

### Product boundary

Production v1 should be a bounded Windows mechanical-design screening product with:

- project-folder inventory;
- STEP/XDE assembly import;
- CAD inspection and supported geometry checks;
- visible unknown and unsupported work;
- reviewed component specifications;
- component-to-CAD binding;
- material, load, restraint, requirement, and scenario review;
- one validated CalculiX linear-static workflow;
- scoped findings and coverage;
- save/reopen and portable project bundles;
- exact run artifacts and offline reproduction;
- signed installation and updates;
- support and recovery tooling.

### Operational requirements

- internal, pilot, and stable release channels;
- signed artifacts and published checksums;
- reproducible release builds;
- rollback and project-format migration strategy;
- supported-version policy;
- dependency and license inventory;
- vulnerability and security-response process;
- backup and recovery documentation;
- release validation records;
- no forced cloud upload of private engineering projects.

### Exit gate

- installer, update, rollback, migration, and recovery drills pass;
- validation and packaged-product suites pass;
- pilot evidence supports usability;
- release artifacts reproduce from the recorded commit;
- documentation and UI limitations agree;
- every analysis names its authoritative backend and validation level.

## Phase 12: Expand toward the full vision

Only after the mechanical v1 produces real value should Prometheus expand based on user demand.

Candidate order:

1. improved geometry and continuous kinematics;
2. structural contact, nonlinear behavior, buckling, and fatigue;
3. bounded thermal workflows;
4. circuits, power, batteries, and motor-controller integration;
5. fluid and CFD workflows;
6. controls and system dynamics;
7. cross-domain coupled analyses;
8. broader component catalog connectors;
9. stable third-party solver and component extensions;
10. optional team collaboration or cloud execution with explicit privacy controls.

Each domain begins with one real project question, one authoritative backend, benchmark evidence, known-pass and known-fail cases, and explicit limitations. Domain breadth is not a substitute for validation.

---

## 8. Immediate next actions

1. Run one documented outside-user session without live developer guidance.
2. Confirm that the outside user can find a loadable STEP, recognize ambiguity,
   and avoid interpreting inventory as a project-wide pass.
3. Use the selected YUBI `BRACKET_GRIPPER` component to design the smallest
   CalculiX linear-static slice.
4. Require explicit reviewed material, load, restraint, displacement limit,
   stress limit, mesh controls, and model assumptions before execution.
5. Implement an analytic benchmark, a known-pass case, and a known-fail case
   with the solver adapter.
6. Add non-synthetic component intake only for sources required by that slice.
7. Consolidate persistence around the proven project workflow.
8. Add recovery and clean-machine packaging evidence before calling the
   product an alpha.

---

## 9. Decision rules for future work

A proposed task should normally satisfy at least one of these conditions:

- unlocks a real project that currently fails;
- enables a new trustworthy engineering finding;
- exposes an important unsupported or failure state honestly;
- reduces setup or review time materially;
- improves validation evidence;
- fixes a release, recovery, security, or reproducibility risk observed in practice.

Defer a task when it primarily:

- generalizes a boundary used by only one workflow;
- creates a universal schema without real examples;
- adds a new domain before the existing workflow is useful;
- hardens a hypothetical threat while an immediate product gate remains unproven;
- improves architecture without improving a project outcome, validation result, or deployment risk;
- hides missing information to make a demonstration look complete.

---

## 10. Definition of success

Prometheus succeeds when an engineer can use it to discover a costly design problem earlier and understand exactly why the finding is credible.

The long-term experience should be:

> Import or build the assembly, add real reviewed components, describe what the design must do, run the supported simulations, see what breaks and what remains unknown, revise the design, and reproduce the evidence before building the physical prototype.

The path to that experience is not to implement every engineering domain at once. It is to prove one valuable real workflow, validate it, deploy it, learn from engineers, and expand without weakening the evidence and failure boundaries already established.
