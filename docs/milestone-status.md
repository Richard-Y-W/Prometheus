# Milestone status

## Milestone 0 — verified

Contracts, decisions, C++ core, Qt shell target, versioned service health, CI, and unified verification are present.

## Milestone 1 — verified

Verified: Qt 6.11.1 application compiles and launches on Windows 11; OCCT 7.9.3 XDE round-trip preserves a positioned three-part hierarchy; names, bounding boxes, volumes, and tessellation are extracted; malformed STEP fails safely; the bundled `fixtures/assemblies/motor-arm.step` loads and renders in Qt Quick 3D. Tree and viewport selection, visibility, isolate/show-all, orbit, zoom, and fit-to-view are implemented. Import runs outside the UI thread. Atomic project save/reopen reloads the CAD artifact and preserves XDE identifiers.

Known limitations: the UI currently flattens nested XDE labels for presentation; cancellation discards a completed OCCT result rather than interrupting an active parser; richer project journaling and CAD-parser process isolation remain later hardening work.

## Milestone 2 — verified vertical path

Normalized manufacturer, component, immutable revision, parameter, source-document, evidence, research-job/event, and project-binding entities are backed by Alembic migrations. Search and exact-SKU research APIs support idempotent jobs, staged progress, cached revision reuse, per-field evidence review, and publication only after every parameter is accepted. The native Add Component workflow calls the live local service, renders source excerpts and review status, publishes the revision, binds it to the selected assembly part, and persists that binding.

Research remains deliberately fixture-backed and offline. Public web acquisition, PDF parsing, LLM providers, source licensing enforcement, and sandboxed document processing are not yet implemented. The next milestone is joint/scenario authoring and deterministic engineering checks.

## Milestone 3 — verified motor-arm path

The desktop can confirm a revolute joint and its axis/limits, review the structured 8 kg motor-arm scenario, and run native deterministic torque-speed, continuous holding, driver-current, periodic one-node thermal RC, and partial center-of-gravity checks. Findings preserve method, evidence, assumptions, limits, signed margins, and the bounded effect of missing gearbox efficiency. Joint, scenario, and findings state is included in the atomic project manifest and restored on reopen.

Current limitations: component parameters are consumed from the fixed published fixture contract rather than fetched dynamically by revision ID; assembly masses and support polygon are unknown; collision sweeps, battery sag, constraint mobility, and general component-class recipes remain future work. The checker makes no certification claim.

## Milestone 4 — verified CAD inspection path

The native viewport provides X-Ray and per-part bounds modes, selected-part SI dimensions/volume, part-to-part center displacement and AABB-clearance measurement, and a visible confirmed joint axis. STEP import uses AABB rejection followed by OCCT Boolean common-volume evaluation for exact static solid interference. Confirmed overlaps enter Test Build with affected part identities, method, and volume.

An overlap remains a warning until its semantic connection is classified because a press fit, shaft engagement, or mounting insertion may be intentional. Motion-range collision, minimum surface-to-surface distance, section planes, transform gizmos, and interference exclusions are not yet implemented.

## Milestone 5 — verified semantic motion path

Confirmed static overlaps can be classified per project as unclassified, intentional engagement, or prohibited interference. Deterministic finding rules map those states to caution, information, or critical failure and preserve the classification across project reopen.

The revolute-joint checker runs asynchronously and rotates the selected B-Rep about the user-confirmed source-part center, axis, and limits. Nineteen evenly spaced positions use bounding-box rejection followed by OCCT Boolean common-volume checking; the connected source/target pair is excluded. The fixture’s 0–90° range is clear at sampled positions, while a seeded reversed range confirms the collision path in native tests.

This is sampled collision detection, not a continuous-clearance guarantee. Adaptive subdivision around low-clearance intervals, exact minimum distance, multi-body motion, collision exclusions beyond the connected pair, and cancellation of an active OCCT sweep remain future work.

## Milestone 6 — verified CAD navigation and inspection path

