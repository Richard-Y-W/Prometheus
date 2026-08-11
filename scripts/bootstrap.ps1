$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location (Join-Path $repo 'backend')
try { uv sync --extra dev } finally { Pop-Location }
Push-Location (Join-Path $repo 'frontend')
try { npm install } finally { Pop-Location }
Write-Host 'Python and reference frontend dependencies are ready.'
Write-Host 'Native desktop prerequisites: MSVC 2022, Qt 6.5+, then configure Qt6_DIR and run cmake --preset windows-debug.'
