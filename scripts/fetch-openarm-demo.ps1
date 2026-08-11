$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$targetDirectory = Join-Path $repo 'out/external-demo'
$target = Join-Path $targetDirectory 'OpenArm_2.0.STEP'
$expectedSha256 = '0F97A5FB308D5B09353AA67BBD32D7C08E55DC5629BCBE20C12F71C82EF13C94'
$url = 'https://drive.usercontent.google.com/download?id=1aU-V3lt_aPrZoRM8FFx6dMQu2f28Ws0I&export=download&confirm=t'

New-Item -ItemType Directory -Force $targetDirectory | Out-Null
if (-not (Test-Path $target) -or (Get-FileHash $target -Algorithm SHA256).Hash -ne $expectedSha256) {
  Write-Output 'Downloading the OpenArm 2.0 STEP assembly (about 48 MB)...'
  Invoke-WebRequest -Uri $url -OutFile $target
}
if ((Get-FileHash $target -Algorithm SHA256).Hash -ne $expectedSha256) {
  throw 'OpenArm demo hash validation failed.'
}
Write-Output $target
