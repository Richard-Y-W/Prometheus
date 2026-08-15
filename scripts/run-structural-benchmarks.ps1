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
& (Join-Path $repo 'out/build/windows-release/desktop/structural/prometheus_run_structural_benchmark.exe') `
  $ccx (Join-Path $output 'axial-tension-bar') axial
if ($LASTEXITCODE -ne 0) { throw 'Axial tension benchmark failed.' }
& (Join-Path $repo 'out/build/windows-release/desktop/structural/prometheus_run_structural_refinement.exe') `
  $ccx (Join-Path $output 'cantilever-refinement')
if ($LASTEXITCODE -ne 0) { throw 'Cantilever refinement benchmark failed.' }

Write-Output $output
