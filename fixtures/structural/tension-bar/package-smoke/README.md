# Structural package seam fixture

These files are synthetic CTest inputs. The mesh is a six-tetrahedron prism,
and the `.sta`, `.dat`, `.frd`, stdout, and stderr files were authored to
exercise the file-package verifier with complete identities and analytically
recognizable rows. They were not produced by CalculiX and are not solver-
validation or mesh-convergence evidence.

CTest uses the real exporter and verifier to prove that:

- the benchmark expectations and exact mesh compile into a reviewed case and
  deck package;
- complete result rows normalize into the expected scoped metrics;
- the tension-bar result profile runs analytic metrics while the generic
  structural profile does not;
- the retained synthetic `.frd` bytes receive an exact result-manifest hash;
- the predeclared displacement limit produces `pass` while the absent stress
  limit remains `indeterminate`; and
- changing the packaged mesh by one byte causes verification to return a
  blocker.

Only `scripts/run-structural-validation.ps1` on the pinned Windows Gmsh and
CalculiX backends can produce the real analytic benchmark evidence.