Viewport navigation now follows conventional CAD behavior: middle-button orbit, Shift+middle pan, wheel zoom, Fit, isometric/top/front presets, perspective/orthographic projection, a compact orientation navigator, and keyboard shortcuts (`F`, `1`, `2`, `3`, `5`, `H`, `Shift+H`). Orthographic fit derives pixels-per-metre from viewport size and assembly diameter.

STEP inspection now exposes B-Rep surface area and face/edge counts in addition to hierarchy, instance names, bounds, solid volume, transforms, and tessellation. Material and mass remain visibly unknown and are never inferred from shape.

Prometheus V1 remains an assembly inspection and virtual-prototyping environment, not a parametric solid modeler. Precise part-placement overrides, transform gizmos, reference frames, and simple placeholder primitives are planned; sketches, feature history, extrusions, fillets, and production drafting are outside the current V1 boundary.

## Milestone 7 — verified placement path

Selected imported parts can receive precise XYZ translation overrides in metres. A placement is applied consistently to Qt rendering, bounds overlays, center/AABB measurements, asynchronous OCCT static-interference recomputation, and subsequent joint-sweep geometry. Overrides are stored by persistent CAD entity ID and restored from the atomic project manifest.

Semantic overlap classifications are retained independently of transient collision results, so an intended-fit decision survives moving parts apart and back together. Rotation placement, interactive transform gizmos, snapping, undo/redo, and multi-selection transforms remain the next CAD interaction increment.

## Milestone 8 — verified rotation and history path

Selected imported parts now support precise XYZ Euler rotation in degrees in addition to XYZ translation. Rotation is evaluated about the center of the part's imported bounds using the documented extrinsic X, then Y, then Z convention; translation is applied afterward. The identical placement is consumed by nested Qt Quick 3D transform nodes, rotated bounds and measurements, OCCT static-interference recomputation, sampled joint sweeps, and project persistence.

Placement edits maintain an application-level undo/redo history exposed through toolbar actions and conventional `Ctrl+Z` / `Ctrl+Y` shortcuts. Native project tests verify rotated extents, geometry recomputation, undo, redo, and save/reopen round-tripping.

Rotation is currently entered numerically. Interactive move/rotate gizmos, snapping and reference-frame selection, multi-selection transforms, and a persisted cross-session command history remain future increments.

## Milestone 9 — axis-constrained manipulation path

Selecting a part now exposes a color-coded world-axis manipulator in the viewport. `W` selects translation and `E` selects rotation; clicking an axis applies a positive constrained increment and Shift-click applies a negative increment. Matching inspector controls provide explicit ±X/±Y/±Z actions with fine, medium, and coarse steps. Each action is a single undoable placement command and therefore triggers the same asynchronous OCCT interference recomputation as a numerical transform.

This increment provides deterministic axis manipulation rather than continuous mouse dragging. Screen-space drag projection, local/reference coordinate systems, geometry/interface snapping, typed transient dimensions, and multi-part transforms remain outstanding. The automated Windows screenshot harness currently launches the app but does not emit its requested PNG, so visual capture automation remains unverified even though QML resource compilation and native tests pass.

## Milestone 10 — continuous transform preview path

The world-axis viewport handles now support continuous mouse dragging. Each axis is projected into screen space and mouse displacement is resolved onto that projected direction; translation uses the resulting world-distance fraction and rotation maps it to angular displacement. Clicking without dragging retains the deterministic step behavior.

Dragging is implemented as a placement transaction. Begin captures the exact initial pose, preview updates Qt properties without invoking OCCT, cancel restores the initial pose, and commit creates one undo record followed by one asynchronous exact-interference refresh. Native tests cover preview responsiveness, cancellation, commit, recomputation, and undo. QML lint completes without errors; its remaining warnings are pre-existing layout and unqualified-access cleanup work.

The manipulator still uses world axes and linear handles for both modes. Local/reference frames, rotation rings, snapping, transient dimensions, multi-selection, and a dedicated desktop interaction test harness remain outstanding.

## Milestone 11 — precision snapping path

Continuous transform dragging now supports deterministic linear and angular snapping using the active fine, medium, or coarse increment. Snapping is enabled by default, can be toggled from the toolbar, and can be bypassed temporarily with Alt during a drag. A color-matched transient dimension follows the cursor and reports the signed constrained displacement in millimetres or degrees.

