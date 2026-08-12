# Component model

The former Program 01A implementation stores a manufacturer, component identity, revision, typed parameters, source documents, and mutable per-field evidence review metadata. Its published revisions are hash-checked and reconstructed through the v1 schemas. Amended Program 01A is replacing that boundary with revision-scoped slots, immutable candidate claims, append-only reviews, exact stored RFC 8785 bytes, and explicit v2 schemas; that implementation is not complete yet.

The certification tier describes the evidence state (`provisional`, `geometry_verified`, `behavior_verified`, `physically_validated`, or `system_validated`). It is not a safety or design certification. The current synthetic PM-36 fixture remains `provisional` and explicitly warns that it is unsuitable for a physical design decision.

The execution-component package is reviewed input. It contains no requirement, solver result, finding, pass, or failure verdict. Program 01B must make C++ consume it before a published revision affects an engineering calculation.

Typed component ports, frames, interfaces, and deterministic compatibility rules are target semantic-graph capabilities. They are not implemented by the current parameter-only fixture path.
