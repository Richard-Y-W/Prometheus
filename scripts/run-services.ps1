$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location (Join-Path $repo 'backend')
try { uv run uvicorn app.main:app --host 127.0.0.1 --port 8000 } finally { Pop-Location }
