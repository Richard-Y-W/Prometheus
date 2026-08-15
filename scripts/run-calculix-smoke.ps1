$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'out/structural-smoke'
$job = 'prometheus_tetra_smoke'

cmake --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows Release configuration failed.' }
cmake --build --preset windows-release --target prometheus_export_structural_smoke
if ($LASTEXITCODE -ne 0) { throw 'Structural smoke exporter build failed.' }

$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
New-Item -ItemType Directory -Force $output | Out-Null
& (Join-Path $repo 'out/build/windows-release/desktop/structural/prometheus_export_structural_smoke.exe') $output
if ($LASTEXITCODE -ne 0) { throw 'Structural smoke deck export failed.' }

Push-Location $output
try {
  & ccx $job
  if ($LASTEXITCODE -ne 0) { throw "CalculiX failed with exit code $LASTEXITCODE." }
} finally {
  Pop-Location
}

$required = @("$job.inp", "$job.dat", "$job.frd", "$job.sta")
foreach ($name in $required) {
  if (-not (Test-Path -LiteralPath (Join-Path $output $name))) {
    throw "CalculiX did not produce required output: $name"
  }
}
Write-Output $output
