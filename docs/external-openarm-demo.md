# Optional OpenArm 2.0 demo

Prometheus can use the OpenArm 2.0 full STEP assembly as a higher-complexity import and rendering stress test. Run `./scripts/run-openarm-demo.ps1` on a Windows native-development installation. The script downloads the approximately 48 MB assembly into the ignored `out/external-demo` directory, verifies its SHA-256 hash, builds Prometheus, and opens the assembly at startup.

Source: [Enactic OpenArm Hardware](https://github.com/enactic/openarm_hardware), file `v2.0/Hardware/OpenArm 2.0/OpenArm_2.0.STEP`.

License: CERN Open Hardware Licence Version 2 — Strongly Reciprocal (`CERN-OHL-S-2.0`), copyright 2025 Enactic, Inc. The upstream license and source remain authoritative. Prometheus does not republish or relicense the STEP file; it is downloaded directly from the upstream project's published Google Drive identifier.

Verified download SHA-256: `0F97A5FB308D5B09353AA67BBD32D7C08E55DC5629BCBE20C12F71C82EF13C94`.

This external assembly is for import/rendering stress testing. It does not arrive with a compiled Prometheus component package, and Prometheus must not infer mass, material, ratings, joints, or safety from its geometry.

Verified Prometheus import result on August 11, 2026: one XDE root, four leaf bodies/compounds, and 596,190 tessellated triangles in approximately 45 seconds on the development workstation. Exact all-pairs interference is deliberately deferred for STEP files larger than 20 MB; the application displays this limitation in its status bar. The upstream STEP groups much of the detailed assembly into compound leaf shapes, so its visible detail is substantially higher than its semantic tree depth.
