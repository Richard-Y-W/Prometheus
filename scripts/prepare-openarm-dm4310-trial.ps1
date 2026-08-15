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
  candidate_claims = @(
    [ordered]@{ id='rated_voltage'; label='Rated voltage'; quantity='voltage'; original_value='24'; original_unit='V'; value_si=24.0; si_unit='V'; source_page=9 },
    [ordered]@{ id='rated_current'; label='Rated current'; quantity='electric_current'; original_value='2.5'; original_unit='A'; value_si=2.5; si_unit='A'; source_page=9 },
    [ordered]@{ id='peak_current'; label='Peak current'; quantity='electric_current'; original_value='7.5'; original_unit='A'; value_si=7.5; si_unit='A'; source_page=9 },
    [ordered]@{ id='rated_torque'; label='Rated torque'; quantity='torque'; original_value='3'; original_unit='N m'; value_si=3.0; si_unit='N m'; source_page=9 },
    [ordered]@{ id='peak_torque'; label='Peak torque'; quantity='torque'; original_value='7'; original_unit='N m'; value_si=7.0; si_unit='N m'; source_page=9 },
    [ordered]@{ id='rated_speed'; label='Rated speed'; quantity='angular_speed'; original_value='120'; original_unit='rpm'; value_si=12.566370614359172; si_unit='rad/s'; source_page=9 },
    [ordered]@{ id='maximum_no_load_speed'; label='Maximum no-load speed'; quantity='angular_speed'; original_value='200'; original_unit='rpm'; value_si=20.943951023931955; si_unit='rad/s'; source_page=9 },
    [ordered]@{ id='reduction_ratio'; label='Reduction ratio'; quantity='ratio'; original_value='10:1'; original_unit='1'; value_si=10.0; si_unit='1'; source_page=9 },
    [ordered]@{ id='outer_diameter'; label='Outer diameter'; quantity='length'; original_value='56'; original_unit='mm'; value_si=0.056; si_unit='m'; source_page=9 },
    [ordered]@{ id='height'; label='Height'; quantity='length'; original_value='46'; original_unit='mm'; value_si=0.046; si_unit='m'; source_page=9 },
    [ordered]@{ id='approximate_mass'; label='Approximate motor mass'; quantity='mass'; original_value='approximately 300'; original_unit='g'; value_si=0.3; si_unit='kg'; source_page=9 }
  )
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
