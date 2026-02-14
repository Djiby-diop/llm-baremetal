param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [ValidateRange(0, 100)]
  [int]$MaxHarmfulRatioPct = 40,
  [ValidateRange(1, 100)]
  [int]$MaxConsecutiveFailures = 2,
  [ValidateRange(1, 1000)]
  [int]$MinAutoApplySamples = 2,
  [switch]$ApplyQuarantine,
  [string]$ConfigPath = "repl.cfg",
  [string]$QuarantineStatePath = "artifacts/m10/quarantine-state.json"
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M10] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M10][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M10][WARN] $msg" -ForegroundColor Yellow }

function Set-Or-Append-CfgKey([string]$path, [string]$key, [string]$value) {
  $lines = @()
  if (Test-Path -LiteralPath $path) {
    $lines = Get-Content -LiteralPath $path
  }

  $pattern = '^[\s#;]*' + [regex]::Escape($key) + '\s*='
  $updated = $false
  $out = New-Object System.Collections.Generic.List[string]
  foreach ($line in $lines) {
    if (-not $updated -and $line -match $pattern) {
      $out.Add("$key=$value")
      $updated = $true
    } else {
      $out.Add($line)
    }
  }
  if (-not $updated) {
    $out.Add("$key=$value")
  }

  Set-Content -LiteralPath $path -Value ($out -join "`r`n") -Encoding ASCII
}

if (-not (Test-Path -LiteralPath $LogPath)) {
  throw "M10 log not found: $LogPath"
}

$log = Get-Content -LiteralPath $LogPath -Raw
Write-Step "Evaluating quality guardrails from $LogPath"

# Parse action outcomes from policy markers.
$events = New-Object System.Collections.Generic.List[string]
$matches = [regex]::Matches($log, 'OK:\s+OO auto-apply:|ERROR:\s+OO auto-apply verification failed:')
foreach ($m in $matches) {
  $token = $m.Value
  if ($token -like 'OK:*') {
    $events.Add('success')
  } else {
    $events.Add('failed')
  }
}

$successCount = 0
$failedCount = 0
$maxConsecutiveFailedSeen = 0
$currentConsecutiveFailed = 0

foreach ($e in $events) {
  if ($e -eq 'success') {
    $successCount++
    $currentConsecutiveFailed = 0
  } else {
    $failedCount++
    $currentConsecutiveFailed++
    if ($currentConsecutiveFailed -gt $maxConsecutiveFailedSeen) {
      $maxConsecutiveFailedSeen = $currentConsecutiveFailed
    }
  }
}

$totalSamples = $successCount + $failedCount
$harmfulRatioPct = 0.0
if ($totalSamples -gt 0) {
  $harmfulRatioPct = ([double]$failedCount * 100.0) / [double]$totalSamples
}

Write-Ok "samples=$totalSamples success=$successCount failed=$failedCount harmful_ratio=$([math]::Round($harmfulRatioPct,1))% max_failed_streak=$maxConsecutiveFailedSeen"

$qualityOk = $true
if ($totalSamples -lt $MinAutoApplySamples) {
  Write-Warn "Insufficient auto-apply samples for strict quality gate (need >=$MinAutoApplySamples, got $totalSamples)"
} else {
  if ($harmfulRatioPct -gt [double]$MaxHarmfulRatioPct) {
    Write-Warn "Harmful ratio breached: $([math]::Round($harmfulRatioPct,1))% > $MaxHarmfulRatioPct%"
    $qualityOk = $false
  } else {
    Write-Ok "Harmful ratio within limit: $([math]::Round($harmfulRatioPct,1))% <= $MaxHarmfulRatioPct%"
  }

  if ($maxConsecutiveFailedSeen -gt $MaxConsecutiveFailures) {
    Write-Warn "Consecutive failures breached: $maxConsecutiveFailedSeen > $MaxConsecutiveFailures"
    $qualityOk = $false
  } else {
    Write-Ok "Consecutive failures within limit: $maxConsecutiveFailedSeen <= $MaxConsecutiveFailures"
  }
}

$quarantineNeeded = (-not $qualityOk)

$stateDir = Split-Path -Parent $QuarantineStatePath
if ($stateDir -and -not (Test-Path -LiteralPath $stateDir)) {
  New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
}

$state = [ordered]@{
  ts_utc = (Get-Date).ToUniversalTime().ToString('o')
  log_path = $LogPath
  samples = $totalSamples
  success = $successCount
  failed = $failedCount
  harmful_ratio_pct = [math]::Round($harmfulRatioPct, 2)
  max_failed_streak = $maxConsecutiveFailedSeen
  max_harmful_ratio_pct = $MaxHarmfulRatioPct
  max_consecutive_failures = $MaxConsecutiveFailures
  min_samples = $MinAutoApplySamples
  quarantine_needed = $quarantineNeeded
  quarantine_applied = $false
  config_path = $ConfigPath
}

if ($quarantineNeeded -and $ApplyQuarantine) {
  if (-not (Test-Path -LiteralPath $ConfigPath)) {
    Write-Warn "Quarantine requested but config not found: $ConfigPath"
  } else {
    Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_auto_apply' -value '0'
    Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_conf_gate' -value '1'
    $state.quarantine_applied = $true
    Write-Ok "Auto-quarantine applied to $ConfigPath (oo_auto_apply=0, oo_conf_gate=1)"
  }
} elseif ($quarantineNeeded) {
  Write-Warn 'Quarantine needed but not applied (use -ApplyQuarantine)'
}

$state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $QuarantineStatePath -Encoding UTF8
Write-Ok "Quarantine state written: $QuarantineStatePath"

if (-not $qualityOk) {
  throw "M10 quality guardrails failed"
}

Write-Ok 'M10 quality guardrails passed'
