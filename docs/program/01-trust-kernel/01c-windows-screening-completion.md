# Program 01C Windows screening completion

Program 01C closed on 2026-08-15 at implementation commit
`519e06cf91a2f8b23bcde31442953a163c2e9e8f`. The claim is intentionally
narrow: Prometheus can open a bounded real mechanical project folder on
Windows, account for its files, import one unambiguous STEP assembly, display
supported geometry information, and keep unsupported engineering questions
visible. It does not mean that the project works or that its design is safe.

## Evidence

- A fresh GCC/Qt/Open Cascade Windows Release build completed successfully.
- All 22 native Release suites passed in 57.26 seconds.
- The pinned [YUBI gripper trial](../../trials/yubi-gripper-windows-screening.md)
  prepared five visible artifacts: one STEP assembly, one BOM, one assembly
  guide, one license notice, and one source manifest.
- The production Release importer loaded the exact 7,613,253-byte assembly at
  SHA-256
  `0db1eefe396d528c9705331f0f5d71a3b67d30d86d9b4edf8fb8be8d84efaac6`.
- Import produced one root, 90 leaf parts, and 37,367 display triangles. Static
  interference was explicitly deferred for this assembly.
- The ordinary `PROMETHEUS_STARTUP_PROJECT_FOLDER` desktop path launched and
  remained running with the prepared project folder.
- Earlier evidence covers the synthetic motor-arm fixture and the larger
  OpenArm 2.0 import boundary. The independent YUBI project exposed an OCCT
  access violation that those inputs did not.

## Defect discovered and resolved

Open Cascade crashed in `ShapeFix_Solid::SolidFromShell` while transferring the
YUBI STEP file. Native exceptions could not safely contain that access
violation. Prometheus now disables automatic STEP shape-healing operations
after `ReadFile()` initializes each transfer session, preserves the imported
topology without that repair, and reports the limitation as an import warning.
The same policy applies to initial import, placement-aware static interference,
and sampled motion re-imports.

This is a conservative screening policy, not a claim that malformed solids
were repaired. Later solver workflows must validate watertightness and model
suitability independently before meshing or analysis.

## Exit-gate disposition

| Requirement | Evidence | Result |
| --- | --- | --- |
| One real Windows project opens without developer intervention | One-command pinned preparation plus ordinary folder startup | Met |
| Every discovered file has a visible state | Folder intake accounts for STEP, CSV, PDF, text license, and JSON manifest | Met for the bounded trial |
| Supported geometry results reproduce | Exact source revision/hash and production importer counts are recorded | Met |
| Unsupported questions remain visible | BOM/PDF are not evaluated; material, load, restraint, contact, motion, and strength remain unknown/deferred | Met |
| No project-wide pass language | The desktop explicitly separates findings, unknowns, and deferred checks | Met |

## Remaining boundary

Program 01C does not establish semantic BOM/PDF understanding, automatic
component matching, arbitrary CAD support, continuous collision clearance,
structural analysis, physical validation, or deployment readiness. Phase 2 /
Program 01D must now test three materially different projects, involve at least
one person outside the core development work, rank actual failures, and choose
the first real structural question.
