# CalculiX smoke fixtures

The `complete` directory is synthetic parser and result-coverage evidence. Its
deck is generated from `structural_smoke_request()`, while its status, data,
and stream files model the expected CalculiX 2.23 output seam.

Passing this fixture does not prove that CalculiX ran. The Windows
`run-calculix-smoke.ps1` command must independently capture a real process exit
status, executable hash, version, standard streams, status file, and data file
and pass all of them through the same verifier.
