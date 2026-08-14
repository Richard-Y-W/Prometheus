# Analysis planner target

The approved architecture calls for a capability registry and planner that select the cheapest validated method whose evidence and applicability gates can answer a proof obligation: typed rules, geometry/kinematics, algebraic low-state models, targeted dynamics, and bounded external solvers.

That general planner is not implemented. The current Qt motor-arm UI invokes a fixed conformance sequence and does not compile arbitrary requirements, compare capability contracts, or calculate project coverage.

A future plan must declare its proof obligation, required reviewed data, affected semantic subgraph, computation backend and version, applicability regime, boundary conditions, uncertainty treatment, execution budget, cache identity, stop/escalation conditions, expected outputs, and invalidating conditions. Insufficient evidence or unavailable execution resolves to `indeterminate` or `not_evaluated`.

Program 04 owns proof-obligation planning. Program 05 owns the isolated solver runtime and adapter SDK. No external solver adapter exists before those gates.
