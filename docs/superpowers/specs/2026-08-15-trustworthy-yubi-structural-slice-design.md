# Trustworthy YUBI Structural Slice Design

## Problem

Prometheus can currently produce a deterministic CalculiX deck from a reviewed
tetrahedral request and parse displacement and stress rows from a `.dat` file.
That preparation layer is not yet a trustworthy structural workflow. A request
containing only zero-valued forces can validate, raw result rows can be accepted
without solver-completion evidence, the checked-in smoke command never calls
the parser, and the mesh importer discards the surface groups an engineer needs
to define loads and restraints.

The next slice must answer one bounded question about Toyota YUBI's
`BRACKET_GRIPPER` without converting absent material, load, boundary-condition,
or convergence evidence into a pass.

## Evidence Boundary

The pinned YUBI BOM identifies the bracket as machined `A2024` and used for a
UR5e attachment. It does not state temper, product form, stock thickness,
supplier, material certificate, load case, allowable stress, safety factor, or
restraint model. Therefore:

- Prometheus may present sourced A2024 property candidates;
- a user may approve a clearly labeled evaluation assumption;
- Prometheus may not label an assumed temper as the bracket's known material;
- an assumption-based solve is a hypothetical YUBI-geometry result, not a
  verified result for the manufactured component.

The source boundary is recorded in:

- Toyota's pinned BOM, which states only `A2024`;
- Kaiser Aluminum's 2024 sheet/coil/plate technical data, which reports typical
  temper-dependent strengths and a typical elastic modulus;
- MIL-HDBK-5J/MMPDS-style tables, which condition design properties on temper,
  product form, thickness, direction, and statistical basis.

MIL-HDBK-5J is a useful public reference but is canceled and replaced by MMPDS.
Neither it nor a producer's typical-properties sheet identifies the material
actually used for this open-source bracket.

## Scope and Dependency Order

This work is split into four testable increments.

1. **Structural trust seams:** request, mesh, deck, and solver-result validation.
2. **Reviewed setup:** visible mesh groups, explicit selections, material and
   scenario inputs, and a blocked/unblocked request preview.
3. **Validation evidence:** analytic agreement, known-pass, known-fail, and
   mesh-refinement checks through the same solver/parser seam.
4. **YUBI evaluation:** one user-approved scenario, followed by a refinement
   check and a narrowly worded result.

The outside-user folder-screening protocol and the JPL Rover regression gate
remain independent work items. They can be prepared alongside these increments,
but their evidence cannot substitute for structural validation.

## Approaches Considered

### Run the bracket immediately with conventional defaults

This would produce an image and numbers quickly, but defaulting temper, load,
restraints, or allowables would answer a question the project never supplied.
The resulting pass or fail would not be attributable to the YUBI bracket.

### Use a separate CAE application for setup and copy results into Prometheus

This could accelerate one manual run, but Prometheus would not own the reviewed
input boundary or reproduce the solver-to-finding path. It would leave the core
product goal untested.

### Build one narrow reviewed path and validate it before the bracket

This is the selected approach. Prometheus will support one solid, isotropic
linear elasticity, full-face fixed restraints, and one distributed resultant
force. The same typed request, CalculiX deck generator, solver evidence
compiler, and finding logic will serve benchmark and YUBI cases.

## 1. Structural Trust Seams

### Request validation

`StructuralRequest` remains the authoritative pre-solver object. Validation
will add the following fail-closed rules:

- geometry identity is exactly `sha256:` followed by 64 lowercase hexadecimal
  characters;
- at least one finite force component is nonzero after deterministic
  compilation;
- duplicate nodal-force IDs are rejected at the request boundary; the
  face-load compiler must aggregate them first;
- analysis and component text cannot inject CalculiX keyword lines through
  control characters or newlines;
- every tetrahedron has positive signed volume above a scale-aware numerical
  floor;
- all tetrahedra belong to one face-connected volume component;
- tetrahedral mean-ratio quality is computed and compared with an explicit,
  reviewed mesh-quality threshold;
- the request records reviewed mesh controls and the minimum observed quality.

The mean-ratio threshold is an ingestion and numerical-sanity rule, not mesh
convergence evidence. The refinement comparison remains a separate gate.

### Surface groups

The Gmsh/Abaqus importer will retain first-order triangular surface elements
(`CPS3`) and their `ELSET` labels alongside `C3D4` volume elements. Each surface
group will expose:

