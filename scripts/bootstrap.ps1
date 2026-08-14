$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location (Join-Path $repo 'backend')
try { uv sync --locked --extra dev } finally { Pop-Location }
Push-Location (Join-Path $repo 'frontend')
try { npm ci } finally { Pop-Location }
Write-Host 'Locked Python and reference frontend dependencies are ready.'
Write-Host 'Required native prerequisites: MSVC 2022 and a matching Qt MSVC 2022 ABI.'
Write-Host 'Run scripts/verify.ps1 to verify headless, integrity-only, and no-OCCT desktop boundaries.'
