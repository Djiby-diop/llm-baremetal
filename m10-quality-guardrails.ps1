param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [ValidateRange(0, 100)]
  [int]$MaxHarmfulRatioPct = 40,
  [ValidateRange(1, 100)]
  [int]$MaxConsecutiveFailures = 2,
  [ValidateRange(1, 1000)]
  [int]$MinAutoApplySamples = 2,
  [ValidateRange(0, 65536)]
  [int]$RuntimeMemMB = 0,
  [switch]$NoAdaptiveThresholds,
  [switch]$ApplyQuarantine,
  [string]$ConfigPath = "repl.cfg",
  [string]$QuarantineStatePath = "artifacts/m10/quarantine-state.json",
  [string]$HistoryPath = "artifacts/m10/history.jsonl",
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M10] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M10][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M10][WARN] $msg" -ForegroundColor Yellow }

function Get-ModelProfile([string]$logText) {
  $dim = 0
  $layers = 0

  $mm = [regex]::Match($logText, 'OK:\s+Model loaded:.*?\(dim=(\d+),\s*layers=(\d+)')
  if ($mm.Success) {
    $dim = [int]$mm.Groups[1].Value
    $layers = [int]$mm.Groups[2].Value
  } else {
    $gm = [regex]::Match($logText, 'GGUF detected:\s+ctx=\d+\s+dim=(\d+)\s+layers=(\d+)')
    if ($gm.Success) {
      $dim = [int]$gm.Groups[1].Value
      $layers = [int]$gm.Groups[2].Value
    }
  }

  $klass = 'unknown'
  if ($dim -gt 0 -and $layers -gt 0) {
    if ($dim -le 320 -and $layers -le 8) {
      $klass = 'tiny'
    } elseif ($dim -le 1024 -and $layers -le 20) {
      $klass = 'medium'
    } else {
      $klass = 'large'
    }
  }

  return [ordered]@{
    dim = $dim
    layers = $layers
    model_class = $klass
  }
}

function Get-RamTier([int]$runtimeMemMB, [string]$logText) {
  $ramMB = $runtimeMemMB
  if ($ramMB -le 0) {
    $rm = [regex]::Match($logText, '\[llmk\]\s+Zone B:.*size=(\d+)\s+MiB')
    if ($rm.Success) {
      $ramMB = [int]$rm.Groups[1].Value
    }
  }

  $tier = 'unknown'
  if ($ramMB -gt 0) {
    if ($ramMB -lt 2048) {
      $tier = 'low'
    } elseif ($ramMB -lt 4096) {
      $tier = 'mid'
    } else {
      $tier = 'high'
    }
  }

  return [ordered]@{
    ram_mb = $ramMB
    ram_tier = $tier
  }
}

function Get-AdaptiveThresholds([int]$baseHarmful,
                                [int]$baseConsecutive,
                                [int]$baseSamples,
                                [string]$modelClass,
                                [string]$ramTier) {
  $harmful = $baseHarmful
  $consecutive = $baseConsecutive
  $samples = $baseSamples

  if ($modelClass -eq 'tiny' -and $ramTier -eq 'high') {
    $harmful = [Math]::Max(15, $harmful - 10)
    $consecutive = [Math]::Max(1, $consecutive - 1)
    $samples = [Math]::Max($samples, 3)
  } elseif ($modelClass -eq 'large' -or $ramTier -eq 'low') {
    $harmful = [Math]::Min(90, $harmful + 15)
    $consecutive = [Math]::Min(10, $consecutive + 1)
    $samples = [Math]::Max(1, $samples - 1)
  } elseif ($modelClass -eq 'unknown' -or $ramTier -eq 'unknown') {
    $harmful = [Math]::Min(90, $harmful + 5)
  }

  return [ordered]@{
    harmful = $harmful
    consecutive = $consecutive
    samples = $samples
  }
}

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

$profile = Get-ModelProfile -logText $log
$ramInfo = Get-RamTier -runtimeMemMB $RuntimeMemMB -logText $log

$effectiveMaxHarmfulRatioPct = $MaxHarmfulRatioPct
$effectiveMaxConsecutiveFailures = $MaxConsecutiveFailures
$effectiveMinAutoApplySamples = $MinAutoApplySamples

