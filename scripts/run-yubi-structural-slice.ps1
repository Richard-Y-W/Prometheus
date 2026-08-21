[CmdletBinding(SupportsShouldProcess)]
param(
  [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

function Get-PrefixedSha256([string]$Path) {
  return 'sha256:' +
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-Utf8([string]$Path, [string]$Text) {
  [System.IO.File]::WriteAllText(
    $Path,
    $Text,
    [System.Text.UTF8Encoding]::new($false))
}

function Require-Marker(
    [string]$Text,
    [string]$Marker,
    [string]$FailureMessage) {
  if ($Text -notmatch [regex]::Escape($Marker)) {
    throw "$FailureMessage Missing marker: $Marker."
  }
}

$repo = Split-Path -Parent $PSScriptRoot
$manifest = Join-Path $repo `
  'fixtures/structural/yubi-bracket/reviewed-pair.json'
$validationRoot = [System.IO.Path]::GetFullPath(
  (Join-Path $repo 'out/validation/yubi-bracket'))
$output = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $validationRoot
} else {
  [System.IO.Path]::GetFullPath($OutputDirectory)
}
$validationPrefix = $validationRoot + [System.IO.Path]::DirectorySeparatorChar
if ($output -ne $validationRoot -and
    -not $output.StartsWith(
      $validationPrefix,
      [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "YUBI trial output must remain under: $validationRoot"
}

if (-not $PSCmdlet.ShouldProcess(
    $output,
    'Build the reviewed-pair tools, run two CalculiX samples, and replay the archive')) {
  Write-Output "Reviewed YUBI trial would use manifest: $manifest"
  Write-Output "Reviewed YUBI trial would write a new directory: $output"
  return
}

if (-not $IsWindows) {
  throw 'The native YUBI structural checkpoint requires the supported Windows solver host.'
}
if (Test-Path -LiteralPath $output) {
  throw "Output directory already exists: $output"
}
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
  throw "Reviewed YUBI manifest is missing: $manifest"
}

$ucrtBin = 'C:\msys64\ucrt64\bin'
if (-not (Test-Path -LiteralPath $ucrtBin -PathType Container)) {
  throw "The required UCRT64 tool directory does not exist: $ucrtBin"
}
$env:Path = "$ucrtBin;$env:Path"
$solver = (Get-Command -Name 'ccx' -CommandType Application `
  -ErrorAction Stop).Source
if ((Split-Path -Parent $solver) -ine $ucrtBin) {
  throw "CalculiX resolved outside the required UCRT64 directory: $solver"
}

cmake --preset windows-structural-release
if ($LASTEXITCODE -ne 0) {
  throw 'Windows structural configuration failed.'
}
cmake --build --preset windows-structural-release --target `
  prometheus_run_reviewed_structural_pair `
  prometheus_replay_structural_run `
  prometheus_reviewed_pair_tests
if ($LASTEXITCODE -ne 0) {
  throw 'Reviewed-pair tools build failed.'
}

$buildDirectory = Join-Path $repo 'out/build/windows-structural-release'
ctest --test-dir $buildDirectory --output-on-failure `
  -R '^prometheus_reviewed_pair_tests$'
if ($LASTEXITCODE -ne 0) {
  throw 'Reviewed-pair preflight tests failed.'
}

$toolRoot = Join-Path $buildDirectory 'desktop/structural'
$runner = Join-Path $toolRoot `
  'prometheus_run_reviewed_structural_pair.exe'
$replay = Join-Path $toolRoot 'prometheus_replay_structural_run.exe'
$outputParent = Split-Path -Parent $output
New-Item -ItemType Directory -Force $outputParent | Out-Null

$runnerLog = & $runner $manifest $solver $output 900 2>&1 | Out-String
$runnerExit = $LASTEXITCODE
if ($runnerExit -ne 0) {
  if (Test-Path -LiteralPath $output -PathType Container) {
    Write-Utf8 (Join-Path $output 'reviewed-pair-runner.log') $runnerLog
  }
  throw "Reviewed YUBI pair execution failed with exit code $runnerExit.`n$runnerLog"
}
Require-Marker $runnerLog 'status=completed' `
  'Reviewed YUBI pair execution did not complete.'
Require-Marker $runnerLog 'archive_schema_version=4.0.0' `
  'Reviewed YUBI pair execution did not emit a v4 archive.'
Write-Utf8 (Join-Path $output 'reviewed-pair-runner.log') $runnerLog
Write-Output $runnerLog.TrimEnd()

$archiveMatch = [regex]::Match(
  $runnerLog,
  '(?m)^archive_manifest=(.+?)\r?$')
if (-not $archiveMatch.Success) {
  throw 'Reviewed YUBI pair execution did not report its archive path.'
}
$archive = $archiveMatch.Groups[1].Value.Trim()
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
  throw "Reviewed YUBI archive is missing: $archive"
}

$replayLog = & $replay $archive 2>&1 | Out-String
$replayExit = $LASTEXITCODE
if ($replayExit -ne 0) {
  Write-Utf8 (Join-Path $output 'reviewed-pair-replay.log') $replayLog
  throw "Reviewed YUBI archive replay failed with exit code $replayExit.`n$replayLog"
}
Require-Marker $replayLog 'status=verified' `
  'Reviewed YUBI archive replay did not verify.'
Write-Utf8 (Join-Path $output 'reviewed-pair-replay.log') $replayLog
Write-Output $replayLog.TrimEnd()

$archiveDocument = Get-Content -LiteralPath $archive -Raw | ConvertFrom-Json
if ($archiveDocument.schema_version -cne '4.0.0') {
  throw 'The replayed YUBI archive does not use schema version 4.0.0.'
}
$coarseBackend = $archiveDocument.samples.coarse.backend
$fineBackend = $archiveDocument.samples.fine.backend
if ($coarseBackend.executable_sha256 -cne $fineBackend.executable_sha256 -or
    $coarseBackend.version -cne $fineBackend.version) {
  throw 'The two YUBI samples do not retain one solver identity and version.'
}

$evaluationMatch = [regex]::Match(
  $runnerLog,
  '(?m)^evaluation=(.+?)\r?$')
$refinementMatch = [regex]::Match(
  $runnerLog,
  '(?m)^refinement=(.+?)\r?$')
$summary = [ordered]@{
  '$schema' = 'urn:prometheus:yubi-structural-trial-summary:0.1.0'
  repository_commit = (git -C $repo rev-parse HEAD).Trim()
  reviewed_pair_manifest_sha256 = Get-PrefixedSha256 $manifest
  archive_sha256 = Get-PrefixedSha256 $archive
  solver = [ordered]@{
    executable_sha256 = [string]$coarseBackend.executable_sha256
    version = [string]$coarseBackend.version
  }
  refinement = if ($refinementMatch.Success) {
    $refinementMatch.Groups[1].Value.Trim()
  } else {
    'unreported'
  }
  evaluation = if ($evaluationMatch.Success) {
    $evaluationMatch.Groups[1].Value.Trim()
  } else {
    'unreported'
  }
  archive_manifest = Split-Path -Leaf $archive
  replay_verified = $true
}
Write-Utf8 (Join-Path $output 'trial-summary.json') `
  (($summary | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
Write-Output $output