- stable source label for the current Gmsh export;
- triangle and unique-node counts;
- area, area-weighted centroid, and representative normal in SI units;
- the triangle connectivity required for highlighting and load compilation.

The importer will verify that surface triangles reference known nodes, do not
duplicate one another, and correspond to boundary faces of the retained volume
mesh. Missing or non-boundary groups fail setup rather than silently producing
an incomplete selection model.

### Solver evidence

Raw `.dat` parsing remains a pure operation, but it will return row identities
instead of maxima alone. A separate `compile_calculix_result()` boundary will
accept:

- the reviewed structural request;
- solver executable identity and version;
- process exit status;
- captured standard output and standard error;
- `.sta` status bytes;
- `.dat` result bytes;
- hashes of the input deck and raw outputs.

For the bounded one-step static model, compilation requires all of the
following:

- process exit code zero;
- CalculiX's successful completion marker in captured output;
- a final converged status row reaching the requested step time;
- exactly the expected node displacement identities;
- exactly the expected C3D4 element/integration-point stress identities;
- no duplicate, missing, unexpected, or non-finite result row.

Any failed condition produces a structured indeterminate result. It never
produces zero-valued metrics or a pass.

### Reproducible smoke

The checked-in smoke runner will capture solver output and invoke a Qt-free
result-verifier executable after `ccx` exits. The command will fail if deck
generation, solver completion, status parsing, identity coverage, or metric
parsing fails. Merely finding `.dat`, `.frd`, and `.sta` files is insufficient.

## 2. Reviewed Structural Setup

### Desktop boundary

A `StructuralSetupController` will adapt the Qt-free structural library to QML.
It will load the exact Gmsh/Abaqus mesh, expose mesh diagnostics and surface
groups, retain explicit user selections, compile a preview request, and list
blocking validation issues. It will not duplicate finite-element calculations.

A minimal `StructuralSetupPanel.qml` will provide:

- a Qt Quick 3D mesh view;
- a list of surface groups with area, centroid, and triangle count;
- visual highlighting of the active group;
- independent restraint and load selection controls;
- material designation, temper, product form, Young's modulus, Poisson ratio,
  and evidence reference;
- force magnitude and unit direction;
- displacement and von Mises limits;
- mesh-control and scenario review confirmations;
- a request summary and explicit blocking reasons.

The first version selects named surface groups from the list and highlights
them in the viewport. Per-triangle mouse picking is outside this bounded slice.

### Boundary-condition compilation

Selected restraint groups compile to the unique set of fully fixed nodes.
Selected load groups compile one total force as a uniform surface traction:

1. normalize the reviewed direction vector;
2. divide the total force by selected surface area;
3. integrate each constant triangular traction as one-third of triangle force
   at each vertex;
4. aggregate shared-node contributions deterministically;
5. emit one `NodalForce` per loaded node in ascending node-ID order.

The UI must state this approximation beside the force input. It must show the
selected area and resultant compiled force so the user can verify magnitude and
direction before confirmation.

### Review contract

The Run/Export action remains blocked until the user explicitly confirms:

1. material designation, temper or assumption status, product form, elastic
   constants, and evidence source;
2. restraint surface groups;
3. load surface groups;
4. force magnitude and direction;
5. displacement and/or stress limit, including its basis;
6. mesh controls and minimum-quality threshold;
7. the complete scenario.

Changing any confirmed input clears scenario confirmation and invalidates prior
results.

## 3. Material Applicability

Prometheus will record a small evidence table rather than silently select one
number. The initial candidate set will include:

- the exact Toyota BOM claim: `A2024`, temper unresolved;
- a producer typical-property candidate for 2024 T4/T351 sheet or plate;
- a public handbook candidate conditioned on 2024-T351 plate and thickness.

Each candidate records source, table/page, value, units, property type
(typical, minimum, or design allowable), product form, thickness range,
direction, and applicability note. A current material certificate, drawing, or
upstream confirmation overrides generic candidates.

The initial research values are candidates, not yet approved inputs:

