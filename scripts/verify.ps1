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
  uv run --locked python scripts/export_contract_schemas.py
  Assert-NativeSuccess 'Contract schema generation'
  uv run --locked python scripts/export_contract_fixture.py
  Assert-NativeSuccess 'Contract fixture generation'
  uv run --locked python scripts/export_program_01b_fixtures.py
  Assert-NativeSuccess 'Program 01B fixture generation'
  git diff --exit-code -- ../schemas ../fixtures/contracts
  Assert-NativeSuccess 'Generated contract byte verification'
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
    @{
      Name = 'windows-headless-debug'
      Boundary = 'windows-headless-boundary-debug'
    },
    @{
      Name = 'windows-integrity-debug'
      Boundary = 'windows-integrity-boundary-debug'
    },
    @{
      Name = 'windows-desktop-no-occt-debug'
      Boundary = 'windows-desktop-no-occt-boundary-debug'
    }
  )
  foreach ($preset in $requiredPresets) {
    cmake --preset $preset.Name
    Assert-NativeSuccess "Configure preset $($preset.Name)"
    cmake --build --preset $preset.Boundary
    Assert-NativeSuccess "Assert required targets with $($preset.Boundary)"
    cmake --build --preset $preset.Name
    Assert-NativeSuccess "Build preset $($preset.Name)"
    ctest --preset $preset.Name
    Assert-NativeSuccess "Test preset $($preset.Name)"
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
