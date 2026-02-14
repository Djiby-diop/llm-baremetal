param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [string]$M10StatePath = "artifacts/m10/quarantine-state.json",
  [string]$M11StatePath = "artifacts/m11/release-state.json",
  [string]$M12StatePath = "artifacts/m12/curriculum-state.json",
  [string]$StatePath = "artifacts/m13/explainability-state.json",
  [string]$HistoryPath = "artifacts/m13/history.jsonl",
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M13] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M13][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M13][WARN] $msg" -ForegroundColor Yellow }

function Read-JsonFile([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return $null }
  $raw = Get-Content -LiteralPath $path -Raw
  if (-not $raw) { return $null }
  try {
    return ($raw | ConvertFrom-Json)
  } catch {
    return $null
  }
}

function Get-PropValue([object]$obj, [string]$name) {
  if ($null -eq $obj) { return $null }
  $prop = $obj.PSObject.Properties[$name]
  if ($null -eq $prop) { return $null }
  return $prop.Value
}

function Get-ReasonCodes([object]$m10State, [object]$m11State, [object]$m12State) {
  $codes = New-Object System.Collections.Generic.List[string]

  if ($null -eq $m10State) {
    $codes.Add('M10_STATE_MISSING')
  } elseif ([bool]$m10State.quarantine_needed) {
    $codes.Add('M10_QUARANTINE_NEEDED')
  } elseif ([bool]$m10State.quality_ok) {
    $codes.Add('M10_QUALITY_OK')
  } else {
    $codes.Add('M10_QUALITY_UNCERTAIN')
  }

  if ($null -eq $m11State) {
    $codes.Add('M11_STATE_MISSING')
  } else {
    $action = ''
    if ($null -ne $m11State.PSObject.Properties['last_action']) {
      $action = [string]$m11State.last_action
    }

    if ($action) {
      $codes.Add("M11_$($action.ToUpperInvariant())")
    } else {
      $codes.Add('M11_ACTION_UNKNOWN')
    }

    if ($null -ne $m11State.PSObject.Properties['release_candidate']) {
      if ([bool]$m11State.release_candidate) {
        $codes.Add('M11_RELEASE_CANDIDATE_TRUE')
      } else {
        $codes.Add('M11_RELEASE_CANDIDATE_FALSE')
      }
    }
  }

  if ($null -eq $m12State) {
    $codes.Add('M12_STATE_MISSING')
  } else {
    $codes.Add('M12_CURRICULUM_AVAILABLE')
    if ($null -ne $m12State.PSObject.Properties['outcome_adapt_direction']) {
      $dir = [string]$m12State.outcome_adapt_direction
      if ($dir) {
        $codes.Add("M12_OUTCOME_$($dir.ToUpperInvariant())")
      }
    }
  }

  return @($codes)
}

if (-not (Test-Path -LiteralPath $LogPath)) {
  throw "M13 log not found: $LogPath"
}

$log = Get-Content -LiteralPath $LogPath -Raw
$m10State = Read-JsonFile -path $M10StatePath
$m11State = Read-JsonFile -path $M11StatePath
$m12State = Read-JsonFile -path $M12StatePath

$decisionLines = @(
  ($log -split "`r?`n") | Where-Object {
    $_ -match 'OK:\s+OO auto-apply:|ERROR:\s+OO auto-apply verification failed:'
  }
)

$decisionEvents = New-Object System.Collections.Generic.List[object]
$idx = 0
foreach ($line in $decisionLines) {
  $idx++
  $outcome = 'unknown'
  $reason = 'AUTO_APPLY_UNKNOWN'
  $nativeReasonId = ''
  if ($line -like 'OK:*') {
    $outcome = 'success'
    $reason = 'AUTO_APPLY_OK'
  } elseif ($line -like 'ERROR:*') {
    $outcome = 'failed'
    $reason = 'AUTO_APPLY_VERIFY_FAILED'
  }

  $rid = [regex]::Match($line, 'reason_id=([A-Za-z0-9_\-]+)')
  if ($rid.Success) {
    $nativeReasonId = $rid.Groups[1].Value
    $reason = $nativeReasonId
  }

  $decisionEvents.Add([ordered]@{
    idx = $idx
    outcome = $outcome
    reason_code = $reason
    reason_code_native = $nativeReasonId
    raw = $line
  })
}

$reasons = Get-ReasonCodes -m10State $m10State -m11State $m11State -m12State $m12State

$thresholdProvenance = [ordered]@{
  m10 = [ordered]@{
    effective_max_harmful_ratio_pct = (Get-PropValue -obj $m10State -name 'effective_max_harmful_ratio_pct')
    effective_max_consecutive_failures = (Get-PropValue -obj $m10State -name 'effective_max_consecutive_failures')
    effective_min_samples = (Get-PropValue -obj $m10State -name 'effective_min_samples')
    model_class = (Get-PropValue -obj $m10State -name 'model_class')
    ram_tier = (Get-PropValue -obj $m10State -name 'ram_tier')
  }
  m11 = [ordered]@{
    release_candidate = (Get-PropValue -obj $m11State -name 'release_candidate')
    m9_window_stable = (Get-PropValue -obj $m11State -name 'm9_window_stable')
    m10_window_stable = (Get-PropValue -obj $m11State -name 'm10_window_stable')
    canary_conf_threshold = (Get-PropValue -obj $m11State -name 'canary_conf_threshold')
    last_action = (Get-PropValue -obj $m11State -name 'last_action')
  }
  m12 = [ordered]@{
    phase = (Get-PropValue -obj $m12State -name 'phase')
    workload_class = (Get-PropValue -obj $m12State -name 'workload_class')
    effective_threshold = (Get-PropValue -obj $m12State -name 'effective_threshold')
    outcome_adapt_direction = (Get-PropValue -obj $m12State -name 'outcome_adapt_direction')
    outcome_adapt_delta = (Get-PropValue -obj $m12State -name 'outcome_adapt_delta')
  }
}

$stateDir = Split-Path -Parent $StatePath
if ($stateDir -and -not (Test-Path -LiteralPath $stateDir)) {
  New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
}

$historyDir = Split-Path -Parent $HistoryPath
if ($historyDir -and -not (Test-Path -LiteralPath $historyDir)) {
  New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
}

$state = @{}
$state['ts_utc'] = (Get-Date).ToUniversalTime().ToString('o')
$state['log_path'] = $LogPath
$state['reason_codes'] = @($reasons)
$state['decision_event_count'] = [int]$decisionEvents.Count
$decisionEventsArray = @()
if ($decisionEvents.Count -gt 0) {
  $decisionEventsArray = $decisionEvents.ToArray()
}
$state['decision_events'] = $decisionEventsArray
$state['threshold_provenance'] = $thresholdProvenance

$sourcePaths = @{}
$sourcePaths['m10'] = $M10StatePath
$sourcePaths['m11'] = $M11StatePath
$sourcePaths['m12'] = $M12StatePath
$state['source_state_paths'] = $sourcePaths

$state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $StatePath -Encoding UTF8
Write-Ok "Explainability state written: $StatePath"

if (-not $NoHistoryWrite) {
  ($state | ConvertTo-Json -Compress -Depth 8) | Add-Content -LiteralPath $HistoryPath
  Write-Ok "Explainability history appended: $HistoryPath"
}

Write-Step "reason_codes=$($reasons -join ',') decisions=$($decisionEvents.Count)"
Write-Ok 'M13 explainability pack complete'
