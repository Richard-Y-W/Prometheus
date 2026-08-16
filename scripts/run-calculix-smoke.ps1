$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'out/structural-smoke'
$job = 'prometheus_axial_smoke'

cmake --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows Release configuration failed.' }
cmake --build --preset windows-release --target prometheus_run_calculix_job
if ($LASTEXITCODE -ne 0) { throw 'Structural smoke runner build failed.' }

$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
New-Item -ItemType Directory -Force $output | Out-Null
$ccx = (Get-Command ccx.exe -ErrorAction Stop).Source
foreach ($extension in 'inp','dat','frd','sta','cvg','12d','eig','fin','hrn','mas','msh','nam','rout','stm') {
  Remove-Item -LiteralPath (Join-Path $output "$job.$extension") -Force -ErrorAction SilentlyContinue
}
$runnerOutput = & (Join-Path $repo 'out/build/windows-release/desktop/structural/prometheus_run_calculix_job.exe') `
  --axial-smoke $ccx $output $job 120 2>&1 | Out-String
$runnerExitCode = $LASTEXITCODE
Write-Output $runnerOutput.TrimEnd()
if ($runnerExitCode -ne 0) {
  throw "CalculiX runner failed with exit code $runnerExitCode."
}
if ($runnerOutput -notmatch 'status=completed evidence=validated') {
  throw 'CalculiX runner did not report completed validated evidence.'
}

$required = @("$job.inp", "$job.dat", "$job.frd", "$job.sta")
foreach ($name in $required) {
  if (-not (Test-Path -LiteralPath (Join-Path $output $name))) {
    throw "CalculiX did not produce required output: $name"
  }
}
Write-Output $output
