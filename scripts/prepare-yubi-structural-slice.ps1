$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'prepare-yubi-gripper-trial.ps1') | Out-Null
$source = Join-Path $repo 'out/external-demo/yubi-hw'
$trial = Join-Path $repo 'out/trials/yubi-bracket-structural'
$materialEvidenceSource = Join-Path $repo 'fixtures/evidence/aluminum-2024-candidates-v1.json'
$materialEvidenceDestination = 'aluminum-2024-candidates-v1.json'
$materialEvidenceSha256 = 'cc0d48a14b9802f43aa599a994b35bbcb02cda4708119bcacbd41b3bf219fbe3'

$files = @(
  [ordered]@{ source='STEP/gripper/BRACKET_GRIPPER.stp'; destination='BRACKET_GRIPPER.stp'; sha256='4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a' },
  [ordered]@{ source='docs/BOM/YUBI Gripper_DYNAMIXEL_BOM.csv'; destination='YUBI Gripper_DYNAMIXEL_BOM.csv'; sha256='5251dad9b3f3c3deb26aeb0d7b887c6418c90593389f95b12df73ab694c7922b' },
  [ordered]@{ source='docs/AssemblyInstruction/YUBI Gripper_DYNAMIXEL_AssemblyGuide.pdf'; destination='YUBI Gripper_DYNAMIXEL_AssemblyGuide.pdf'; sha256='db39b37599c0d5f3ced95e60d911b88b0a05cb54a30fdabfebba6286fe4c8882' },
  [ordered]@{ source='LICENSE'; destination='YUBI-HARDWARE-LICENSE.txt'; sha256='c505877867b60d84e22e1c74a91306635e922483344c558e69164d237ff7d7f2' }
)

New-Item -ItemType Directory -Force $trial | Out-Null
foreach ($file in $files) {
  $sourcePath = Join-Path $source $file.source
  if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {
    throw "YUBI structural source hash validation failed: $($file.source)"
  }
  Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $trial $file.destination) -Force
}
if ((Get-FileHash -LiteralPath $materialEvidenceSource -Algorithm SHA256).Hash.ToLowerInvariant() -ne $materialEvidenceSha256) {
  throw 'YUBI structural material-evidence hash validation failed.'
}
Copy-Item -LiteralPath $materialEvidenceSource -Destination (Join-Path $trial $materialEvidenceDestination) -Force

$manifest = [ordered]@{
  schema = 'urn:prometheus:structural-slice-candidate:0.1.0'
  status = 'setup_blocked'
  component = [ordered]@{
    name = 'YUBI BRACKET_GRIPPER'
    upstream = 'https://github.com/Toyota/yubi-hw'
    source_commit = 'e8334ff04945ccf56c0576a56f6fab74b63daaa2'
    geometry_file = 'BRACKET_GRIPPER.stp'
    geometry_sha256 = '4a6fba05b237b725be2ca4e5ba7f7617674b4bcae4164ff32e88d9e75275017a'
    bom_material_candidate = 'A2024'
  }
  material_evidence = [ordered]@{
    path = $materialEvidenceDestination
    sha256 = $materialEvidenceSha256
    schema = 'urn:prometheus:material-candidate-evidence:0.1.0'
  }
  material_applicability = 'unresolved'
  review = [ordered]@{
    exact_material_and_temper_reviewed = $false
    elastic_properties_reviewed = $false
    load_magnitude_and_direction_reviewed = $false
    restraint_faces_reviewed = $false
    displacement_limit_reviewed = $false
    stress_limit_reviewed = $false
    mesh_controls_reviewed = $false
    scenario_confirmed = $false
  }
  blocked_reason = 'Meshing may be evaluated, but CalculiX execution is blocked until every required setup review is complete.'
}
$manifestPath = Join-Path $trial 'prometheus-structural-candidate.json'
$json = $manifest | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine,
  [System.Text.UTF8Encoding]::new($false))

Write-Output $trial
