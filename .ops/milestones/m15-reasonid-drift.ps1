param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [string]$HistoryPath = "artifacts/m13/history.jsonl",
  [string]$StatePath = "artifacts/m15/drift-state.json",
  [string]$DriftHistoryPath = "artifacts/m15/history.jsonl",
  [ValidateRange(1, 50)]
  [int]$BaselineWindow = 5,
  [ValidateRange(1, 50)]
  [int]$MinBaselineRunsForDrift = 3,
  [ValidateRange(1, 100)]
  [int]$MinSamplesForDrift = 5,
  [ValidateRange(1, 200)]
  [int]$MaxShareDriftPct = 80,
  [ValidateRange(1, 100)]
  [int]$MaxUnknownSharePct = 30,
  [switch]$FailOnDrift,
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M15] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M15][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M15][WARN] $msg" -ForegroundColor Yellow }

function Read-JsonLines([string]$path) {
  $items = @()
  if (-not (Test-Path -LiteralPath $path)) { return $items }

  $lines = Get-Content -LiteralPath $path -ErrorAction SilentlyContinue
  foreach ($line in $lines) {
    if (-not $line) { continue }
    try {
      $obj = $line | ConvertFrom-Json -ErrorAction Stop
      if ($null -ne $obj) { $items += $obj }
    } catch {
    }
  }
  return $items
}

function Get-ReasonIdsFromText([string]$text) {
  $ids = New-Object System.Collections.Generic.List[string]
  foreach ($line in ($text -split "`r?`n")) {
    if ($line -match 'reason_id=([A-Za-z0-9_\-]+)') {
      $id = $Matches[1]
      if ($id) { $ids.Add($id) }
    }
  }
  return @($ids)
}

function Get-Distribution([string[]]$ids) {
  $map = @{}
  $total = 0
  foreach ($id in $ids) {
    if (-not $id) { continue }
    $total++
    if ($map.ContainsKey($id)) {
      $map[$id] = [int]$map[$id] + 1
    } else {
      $map[$id] = 1
    }
  }

  $shares = @{}
  if ($total -gt 0) {
    foreach ($k in $map.Keys) {
      $shares[$k] = [math]::Round((([double]$map[$k] * 100.0) / [double]$total), 2)
    }
  }

  return [ordered]@{
    total = $total
    counts = $map
    shares = $shares
  }
}

function Merge-BaselineIds([object[]]$entries) {
  $ids = New-Object System.Collections.Generic.List[string]
  foreach ($entry in $entries) {
    if ($null -eq $entry) { continue }
    $eventsProp = $entry.PSObject.Properties['decision_events']
    if ($null -eq $eventsProp -or $null -eq $eventsProp.Value) { continue }

    foreach ($ev in $eventsProp.Value) {
      if ($null -eq $ev) { continue }
      $nativeProp = $ev.PSObject.Properties['reason_code_native']
      if ($null -ne $nativeProp -and $nativeProp.Value) {
        $ids.Add([string]$nativeProp.Value)
        continue
      }

      $reasonProp = $ev.PSObject.Properties['reason_code']
      if ($null -ne $reasonProp -and $reasonProp.Value) {
        $ids.Add([string]$reasonProp.Value)
      }
    }
  }
  return @($ids)
}

if (-not (Test-Path -LiteralPath $LogPath)) {
  throw "M15 log not found: $LogPath"
}

$logText = Get-Content -LiteralPath $LogPath -Raw
$currentIds = Get-ReasonIdsFromText -text $logText
$currentDist = Get-Distribution -ids $currentIds

$history = Read-JsonLines -path $HistoryPath
$baselineEntries = @($history | Select-Object -Last $BaselineWindow)
$baselineIds = Merge-BaselineIds -entries $baselineEntries
$baselineDist = Get-Distribution -ids $baselineIds

Write-Step "current_reason_ids=$($currentDist.total) baseline_reason_ids=$($baselineDist.total) baseline_runs=$($baselineEntries.Count)"

$anomalies = New-Object System.Collections.Generic.List[string]
$driftBreaches = New-Object System.Collections.Generic.List[string]
$ok = $true

$baselineReady = ($baselineEntries.Count -ge $MinBaselineRunsForDrift) -and ($baselineDist.total -ge $MinSamplesForDrift)
$learningMode = -not $baselineReady

