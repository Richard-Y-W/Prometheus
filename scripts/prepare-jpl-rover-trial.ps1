param(
  [switch]$Refresh
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$arguments = @('-DPROMETHEUS_JPL_MODE=prepare')
if ($Refresh) {
  $arguments += '-DPROMETHEUS_JPL_REFRESH=ON'
}

Push-Location $repo
try {
  & cmake @arguments -P (Join-Path $PSScriptRoot 'jpl-rover-trial.cmake')
  if ($LASTEXITCODE -ne 0) {
    throw "Pinned JPL Rover preparation failed (exit $LASTEXITCODE)."
  }
} finally {
  Pop-Location
}

Write-Output (Join-Path $repo 'out/trials/jpl-open-source-rover-0c4a0d9')
