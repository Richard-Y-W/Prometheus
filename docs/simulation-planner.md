# Simulation planner

The planner chooses the cheapest method whose evidence and applicability gates answer a requested question: rule checks, geometry/kinematics, algebraic low-state models, targeted rigid-body dynamics, simplified structural/thermal checks, then explicit opt-in external solvers.

Plans declare required data, affected subgraph, dependencies, checker version, budget, cache keys, stop/escalation conditions, and expected outputs. Insufficient evidence produces `indeterminate`. Cache identity includes assembly subgraph, scenario, component revisions, checker settings/version, and uncertainty policy.