if ($learningMode) {
  Write-Warn "Baseline not ready for strict drift gate (need >=$MinBaselineRunsForDrift runs and >=$MinSamplesForDrift baseline samples; got runs=$($baselineEntries.Count) samples=$($baselineDist.total)). Skipping drift evaluation."
}

if ($learningMode) {
  # Intentionally skip drift detection until baseline is meaningful.
} elseif ($currentDist.total -lt $MinSamplesForDrift) {
  Write-Warn "Insufficient reason_id samples for strict drift gate (need >=$MinSamplesForDrift, got $($currentDist.total))"
} else {
  $allKeys = New-Object System.Collections.Generic.HashSet[string]
  foreach ($k in $currentDist.counts.Keys) { [void]$allKeys.Add([string]$k) }
  foreach ($k in $baselineDist.counts.Keys) { [void]$allKeys.Add([string]$k) }

  foreach ($k in $allKeys) {
    $curShare = 0.0
    $baseShare = 0.0

    if ($currentDist.shares.ContainsKey($k)) { $curShare = [double]$currentDist.shares[$k] }
    if ($baselineDist.shares.ContainsKey($k)) { $baseShare = [double]$baselineDist.shares[$k] }

    if ($baseShare -le 0.0 -and $curShare -gt 0.0) {
      $anomalies.Add("new_reason_id:$k share=$([math]::Round($curShare,2))%")
      continue
    }

    if ($baseShare -gt 0.0) {
      $driftPct = [math]::Abs((($curShare - $baseShare) / $baseShare) * 100.0)
      if ($driftPct -gt [double]$MaxShareDriftPct) {
        $driftBreaches.Add("reason_id:$k drift=$([math]::Round($driftPct,1))% cur=$([math]::Round($curShare,2))% base=$([math]::Round($baseShare,2))%")
      }
    }
  }

  $unknownShare = 0.0
  if ($currentDist.shares.ContainsKey('AUTO_APPLY_UNKNOWN')) {
    $unknownShare = [double]$currentDist.shares['AUTO_APPLY_UNKNOWN']
  }
  if ($unknownShare -gt [double]$MaxUnknownSharePct) {
    $anomalies.Add("unknown_reason_share=$([math]::Round($unknownShare,2))%")
  }

  if ($driftBreaches.Count -gt 0 -or $anomalies.Count -gt 0) {
    $ok = $false
  }
}

$stateDir = Split-Path -Parent $StatePath
if ($stateDir -and -not (Test-Path -LiteralPath $stateDir)) {
  New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
}

$historyDir = Split-Path -Parent $DriftHistoryPath
if ($historyDir -and -not (Test-Path -LiteralPath $historyDir)) {
  New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
}

$state = [ordered]@{
  ts_utc = (Get-Date).ToUniversalTime().ToString('o')
  log_path = $LogPath
  baseline_history_path = $HistoryPath
  baseline_window = $BaselineWindow
  min_baseline_runs_for_drift = $MinBaselineRunsForDrift
  baseline_runs = $baselineEntries.Count
  current_total_reason_ids = $currentDist.total
  baseline_total_reason_ids = $baselineDist.total
  current_shares = $currentDist.shares
  baseline_shares = $baselineDist.shares
  baseline_ready = $baselineReady
  learning_mode = $learningMode
  drift_breaches = @($driftBreaches)
  anomalies = @($anomalies)
  max_share_drift_pct = $MaxShareDriftPct
  max_unknown_share_pct = $MaxUnknownSharePct
  min_samples_for_drift = $MinSamplesForDrift
  drift_ok = $ok
}

$state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $StatePath -Encoding UTF8
Write-Ok "Drift state written: $StatePath"

if (-not $NoHistoryWrite) {
  ($state | ConvertTo-Json -Compress -Depth 8) | Add-Content -LiteralPath $DriftHistoryPath
  Write-Ok "Drift history appended: $DriftHistoryPath"
}

if ($driftBreaches.Count -gt 0) {
  foreach ($b in $driftBreaches) { Write-Warn "Drift breach: $b" }
}
if ($anomalies.Count -gt 0) {
  foreach ($a in $anomalies) { Write-Warn "Anomaly: $a" }
}

if ($FailOnDrift -and (-not $ok)) {
  throw 'M15 reason_id drift guardrails failed'
}

Write-Ok 'M15 reason_id drift guardrails complete'
