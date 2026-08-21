# CalculiX smoke fixtures

The `complete` directory is historical synthetic tetrahedron parser and
result-coverage evidence. Its status, data, and stream files model the expected
CalculiX 2.23 output seam; they are not the current compiled axial smoke job.

Passing this fixture does not prove that CalculiX ran. The Windows
`run-calculix-smoke.ps1` command independently constructs a reviewed compiled
axial setup, runs a real process through the production runner, captures its
identity and raw evidence, and requires completed validated output.