Escape cancels the active preview transaction and restores the exact starting placement without producing a history entry or collision job. Click suppression prevents the release following cancellation from becoming an unintended step operation. Native builds/tests and QML lint pass; local/reference frames, interface and geometry-feature snaps, rotation rings, and automated pointer-event tests remain outstanding.

## Milestone 12 — world/local transform frames

The transform manipulator can now switch between World and Local coordinate frames. In Local mode the visible axes inherit the selected part's canonical X→Y→Z orientation, translation follows the corresponding rotated basis vector, and rotation is composed on the right side of the current rotation matrix before conversion back to the persisted canonical Euler representation. This avoids the incorrect shortcut of adding a world Euler component for a local-axis rotation.

Native tests verify that local X becomes world Y after a 90-degree Z rotation and that a local-axis rotation composes to the expected canonical pose. Both click-step and continuous drag paths use the same frame semantics. User-defined reference frames, rotation-ring interaction, geometric feature snapping, and gimbal-lock-specific UI guidance remain outstanding.

## Milestone 13 — bounds-anchor snap-to-mate path

Every imported part now exposes seven deterministic placement anchors: its bounds center and the centers of its six local bounds faces. Anchor world positions are derived from the imported B-Rep bounds and the complete current rigid placement, including rotation. The Snap Mate workflow translates a chosen moving anchor onto a chosen target anchor, preserves the moving part's orientation, creates one undoable placement command, and runs exact OCCT interference recomputation.

The UI explicitly identifies these as bounds-derived geometry anchors. Snapping does not infer a mounting interface, mate, fastener, joint, fit, or load path. Native tests verify anchor generation, rotated world coordinates, exact coincidence after snapping, and undo restoration. Authoritative component-package ports, user-defined frames, axis alignment, offset mates, and semantic connection creation remain outstanding.

## Milestone 14 — persisted semantic connection path

Snap Mate can now optionally create a user-confirmed semantic assembly edge with fixed, revolute, sliding, or contact type. Each record retains persistent source and target CAD entity IDs, both anchor IDs, connection type, confirmation basis, anchor origin, and a provisional semantic status. Connections are separate from computed overlaps and are saved in and restored from the atomic project manifest after validating that both referenced CAD entities still exist.

A dedicated Connections inspector lists these graph edges and permits removal without moving geometry. The system continues to label bounds-anchor edges as provisional and does not treat them as authoritative component-package interfaces or complete joint definitions. Native integration coverage verifies creation and project round-tripping. Evidence-backed ports, axis/limit completion for motion joints, compatibility descriptors, and connection-aware constraint analysis remain outstanding.

## Milestone 15 — rough V1 release baseline

The repository now includes a release-status matrix that distinguishes implemented, partial, fixture-backed, and unsupported behavior across physics, electrical compatibility, component intelligence, CAD usability, assembly semantics, and product hardening. Representative verified screenshots are checked into `docs/images` and presented in the README.

The deterministic physics library now includes a rectangular support-polygon tipping primitive. It projects the center of gravity along effective gravity under declared planar acceleration and reports signed boundary margin, limiting edge, and stable/unstable state. Unit tests cover static stability, acceleration-induced tipping, and invalid support geometry. Complete desktop per-part mass and support-polygon authoring remains explicitly partial.

## Milestone 16 — high-detail external CAD stress test

The OpenArm 2.0 full STEP assembly is available through an on-demand, hash-verified demo launcher with upstream CERN-OHL-S-2.0 attribution. Prometheus imported the 47.8 MB source as one XDE root, four compound leaf bodies, and 596,190 tessellated triangles. The upstream grouping preserves substantial visual detail but exposes limited semantic tree depth.

STEP files larger than 20 MB now use a coarser 1.5 mm display deflection and defer eager all-pairs Boolean interference. The desktop reports that deferral rather than implying collision was evaluated. An import-only native smoke mode validates external assemblies independently of the fixture-specific collision assertions. Automated Qt capture still fails to emit a post-import image on this Windows runner and remains an explicit hardening issue.
