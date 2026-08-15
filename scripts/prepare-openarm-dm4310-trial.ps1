$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$external = Join-Path $repo 'out/external-demo'
$trial = Join-Path $repo 'out/trials/openarm-2-dm-j4310'
$assembly = Join-Path $external 'OpenArm_2.0.STEP'
$manual = Join-Path $external 'DM-J4310-2EC-V1.1-manual.pdf'

$assemblySha256 = '0F97A5FB308D5B09353AA67BBD32D7C08E55DC5629BCBE20C12F71C82EF13C94'
$manualSha256 = 'AAD2C3AE95932393C87B4D149A4009C8E93AF727DF8D787472DE807E2703D68E'
$manualUrl = 'https://docs.openarm.dev/assets/files/dm4310-5d089e0b013d8e2fc203c5b670008a66.pdf'

& (Join-Path $PSScriptRoot 'fetch-openarm-demo.ps1') | Out-Null
if (-not (Test-Path $manual) -or
    (Get-FileHash $manual -Algorithm SHA256).Hash -ne $manualSha256) {
  Invoke-WebRequest -Uri $manualUrl -OutFile $manual
}
if ((Get-FileHash $assembly -Algorithm SHA256).Hash -ne $assemblySha256) {
  throw 'OpenArm assembly hash validation failed.'
}
if ((Get-FileHash $manual -Algorithm SHA256).Hash -ne $manualSha256) {
  throw 'DM-J4310 manual hash validation failed.'
}

New-Item -ItemType Directory -Force $trial | Out-Null
Copy-Item -LiteralPath $assembly -Destination (Join-Path $trial 'OpenArm_2.0.STEP') -Force
Copy-Item -LiteralPath $manual -Destination (Join-Path $trial 'DM-J4310-2EC-V1.1-manual.pdf') -Force

$manifest = [ordered]@{
  schema = 'urn:prometheus:trial-source-manifest:0.1.0'
  status = 'candidate_evidence_only'
  project = [ordered]@{
    name = 'OpenArm 2.0'
    upstream = 'https://github.com/enactic/openarm_hardware'
    license = 'CERN-OHL-S-2.0'
    assembly_file = 'OpenArm_2.0.STEP'
    assembly_sha256 = $assemblySha256.ToLowerInvariant()
  }
  component = [ordered]@{
    manufacturer = 'DAMIAO'
    part_number = 'DM-J4310-2EC V1.1'
    relationship = 'OpenArm 2.0 J5-J8 actuator candidate'
    source_page = 'https://docs.openarm.dev/hardware/openarm-2.0/motor/'
    source_file = 'DM-J4310-2EC-V1.1-manual.pdf'
    source_sha256 = $manualSha256.ToLowerInvariant()
  }
  review = [ordered]@{
    published_component = $false
    geometry_binding_confirmed = $false
    specification_claims_reviewed = $false
    note = 'This manifest records sources only. It does not authorize engineering values or a simulation.'
  }
  initial_questions = @(
    'Can Prometheus identify and bind the selected J5-J8 motor geometry to this exact component revision?',
    'Does the motor envelope and mounting geometry fit without static interference?',
    'After the operating scenario is reviewed, is rated torque sufficient for the bounded joint load?'
  )
}
$manifestPath = Join-Path $trial 'prometheus-trial-source-manifest.json'
$json = $manifest | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine,
  [System.Text.UTF8Encoding]::new($false))

Write-Output $trial
