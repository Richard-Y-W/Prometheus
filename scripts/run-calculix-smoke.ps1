$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'out/structural-smoke'
$job = 'prometheus_tetra_smoke'

cmake --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows Release configuration failed.' }
cmake --build --preset windows-release --target prometheus_export_structural_smoke prometheus_verify_structural_smoke
if ($LASTEXITCODE -ne 0) { throw 'Structural smoke tools build failed.' }

$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
$solver = (Get-Command -Name 'ccx' -CommandType Application -ErrorAction Stop).Source
$solverHash = 'sha256:' + (Get-FileHash -LiteralPath $solver -Algorithm SHA256).Hash.ToLowerInvariant()
$versionOutput = (& $solver -v 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not obtain the CalculiX version.' }
$solverVersion = ($versionOutput -replace '[\r\n]+', ' | ').Trim()
if ([string]::IsNullOrWhiteSpace($solverVersion)) { throw 'CalculiX returned an empty version.' }

New-Item -ItemType Directory -Force $output | Out-Null
foreach ($name in @("$job.inp", "$job.dat", "$job.frd", "$job.sta", "$job.stdout.txt", "$job.stderr.txt")) {
  Remove-Item -LiteralPath (Join-Path $output $name) -Force -ErrorAction SilentlyContinue
}
& (Join-Path $repo 'out/build/windows-release/desktop/structural/prometheus_export_structural_smoke.exe') $output
if ($LASTEXITCODE -ne 0) { throw 'Structural smoke deck export failed.' }

$stdout = Join-Path $output "$job.stdout.txt"
$stderr = Join-Path $output "$job.stderr.txt"
$process = Start-Process -FilePath $solver -ArgumentList $job `
  -WorkingDirectory $output -Wait -PassThru -NoNewWindow `
  -RedirectStandardOutput $stdout -RedirectStandardError $stderr

$verifier = Join-Path $repo 'out/build/windows-release/desktop/structural/prometheus_verify_structural_smoke.exe'
& $verifier $output $process.ExitCode $solverHash $solverVersion $stdout $stderr
if ($LASTEXITCODE -ne 0) { throw 'Structural smoke result verification failed.' }

$required = @("$job.inp", "$job.dat", "$job.frd", "$job.sta", "$job.stdout.txt", "$job.stderr.txt")
foreach ($name in $required) {
  if (-not (Test-Path -LiteralPath (Join-Path $output $name))) {
    throw "CalculiX did not produce required output: $name"
  }
}
Write-Output $output
