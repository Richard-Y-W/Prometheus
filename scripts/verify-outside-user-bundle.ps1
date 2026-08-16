[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BundleRoot,
  [string]$ExpectedRoverExpectationPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$manifestName = 'bundle-manifest.json'
$metadataName = 'Bundle-Metadata.json'
$manifestSchema = 'urn:prometheus:outside-user-bundle-manifest:1'
$metadataSchema = 'urn:prometheus:outside-user-screening-bundle:1'
$roverRevision = '0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7'
$roverExpectationSha256 = '9e8dc9544db12865dc5e8e3020747d6b07ee105790d7451f972cbe7044cc41d1'

function Require-Condition {
  param([bool]$Condition, [string]$Message)
  if (-not $Condition) { throw $Message }
}

function Assert-ExactProperties {
  param($Value, [string[]]$Expected, [string]$Label)
  Require-Condition ($null -ne $Value) "$Label is missing."
  $actual = [string[]]@($Value.PSObject.Properties.Name)
  $wanted = [string[]]@($Expected)
  [Array]::Sort($actual, [System.StringComparer]::Ordinal)
  [Array]::Sort($wanted, [System.StringComparer]::Ordinal)
  Require-Condition ($actual.Count -eq $wanted.Count) `
    "$Label has an unexpected property count."
  for ($index = 0; $index -lt $wanted.Count; $index++) {
    Require-Condition ($actual[$index] -ceq $wanted[$index]) `
      "$Label has unexpected or missing properties."
  }
}

