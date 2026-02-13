param(
  [switch]$NoModel,
  [string]$ModelBin = 'stories110M.bin',
  [switch]$Gui,
  [switch]$SkipRun,
  [ValidateSet('auto','whpx','tcg','none')]
  [string]$Accel = 'auto',
  [ValidateRange(512, 8192)]
  [int]$MemMB = 4096
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

Write-Host "[M6.1] Verify release flow" -ForegroundColor Cyan
Write-Host "  Step 1/3: preflight" -ForegroundColor Gray
& (Join-Path $PSScriptRoot 'preflight-host.ps1')
if ($LASTEXITCODE -ne 0) {
  throw "Preflight failed with exit code $LASTEXITCODE"
}

Write-Host "  Step 2/3: build image" -ForegroundColor Gray
if ($NoModel) {
  & (Join-Path $PSScriptRoot 'build.ps1') -NoModel
} else {
  & (Join-Path $PSScriptRoot 'build.ps1') -ModelBin $ModelBin
}
if ($LASTEXITCODE -ne 0) {
  throw "Build failed with exit code $LASTEXITCODE"
}

if ($SkipRun) {
  Write-Host "  Step 3/3: run skipped (-SkipRun)" -ForegroundColor Yellow
  Write-Host "[M6.1] READY: preflight + build passed." -ForegroundColor Green
  exit 0
}

Write-Host "  Step 3/3: launch QEMU" -ForegroundColor Gray
$runArgs = @('-Preflight', '-Accel', $Accel, '-MemMB', $MemMB)
if ($Gui) { $runArgs += '-Gui' }
& (Join-Path $PSScriptRoot 'run.ps1') @runArgs
exit $LASTEXITCODE
