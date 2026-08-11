$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
cmake --preset windows-debug
cmake --build --preset windows-debug
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
& (Join-Path $repo 'out/build/windows-debug/desktop/app/prometheus_desktop.exe')
