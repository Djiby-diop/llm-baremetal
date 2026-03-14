[CmdletBinding(PositionalBinding = $false)]
param(
  [ValidateSet('auto','whpx','tcg','none')]
  [string]$Accel = 'tcg',

  [ValidateRange(512, 8192)]
  [int]$MemMB = 1024,

  [ValidateRange(30, 86400)]
  [int]$TimeoutSec = 480,

  [switch]$SkipPrebuild,
  [switch]$SkipExport,

  [string]$ExportPath,

  [string]$OoHostRoot
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$workspaceRoot = Split-Path -Parent $root
$build = Join-Path $root 'build.ps1'
$run = Join-Path $root 'run.ps1'
$autorunSource = Join-Path $root 'llmk-autorun-handoff-smoke.txt'
$autorunTarget = Join-Path $root 'llmk-autorun.txt'
$replCfg = Join-Path $root 'repl.cfg'
$handoffTarget = Join-Path $root 'sovereign_export.json'

if (-not $PSBoundParameters.ContainsKey('OoHostRoot')) {
  if ($env:OO_HOST_ROOT) {
    $OoHostRoot = $env:OO_HOST_ROOT
  } else {
    $OoHostRoot = Join-Path $workspaceRoot 'oo-host'
  }
}

$ooHostRoot = [System.IO.Path]::GetFullPath($OoHostRoot)
$dataDir = Join-Path $ooHostRoot 'data'

if (-not (Test-Path -LiteralPath $build)) { throw "Missing: $build" }
if (-not (Test-Path -LiteralPath $run)) { throw "Missing: $run" }
if (-not (Test-Path -LiteralPath $autorunSource)) { throw "Missing autorun script: $autorunSource" }
if (-not (Test-Path -LiteralPath $ooHostRoot)) { throw "Missing oo-host workspace: $ooHostRoot" }

if (-not $PSBoundParameters.ContainsKey('ExportPath')) {
  $ExportPath = Join-Path $dataDir 'sovereign_export.json'
}

function Backup-File([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return $null }
  $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
  Copy-Item -LiteralPath $path -Destination $tmp -Force
  return $tmp
}

function Restore-File([string]$path, [string]$backup) {
  if ($backup -and (Test-Path -LiteralPath $backup)) {
    Copy-Item -LiteralPath $backup -Destination $path -Force
    Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
  } elseif (Test-Path -LiteralPath $path) {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
  }
}

function Assert-Match([string]$text, [string]$pattern, [string]$message) {
  if ($text -notmatch $pattern) {
    throw $message
  }
}

function Invoke-BuildWithRetry() {
  $buildArgs = @(
    '-NoProfile',
    '-ExecutionPolicy','Bypass',
    '-File', $build,
    '-NoModel'
  )
  if ($SkipPrebuild) { $buildArgs += '-SkipPrebuild' }

  for ($attempt = 1; $attempt -le 3; $attempt++) {
    & powershell @buildArgs
    if ($LASTEXITCODE -eq 0) {
      return
    }
    Write-Host "[Handoff] WARN: build attempt $attempt failed (exit=$LASTEXITCODE)" -ForegroundColor Yellow
    & wsl --shutdown | Out-Null
    Start-Sleep -Seconds 2
  }

  throw "build.ps1 failed after retries"
}

function Write-HandoffExport() {
  if ($SkipExport) { return }

  $ooHostExe = Join-Path $ooHostRoot 'target\debug\oo-host.exe'
  if (Test-Path -LiteralPath $ooHostExe) {
    & $ooHostExe --data-dir $dataDir export sovereign --out $ExportPath
    if ($LASTEXITCODE -ne 0) { throw "oo-host export failed ($LASTEXITCODE)" }
    return
  }

  $cargo = Get-Command cargo -ErrorAction SilentlyContinue
  if (-not $cargo) {
    throw "cargo not found and oo-host.exe missing; cannot generate sovereign export"
  }

  Push-Location -LiteralPath $ooHostRoot
  try {
    & cargo run --quiet -- --data-dir $dataDir export sovereign --out $ExportPath
    if ($LASTEXITCODE -ne 0) { throw "cargo run export failed ($LASTEXITCODE)" }
  }
  finally {
    Pop-Location
  }
}

Write-HandoffExport

if (-not (Test-Path -LiteralPath $ExportPath)) {
  throw "Missing sovereign export: $ExportPath"
}

$export = Get-Content -LiteralPath $ExportPath -Raw | ConvertFrom-Json

$replBak = Backup-File $replCfg
$autorunBak = Backup-File $autorunTarget
$handoffBak = Backup-File $handoffTarget

$serialPath = Join-Path ([System.IO.Path]::GetTempPath()) ("llm-baremetal-serial-handoff-{0:yyyyMMdd-HHmmss}.txt" -f (Get-Date))
$serialErrPath = "$serialPath.err"

try {
  $cfg = @(
    'autorun_autostart=1',
    'autorun_shutdown_when_done=1',
    'autorun_file=llmk-autorun.txt',
    'oo_enable=1'
  ) -join "`n"
  Set-Content -LiteralPath $replCfg -Value $cfg -Encoding ASCII -NoNewline
  Copy-Item -LiteralPath $autorunSource -Destination $autorunTarget -Force
  Copy-Item -LiteralPath $ExportPath -Destination $handoffTarget -Force

  Write-Host "[Handoff] Build (NoModel)" -ForegroundColor Cyan
  Invoke-BuildWithRetry

  Write-Host "[Handoff] Run QEMU (accel=$Accel mem=${MemMB}MB)" -ForegroundColor Cyan
  $psArgs = @(
    '-NoProfile',
    '-ExecutionPolicy','Bypass',
    '-File', $run,
    '-Accel', $Accel,
    '-MemMB', $MemMB
  )

  if (Test-Path -LiteralPath $serialPath) { Remove-Item -LiteralPath $serialPath -Force -ErrorAction SilentlyContinue }
  if (Test-Path -LiteralPath $serialErrPath) { Remove-Item -LiteralPath $serialErrPath -Force -ErrorAction SilentlyContinue }

  $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList $psArgs -WorkingDirectory $root -NoNewWindow -PassThru -RedirectStandardOutput $serialPath -RedirectStandardError $serialErrPath
  $ok = $proc.WaitForExit($TimeoutSec * 1000)
  if (-not $ok) {
    try {
      $kids = Get-CimInstance Win32_Process -Filter ("ParentProcessId={0}" -f $proc.Id) -ErrorAction SilentlyContinue
      foreach ($k in $kids) {
        try { Stop-Process -Id $k.ProcessId -Force -ErrorAction SilentlyContinue } catch {}
      }
      Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    } catch {}
    throw "Timeout after ${TimeoutSec}s. Serial: $serialPath"
  }

  if (Test-Path -LiteralPath $serialErrPath) {
    Add-Content -LiteralPath $serialPath -Value "`n--- STDERR ---`n" -Encoding ASCII
    Get-Content -LiteralPath $serialErrPath -ErrorAction SilentlyContinue | Add-Content -LiteralPath $serialPath -Encoding ASCII
  }

  $serial = if (Test-Path -LiteralPath $serialPath) { Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue } else { '' }

  if ($proc.ExitCode -ne 0) {
    Write-Host "[Handoff] QEMU wrapper exit=$($proc.ExitCode)" -ForegroundColor Yellow
  }

  Assert-Match $serial 'OK: REPL ready \(no model\)' "Missing no-model REPL marker. Serial: $serialPath"
  Assert-Match $serial '(?m)^\[oo_handoff\] file=sovereign_export\.json\s*$' "Missing handoff file marker. Serial: $serialPath"
  Assert-Match $serial '(?m)^\[oo_handoff\] schema_version=1\s*$' "Missing schema_version output. Serial: $serialPath"
  Assert-Match $serial '(?m)^\[oo_handoff\] export_kind=oo_sovereign_handoff\s*$' "Missing export_kind output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff] organism_id=$([string]$export.organism_id)")) "Missing organism_id output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff] continuity_epoch=$([uint64]$export.continuity_epoch)")) "Missing continuity_epoch output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff] mode=$([string]$export.mode)")) "Missing mode output. Serial: $serialPath"
  $expectedRecovery = if ($null -ne $export.last_recovery_reason -and [string]$export.last_recovery_reason -ne '') { [string]$export.last_recovery_reason } else { 'none' }
  Assert-Match $serial ([regex]::Escape("[oo_handoff] last_recovery_reason=$expectedRecovery")) "Missing last_recovery_reason output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff] policy.enforcement=$([string]$export.policy.enforcement)")) "Missing policy enforcement output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff] active_goal_count=$([int]$export.active_goal_count)")) "Missing active_goal_count output. Serial: $serialPath"
  Assert-Match $serial '(?m)^\[oo_handoff\] recent_events:\s*$' "Missing recent_events header. Serial: $serialPath"
  Assert-Match $serial '(?m)^  - .+$' "Missing recent event lines. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_handoff_apply] host.mode=normal')) "Missing handoff apply host mode output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff_apply] host.continuity_epoch=$([uint64]$export.continuity_epoch)")) "Missing handoff apply continuity output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff_apply] host.last_recovery_reason=$expectedRecovery")) "Missing handoff apply recovery output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_handoff_apply] mode_result=kept_local_safer safe')) "Missing handoff apply safe-mode result. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_handoff_apply] local.policy.before=off')) "Missing local policy baseline output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_handoff_apply] policy_result=applied observe')) "Missing handoff apply policy result. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_handoff_apply] continuity_result=recorded')) "Missing handoff apply continuity receipt result. Serial: $serialPath"
  Assert-Match $serial '(?m)^\[oo_handoff_receipt\] file=OOHANDOFF\.TXT\s*$' "Missing handoff receipt file output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff_receipt] organism_id=$([string]$export.organism_id)")) "Missing handoff receipt organism_id output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff_receipt] mode=$([string]$export.mode)")) "Missing handoff receipt mode output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff_receipt] policy_enforcement=$([string]$export.policy.enforcement)")) "Missing handoff receipt enforcement output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff_receipt] continuity_epoch=$([uint64]$export.continuity_epoch)")) "Missing handoff receipt continuity output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_handoff_receipt] last_recovery_reason=$expectedRecovery")) "Missing handoff receipt recovery output. Serial: $serialPath"
  Assert-Match $serial '(?m)^\[oo_continuity\] receipt\.present=1\s*$' "Missing continuity receipt presence output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape("[oo_continuity] receipt.mode=$([string]$export.mode)")) "Missing continuity receipt mode output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_continuity] local.mode=safe')) "Missing continuity local mode output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_continuity] recovery.mode=safe')) "Missing continuity recovery mode output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_continuity] summary=aligned')) "Missing continuity summary output. Serial: $serialPath"
  Assert-Match $serial ([regex]::Escape('[oo_continuity] reason=local_safer_than_receipt')) "Missing continuity reason output. Serial: $serialPath"
  Assert-Match $serial '(?m)^\[autorun\] done\s*$' "Missing autorun completion marker. Serial: $serialPath"

  Write-Host "[Handoff] PASS" -ForegroundColor Green
}
finally {
  Restore-File -path $replCfg -backup $replBak
  Restore-File -path $autorunTarget -backup $autorunBak
  Restore-File -path $handoffTarget -backup $handoffBak
}
