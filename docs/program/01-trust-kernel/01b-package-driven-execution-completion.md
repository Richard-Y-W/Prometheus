# Program 01B package-driven execution completion record

- Status: complete under the bounded `contract_tested` gate
- Closed: 2026-08-13 (America/Denver)
- Validation level: `contract_tested`
- Verified implementation and CI commit: `dd5b915ae0fa23f0d48fb7e4f8df4a9834c9816d`
- Evidence-bound closure commit: the commit containing this file with subject `chore: close Program 01B package execution gate`
- Release-gate evidence: [GitHub Actions run 31772804542](https://github.com/Richard-Y-W/Prometheus/actions/runs/31772804542), conclusion `success`
- Authoritative backend: `motor_arm_builtin_v1`, contract version `1.0.0`
- Component-package contract: `urn:prometheus:schema:execution-component:2.0.0`
- Scenario, request, result, and manifest contracts: version `1.0.0`

## Bounded release claim

For the two checked-in synthetic motor fixtures and the reviewed motor-arm scenario, the exact
reviewed component-package bytes drove `motor_arm_builtin_v1` through the shared C++ execution
library. The resulting request, result, and manifest objects were stored immutably and reproduced
exactly offline by both the desktop and the replay CLI when the recorded numeric execution identity
was available.

This closes one evidence-to-execution loop. It establishes neither arbitrary-project verification
nor a general engineering physics engine. It provides no physical validation, certification,
universal artifact intake, requirement compiler, capability planner, external solver execution, or
assembly-wide pass. A package hash identifies reviewed bytes; it does not prove that the synthetic
source values or the bounded equations describe a physical product.

## Implementation boundary and commits

Program 01B preserves the Program 01A closure at
`ae0cf2d2cba8716e78554a113f5429b5454b06b3`. Its approved design is commit
`cd1bbe9b4f268785bacd54f87f3d40324a8f762e`, and its execution plan is commit
`9e50908b2def45746dbd818cdd755c14fccb439b`.

The implementation and release-repair commits are:

| Commit | Change |
| --- | --- |
| `cfbf5c2d4d8005cdd583feeb8a28f14ae062eddf` | Define the closed Program 01B execution contracts |
| `29e558000b40105567fc4d65dafb5f9291ea6d86` | Add execution-ready Motor A and Motor B synthetic packages |
| `cf7609e093efd00ed02fbef584fbc6dae6bfa94e` | Publish packages with the exact consumer contract |
| `e8b6c435d0d58181a6ecda4fefca08e553a2023e` | Remove Qt from native integrity verification |
| `fb27567d00001b6fb74885d8a9d36c769a61519b` | Isolate concurrent integrity fixtures |
| `d4c0bb25e1a7173951d98f364d71972d565209e7` | Add strict C++ package, scenario, request, result, and manifest models |
| `aec6eda8ed0007be2cac5865c871f2c6137a463c` | Map reviewed package claims into typed C++ motor inputs |
| `62fba615b19b02e04b3b35812f937dc4cfb4d8aa` | Add the authoritative built-in backend and finding compiler |
| `c34a5b081d69dc2261b05d90e4f1ff88378a52be` | Compile deterministic analysis runs and fixtures |
| `17dd9812657afdacea3cf17bf5b95ff60153d60f` | Add the bounded immutable execution-object store |
| `47d4e99ae07d1bf9a471d1f3a68fe2955a92a4a3` | Add transactional, idempotent run publication |
| `d46bb257a52de495d1e307e24e80b421481c0c9e` | Add the exact offline replay CLI |
| `868644b26485178e7008ef4ab7a3d96e510d82f3` | Acquire and re-verify exact package bytes |
| `c5af7265e4514b577af7f990da93435a8f9086f6` | Centralize versioned desktop project ownership |
| `6ac9a7ee4ebbc9a455bb0f9a154c720b26400d75` | Connect the shared execution path to desktop controllers |
| `f02aa966c0c3abc9fa7f136a129afbc3a3667393` | Add the reviewed package, scenario, execution, history, and replay workflow |
| `1d34b2ac5e61d1b14fd9cb2c42832dd064c60de3` | Remove the duplicate historical Python motor authority |
| `d71237052d056a39506d98cd79414092ae415043` | Bind the selected catalog identity |
| `4b1c35abfcaad7e40beb4b9755eeb4d1329a29a7` | Map visible claim reviews to their delegate rows |
| `731b0873cc8663afaec96e7589b90e6a85741cdf` | Add the Program 01B release matrix and support scripts |
| `37e20335406c9b5a0a38d9ead7afeb2b56b3b0d9` | Make consumer tests compile under MSVC |
| `0b5c96645ccb45d3548df68f5644a6127de50a93` | Make failure classification independent of C++ argument evaluation order |
| `862c564a81c6e7d56ac6db5e717064fdba8be940` | Preserve fixture bytes and Windows numeric-runtime identity |
| `dd5b915ae0fa23f0d48fb7e4f8df4a9834c9816d` | Stabilize Windows replay process launch and binary stdout |

Production calculation authority is singular: only `prometheus_execution` invokes
`motor_arm_builtin_v1`. The desktop and replay CLI call that same library. Python prepares,
reviews, publishes, and exports input bytes but contains no production motor equation or finding
decision. QML, controllers, persistence, and replay do not duplicate the calculation.

## Dependency and contract identities

| Boundary | Verified identity |
| --- | --- |
| Native language/build | C++20; CMake 3.24+ |
| Canonical JSON | nlohmann/json commit `55f93686c01528224f448c19128836e7df245f72`; Ryu commit `3377662b1958dbdefb679e2c110368512cccf4f6` |
| Qt-free SHA-256 | PicoSHA2 commit `161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29` |
| Python package canonicalization | `rfc8785==0.1.4` |
| Locked backend | FastAPI 0.141.1, Pydantic 2.13.4, SQLAlchemy 2.0.51, Alembic 1.19.1, psycopg 3.3.4, pytest 8.4.2 |
| Locked archived frontend | React 18.3.1, Three.js 0.170.0, Vite 6.4.3, Vitest 3.2.7 |
| Local native acceptance | Qt 6.11.1 and Open Cascade 7.9.3 on macOS arm64 |
| Required native CI | Qt 6.11.1 on macOS; Qt 6.8.3 on Linux and Windows MSVC 2022 |
| Consumer contract | hash `sha256:e185ee08c987b30cf20d69af06a8754224f25068e499b052a49566c137bd0155` |

`third_party/manifest.json` records the upstream URL, commit, license, and exact SHA-256 value for
all 12 vendored files. Backend and frontend transitive identities remain governed by the checked-in
`uv.lock` and `package-lock.json`; no build-time native download is part of the execution boundary.

## Required release matrix

The push run for verified implementation commit
`dd5b915ae0fa23f0d48fb7e4f8df4a9834c9816d` completed all nine required jobs. No required job was
skipped or cancelled.

| Required job | Observed toolchain or service | Result |
| --- | --- | --- |
| Backend SQLite / Python 3.11 | CPython 3.11.15 | `395 passed, 1 skipped, 1 warning`; the skip is the PostgreSQL-only enablement check |
| Backend SQLite / Python 3.12 | CPython 3.12.3 | `395 passed, 1 skipped, 1 warning` |
| Backend SQLite / Python 3.13 | CPython 3.13.15 | `395 passed, 1 skipped, 1 warning` |
| Backend SQLite / Python 3.14 | CPython 3.14.7 | `395 passed, 1 skipped, 1 warning` |
| Backend PostgreSQL 17 / Python 3.12 | PostgreSQL 17.11; CPython 3.12.3 | `426 passed, 1 warning`; no tests skipped |
| Frontend / Node 20 | Node 20 workflow | 1 test file and 4 tests passed; production build passed; install and explicit audit each reported 0 vulnerabilities |
| Native / Linux | GNU C++ 13.3.0; Qt 6.8.3 | Headless 13/13, integrity-only 3/3, and OCCT-disabled desktop 19/19 tests passed |
| Native / macOS | AppleClang 21.0.0.21000101; Qt 6.11.1 | Headless 13/13, integrity-only 3/3, and OCCT-disabled desktop 19/19 tests passed |
| Native / Windows MSVC | Windows 2022; MSVC 19.44.35228.0; Qt 6.8.3 | Headless 13/13, integrity-only 3/3, and OCCT-disabled desktop 19/19 tests passed |

GitHub warned that SHA-pinned JavaScript actions still declared the deprecated Node 20 action
runtime and were forced onto Node 24. The archived frontend itself remained on the workflow's Node
20 toolchain. The backend warning is retained rather than converted into a warning-free claim.

## Local release-gate evidence

The final local gate used macOS 26.5.2 (`25F84`) on arm64, AppleClang 21.0.0, CMake 4.4.2,
Ninja 1.13.2, Qt 6.11.1, Open Cascade 7.9.3, PostgreSQL 17.10, CPython 3.11.15,
Node 24.14.1, and npm 11.11.0.

| Gate | Command or procedure | Observed result |
| --- | --- | --- |
| Repository formatting | `git diff --check` | No whitespace errors |
| Vendored native bytes | `python3 scripts/verify-vendored-dependencies.py` | All 12 manifest entries, commits, licenses, and hashes verified |
| Generated contracts | Run all three backend exporters, then `git diff --exit-code -- ../schemas ../fixtures/contracts` | No generated schema or fixture drift |
| SQLite backend | Locked full `pytest -q` suite | `395 passed, 1 skipped, 1 warning` |
| PostgreSQL backend | Locked full suite with `PROMETHEUS_TEST_POSTGRES_URL` against PostgreSQL 17.10 | `426 passed, 1 warning`; no skips |
| Frontend | `npm ci`, `npm test`, `npm run build`, `npm audit --audit-level=high` | 4 tests passed; build passed; audit reported 0 vulnerabilities |
| Headless native | Configure, build, and test `headless-debug` | 13/13 tests passed |
| Integrity-only native | Configure, build, and test `integrity-debug` | 3/3 tests passed |
| Complete desktop without OCCT | Configure, build, and test `desktop-no-occt-debug` | 19/19 tests passed |
| OCCT-enabled desktop | Configure with Qt 6.11.1 and Open Cascade 7.9.3, build, and run CTest | 20/20 tests passed |

An additional Ubuntu x86_64 container reproduced the execution-failure classification defect seen
in CI and then passed the targeted execution/replay tests after the fix. This was a portability
diagnostic, not a substitute for the required Linux job above.

## Manual Motor A/Motor B acceptance

The release demonstration used the OCCT-enabled macOS build and saved:

`/Users/byungkim/Desktop/Prometheus-Program-01B-Release-Demo.prometheus`

with the sibling content-addressed store
`Prometheus-Program-01B-Release-Demo.prometheus.data`. The saved project refers to assembly hash
`sha256:377302b669b12b89e2c020dc4c29e1c63c4920587920eb8f02ff54ca73bf977d`.
The object store's nine named SHA-256 paths were independently rehashed and matched their path
identities.

Both runs used the same reviewed scenario object:

- scenario hash `sha256:7c096ba99c93b382c3e4eb9d61421bb3f75cf8b1361101140c458ecd298bd1f0`;
- payload 8 kg, arm radius 0.2 m, rotation 1.5707963267948966 rad;
- move duration 1.2 s, hold duration 4 s, cycle duration 10 s;
- ambient temperature 35 degC; and
- motion profile `symmetric_triangular_velocity`.

| Evidence | Motor A | Motor B |
| --- | --- | --- |
| Reviewed distinguishing value | continuous torque 0.208 N*m | continuous torque 0.320 N*m |
| Package hash | `sha256:29080ff8496a23704dfdd21ca893c381a8d0a7c3e60100df44333b2ff5d56d59` | `sha256:d07aebe5d0c0ea076df6744ee9ccc66a2f03fd2470f29a57780a20b94c1725ea` |
| Request hash | `sha256:f39614264457f55730a53fb7061bb6b3c02ff119af61cfa64ff2b9fbcf7215ab` | `sha256:b8bfd252165ee2a587c2ee0d80c994dd0a0bd56079117843ca77934238741289` |
| Result hash | `sha256:4c808c7cc9c817b46f77f10b8654a790464d8a023cd835a81dcf770a09bf78ba` | `sha256:6099170978b5070801dc66909ca090e8ebcc43ccd549318ba6c3eed5b66937a2` |
| Manifest hash | `sha256:3162a9f406cd728249c1a179bada09e49028aef83f0c5a54ea7d2c6bc41f283f` | `sha256:7c990251152fc585eec0c508722509b66f3762d5a2116c0a0e4d7e77c714daf0` |
| Holding comparison | 0.208 N*m `>=` 0.224152 N*m | 0.320 N*m `>=` 0.224152 N*m |
| Signed holding margin | `-0.0720582461900853` | `0.4276026981690996` |
| Scoped outcomes | 3 pass, 1 fail | 4 pass, 0 fail |
| Requested/evaluated obligations | 4/4 | 4/4 |

These are the manual service-publication and runtime identities, not the separate checked-in
contract-vector hashes. The normalized engineering-input vectors differ only in continuous torque;
the package graphs retain distinct fixture, revision, claim, review, and evidence identities.

The calculation arrays, non-holding obligation quantities, scenario, and numeric profile were equal
between the two runs. Only the holding outcome changed as expected from the reviewed continuous-
torque value. Center of gravity remained explicitly uncovered because the point-payload model has
no assembly part-mass distribution. The persisted project kept geometry separate with
`geometry_status=not_evaluated` and no geometry finding in the execution result; it did not turn
the motor outcomes into a project verdict.

### Recorded numeric execution identity

- build fingerprint: `sha256:6f84af265b771aeb3381fca944f2003774c13354aa60dfeb8b46185afb15385a`;
- compiler: AppleClang `21.0.0.21000101`;
- operating system: macOS arm64 release `25.5.0`;
- standard library: libc++ `210106`;
- math runtime: `apple-libSystem` `1356.0.0`;
- rounding mode: `to_nearest`;
- floating-point contraction: disabled; and
- fast math: false.

### Offline reopen and replay

After both runs were committed, the desktop was closed and the local backend was stopped normally.
The same saved project reopened without the service, displayed both verified recorded histories,
and reproduced both results with exact result-hash matches. The CLI replayed each manifest from the
same build, exited 0, and emitted a closed machine-readable report with `status=exact_match` and
equal recorded/replayed hashes.

A temporary process interposer denied both IPv4 and IPv6 `connect` calls with `ENETDOWN`; its probe
confirmed `network_boundary=ipv4_ipv6_connect_denied` before the CLI replays. This is process-level
network-denial evidence, not an OS-wide network-isolation claim. The CLI also has no networking or
Python dependency in its link boundary.

No screenshot is claimed. macOS denied screen capture because the invoking terminal did not have
Screen Recording permission. Accessibility-state output, immutable object bytes, hashes, CLI
stdout/exit status, and command output were retained instead; the absent screenshot remains an
evidence limitation.

## Failure-injection and authority coverage

The passing suites exercised these fail-closed paths:

- noncanonical, wrong-hash, wrong-length, unsupported-schema, blocked, unreviewed, incomplete,
  unit-incompatible, cross-reference-mismatched, oversized, and non-finite execution inputs;
- absent, weak, duplicate, malformed, or inconsistent ETags; encoded, redirected, partial, and
  network-failed exact-package responses; and retry after acquisition failure;
- immutable-object collisions, unsafe hashes, missing/corrupt objects, and symlink substitution at
  project, sidecar, object-tree, fan-out, destination, temporary-file, assembly, and lock paths;
- injected failures before and after object/project temporary creation, write, flush, verification,
  rename, and project replacement for create, bind, scenario, and publication operations;
- cancellation before publication and at commit, idempotent repeat publication, writer contention,
  read contention, timeout, process death, and stale temporary cleanup ownership;
- missing/corrupt projects or sidecars, uncommitted manifests, bad manifest reference metadata,
  missing or changed CAD, unavailable backend/numeric identity, non-finite calculation output, and
  exact replay-result mismatch; and
- desktop reopen with missing CAD/result/package/scenario, object-store publication failure, and
  preservation of recorded failure state without converting it into a pass.

Repository authority tests reject production Python motor formulas and findings, production calls
to `motor_arm_builtin_v1` outside `prometheus_execution`, component constants in QML, controllers,
replay, or persistence, and any desktop/CLI divergence from the shared execution targets. Reference
values from the removed Python module survive only in a checked-in, non-authoritative parity record;
its intentional thermal-model difference is labeled rather than silently treated as equivalent.

## Known limitations and exclusions

- Motor A, Motor B, their evidence, and the STEP assembly are synthetic conformance fixtures with
  `physical_validation_status=unvalidated`. They are unsuitable for a physical design decision.
- The backend evaluates four fixed motor-arm obligations. It does not infer requirements or select
  analyses, and it leaves center of gravity uncovered.
- Torque-speed and current checks are algebraic. Thermal output is a one-node periodic RC estimate
  that excludes gearbox and housing heat paths. No stress, fatigue, fastening, tolerance, fluid,
  circuit, controls, manufacturing, safety, or continuous-collision claim is made.
- The consumer accepts exact declared units; no general unit-conversion engine exists.
- Exact replay is conditioned on the recorded backend build and numeric identity. Cross-platform
  tests compare classifications and tolerances; they do not require identical floating-point bytes.
- CI requires the complete OCCT-disabled desktop on Linux, macOS, and Windows. The OCCT-enabled
  20-test acceptance was local on macOS and does not validate every CAD adapter/platform pair.
- The run store is a bounded Program 01B sidecar, not the universal content-addressed artifact
  index or portable project bundle planned for later programs.
- No arbitrary-file intake, OCR, deterministic parser worker, evidence extraction, semantic system
  graph, proof-obligation compiler, capability planner, isolated solver runtime, or external
  numerical backend is implemented.
- Authorization, signatures, trusted timestamps, packaged-product recovery, and defense against an
  attacker who can replace both the application and its local data remain outside this gate.
- The GitHub action-runtime deprecation warning and unavailable screenshot remain open maintenance
  and evidence-quality items; neither affects the recorded byte/result identities.

## Program 01C handoff

Program 01C safe evidence acquisition is next and has not started. It must preserve source bytes,
introduce bounded sandboxed deterministic parser workers and optional candidate extraction, retain
licensing and provenance state, and keep every extracted value subject to explicit review. Program
01B supplies the execution boundary that those future reviewed inputs must reach; it does not imply
that broader acquisition or arbitrary project verification already exists.
