param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [string]$QuarantineStatePath = "artifacts/m10/quarantine-state.json",
  [string]$M9HistoryPath = "artifacts/m9/history.jsonl",
  [string]$M10HistoryPath = "artifacts/m10/history.jsonl",
  [string]$ConfigPath = "repl.cfg",
  [string]$ReleaseHistoryPath = "artifacts/m11/release-history.jsonl",
  [string]$ReleaseStatePath = "artifacts/m11/release-state.json",
  [ValidateRange(1, 100)]
  [int]$StableStreakNeeded = 3,
  [ValidateRange(1, 20)]
  [int]$CanaryBoots = 2,
  [ValidateRange(1, 1000)]
  [int]$MinSamplesForRelease = 2,
  [ValidateRange(1, 50)]
  [int]$M9StableWindow = 3,
  [ValidateRange(1, 50)]
  [int]$M10StableWindow = 3,
  [ValidateRange(0, 100)]
  [int]$CanaryConfThreshold = 20,
  [switch]$ApplyRelease,
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M11] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M11][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M11][WARN] $msg" -ForegroundColor Yellow }

function Read-CfgValue([string]$path, [string]$key) {
  if (-not (Test-Path -LiteralPath $path)) { return $null }
  $pattern = '^[\s#;]*' + [regex]::Escape($key) + '\s*=\s*(.+?)\s*$'
  foreach ($line in (Get-Content -LiteralPath $path)) {
    $m = [regex]::Match($line, $pattern)
    if ($m.Success) {
      return $m.Groups[1].Value
    }
  }
  return $null
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

function Read-JsonFile([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return $null }
  $raw = Get-Content -LiteralPath $path -Raw
  if (-not $raw) { return $null }
  return ($raw | ConvertFrom-Json)
}

function Read-History([string]$path) {
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

function Get-StableStreak([object[]]$history) {
  $streak = 0
  for ($i = $history.Count - 1; $i -ge 0; $i--) {
    $h = $history[$i]
    if ($h.release_candidate -eq $true) {
      $streak++
      continue
    }
    break
  }
  return $streak
}

function Test-RecentWindowAllTrue([object[]]$history, [string]$propertyName, [int]$window) {
  if (-not $history -or $history.Count -lt $window) {
    return $false
  }

  $tail = @($history | Select-Object -Last $window)
  foreach ($item in $tail) {
    if ($null -eq $item) { return $false }
    $prop = $item.PSObject.Properties[$propertyName]
    if ($null -eq $prop) { return $false }
    if (-not [bool]$prop.Value) { return $false }
  }
  return $true
}

if (-not (Test-Path -LiteralPath $LogPath)) {
  throw "M11 log not found: $LogPath"
}

$log = Get-Content -LiteralPath $LogPath -Raw
Write-Step "Evaluating self-healing release from $LogPath"

$quarantineState = Read-JsonFile -path $QuarantineStatePath
if ($null -eq $quarantineState) {
  throw "M11 quarantine state not found: $QuarantineStatePath"
}

$releaseState = Read-JsonFile -path $ReleaseStatePath
if ($null -eq $releaseState) {
  $releaseState = [ordered]@{
    canary_active = $false
    canary_runs_left = 0
    last_action = 'init'
    ts_utc = (Get-Date).ToUniversalTime().ToString('o')
  }
}

$autoApplyValue = Read-CfgValue -path $ConfigPath -key 'oo_auto_apply'
$isCfgQuarantined = ($autoApplyValue -eq '0')
$stateQuarantineNeeded = [bool]$quarantineState.quarantine_needed
$quarantined = ($isCfgQuarantined -or $stateQuarantineNeeded)

$eventTokens = @(
  ($log -split "`r?`n") | Where-Object {
    $_ -match 'OK:\s+OO auto-apply:|ERROR:\s+OO auto-apply verification failed:'
  }
)
$successCount = 0
$failedCount = 0
$maxConsecutiveFailedSeen = 0
$currentConsecutiveFailed = 0

foreach ($eventLine in $eventTokens) {
  if ($eventLine -like 'OK:*') {
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

$effectiveMaxHarmfulRatioPct = 40
$effectiveMaxConsecutiveFailures = 2
if ($null -ne $quarantineState.effective_max_harmful_ratio_pct) {
  $effectiveMaxHarmfulRatioPct = [int]$quarantineState.effective_max_harmful_ratio_pct
}
if ($null -ne $quarantineState.effective_max_consecutive_failures) {
  $effectiveMaxConsecutiveFailures = [int]$quarantineState.effective_max_consecutive_failures
}

$releaseCandidate = $false
if ($totalSamples -ge $MinSamplesForRelease -and
    $harmfulRatioPct -le [double]$effectiveMaxHarmfulRatioPct -and
    $maxConsecutiveFailedSeen -le $effectiveMaxConsecutiveFailures) {
  $releaseCandidate = $true
}

$m9History = Read-History -path $M9HistoryPath
$m10History = Read-History -path $M10HistoryPath

$m9WindowStable = Test-RecentWindowAllTrue -history $m9History -propertyName 'pass' -window $M9StableWindow
$m10WindowStable = Test-RecentWindowAllTrue -history $m10History -propertyName 'quality_ok' -window $M10StableWindow

$releaseCandidateCoupled = ($releaseCandidate -and $m9WindowStable -and $m10WindowStable)

$historyDir = Split-Path -Parent $ReleaseHistoryPath
if ($historyDir -and -not (Test-Path -LiteralPath $historyDir)) {
  New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
}

$stateDir = Split-Path -Parent $ReleaseStatePath
if ($stateDir -and -not (Test-Path -LiteralPath $stateDir)) {
  New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
}

$history = Read-History -path $ReleaseHistoryPath
$newEntry = [ordered]@{
  ts_utc = (Get-Date).ToUniversalTime().ToString('o')
  log_path = $LogPath
  quarantined = $quarantined
  canary_active = [bool]$releaseState.canary_active
  canary_runs_left = [int]$releaseState.canary_runs_left
  samples = $totalSamples
  success = $successCount
  failed = $failedCount
  harmful_ratio_pct = [math]::Round($harmfulRatioPct, 2)
  max_failed_streak = $maxConsecutiveFailedSeen
  effective_max_harmful_ratio_pct = $effectiveMaxHarmfulRatioPct
  effective_max_consecutive_failures = $effectiveMaxConsecutiveFailures
  min_samples_for_release = $MinSamplesForRelease
  release_candidate_raw = $releaseCandidate
  m9_window_stable = $m9WindowStable
  m10_window_stable = $m10WindowStable
  m9_stable_window = $M9StableWindow
  m10_stable_window = $M10StableWindow
  release_candidate = $releaseCandidateCoupled
  stable_streak_needed = $StableStreakNeeded
}

$combinedHistory = @($history)
$combinedHistory += $newEntry
$stableStreak = Get-StableStreak -history $combinedHistory
Write-Step "release_candidate_raw=$releaseCandidate m9_window_stable=$m9WindowStable m10_window_stable=$m10WindowStable"
Write-Step "release_candidate_coupled=$releaseCandidateCoupled stable_streak=$stableStreak/$StableStreakNeeded quarantined=$quarantined canary_active=$($releaseState.canary_active)"

$action = 'none'

if ($quarantined -and -not [bool]$releaseState.canary_active) {
  if ($stableStreak -ge $StableStreakNeeded) {
    if ($ApplyRelease) {
      Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_auto_apply' -value '1'
      Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_conf_gate' -value '1'
      Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_conf_threshold' -value "$CanaryConfThreshold"

      $releaseState.canary_active = $true
      $releaseState.canary_runs_left = $CanaryBoots
      $releaseState.last_action = 'canary_start'
      $releaseState.ts_utc = (Get-Date).ToUniversalTime().ToString('o')
      $action = 'canary_start'
      Write-Ok "Canary started: oo_auto_apply=1 with oo_conf_threshold=$CanaryConfThreshold for $CanaryBoots run(s)"
    } else {
      $action = 'canary_ready'
      Write-Warn 'Release conditions met but not applied (use -ApplyRelease)'
    }
  } else {
    $action = 'keep_quarantine'
    Write-Ok 'Quarantine kept: stable streak not reached'
  }
} elseif ([bool]$releaseState.canary_active) {
  if ($releaseCandidateCoupled) {
    $remaining = [int]$releaseState.canary_runs_left - 1
    if ($remaining -le 0) {
      if ($ApplyRelease) {
        Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_auto_apply' -value '1'
        Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_conf_gate' -value '1'
        Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_conf_threshold' -value '0'
      }

      $releaseState.canary_active = $false
      $releaseState.canary_runs_left = 0
      $releaseState.last_action = 'full_release'
      $releaseState.ts_utc = (Get-Date).ToUniversalTime().ToString('o')
      $action = 'full_release'
      Write-Ok 'Canary successful: full release applied (oo_conf_threshold=0)'
    } else {
      $releaseState.canary_runs_left = $remaining
      $releaseState.last_action = 'canary_progress'
      $releaseState.ts_utc = (Get-Date).ToUniversalTime().ToString('o')
      $action = 'canary_progress'
      Write-Ok "Canary progressing: $remaining run(s) remaining"
    }
  } else {
    if ($ApplyRelease) {
      Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_auto_apply' -value '0'
      Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_conf_gate' -value '1'
    }

    $releaseState.canary_active = $false
    $releaseState.canary_runs_left = 0
    $releaseState.last_action = 'canary_rollback'
    $releaseState.ts_utc = (Get-Date).ToUniversalTime().ToString('o')
    $action = 'canary_rollback'
    Write-Warn 'Canary failed: rollback to quarantine (oo_auto_apply=0)'
  }
} else {
  $action = 'no_quarantine'
  Write-Ok 'No quarantine active; no release action required'
}

$releaseStateObj = [ordered]@{
  ts_utc = (Get-Date).ToUniversalTime().ToString('o')
  last_action = $action
  quarantined = $quarantined
  canary_active = [bool]$releaseState.canary_active
  canary_runs_left = [int]$releaseState.canary_runs_left
  stable_streak = $stableStreak
  stable_streak_needed = $StableStreakNeeded
  release_candidate_raw = $releaseCandidate
  m9_window_stable = $m9WindowStable
  m10_window_stable = $m10WindowStable
  m9_stable_window = $M9StableWindow
  m10_stable_window = $M10StableWindow
  release_candidate = $releaseCandidateCoupled
  canary_boots = $CanaryBoots
  min_samples_for_release = $MinSamplesForRelease
  canary_conf_threshold = $CanaryConfThreshold
  config_path = $ConfigPath
  quarantine_state_path = $QuarantineStatePath
  release_history_path = $ReleaseHistoryPath
}

$releaseStateObj | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ReleaseStatePath -Encoding UTF8
Write-Ok "Release state written: $ReleaseStatePath"

if (-not $NoHistoryWrite) {
  ($newEntry | ConvertTo-Json -Compress) | Add-Content -LiteralPath $ReleaseHistoryPath
  Write-Ok "Release history appended: $ReleaseHistoryPath"
}

Write-Ok 'M11 self-healing evaluation complete'
