$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$trialFolder = & (Join-Path $PSScriptRoot 'prepare-yubi-gripper-trial.ps1') |
  Select-Object -Last 1
$assembly = Join-Path $trialFolder 'YUBI Gripper Assy_DYNAMIXEL.stp'

cmake --preset windows-release
cmake --build --preset windows-release

$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
& (Join-Path $repo 'out/build/windows-release/desktop/cad/prometheus_step_import_tests.exe') --import-only $assembly
if ($LASTEXITCODE -ne 0) {
  throw "The production STEP importer rejected the YUBI assembly (exit $LASTEXITCODE)."
}

$env:PROMETHEUS_STARTUP_PROJECT_FOLDER = $trialFolder
Remove-Item Env:PROMETHEUS_STARTUP_STEP -ErrorAction SilentlyContinue
& (Join-Path $repo 'out/build/windows-release/desktop/app/prometheus_desktop.exe')
