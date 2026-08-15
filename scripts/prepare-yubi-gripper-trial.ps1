$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$externalRoot = Join-Path $repo 'out/external-demo'
$source = Join-Path $externalRoot 'yubi-hw'
$trial = Join-Path $repo 'out/trials/yubi-gripper-dynamixel'

$upstream = 'https://github.com/Toyota/yubi-hw.git'
$sourceCommit = 'e8334ff04945ccf56c0576a56f6fab74b63daaa2'
$files = @(
  [ordered]@{
    source = 'STEP/gripper/YUBI Gripper Assy_DYNAMIXEL.stp'
    destination = 'YUBI Gripper Assy_DYNAMIXEL.stp'
    sha256 = '0db1eefe396d528c9705331f0f5d71a3b67d30d86d9b4edf8fb8be8d84efaac6'
    role = 'primary_step_assembly'
  },
  [ordered]@{
    source = 'docs/BOM/YUBI Gripper_DYNAMIXEL_BOM.csv'
    destination = 'YUBI Gripper_DYNAMIXEL_BOM.csv'
    sha256 = '5251dad9b3f3c3deb26aeb0d7b887c6418c90593389f95b12df73ab694c7922b'
    role = 'unevaluated_bom'
  },
  [ordered]@{
    source = 'docs/AssemblyInstruction/YUBI Gripper_DYNAMIXEL_AssemblyGuide.pdf'
    destination = 'YUBI Gripper_DYNAMIXEL_AssemblyGuide.pdf'
    sha256 = 'db39b37599c0d5f3ced95e60d911b88b0a05cb54a30fdabfebba6286fe4c8882'
    role = 'unevaluated_assembly_guide'
  },
  [ordered]@{
    source = 'LICENSE'
    destination = 'YUBI-HARDWARE-LICENSE.txt'
    sha256 = 'c505877867b60d84e22e1c74a91306635e922483344c558e69164d237ff7d7f2'
    role = 'license_notice'
  }
)

New-Item -ItemType Directory -Force $externalRoot | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $source '.git'))) {
  git clone $upstream $source
}

$hasCommit = git -C $source cat-file -e "$sourceCommit`^{commit}" 2>$null
if ($LASTEXITCODE -ne 0) {
  git -C $source fetch origin $sourceCommit
}
git -C $source checkout --detach $sourceCommit | Out-Null
if ((git -C $source rev-parse HEAD).Trim() -ne $sourceCommit) {
  throw 'YUBI source commit validation failed.'
}

New-Item -ItemType Directory -Force $trial | Out-Null
foreach ($file in $files) {
  $sourcePath = Join-Path $source $file.source
  if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Required YUBI source file is missing: $($file.source)"
  }
  if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {
    throw "YUBI source hash validation failed: $($file.source)"
  }
  Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $trial $file.destination) -Force
}

$manifest = [ordered]@{
  schema = 'urn:prometheus:real-project-trial:0.1.0'
  status = 'screening_evidence_only'
  project = [ordered]@{
    name = 'YUBI Gripper (DYNAMIXEL)'
    upstream = 'https://github.com/Toyota/yubi-hw'
    source_commit = $sourceCommit
    license = 'CERN-OHL-W-2.0'
  }
  files = $files | ForEach-Object {
    [ordered]@{
      path = $_.destination
      sha256 = $_.sha256
      role = $_.role
    }
  }
  claim_boundary = [ordered]@{
    geometry_imported = $true
    bom_parsed = $false
    assembly_guide_parsed = $false
    material_known = $false
    loads_known = $false
    restraints_known = $false
    strength_evaluated = $false
    project_pass = $false
  }
}
$manifestPath = Join-Path $trial 'prometheus-trial-manifest.json'
$json = $manifest | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine,
  [System.Text.UTF8Encoding]::new($false))

Write-Output $trial
