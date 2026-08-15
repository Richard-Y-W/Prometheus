$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$trialFolder = & (Join-Path $PSScriptRoot 'prepare-openarm-dm4310-trial.ps1') |
  Select-Object -Last 1

cmake --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows Release configuration failed.' }
cmake --build --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows Release build failed.' }

$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
$env:PROMETHEUS_STARTUP_PROJECT_FOLDER = $trialFolder
Remove-Item Env:PROMETHEUS_STARTUP_STEP -ErrorAction SilentlyContinue

& (Join-Path $repo 'out/build/windows-release/desktop/app/prometheus_desktop.exe')
