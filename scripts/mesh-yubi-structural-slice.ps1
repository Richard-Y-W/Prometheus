$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$trial = & (Join-Path $PSScriptRoot 'prepare-yubi-structural-slice.ps1') |
  Select-Object -Last 1
$geometry = Join-Path $trial 'BRACKET_GRIPPER.stp'
$mesh = Join-Path $trial 'BRACKET_GRIPPER.gmsh.inp'

cmake --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows Release configuration failed.' }
cmake --build --preset windows-release --target prometheus_structural_mesh_probe
if ($LASTEXITCODE -ne 0) { throw 'Structural mesh probe build failed.' }

$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
& gmsh $geometry -3 -format inp -o $mesh -clmin 1.0 -clmax 3.0 -v 3
if ($LASTEXITCODE -ne 0) { throw "Gmsh failed with exit code $LASTEXITCODE." }
if (-not (Test-Path -LiteralPath $mesh)) {
  throw 'Gmsh reported success but its mesh output is missing.'
}

& (Join-Path $repo 'out/build/windows-release/desktop/structural/prometheus_structural_mesh_probe.exe') $mesh
if ($LASTEXITCODE -ne 0) { throw 'The generated bracket mesh failed strict SI import.' }
Write-Output $mesh