if (-not $NoAdaptiveThresholds) {
  $adaptive = Get-AdaptiveThresholds -baseHarmful $MaxHarmfulRatioPct `
                                    -baseConsecutive $MaxConsecutiveFailures `
                                    -baseSamples $MinAutoApplySamples `
                                    -modelClass $profile.model_class `
                                    -ramTier $ramInfo.ram_tier
  $effectiveMaxHarmfulRatioPct = [int]$adaptive.harmful
  $effectiveMaxConsecutiveFailures = [int]$adaptive.consecutive
  $effectiveMinAutoApplySamples = [int]$adaptive.samples
  Write-Step "Adaptive thresholds: class=$($profile.model_class) dim=$($profile.dim) layers=$($profile.layers) ram=$($ramInfo.ram_mb)MB tier=$($ramInfo.ram_tier)"
  Write-Step "Effective thresholds: harmful<=$effectiveMaxHarmfulRatioPct% consecutive<=$effectiveMaxConsecutiveFailures min_samples>=$effectiveMinAutoApplySamples"
} else {
  Write-Step 'Adaptive thresholds disabled'
}

# Parse action outcomes from policy markers.
$events = New-Object System.Collections.Generic.List[string]
$eventTokens = [regex]::Matches($log, 'OK:\s+OO auto-apply:|ERROR:\s+OO auto-apply verification failed:')
foreach ($m in $eventTokens) {
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
if ($totalSamples -lt $effectiveMinAutoApplySamples) {
  Write-Warn "Insufficient auto-apply samples for strict quality gate (need >=$effectiveMinAutoApplySamples, got $totalSamples)"
} else {
  if ($harmfulRatioPct -gt [double]$effectiveMaxHarmfulRatioPct) {
    Write-Warn "Harmful ratio breached: $([math]::Round($harmfulRatioPct,1))% > $effectiveMaxHarmfulRatioPct%"
    $qualityOk = $false
  } else {
    Write-Ok "Harmful ratio within limit: $([math]::Round($harmfulRatioPct,1))% <= $effectiveMaxHarmfulRatioPct%"
  }

  if ($maxConsecutiveFailedSeen -gt $effectiveMaxConsecutiveFailures) {
    Write-Warn "Consecutive failures breached: $maxConsecutiveFailedSeen > $effectiveMaxConsecutiveFailures"
    $qualityOk = $false
  } else {
    Write-Ok "Consecutive failures within limit: $maxConsecutiveFailedSeen <= $effectiveMaxConsecutiveFailures"
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
  effective_max_harmful_ratio_pct = $effectiveMaxHarmfulRatioPct
  effective_max_consecutive_failures = $effectiveMaxConsecutiveFailures
  effective_min_samples = $effectiveMinAutoApplySamples
  adaptive_thresholds_enabled = (-not $NoAdaptiveThresholds)
  model_class = $profile.model_class
  model_dim = $profile.dim
  model_layers = $profile.layers
  ram_mb = $ramInfo.ram_mb
  ram_tier = $ramInfo.ram_tier
  quality_ok = $qualityOk
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

if (-not $NoHistoryWrite) {
  $historyDir = Split-Path -Parent $HistoryPath
  if ($historyDir -and -not (Test-Path -LiteralPath $historyDir)) {
    New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
  }

  $entry = [ordered]@{
    ts_utc = (Get-Date).ToUniversalTime().ToString('o')
    log_path = $LogPath
    samples = $totalSamples
    success = $successCount
    failed = $failedCount
    harmful_ratio_pct = [math]::Round($harmfulRatioPct, 2)
    max_failed_streak = $maxConsecutiveFailedSeen
    effective_max_harmful_ratio_pct = $effectiveMaxHarmfulRatioPct
    effective_max_consecutive_failures = $effectiveMaxConsecutiveFailures
    effective_min_samples = $effectiveMinAutoApplySamples
    model_class = $profile.model_class
    model_dim = $profile.dim
    model_layers = $profile.layers
    ram_mb = $ramInfo.ram_mb
    ram_tier = $ramInfo.ram_tier
    quality_ok = $qualityOk
    quarantine_needed = $quarantineNeeded
  }

  ($entry | ConvertTo-Json -Compress) | Add-Content -LiteralPath $HistoryPath
  Write-Ok "History appended: $HistoryPath"
}

if (-not $qualityOk) {
  throw "M10 quality guardrails failed"
}

Write-Ok 'M10 quality guardrails passed'
