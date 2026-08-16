$ErrorActionPreference = 'Stop'

function Write-JsonUtf8([string]$Path, [object]$Value) {
  $json = $Value | ConvertTo-Json -Depth 24
  [System.IO.File]::WriteAllText(
    $Path,
    $json + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))
}

function Get-PrefixedSha256([string]$Path) {
  return 'sha256:' +
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-DisplacementFindingStatus([string]$Path) {
  $result = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
  $finding = $result.findings |
    Where-Object { $_.metric -eq 'maximum_displacement' } |
    Select-Object -First 1
  if ($null -eq $finding) { return $null }
  return $finding.status
}

$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $repo 'fixtures/structural/tension-bar'
$geometry = Join-Path $fixture 'tension-bar.geo'
$expectationsPath = Join-Path $fixture 'expectations.json'
$output = Join-Path $repo 'out/validation/structural/tension-bar'
$expectations = Get-Content -LiteralPath $expectationsPath -Raw |
  ConvertFrom-Json
if ($expectations.'$schema' -ne
    'urn:prometheus:structural-validation-expectations:0.1.0') {
  throw 'Structural validation expectations schema is unsupported.'
}
$acceptance = $expectations.acceptance
$analytic = $expectations.analytic
$meshCases = $expectations.mesh_cases
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"

cmake --preset windows-structural-release
if ($LASTEXITCODE -ne 0) { throw 'Windows Release configuration failed.' }
cmake --build --preset windows-structural-release --target `
  prometheus_export_structural_case prometheus_verify_structural_case
if ($LASTEXITCODE -ne 0) { throw 'Structural validation tools build failed.' }

$gmsh = (Get-Command -Name 'gmsh' -CommandType Application `
  -ErrorAction Stop).Source
