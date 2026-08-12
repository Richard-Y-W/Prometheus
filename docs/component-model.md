# Component model

Program 01A stores a manufacturer, component identity, revision, typed parameters, source documents, and evidence review records. Published revisions are hash-locked and export through `schemas/execution-component.schema.json`; engineering value and evidence shapes are defined in `schemas/engineering-value.schema.json` and `schemas/evidence-record.schema.json`.

The certification tier describes the evidence state (`provisional`, `geometry_verified`, `behavior_verified`, `physically_validated`, or `system_validated`). It is not a safety or design certification. The current synthetic PM-36 fixture remains `provisional` and explicitly warns that it is unsuitable for a physical design decision.

The execution-component package is reviewed input. It contains no requirement, solver result, finding, pass, or failure verdict. Program 01B must make C++ consume it before a published revision affects an engineering calculation.

Typed component ports, frames, interfaces, and deterministic compatibility rules are target semantic-graph capabilities. They are not implemented by the current parameter-only fixture path.
