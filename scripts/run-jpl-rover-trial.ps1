param(
  [switch]$OpenDesktop,
  [string]$Preset = 'windows-release'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$driver = Join-Path $PSScriptRoot 'jpl-rover-trial.cmake'
$trialFolder = Join-Path $repo 'out/trials/jpl-open-source-rover-0c4a0d9'

Push-Location $repo
try {
  & cmake "-DPROMETHEUS_JPL_MODE=verify" "-DPROMETHEUS_JPL_PRESET=$Preset" -P $driver
  if ($LASTEXITCODE -ne 0) {
    throw "Pinned JPL Rover verification failed (exit $LASTEXITCODE)."
  }

  if ($OpenDesktop) {
    & cmake --build --preset $Preset --target prometheus_desktop
    if ($LASTEXITCODE -ne 0) {
      throw "Prometheus desktop build failed (exit $LASTEXITCODE)."
    }
    $env:Path = "C:\msys64\ucrt64\bin;$env:Path"
    $env:PROMETHEUS_STARTUP_PROJECT_FOLDER = $trialFolder
    Remove-Item Env:PROMETHEUS_STARTUP_STEP -ErrorAction SilentlyContinue
    Start-Process -FilePath (Join-Path $repo "out/build/$Preset/desktop/app/prometheus_desktop.exe")
  }
} finally {
  Pop-Location
}
