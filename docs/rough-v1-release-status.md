# Rough V1 release status

This repository is an engineering preview demonstrating the complete Prometheus thesis: import real CAD, compile traceable component evidence, describe a scenario, execute deterministic checks, and report understandable findings. It is not a safety certification product.

## 1. Physics

Implemented and tested: motor torque-speed, acceleration torque, current estimate, continuous holding, periodic one-node thermal RC, center-of-gravity primitive, rectangular support-polygon tipping primitive with effective gravity, exact static solid interference, and sampled revolute-range collision.

Partial: the desktop motor-arm flow reports payload COG but does not yet provide complete per-part mass/support-polygon authoring. Shaft, beam/bracket, bearing, general retention, and general constraint-Jacobian checks remain unsupported in the desktop flow.

## 2. Electrical compatibility

Implemented in the motor fixture path: driver current-limit evaluation and evidence-backed motor electrical parameters. General voltage, connector, power-path, supply/battery current, and battery-sag workflows remain future work.

## 3. Component intelligence

Implemented: normalized versioned research entities, evidence claims, source metadata, review-before-publication, immutable published revisions, cached reuse, and a mock/offline research provider that works without an API key.

Mocked: public manufacturer search, live PDF parsing, chart digitization, and live LLM providers. The fixture provider never claims public research occurred.

## 4. CAD usability

Implemented: STEP/XDE import, hierarchy extraction, persistent IDs, transforms, tessellation, topology/volume/bounds metadata, selection, hide/isolate, standard cameras, world/local move and rotate, snapping increments, transient dimensions, undo/redo, bounds-derived snap-to-mate, measurement, and project round-tripping.

Partial: bounds anchors are placement aids, not authoritative mounting ports. Rotation rings, box/multi-selection, section planes, production drafting, sketches, and parametric feature history remain unsupported.

## 5. Constraints and assembly semantics

Implemented: persisted user-confirmed fixed/revolute/sliding/contact graph edges, revolute axis/limits for the motor-arm test, overlap classification, and sampled collision with connected-pair exclusion.

Partial: general degree-of-freedom counting, missing axial/radial retention, load-path analysis, power-path analysis, and authoritative interface compatibility remain future work.

## 6. Product hardening

Implemented: environment-based configuration, request validation, normalized persistence, atomic native project save, file/hash/version metadata, CI, dependency audit, and a unified verification script.

Partial: production CAD/PDF process sandboxing, active parser interruption, comprehensive URL acquisition/SSRF enforcement, packaged crash reporting, signed Windows installer, and automated desktop pointer-event E2E tests remain release-hardening work.

## Reproduce the verified build

Run `./scripts/verify.ps1` from PowerShell. Native Windows users with the documented MSYS2 Qt/OpenCascade prerequisites can launch `./out/build/windows-debug/desktop/app/prometheus_desktop.exe` after building the `windows-debug` preset.
