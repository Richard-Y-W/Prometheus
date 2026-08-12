# Product scope

## Product goal

Prometheus targets heterogeneous engineering projects rather than one machine class. The intended workflow accepts the files an engineer already has, preserves and relates them, asks what the project must do, compiles those requirements into proof obligations, selects bounded local analyses, and reports violations, scoped non-violations, unknowns, and coverage.

Initial domain goals are geometry/kinematics, structural mechanics, thermal analysis, circuits/power, CFD, and controls/system dynamics. Each supported recipe must state its required inputs, applicable regime, boundary conditions, computation backend, validation level, uncertainty treatment, and invalidating conditions. Six domain labels do not imply universal physics coverage.

## Current repository scope

The present codebase provides:

- real Open Cascade STEP/XDE import and assembly inspection;
- a separate synthetic motor-arm conformance fixture;
- exact lookup of one synthetic component source, not public component research;
- explicit per-field evidence review and canonical published component input;
- fixed C++ motor-arm conformance calculations that do not yet consume the published package;
- no production Python engineering-decision path.

The repository cannot determine whether an arbitrary engineering project works. It has no general artifact store, cross-file semantic graph, requirement compiler, capability planner, isolated solver runtime, coverage engine, or external numerical solver adapter.

## Claim boundary

Prometheus may eventually report `no_violations_detected_within_scope` only when named requirements, scenarios, reviewed inputs, applicable models, valid executions, and coverage support that statement. It must separately report violated, indeterminate, unsupported, not-applicable, and not-evaluated obligations.

Prometheus does not claim certification, regulatory approval, safety assurance, unrestricted rigid-body dynamics, nonlinear FEA, fatigue or fracture analysis, general CFD, electromagnetic simulation, or project-wide correctness. Physical tests and qualified engineering review remain distinct evidence.

The [master roadmap](program/00-master-roadmap.md) defines the capability gates. Program 01B is next: reviewed package values must drive C++ outcomes and reproduce offline before broader evidence acquisition or solver work begins.