| Candidate | Elastic data | Applicability limit |
| --- | --- | --- |
| Kaiser 2024 T4/T351 sheet, coil, and plate | Typical `E = 73.1 GPa` | Producer-typical data; it does not identify YUBI stock or temper. |
| MIL-HDBK-5J bare 2024 sheet/plate, all tempers, thickness at least 0.250 in | tensile modulus `E = 10.7 Msi` (`73.8 GPa`), `nu = 0.33` | Public canceled handbook data; product form, temper, thickness, and basis must match. Table 3.2.3.0(d) does not state a material direction for `E`. |
| Toyota YUBI BOM | No elastic values | Establishes only `A2024`; temper and product form are absent. |

Source records:

- [pinned Toyota YUBI BOM](https://raw.githubusercontent.com/Toyota/yubi-hw/e8334ff04945ccf56c0576a56f6fab74b63daaa2/docs/BOM/YUBI%20Gripper_DYNAMIXEL_BOM.csv)
- [Kaiser Aluminum 2024 technical data](https://online.kaiseraluminum.com/depot/PublicProductInformation/Document/1012/Kaiser_Aluminum_2024_Sheet_Coil_and_Plate.pdf)
- [DLA ASSIST record for MIL-HDBK-5J](https://quicksearch.dla.mil/qsDocDetails.aspx?ident_number=53876)

The exact STEP model is 8 mm across its thinnest bounding-box dimension, but
finished geometry does not prove the starting stock thickness or product form.

If the user approves T351 only as an evaluation assumption, the result title
and manifest must say `assumed 2024-T351`; it cannot say that the YUBI bracket
is known to be T351. Stress limits are separate reviewed requirements and will
not be generated automatically from typical yield strength.

## 4. Solver Validation and YUBI Execution

### Analytic benchmark

A prismatic tension-bar fixture will use a uniform end traction and fixed
opposite face. Closed-form axial displacement `F L / (A E)` and nominal stress
`F / A` provide independent references. The fixture will be solved at three
mesh resolutions through the production deck, solver, status, parser, and
finding path.

The benchmark record will declare tolerances before reading the final results.
If the selected C3D4 formulation cannot meet them, the structural workflow
remains unvalidated and the YUBI run stays blocked.

### Known-pass and known-fail

The same benchmark geometry will run with two predeclared requirement sets:

- a limit above the analytic/reference result that must compile to a scoped
  pass;
- a limit below the analytic/reference result that must compile to a scoped
  fail.

Both cases must use the same result compiler as the YUBI case. This tests
finding polarity, not only solver numerics.

### Mesh refinement

Coarse, medium, and fine meshes will record node/element counts, minimum and
distributional quality, maximum displacement, maximum von Mises stress,
runtime, convergence evidence, and raw artifact hashes. The comparison must
state its metric and tolerance before deciding that refinement is adequate.

Peak stress at an idealized fixed boundary can be singular. If stress does not
stabilize, Prometheus must report that nonconvergence and cannot turn the peak
stress requirement into a pass. A stable displacement result may still be
reported separately within its scope.

### YUBI result

The exact bracket geometry and user-reviewed setup will run only after the
benchmark gates pass. The report will include:

- exact geometry, mesh, deck, solver, status, and result identities;
- selected material and whether its temper is known or assumed;
- selected faces and compiled load resultant;
- convergence and refinement evidence;
- requirement-level pass/fail/indeterminate findings;
- assumptions, warnings, and questions not evaluated.

No project-wide or certification claim follows from this single linear-static
case.

## 5. Outside-User Folder Screening

Prometheus will provide a clean-machine package, a fixed trial folder, a
one-page task sheet, and a structured observation form. The participant must
work without live developer guidance. The record will capture setup time,
inventory time, assembly/ambiguity interpretation, filter use, unsupported
versus not-evaluated understanding, mistaken pass interpretations, confusion,
and workarounds.

An AI agent, the project owner, or a developer rehearsing the script does not
satisfy this gate. Until another person completes the session and their actual
observations are recorded, Program 01D remains open.

## Deliverables and Stop Conditions

The implementation may proceed through the core, panel, material-candidate,
and benchmark infrastructure without a final YUBI scenario. It must stop before
the YUBI solve when any of these remains unresolved:

- no user-approved material applicability state;
- no explicit face selections;
- no explicit force and limits;
- failed benchmark or result-coverage gate;
- inadequate or nonconvergent mesh-refinement evidence;
- unavailable or unversioned solver backend.

The outside-user session can be prepared by this implementation, but only a
real participant can complete it.