$solver = (Get-Command -Name 'ccx' -CommandType Application `
  -ErrorAction Stop).Source
$gmshHash = Get-PrefixedSha256 $gmsh
$solverHash = Get-PrefixedSha256 $solver
$gmshVersion = (& $gmsh -version 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($gmshVersion)) {
  throw 'Could not obtain the Gmsh version.'
}
$solverVersionOutput = (& $solver -v 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or
    [string]::IsNullOrWhiteSpace($solverVersionOutput)) {
  throw 'Could not obtain the CalculiX version.'
}
$solverVersion = ($solverVersionOutput -replace '[\r\n]+', ' | ').Trim()

$exporter = Join-Path $repo `
  'out/build/windows-structural-release/desktop/structural/prometheus_export_structural_case.exe'
$verifier = Join-Path $repo `
  'out/build/windows-structural-release/desktop/structural/prometheus_verify_structural_case.exe'
$exporterHash = Get-PrefixedSha256 $exporter
$verifierHash = Get-PrefixedSha256 $verifier
$job = 'prometheus_structural_case'
$maximumRefinementChange = [double]$acceptance.medium_to_fine_loaded_face_displacement_change_maximum

New-Item -ItemType Directory -Force $output | Out-Null
$pendingRefinement = [ordered]@{
  '$schema' = 'urn:prometheus:structural-refinement-evidence:0.1.0'
  complete = $false
  criteria_satisfied = $false
  evidence_sha256 = @()
  maximum_allowed_change_fraction = $maximumRefinementChange
  medium_to_fine_displacement_change_fraction = 0.0
}
$pendingRefinementPath = Join-Path $output 'refinement-pending.json'
Write-JsonUtf8 $pendingRefinementPath $pendingRefinement

$runs = [ordered]@{}
foreach ($label in @('coarse', 'medium', 'fine')) {
  $caseDirectory = Join-Path $output $label
  New-Item -ItemType Directory -Force $caseDirectory | Out-Null
  $mesh = Join-Path $caseDirectory 'tension-bar.gmsh.inp'
  $gmshLog = Join-Path $caseDirectory 'gmsh.log.txt'
  Remove-Item -LiteralPath $mesh -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $gmshLog -Force -ErrorAction SilentlyContinue
  $target = [double]$meshCases.$label.target_size_m
  $targetText = $target.ToString('R',
    [Globalization.CultureInfo]::InvariantCulture)
  $gmshStopwatch = [Diagnostics.Stopwatch]::StartNew()
  $gmshOutput = & $gmsh $geometry -3 -order 1 -format inp -o $mesh `
    -setnumber MeshTarget $targetText -v 3 2>&1 | Out-String
  $gmshExitCode = $LASTEXITCODE
  $gmshStopwatch.Stop()
  [System.IO.File]::WriteAllText(
    $gmshLog,
    $gmshOutput,
    [System.Text.UTF8Encoding]::new($false))
  if ($gmshExitCode -ne 0 -or -not (Test-Path -LiteralPath $mesh)) {
    throw "Gmsh failed for the $label tension-bar mesh."
  }

  & $exporter --tension-bar $expectationsPath $mesh $caseDirectory `
    $label known_pass
  if ($LASTEXITCODE -ne 0) {
    throw "Structural case export failed for the $label mesh."
  }
  $casePath = Join-Path $caseDirectory 'reviewed-structural-case.json'
  $reviewedCase = Get-Content -LiteralPath $casePath -Raw |
    ConvertFrom-Json
  $nodeCount = @($reviewedCase.request.mesh.nodes).Count
  $elementCount = @($reviewedCase.request.mesh.elements).Count

  $stdout = Join-Path $caseDirectory "$job.stdout.txt"
  $stderr = Join-Path $caseDirectory "$job.stderr.txt"
  foreach ($extension in @('dat', 'frd', 'sta')) {
    Remove-Item -LiteralPath (Join-Path $caseDirectory "$job.$extension") `
      -Force -ErrorAction SilentlyContinue
  }
  Remove-Item -LiteralPath $stdout -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $stderr -Force -ErrorAction SilentlyContinue
  $solverStopwatch = [Diagnostics.Stopwatch]::StartNew()
  $process = Start-Process -FilePath $solver -ArgumentList $job `
    -WorkingDirectory $caseDirectory -Wait -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $solverStopwatch.Stop()
  foreach ($extension in @('dat', 'frd', 'sta')) {
    if (-not (Test-Path -LiteralPath
        (Join-Path $caseDirectory "$job.$extension"))) {
      throw "CalculiX omitted $job.$extension for the $label mesh."
    }
  }

  $pendingResult = Join-Path $caseDirectory 'result-refinement-pending.json'
  Remove-Item -LiteralPath $pendingResult -Force -ErrorAction SilentlyContinue
  $elapsedText = $solverStopwatch.Elapsed.TotalMilliseconds.ToString(
    'R', [Globalization.CultureInfo]::InvariantCulture)
  & $verifier $caseDirectory $process.ExitCode $solverHash $solverVersion `
    $stdout $stderr $pendingRefinementPath $elapsedText $pendingResult
  $pendingVerifierExit = $LASTEXITCODE
  if ($pendingVerifierExit -ne 9 -or
      -not (Test-Path -LiteralPath $pendingResult)) {
    throw "The $label provisional verification did not fail closed on pending refinement."
  }
  $pending = Get-Content -LiteralPath $pendingResult -Raw |
    ConvertFrom-Json
  if ($null -eq $pending.metrics -or $null -eq $pending.benchmark_metrics) {
    throw "The $label solver evidence did not compile normalized metrics."
  }
  $pendingBenchmark = $pending.benchmark_metrics
  $runs[$label] = [ordered]@{
    directory = $caseDirectory
    mesh_target_m = $target
    mesh_sha256 = Get-PrefixedSha256 $mesh
    node_count = $nodeCount
    element_count = $elementCount
    gmsh_elapsed_milliseconds = $gmshStopwatch.Elapsed.TotalMilliseconds
    gmsh_log_sha256 = Get-PrefixedSha256 $gmshLog
    gmsh_parameters = [ordered]@{
      dimension = 3
      element_order = 1
      format = 'inp'
      target_size_m = $target
      verbosity = 3
    }
    solver_elapsed_milliseconds = $solverStopwatch.Elapsed.TotalMilliseconds
    solver_exit_code = $process.ExitCode
    stdout = $stdout
    stderr = $stderr
    pending_result = $pendingResult
    average_loaded_face_axial_displacement_m =
      [double]$pendingBenchmark.average_loaded_face_axial_displacement_m
    volume_weighted_central_axial_stress_pa =
      [double]$pendingBenchmark.volume_weighted_central_axial_stress_pa
  }
}

