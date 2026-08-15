$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$externalRoot = Join-Path $repo 'out/external-demo'
$source = Join-Path $externalRoot 'open-source-rover'
$sourceCommit = '0c4a0d97ba09d028a9ca380ae8e6729ac4b8bef7'
$trial = Join-Path $repo 'out/trials/jpl-open-source-rover-0c4a0d9'
$archive = Join-Path $externalRoot 'jpl-open-source-rover-0c4a0d9.zip'

New-Item -ItemType Directory -Force $externalRoot | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $source '.git'))) {
  git clone --depth 1 https://github.com/nasa-jpl/open-source-rover.git $source
}
git -C $source cat-file -e "$sourceCommit`^{commit}" 2>$null
if ($LASTEXITCODE -ne 0) {
  git -C $source fetch --depth 1 origin $sourceCommit
  if ($LASTEXITCODE -ne 0) {
    throw 'Could not fetch the pinned JPL Open Source Rover revision.'
  }
}
git -C $source checkout --detach $sourceCommit | Out-Null
if ((git -C $source rev-parse HEAD).Trim() -ne $sourceCommit) {
  throw 'JPL Open Source Rover revision validation failed.'
}

if (-not (Test-Path -LiteralPath $trial)) {
  git -C $source archive --format=zip --output=$archive $sourceCommit
  if ($LASTEXITCODE -ne 0) {
    throw 'Could not create the pinned JPL Open Source Rover archive.'
  }
  New-Item -ItemType Directory -Force $trial | Out-Null
  Expand-Archive -LiteralPath $archive -DestinationPath $trial
}

Write-Output $trial
