$ErrorActionPreference = 'Stop'

function Assert-NativeSuccess {
  param([Parameter(Mandatory = $true)][string]$Description)
  if ($LASTEXITCODE -ne 0) {
    throw "$Description failed with exit code $LASTEXITCODE"
  }
}

$repo = Split-Path -Parent $PSScriptRoot
Push-Location (Join-Path $repo 'backend')
try {
  uv sync --locked --extra dev
  Assert-NativeSuccess 'Backend dependency synchronization'
  uv run --locked python ../scripts/verify-vendored-dependencies.py
  Assert-NativeSuccess 'Vendored dependency verification'
  uv run --locked pytest -q
  Assert-NativeSuccess 'Backend test suite'
} finally { Pop-Location }
Push-Location (Join-Path $repo 'frontend')
try {
  npm ci
  Assert-NativeSuccess 'Frontend dependency installation'
  npm test
  Assert-NativeSuccess 'Frontend test suite'
  npm run build
  Assert-NativeSuccess 'Frontend production build'
  npm audit --audit-level=high
  Assert-NativeSuccess 'Frontend dependency audit'
} finally { Pop-Location }
Push-Location $repo
try {
  $requiredPresets = @(
    'windows-headless-debug',
    'windows-integrity-debug',
    'windows-desktop-no-occt-debug'
  )
  foreach ($preset in $requiredPresets) {
    cmake --preset $preset
    Assert-NativeSuccess "Configure preset $preset"
    cmake --build --preset $preset
    Assert-NativeSuccess "Build preset $preset"
    ctest --preset $preset
    Assert-NativeSuccess "Test preset $preset"
  }

  $ucrtQt = 'C:\msys64\ucrt64\lib\cmake\Qt6\Qt6Config.cmake'
  $ucrtOcct = Get-ChildItem `
    'C:\msys64\ucrt64\lib\cmake' `
    -Filter 'OpenCASCADEConfig.cmake' `
    -Recurse `
    -ErrorAction SilentlyContinue | Select-Object -First 1
  if ((Test-Path $ucrtQt) -and $null -ne $ucrtOcct) {
    cmake --preset windows-debug
    Assert-NativeSuccess 'Configure optional OCCT-enabled Windows adapter'
    cmake --build --preset windows-debug
    Assert-NativeSuccess 'Build optional OCCT-enabled Windows adapter'
    ctest --preset windows-debug
    Assert-NativeSuccess 'Test optional OCCT-enabled Windows adapter'
  } else {
    Write-Host 'Optional OCCT-enabled UCRT64 verification unavailable; required no-OCCT desktop verification passed.'
  }
} finally { Pop-Location }
