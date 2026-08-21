$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'out/structural-benchmarks'

cmake --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows Release configuration failed.' }
cmake --build --preset windows-release --target `
  prometheus_run_structural_benchmark prometheus_run_structural_refinement
if ($LASTEXITCODE -ne 0) { throw 'Structural benchmark build failed.' }

$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
$ccx = (Get-Command ccx.exe -ErrorAction Stop).Source
$benchmark = Join-Path $repo `
  'out/build/windows-release/desktop/structural/prometheus_run_structural_benchmark.exe'
$refinement = Join-Path $repo `
  'out/build/windows-release/desktop/structural/prometheus_run_structural_refinement.exe'
$axialLog = & $benchmark $ccx (Join-Path $output 'axial-tension-bar') axial `
  2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw 'Axial tension benchmark failed.' }
if ($axialLog -notmatch [regex]::Escape('archive_schema_version=4.0.0')) {
  throw 'Axial tension benchmark did not produce archive v4.'
}
Write-Output $axialLog.TrimEnd()
$refinementLog = & $refinement `
  $ccx (Join-Path $output 'cantilever-refinement') 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw 'Cantilever refinement benchmark failed.' }
$requiredMarkers = @(
  'archive_schema_version=4.0.0',
  'observable.cantilever.maximum_displacement.change_fraction=',
  'observable.cantilever.section_von_mises.change_fraction=',
  'global.maximum_von_mises_stress.participated_in_acceptance=false',
  'global.maximum_von_mises_stress.status=not_converged_in_this_study'
)
foreach ($marker in $requiredMarkers) {
  if ($refinementLog -notmatch [regex]::Escape($marker)) {
    throw "Cantilever refinement evidence is incomplete. Missing marker: $marker."
  }
}
Write-Output $refinementLog.TrimEnd()

Write-Output $output