$coarseRun = $runs['coarse']
$mediumRun = $runs['medium']
$fineRun = $runs['fine']
$meshIdentities = @($runs.Values | ForEach-Object { $_['mesh_sha256'] })
if (($meshIdentities | Select-Object -Unique).Count -ne 3) {
  throw 'Coarse, medium, and fine meshes must have distinct byte identities.'
}
if (-not ($coarseRun['node_count'] -lt $mediumRun['node_count'] -and
          $mediumRun['node_count'] -lt $fineRun['node_count'] -and
          $coarseRun['element_count'] -lt $mediumRun['element_count'] -and
          $mediumRun['element_count'] -lt $fineRun['element_count'])) {
  throw 'Mesh refinement did not strictly increase node and element coverage.'
}
$mediumDisplacement =
  [double]$mediumRun['average_loaded_face_axial_displacement_m']
$fineDisplacement =
  [double]$fineRun['average_loaded_face_axial_displacement_m']
if ([Math]::Abs($fineDisplacement) -le 0.0) {
  throw 'Fine loaded-face displacement is zero; refinement is undefined.'
}
$refinementChange = [Math]::Abs($fineDisplacement - $mediumDisplacement) /
  [Math]::Abs($fineDisplacement)
$refinementPassed = $refinementChange -le $maximumRefinementChange
$completeRefinement = [ordered]@{
  '$schema' = 'urn:prometheus:structural-refinement-evidence:0.1.0'
  complete = $true
  criteria_satisfied = $refinementPassed
  evidence_sha256 = @(
    (Get-PrefixedSha256 $mediumRun['pending_result']),
    (Get-PrefixedSha256 $fineRun['pending_result'])
  )
  maximum_allowed_change_fraction = $maximumRefinementChange
  medium_to_fine_displacement_change_fraction = $refinementChange
}
$completeRefinementPath = Join-Path $output 'refinement-complete.json'
Write-JsonUtf8 $completeRefinementPath $completeRefinement

foreach ($label in @('coarse', 'medium', 'fine')) {
  $run = $runs[$label]
  $resultPath = Join-Path $run.directory 'result.json'
  Remove-Item -LiteralPath $resultPath -Force -ErrorAction SilentlyContinue
  $elapsedText = ([double]$run.solver_elapsed_milliseconds).ToString(
    'R', [Globalization.CultureInfo]::InvariantCulture)
  & $verifier $run.directory $run.solver_exit_code $solverHash `
    $solverVersion $run.stdout $run.stderr $completeRefinementPath `
    $elapsedText $resultPath
  $run['final_verifier_exit'] = $LASTEXITCODE
  $run['result'] = $resultPath
}

$knownFailDirectory = Join-Path $output 'fine-known-fail'
New-Item -ItemType Directory -Force $knownFailDirectory | Out-Null
& $exporter --tension-bar $expectationsPath `
  (Join-Path $fineRun['directory'] 'tension-bar.gmsh.inp') `
  $knownFailDirectory fine known_fail
