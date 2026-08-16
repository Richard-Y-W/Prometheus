[CmdletBinding()]
param(
  [string]$Preset = 'windows-release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Require-Condition {
  param([bool]$Condition, [string]$Message)
  if (-not $Condition) { throw $Message }
}

function Assert-NativeSuccess {
  param([string]$Description)
  if ($LASTEXITCODE -ne 0) {
    throw "$Description failed with exit code $LASTEXITCODE."
  }
}

function Assert-SafeGeneratedChild {
  param([string]$Path, [string]$Parent)
  $candidate = [System.IO.Path]::GetFullPath($Path)
  $boundedRoot = [System.IO.Path]::GetFullPath($Parent).TrimEnd([char[]]@('\', '/'))
  $prefix = $boundedRoot + [System.IO.Path]::DirectorySeparatorChar
  Require-Condition ($candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) `
    "Refusing generated-path cleanup outside '$boundedRoot': '$candidate'."
  if (Test-Path -LiteralPath $candidate) {
    $item = Get-Item -LiteralPath $candidate -Force
    Require-Condition (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) `
      "Refusing generated-path cleanup of a reparse point: '$candidate'."
  }
}

function Remove-SafeGeneratedPath {
  param([string]$Path, [string]$Parent)
  Assert-SafeGeneratedChild $Path $Parent
  if (Test-Path -LiteralPath $Path) {
    Remove-Item -LiteralPath $Path -Recurse -Force
  }
}

function Write-Utf8File {
  param([string]$Path, [string]$Text)
  $encoding = New-Object System.Text.UTF8Encoding($false)
  [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Get-RelativePayloadPath {
  param([string]$RootPrefix, [string]$FullName)
  $full = [System.IO.Path]::GetFullPath($FullName)
  Require-Condition ($full.StartsWith($RootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) `
    "Payload escaped the bundle stage: '$FullName'."
  return $full.Substring($RootPrefix.Length).Replace([char]'\', [char]'/')
}

function Write-BundleManifest {
  param([string]$Stage)
  $root = [System.IO.Path]::GetFullPath($Stage).TrimEnd([char[]]@('\', '/'))
  $rootPrefix = $root + [System.IO.Path]::DirectorySeparatorChar
  $paths = [string[]]@(
    Get-ChildItem -LiteralPath $root -Force -File -Recurse |
      ForEach-Object { Get-RelativePayloadPath $rootPrefix $_.FullName } |
      Where-Object { $_ -cne 'bundle-manifest.json' }
  )
  [Array]::Sort($paths, [System.StringComparer]::Ordinal)
  $entries = @()
  foreach ($relativePath in $paths) {
    $nativePath = $relativePath.Replace(
      [char]'/', [System.IO.Path]::DirectorySeparatorChar)
    $item = Get-Item -LiteralPath (Join-Path $root $nativePath) -Force
    Require-Condition (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) `
      "Bundle payload may not be a reparse point: '$relativePath'."
    $entries += [ordered]@{
      path = $relativePath
      byte_size = [long]$item.Length
      sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
  }
  $manifest = [ordered]@{
    schema = 'urn:prometheus:outside-user-bundle-manifest:1'
    files = @($entries)
  }
  Write-Utf8File (Join-Path $root 'bundle-manifest.json') `
    (($manifest | ConvertTo-Json -Depth 5) + "`n")
}

Require-Condition ($env:OS -eq 'Windows_NT') `
  'The deployable outside-user package must be built on Windows.'
Require-Condition ($Preset -cmatch '^[A-Za-z0-9._-]+$') `
  "Unsafe CMake preset name: '$Preset'."
$ucrtBinaryDirectory = 'C:\msys64\ucrt64\bin'
if (Test-Path -LiteralPath $ucrtBinaryDirectory -PathType Container) {
  $env:Path = "$ucrtBinaryDirectory;$env:Path"
}

$repo = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repo 'out/outside-user'
$trial = Join-Path $repo 'out/trials/jpl-open-source-rover-0c4a0d9'
$expectation = Join-Path $repo 'docs/trials/jpl-open-source-rover-expectations.json'
$verifier = Join-Path $PSScriptRoot 'verify-outside-user-bundle.ps1'
$roverDriver = Join-Path $PSScriptRoot 'jpl-rover-trial.cmake'

$dirty = & git -C $repo status --porcelain
Assert-NativeSuccess 'Git worktree inspection'
Require-Condition ([string]::IsNullOrWhiteSpace(($dirty -join "`n"))) `
  'Refusing to package an uncommitted worktree.'
$prometheusRevision = (& git -C $repo rev-parse HEAD).Trim()
Assert-NativeSuccess 'Prometheus revision resolution'
Require-Condition ($prometheusRevision -cmatch '^[0-9a-f]{40}$') `
  'Prometheus revision is not a full lowercase Git object ID.'
$revisionShort = $prometheusRevision.Substring(0, 12)
$bundleName = "Prometheus-Outside-User-$revisionShort"
$stage = Join-Path $outputRoot $bundleName
$verificationExtract = Join-Path $outputRoot "$bundleName-verify"
$zipPath = Join-Path $outputRoot "$bundleName.zip"

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
Remove-SafeGeneratedPath $stage $outputRoot
Remove-SafeGeneratedPath $verificationExtract $outputRoot
Remove-SafeGeneratedPath $zipPath $outputRoot

Push-Location $repo
try {
  & cmake '-DPROMETHEUS_JPL_MODE=verify' "-DPROMETHEUS_JPL_PRESET=$Preset" `
    -P $roverDriver
  Assert-NativeSuccess 'Pinned Rover verification'

  & cmake --preset $Preset
  Assert-NativeSuccess 'Windows package configuration'
  & cmake --build --preset $Preset --target prometheus_desktop
  Assert-NativeSuccess 'Windows desktop package build'
} finally {
  Pop-Location
}

$builtExecutable = Join-Path $repo "out/build/$Preset/desktop/app/prometheus_desktop.exe"
Require-Condition (Test-Path -LiteralPath $builtExecutable -PathType Leaf) `
  "Built desktop executable is missing: '$builtExecutable'."
Require-Condition (Test-Path -LiteralPath $trial -PathType Container) `
  'Validated Rover trial is missing after verification.'

$appDirectory = Join-Path $stage 'App'
$projectDirectory = Join-Path $stage 'Project/JPL-Open-Source-Rover'
$instructionsDirectory = Join-Path $stage 'Instructions'
New-Item -ItemType Directory -Path $appDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $projectDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $instructionsDirectory -Force | Out-Null
$stagedExecutable = Join-Path $appDirectory 'prometheus_desktop.exe'
Copy-Item -LiteralPath $builtExecutable -Destination $stagedExecutable

& robocopy $trial $projectDirectory /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /XJ `
  /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -gt 7) {
  throw "Rover snapshot copy failed with robocopy exit code $LASTEXITCODE."
}
$projectFileCount = @(Get-ChildItem -LiteralPath $projectDirectory -Force -File -Recurse).Count
Require-Condition ($projectFileCount -eq 967) `
  "Packaged Rover snapshot has $projectFileCount files; expected 967."

$deploymentTool = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if ($null -eq $deploymentTool) {
  $deploymentTool = Get-Command windeployqt6.exe -ErrorAction SilentlyContinue
}
Require-Condition ($null -ne $deploymentTool) `
  'windeployqt was not found on PATH.'
& $deploymentTool.Source --release --qmldir (Join-Path $repo 'desktop/ui') `
  --compiler-runtime --dir $appDirectory $stagedExecutable
Assert-NativeSuccess 'Qt runtime deployment'

Copy-Item -LiteralPath (Join-Path $repo 'docs/trials/outside-user-screening-task-sheet.md') `
  -Destination (Join-Path $instructionsDirectory 'Participant-Task.md')
Copy-Item -LiteralPath (Join-Path $repo 'docs/trials/outside-user-screening-observation-form.md') `
  -Destination (Join-Path $instructionsDirectory 'Blank-Observation-Form.md')
Copy-Item -LiteralPath $verifier -Destination (Join-Path $stage 'Verify-Bundle.ps1')

$expectationSha256 = (Get-FileHash -LiteralPath $expectation -Algorithm SHA256).Hash.ToLowerInvariant()
Require-Condition ($expectationSha256 -ceq '9e8dc9544db12865dc5e8e3020747d6b07ee105790d7451f972cbe7044cc41d1') `
  'Checked-in Rover expectation changed without updating the bundle verifier pin.'
$metadata = [ordered]@{
  schema = 'urn:prometheus:outside-user-screening-bundle:1'
  prometheus_revision = $prometheusRevision
  rover_revision = '0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7'
  rover_expectation_sha256 = $expectationSha256
}
Write-Utf8File (Join-Path $stage 'Bundle-Metadata.json') `
  (($metadata | ConvertTo-Json -Depth 3) + "`n")

$sourceNotice = @'
Prometheus outside-user screening source notice

The supplied project snapshot is NASA/JPL Open Source Rover from:
https://github.com/nasa-jpl/open-source-rover

Pinned Git revision:
0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7

The upstream license text is included unchanged at:
Project\JPL-Open-Source-Rover\LICENSE.txt
'@
Write-Utf8File (Join-Path $stage 'SOURCE-NOTICE.txt') ($sourceNotice + "`n")

$launcher = @'
@echo off
setlocal
set "PROMETHEUS_STARTUP_PROJECT_FOLDER=%~dp0Project\JPL-Open-Source-Rover"
set "PROMETHEUS_STARTUP_STEP="
start "" "%~dp0App\prometheus_desktop.exe"
'@
Write-Utf8File (Join-Path $stage 'Launch-Prometheus.cmd') ($launcher + "`r`n")

$textExtensions = @('.txt', '.md', '.cmd', '.json', '.conf', '.ini')
$textFiles = @(Get-ChildItem -LiteralPath $stage -Force -File -Recurse |
  Where-Object { $textExtensions -contains $_.Extension.ToLowerInvariant() })
$sourcePathSpellings = @($repo, $repo.Replace([char]'\', [char]'/'))
foreach ($textFile in $textFiles) {
  $text = Get-Content -LiteralPath $textFile.FullName -Raw
  foreach ($sourcePath in $sourcePathSpellings) {
    Require-Condition (-not $text.Contains($sourcePath)) `
      "Bundle text leaks a source-tree path in '$($textFile.FullName)'."
  }
}
$startupMentions = @($textFiles | Select-String -SimpleMatch `
  'PROMETHEUS_STARTUP_PROJECT_FOLDER')
Require-Condition ($startupMentions.Count -eq 1 -and
                   $startupMentions[0].Path -ceq (Join-Path $stage 'Launch-Prometheus.cmd')) `
  'The startup project folder must be set only by Launch-Prometheus.cmd.'

Write-BundleManifest $stage
& $verifier -BundleRoot $stage -ExpectedRoverExpectationPath $expectation | Out-Host

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
  $stage, $zipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)
New-Item -ItemType Directory -Path $verificationExtract -Force | Out-Null
[System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $verificationExtract)
& $verifier -BundleRoot $verificationExtract `
  -ExpectedRoverExpectationPath $expectation | Out-Host
Remove-SafeGeneratedPath $verificationExtract $outputRoot

$zipSha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "Package: $zipPath"
Write-Output "SHA-256: $zipSha256"
Write-Output "Payload files: $((Get-Content -LiteralPath (Join-Path $stage 'bundle-manifest.json') -Raw | ConvertFrom-Json).files.Count)"
