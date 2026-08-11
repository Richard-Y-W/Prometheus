$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location (Join-Path $repo 'backend')
try { uv run pytest -q } finally { Pop-Location }
Push-Location (Join-Path $repo 'frontend')
try { npm test; npm run build; npm audit --audit-level=high } finally { Pop-Location }
Push-Location $repo
try {
  cmake --preset headless-debug
  cmake --build --preset headless-debug
  ctest --preset headless-debug
} finally { Pop-Location }
