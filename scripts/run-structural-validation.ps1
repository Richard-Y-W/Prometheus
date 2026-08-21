$ErrorActionPreference = 'Stop'

function Get-PrefixedSha256([string]$Path) {
  return 'sha256:' +
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Invoke-CheckedTool(
    [string]$Executable,
    [string[]]$Arguments,
    [string]$RequiredMarker,
    [string]$FailureMessage) {
  $output = & $Executable @Arguments 2>&1 | Out-String
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) {
    throw "$FailureMessage Exit code: $exitCode."
  }
  if ($output -notmatch [regex]::Escape($RequiredMarker)) {
    throw "$FailureMessage Missing marker: $RequiredMarker."
  }
  return $output
}

function Assert-RequiredMarkers(
    [string]$Output,
    [string[]]$Markers,
    [string]$FailureMessage) {
  foreach ($marker in $Markers) {
    if ($Output -notmatch [regex]::Escape($marker)) {
      throw "$FailureMessage Missing marker: $marker."
    }
  }
}

$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'out/validation/structural'
$axialOutput = Join-Path $output 'axial'
$cantileverOutput = Join-Path $output 'cantilever'
$buildDirectory = Join-Path $repo 'out/build/windows-structural-release'

$ucrtBin = 'C:\msys64\ucrt64\bin'
if (-not (Test-Path -LiteralPath $ucrtBin -PathType Container)) {
  throw "The required UCRT64 tool directory does not exist: $ucrtBin"
}
$env:Path = "$ucrtBin;$env:Path"
$solver = (Get-Command -Name 'ccx' -CommandType Application `
  -ErrorAction Stop).Source
if ((Split-Path -Parent $solver) -ine $ucrtBin) {
  throw "CalculiX resolved outside the required UCRT64 tool directory: $solver"
}

cmake --preset windows-structural-release
if ($LASTEXITCODE -ne 0) { throw 'Windows structural configuration failed.' }
cmake --build --preset windows-structural-release --target `
  prometheus_run_structural_benchmark `
  prometheus_run_structural_refinement `
  prometheus_replay_structural_run `
  prometheus_structural_tests
if ($LASTEXITCODE -ne 0) { throw 'Structural validation tools build failed.' }
ctest --test-dir $buildDirectory `
  --output-on-failure -R '^prometheus_structural_tests$'
if ($LASTEXITCODE -ne 0) {
  throw 'Structural known-pass/known-fail fixture gate failed.'
}

$toolRoot = Join-Path $buildDirectory 'desktop/structural'
$benchmark = Join-Path $toolRoot 'prometheus_run_structural_benchmark.exe'
$refinement = Join-Path $toolRoot 'prometheus_run_structural_refinement.exe'
$replay = Join-Path $toolRoot 'prometheus_replay_structural_run.exe'
New-Item -ItemType Directory -Force $output | Out-Null

$axialLog = Invoke-CheckedTool $benchmark `
  @($solver, $axialOutput, 'axial') `
  'benchmark=passed' `
  'The axial analytic benchmark failed.'
Write-Output $axialLog.TrimEnd()
$manifestMatch = [regex]::Match(
  $axialLog,
  '(?m)^archive_manifest=(.+?)\r?$')
if (-not $manifestMatch.Success) {
  throw 'The axial benchmark did not emit a structural archive manifest.'
}
$manifest = $manifestMatch.Groups[1].Value.Trim()
if (-not (Test-Path -LiteralPath $manifest)) {
  throw 'The emitted structural archive manifest does not exist.'
}
$replayLog = Invoke-CheckedTool $replay `
  @($manifest) `
  'status=verified' `
  'Offline replay rejected the axial benchmark archive.'
Write-Output $replayLog.TrimEnd()
$archiveDocument = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($archiveDocument.schema_version -cne '4.0.0') {
  throw 'The axial archive does not use structural schema version 4.0.0.'
}
$coarseBackend = $archiveDocument.samples.coarse.backend
$fineBackend = $archiveDocument.samples.fine.backend
$coarseVersion = [string]$coarseBackend.version
$fineVersion = [string]$fineBackend.version
if ([string]::IsNullOrWhiteSpace($coarseVersion) -or
    $coarseVersion -cne $fineVersion) {
  throw 'The axial archive backend versions are missing or inconsistent.'
}
$coarseSolverSha256 = [string]$coarseBackend.executable_sha256
$fineSolverSha256 = [string]$fineBackend.executable_sha256
if ($coarseSolverSha256 -cnotmatch '^sha256:[0-9a-f]{64}$' -or
    $fineSolverSha256 -cnotmatch '^sha256:[0-9a-f]{64}$' -or
    $coarseSolverSha256 -cne $fineSolverSha256) {
  throw 'The axial archive backend executable identities are invalid.'
}
$selectedSolverSha256 = Get-PrefixedSha256 $solver
if ($coarseSolverSha256 -cne $selectedSolverSha256) {
  throw 'The axial archive backend does not match the selected CalculiX executable.'
}
$solverVersion = $coarseVersion
$solverSha256 = $coarseSolverSha256
$refinementLog = Invoke-CheckedTool $refinement `
  @($solver, $cantileverOutput) `
  'refinement=passed' `
  'The cantilever refinement gate failed.'
Assert-RequiredMarkers $refinementLog `
  @(
    'archive_schema_version=4.0.0',
    'observable.cantilever.maximum_displacement.change_fraction=',
    'observable.cantilever.section_von_mises.change_fraction=',
    'global.maximum_von_mises_stress.participated_in_acceptance=false',
    'global.maximum_von_mises_stress.status=not_converged_in_this_study'
  ) `
  'The cantilever scoped-convergence evidence is incomplete.'
Write-Output $refinementLog.TrimEnd()

$summary = [ordered]@{
  '$schema' = 'urn:prometheus:structural-validation-summary:0.2.0'
  solver = [ordered]@{
    executable_sha256 = $solverSha256
    version = $solverVersion
  }
  tools = [ordered]@{
    benchmark_sha256 = Get-PrefixedSha256 $benchmark
    refinement_sha256 = Get-PrefixedSha256 $refinement
    replay_sha256 = Get-PrefixedSha256 $replay
  }
  archive = [ordered]@{
    manifest = $manifest
    manifest_sha256 = Get-PrefixedSha256 $manifest
  }
  gate = [ordered]@{
    axial_closed_form_benchmark = $true
    cantilever_mesh_refinement = $true
    known_pass_known_fail_polarity = $true
    offline_archive_replay = $true
  }
  logs = [ordered]@{
    axial = $axialLog.Trim()
    cantilever = $refinementLog.Trim()
    replay = $replayLog.Trim()
  }
}
$summaryPath = Join-Path $output 'validation-summary.json'
$json = $summary | ConvertTo-Json -Depth 12
[System.IO.File]::WriteAllText(
  $summaryPath,
  $json + [Environment]::NewLine,
  [System.Text.UTF8Encoding]::new($false))
Write-Output $summaryPath