function Get-RelativePayloadPath {
  param([string]$RootPrefix, [string]$FullName)
  $full = [System.IO.Path]::GetFullPath($FullName)
  Require-Condition ($full.StartsWith($RootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) `
    "Payload escaped the bundle root: $FullName"
  return $full.Substring($RootPrefix.Length).Replace([char]'\', [char]'/')
}

$rootWithTerminator = [System.IO.Path]::GetFullPath($BundleRoot)
$volumeRoot = [System.IO.Path]::GetPathRoot($rootWithTerminator)
Require-Condition (-not $rootWithTerminator.Equals($volumeRoot, [System.StringComparison]::OrdinalIgnoreCase)) `
  'A filesystem root cannot be used as a bundle root.'
$root = $rootWithTerminator.TrimEnd([char[]]@('\', '/'))
Require-Condition (Test-Path -LiteralPath $root -PathType Container) `
  "Bundle root is not a directory: $root"
$rootItem = Get-Item -LiteralPath $root -Force
Require-Condition (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) `
  'Bundle root may not be a symbolic link or reparse point.'
$rootPrefix = $root + [System.IO.Path]::DirectorySeparatorChar

$manifestPath = Join-Path $root $manifestName
Require-Condition (Test-Path -LiteralPath $manifestPath -PathType Leaf) `
  "Bundle manifest is missing: $manifestName"
$manifestItem = Get-Item -LiteralPath $manifestPath -Force
Require-Condition (($manifestItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) `
  'Bundle manifest may not be a symbolic link or reparse point.'
try {
  $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
} catch {
  throw "Bundle manifest is not valid JSON: $($_.Exception.Message)"
}
Assert-ExactProperties $manifest @('schema', 'files') 'Bundle manifest'
Require-Condition ($manifest.schema -is [string] -and $manifest.schema -ceq $manifestSchema) `
  'Bundle manifest schema is unsupported.'
Require-Condition ($manifest.files -is [System.Array] -and $manifest.files.Count -gt 0) `
  'Bundle manifest files must be a nonempty array.'

$listedPaths = New-Object 'System.Collections.Generic.List[string]'
$caseFoldedPaths = New-Object 'System.Collections.Generic.HashSet[string]' `
  -ArgumentList ([System.StringComparer]::OrdinalIgnoreCase)
$previousPath = $null
foreach ($entry in $manifest.files) {
  Assert-ExactProperties $entry @('path', 'byte_size', 'sha256') 'Manifest file entry'
  Require-Condition ($entry.path -is [string] -and $entry.path.Length -gt 0) `
    'Manifest path must be a nonempty string.'
  $relativePath = [string]$entry.path
  Require-Condition (-not $relativePath.Contains('\') -and
                     -not $relativePath.StartsWith('/') -and
                     -not $relativePath.Contains(':')) `
    "Manifest path is not a portable relative path: '$relativePath'"
  $segments = [string[]]$relativePath.Split('/')
  foreach ($segment in $segments) {
    Require-Condition ($segment.Length -gt 0 -and $segment -cne '.' -and $segment -cne '..') `
      "Manifest path contains an unsafe component: '$relativePath'"
  }
  Require-Condition ($relativePath -cne $manifestName) `
    'The bundle manifest must be the sole unlisted file.'
  if ($null -ne $previousPath) {
    Require-Condition ([string]::CompareOrdinal($previousPath, $relativePath) -lt 0) `
      "Manifest paths are not in strict ordinal order at '$relativePath'."
  }
  Require-Condition ($caseFoldedPaths.Add($relativePath)) `
    "Manifest contains a duplicate or Windows-colliding path: '$relativePath'"
  $listedPaths.Add($relativePath)
  $previousPath = $relativePath

  Require-Condition (($entry.byte_size -is [int]) -or ($entry.byte_size -is [long])) `
    "Manifest byte_size is not an integer for '$relativePath'."
  $expectedSize = [long]$entry.byte_size
  Require-Condition ($expectedSize -ge 0) `
    "Manifest byte_size is negative for '$relativePath'."
  Require-Condition ($entry.sha256 -is [string] -and
                     $entry.sha256 -cmatch '^[0-9a-f]{64}$') `
    "Manifest SHA-256 is not strict lowercase hexadecimal for '$relativePath'."

  $nativeRelativePath = $relativePath.Replace(
    [char]'/', [System.IO.Path]::DirectorySeparatorChar)
  $payloadPath = Join-Path $root $nativeRelativePath
  Require-Condition (Test-Path -LiteralPath $payloadPath -PathType Leaf) `
    "Manifest payload is missing: '$relativePath'"
  $payload = Get-Item -LiteralPath $payloadPath -Force
  Require-Condition (($payload.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) `
    "Manifest payload may not be a symbolic link or reparse point: '$relativePath'"
  Require-Condition ([long]$payload.Length -eq $expectedSize) `
    "Manifest byte size mismatch for '$relativePath': expected $expectedSize, actual $($payload.Length)."
  $actualSha256 = (Get-FileHash -LiteralPath $payload.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  Require-Condition ($actualSha256 -ceq [string]$entry.sha256) `
    "Manifest SHA-256 mismatch for '$relativePath': expected '$($entry.sha256)', actual '$actualSha256'."
}

$reparseEntry = Get-ChildItem -LiteralPath $root -Force -Recurse |
  Where-Object {
    ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
  } | Select-Object -First 1
Require-Condition ($null -eq $reparseEntry) `
  'Bundle contains a symbolic link or reparse point.'
$actualPaths = [string[]]@(
  Get-ChildItem -LiteralPath $root -Force -File -Recurse |
    ForEach-Object { Get-RelativePayloadPath $rootPrefix $_.FullName } |
    Where-Object { $_ -cne $manifestName }
)
[Array]::Sort($actualPaths, [System.StringComparer]::Ordinal)
Require-Condition ($actualPaths.Count -eq $listedPaths.Count) `
  "Bundle payload count mismatch: manifest lists $($listedPaths.Count), bundle contains $($actualPaths.Count)."
for ($index = 0; $index -lt $actualPaths.Count; $index++) {
  Require-Condition ($actualPaths[$index] -ceq $listedPaths[$index]) `
    "Bundle has a missing or extra path: manifest '$($listedPaths[$index])', actual '$($actualPaths[$index])'."
}

$metadataPath = Join-Path $root $metadataName
Require-Condition (Test-Path -LiteralPath $metadataPath -PathType Leaf) `
  "Bundle metadata is missing: $metadataName"
try {
  $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
} catch {
  throw "Bundle metadata is not valid JSON: $($_.Exception.Message)"
}
Assert-ExactProperties $metadata `
  @('schema', 'prometheus_revision', 'rover_revision',
    'rover_expectation_sha256') 'Bundle metadata'
Require-Condition ($metadata.schema -is [string] -and
                   $metadata.schema -ceq $metadataSchema) `
  'Bundle metadata schema is unsupported.'
Require-Condition ($metadata.rover_revision -is [string] -and
                   $metadata.rover_revision -ceq $roverRevision) `
  'Bundle metadata names the wrong Rover revision.'
Require-Condition ($metadata.prometheus_revision -is [string] -and
                   $metadata.prometheus_revision -cmatch '^[0-9a-f]{12,40}$') `
  'Bundle metadata has an invalid Prometheus revision.'
Require-Condition ($metadata.rover_expectation_sha256 -is [string] -and
                   $metadata.rover_expectation_sha256 -cmatch '^[0-9a-f]{64}$' -and
                   $metadata.rover_expectation_sha256 -ceq $roverExpectationSha256) `
  'Bundle metadata does not match the reviewed Rover expectation identity.'

if (-not [string]::IsNullOrWhiteSpace($ExpectedRoverExpectationPath)) {
  Require-Condition (Test-Path -LiteralPath $ExpectedRoverExpectationPath -PathType Leaf) `
    "Checked-in Rover expectation is missing: $ExpectedRoverExpectationPath"
  $checkedInSha256 = (Get-FileHash -LiteralPath $ExpectedRoverExpectationPath -Algorithm SHA256).Hash.ToLowerInvariant()
  Require-Condition ($checkedInSha256 -ceq $roverExpectationSha256) `
    "Verifier Rover expectation pin is stale: expected '$roverExpectationSha256', checked-in '$checkedInSha256'."
  Require-Condition ($metadata.rover_expectation_sha256 -ceq $checkedInSha256) `
    'Bundle Rover expectation identity differs from the checked-in source.'
}

Write-Output "Verified $($listedPaths.Count) bundle payload files under $root"