if ($LASTEXITCODE -ne 0) { throw 'Known-fail case export failed.' }
$fineDeck = Join-Path $fineRun['directory'] "$job.inp"
$failDeck = Join-Path $knownFailDirectory "$job.inp"
if ((Get-PrefixedSha256 $fineDeck) -ne (Get-PrefixedSha256 $failDeck)) {
  throw 'Known-pass and known-fail cases changed solver deck bytes.'
}
foreach ($name in @(
    "$job.dat", "$job.frd", "$job.sta", "$job.stdout.txt",
    "$job.stderr.txt")) {
  Copy-Item -LiteralPath (Join-Path $fineRun['directory'] $name) `
    -Destination (Join-Path $knownFailDirectory $name) -Force
}
$knownFailResult = Join-Path $knownFailDirectory 'result.json'
Remove-Item -LiteralPath $knownFailResult -Force -ErrorAction SilentlyContinue
$fineElapsedText = ([double]$fineRun['solver_elapsed_milliseconds']).ToString(
  'R', [Globalization.CultureInfo]::InvariantCulture)
& $verifier $knownFailDirectory $fineRun['solver_exit_code'] $solverHash `
  $solverVersion (Join-Path $knownFailDirectory "$job.stdout.txt") `
  (Join-Path $knownFailDirectory "$job.stderr.txt") `
  $completeRefinementPath $fineElapsedText $knownFailResult
$knownFailVerifierExit = $LASTEXITCODE

$analyticDisplacement = [double]$analytic.loaded_face_axial_displacement_m
$analyticStress = [double]$analytic.volume_weighted_central_axial_stress_pa
$fineStress = [double]$fineRun['volume_weighted_central_axial_stress_pa']
$displacementError = [Math]::Abs($fineDisplacement - $analyticDisplacement) /
  [Math]::Abs($analyticDisplacement)
$stressError = [Math]::Abs($fineStress - $analyticStress) /
  [Math]::Abs($analyticStress)
$displacementTolerance =
  [double]$acceptance.fine_loaded_face_displacement_relative_error_maximum
$stressTolerance =
  [double]$acceptance.fine_volume_weighted_central_axial_stress_relative_error_maximum
$knownPassStatus = Get-DisplacementFindingStatus $fineRun['result']
$knownFailStatus = Get-DisplacementFindingStatus $knownFailResult

$gate = [ordered]@{
  three_complete_results =
    (($runs.Values | Where-Object { $_['final_verifier_exit'] -ne 0 }).Count -eq 0)
  fine_displacement_analytic = ($displacementError -le $displacementTolerance)
  fine_central_stress_analytic = ($stressError -le $stressTolerance)
  medium_to_fine_refinement = $refinementPassed
  known_pass_polarity = ($knownPassStatus -eq 'pass')
  known_fail_polarity =
    ($knownFailVerifierExit -eq 0 -and $knownFailStatus -eq 'fail')
}
$summary = [ordered]@{
  '$schema' = 'urn:prometheus:structural-validation-summary:0.1.0'
  expectations_sha256 = Get-PrefixedSha256 $expectationsPath
  geometry_sha256 = Get-PrefixedSha256 $geometry
  runner_sha256 = Get-PrefixedSha256 $PSCommandPath
  gmsh = [ordered]@{ executable_sha256 = $gmshHash; version = $gmshVersion }
  solver = [ordered]@{
    executable_sha256 = $solverHash
    version = $solverVersion
  }
  tools = [ordered]@{
    exporter_sha256 = $exporterHash
    verifier_sha256 = $verifierHash
  }
  comparisons = [ordered]@{
    fine_loaded_face_displacement_m = $fineDisplacement
    analytic_loaded_face_displacement_m = $analyticDisplacement
    fine_displacement_relative_error = $displacementError
    fine_displacement_relative_error_maximum = $displacementTolerance
    fine_central_axial_stress_pa = $fineStress
    analytic_central_axial_stress_pa = $analyticStress
    fine_central_stress_relative_error = $stressError
    fine_central_stress_relative_error_maximum = $stressTolerance
    medium_to_fine_displacement_change = $refinementChange
    medium_to_fine_displacement_change_maximum = $maximumRefinementChange
  }
  finding_polarity = [ordered]@{
    known_pass = $knownPassStatus
    known_fail = $knownFailStatus
  }
  runs = $runs
  gate = $gate
}
$summaryPath = Join-Path $output 'validation-summary.json'
Write-JsonUtf8 $summaryPath $summary

if (($gate.Values | Where-Object { -not $_ }).Count -ne 0) {
  throw "Structural validation gate failed; inspect $summaryPath"
}
Write-Output $summaryPath
