$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$stepPath = & (Join-Path $PSScriptRoot 'fetch-openarm-demo.ps1') | Select-Object -Last 1
cmake --preset windows-debug
cmake --build --preset windows-debug
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
$env:PROMETHEUS_STARTUP_STEP = $stepPath
& (Join-Path $repo 'out/build/windows-debug/desktop/app/prometheus_desktop.exe')
