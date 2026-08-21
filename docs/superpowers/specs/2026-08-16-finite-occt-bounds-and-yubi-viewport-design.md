# Finite Open Cascade bounds and YUBI viewport design

Status: approved in conversation on 2026-08-16.

## Problem

The pinned Toyota YUBI `YUBI Gripper Assy_DYNAMIXEL.stp` imports geometry, but
the current Open Cascade path accepts an open `Bnd_Box` as an ordinary finite
box. In the observed macOS run, `Solid 1` retained a reported size of
`2e+97 × 2e+97 × 2e+97 m` despite finite volume, surface area, and tessellated
geometry. `CadController` then used those values for the project scene extent.
The perspective camera moved beyond its fixed far clipping plane, leaving the
viewport black.

The same open bound also defeats broad-phase pair rejection. The YUBI import
entered a long `BRepAlgoAPI_Common` face-intersection operation for a pair that
should first have received a finite spatial bound. Deferring static
interference allowed the import to finish, but it did not correct the poisoned
viewport extent.

## Decision

Prometheus will treat only closed, finite Open Cascade bounds as authoritative.
When a transferred shape has finite tessellated vertices but Open Cascade
returns a void, open, or non-finite `Bnd_Box`, the importer will derive a finite
fallback box from those vertices. The fallback describes the displayed mesh;
it does not heal, replace, or certify the source B-Rep.

The resulting finite box will serve two existing consumers:

1. `AssemblyNode.bounds`, which controls viewport centering, fitting, property
   display, and mesh interaction.
2. `LeafShape.bounds`, which performs broad-phase rejection before an exact
   Open Cascade common-volume operation.

When an exact interference attempt runs, it will still use the original B-Rep
shapes. The fallback box can exclude spatially disjoint pairs; it cannot create
an interference finding or turn a failed boolean into a clear result.

## Interactive interference policy

The first finite-bounds implementation retained all 90 YUBI solids in the
broad phase, but a normal desktop import still remained inside one
`BRepAlgoAPI_Common` face/face intersection 166 seconds after launch. A macOS
process sample bound that delay to the Open Cascade boolean worker rather than
hashing, parsing, tessellation, camera fitting, or repeated execution.

An interactive import will therefore defer the complete static-interference
batch when any candidate leaf required tessellation-derived bounds. This rule
uses the condition already observed during the single import pass; it does not
reparse the file, remesh the assembly, run a preliminary boolean, or use file
size as a proxy for topology risk. The returned result will contain no static
interference pass or clear claim and will carry the existing explicit deferred
state. The desktop can display and inspect the finite mesh while reporting
static interference as `not_evaluated`.

The standalone `static_interferences` API remains the code boundary for an
explicit exact attempt. That path uses the original B-Rep shapes and finite
broad-phase boxes. The current desktop does not launch this API while
`collisionDeferred` is true because the in-process operation cannot be safely
cancelled. Exposing an engineer-triggered retry requires the later isolated
solver-runtime boundary; until then, the result remains unknown rather than
clear. Assemblies whose leaves all have closed finite B-Rep bounds retain the
current automatic exact-interference behavior.

## Bounds contract

A usable imported bound must satisfy all of the following:

- the box is neither void nor open;
- all six extrema are finite;
- each minimum is less than or equal to its corresponding maximum; and
- converting between Open Cascade millimetres and Prometheus metres preserves
  finite values.

If the B-Rep box violates that contract, the importer scans the already-created
display mesh. Every coordinate must be finite, and at least one complete vertex
must exist. The importer computes minima and maxima in metres, stores them on
the `AssemblyNode`, and reconstructs the leaf broad-phase box in millimetres.

If neither the B-Rep box nor the tessellation supplies finite bounds, the node
remains visible in the hierarchy without renderable geometry under the existing
skipped-geometry warning. Prometheus must not substitute an arbitrary clamp,
origin-sized box, or project-wide success state.

## Data flow

```text
Transferred TopoDS_Shape
        |
        +--> BRepBndLib::Add --> closed and finite? --> use B-Rep bounds
        |                              |
        |                              no
        |                              v
        +--> finite tessellated vertices --> derive display-mesh bounds
                                               |
                                               +--> AssemblyNode.bounds (m)
                                               +--> LeafShape.bounds (mm)

LeafShape bounds --> any tessellation fallback? -- yes --> deferred / unknown
                         |
                         no
                         v
                  broad-phase exclusion --> original B-Rep exact boolean
AssemblyNode bounds --> CadController scene extent --> Qt Quick 3D camera fit
```

## Rejected alternatives

- **Clamp the camera extent.** This could make some geometry visible while
  leaving incorrect per-part dimensions and pathological collision candidates.
- **Skip every shape with an open B-Rep box.** The YUBI shape has finite
  tessellated geometry and useful topology; discarding it would turn a bounded
  metadata defect into missing project evidence.
- **Repair or heal the STEP shape automatically.** The earlier YUBI trial found
  that automatic Open Cascade healing could access-violate. This change retains
  the established no-healing boundary.
- **Lower the file-size threshold.** The 7.6 MB YUBI file is smaller than the
  current 20 MB threshold but contains a pathological exact pair. Byte count
  does not measure boolean-operation risk.
- **Run an exact preflight and cancel it after a timeout.** Open Cascade's
  in-process boolean call has no safe cancellation boundary in the current
  adapter. A preflight would also repeat the expensive calculation the user is
  trying to avoid.
- **Keep a viewing-only build flag.** A transient flag can demonstrate the
  mesh, but it leaves the normal product path blocked and makes the displayed
  behavior depend on an undocumented binary.

## Verification

The regression must fail on the pre-fix importer by observing at least one
non-finite or unreasonably open YUBI leaf bound. After the fix, verification
must establish:

- every renderable imported leaf has ordered finite bounds;
- the pinned YUBI assembly still produces nonzero roots, leaves, and triangles;
- the YUBI viewport reports finite metre-scale dimensions and renders after
  `Fit`;
- a normal interactive YUBI import completes with static interference marked
  deferred because its leaves used tessellation-derived bounds;
- the existing synthetic motor-arm exact-interference test still finds its
  known overlap;
- malformed STEP input still fails closed;
- deferred interference remains explicitly `not_evaluated`; and
- the headless and Open Cascade desktop test targets continue to pass.

The visual check proves only that the imported tessellation can be framed and
displayed. It does not validate YUBI topology, assembly intent, interference,
materials, loads, structural strength, or project-wide correctness.
