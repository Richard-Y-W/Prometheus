$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$verifier = Join-Path $repo 'scripts/verify-outside-user-bundle.ps1'
$expectedRover = Join-Path $repo 'docs/trials/jpl-open-source-rover-expectations.json'
$fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
  ("prometheus-outside-user-fixture-" + [Guid]::NewGuid().ToString('N'))
$bundle = Join-Path $fixtureRoot 'bundle'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Require-Condition {
  param([bool]$Condition, [string]$Message)
  if (-not $Condition) { throw $Message }
}

function Write-Utf8File {
  param([string]$Path, [string]$Text)
  [System.IO.File]::WriteAllText($Path, $Text, $utf8NoBom)
}

function Get-RelativePayloadPath {
  param([string]$Root, [string]$FullName)
  $prefix = [System.IO.Path]::GetFullPath($Root).TrimEnd([char[]]@('\', '/')) + `
    [System.IO.Path]::DirectorySeparatorChar
  return $FullName.Substring($prefix.Length).Replace([char]'\', [char]'/')
}

function Write-TestManifest {
  $paths = [string[]]@(
    Get-ChildItem -LiteralPath $bundle -Force -File -Recurse |
      ForEach-Object { Get-RelativePayloadPath $bundle $_.FullName } |
      Where-Object { $_ -cne 'bundle-manifest.json' }
  )
  [Array]::Sort($paths, [System.StringComparer]::Ordinal)
  $entries = @()
  foreach ($relativePath in $paths) {
    $nativePath = $relativePath.Replace(
      [char]'/', [System.IO.Path]::DirectorySeparatorChar)
    $file = Get-Item -LiteralPath (Join-Path $bundle $nativePath) -Force
    $entries += [ordered]@{
      path = $relativePath
      byte_size = [long]$file.Length
      sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
  }
  $manifest = [ordered]@{
    schema = 'urn:prometheus:outside-user-bundle-manifest:1'
    files = @($entries)
  }
  Write-Utf8File (Join-Path $bundle 'bundle-manifest.json') `
    (($manifest | ConvertTo-Json -Depth 5) + "`n")
}

function Reset-TestBundle {
  if (Test-Path -LiteralPath $bundle) {
    Remove-Item -LiteralPath $bundle -Recurse -Force
  }
  New-Item -ItemType Directory -Path (Join-Path $bundle 'App') -Force | Out-Null
  Write-Utf8File (Join-Path $bundle 'App/payload.txt') "payload`n"
  $expectedHash = (Get-FileHash -LiteralPath $expectedRover -Algorithm SHA256).Hash.ToLowerInvariant()
  $metadata = [ordered]@{
    schema = 'urn:prometheus:outside-user-screening-bundle:1'
    prometheus_revision = '1111111111111111111111111111111111111111'
    rover_revision = '0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7'
    rover_expectation_sha256 = $expectedHash
  }
  Write-Utf8File (Join-Path $bundle 'Bundle-Metadata.json') `
    (($metadata | ConvertTo-Json -Depth 3) + "`n")
  Write-TestManifest
}

function Test-VerifierAccepts {
  try {
    & $verifier -BundleRoot $bundle -ExpectedRoverExpectationPath $expectedRover *> $null
    return $true
  } catch {
    return $false
  }
}

try {
  Require-Condition (Test-Path -LiteralPath $verifier -PathType Leaf) `
    'Bundle verifier script is missing.'
  New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null

  Reset-TestBundle
  Require-Condition (Test-VerifierAccepts) 'Complete bundle was rejected.'

  Reset-TestBundle
  Remove-Item -LiteralPath (Join-Path $bundle 'App/payload.txt') -Force
  Require-Condition (-not (Test-VerifierAccepts)) 'Missing payload was accepted.'

  Reset-TestBundle
  Add-Content -LiteralPath (Join-Path $bundle 'App/payload.txt') -Value 'altered'
  Require-Condition (-not (Test-VerifierAccepts)) 'Altered payload was accepted.'

  Reset-TestBundle
  Write-Utf8File (Join-Path $bundle 'extra.txt') "extra`n"
  Require-Condition (-not (Test-VerifierAccepts)) 'Extra payload was accepted.'

  Reset-TestBundle
  $metadataPath = Join-Path $bundle 'Bundle-Metadata.json'
  $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
  $metadata.rover_expectation_sha256 = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
  Write-Utf8File $metadataPath (($metadata | ConvertTo-Json -Depth 3) + "`n")
  Write-TestManifest
  Require-Condition (-not (Test-VerifierAccepts)) `
    'Wrong embedded Rover expectation identity was accepted.'

  Reset-TestBundle
  $manifestPath = Join-Path $bundle 'bundle-manifest.json'
  $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
  $manifest.files[0].sha256 = 'A' + $manifest.files[0].sha256.Substring(1)
  Write-Utf8File $manifestPath (($manifest | ConvertTo-Json -Depth 5) + "`n")
  Require-Condition (-not (Test-VerifierAccepts)) `
    'Non-lowercase manifest SHA-256 was accepted.'
} finally {
  if (Test-Path -LiteralPath $fixtureRoot) {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
  }
}
