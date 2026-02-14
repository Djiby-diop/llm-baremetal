param(
  [string]$DriftHistoryPath = "artifacts/m15/history.jsonl",
  [string]$ExplainabilityHistoryPath = "artifacts/m13/history.jsonl",
  [string]$StatePath = "artifacts/m15/dashboard-state.json",
  [string]$MarkdownPath = "artifacts/m15/dashboard.md",
  [string]$DashboardHistoryPath = "artifacts/m15/dashboard-history.jsonl",
  [ValidateRange(5, 500)]
  [int]$WindowRuns = 20,
  [ValidateRange(7, 365)]
  [int]$WeeklyWindowDays = 28,
  [ValidateRange(1, 20)]
  [int]$TopReasonIds = 5,
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M15.1] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M15.1][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M15.1][WARN] $msg" -ForegroundColor Yellow }

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

function Get-Prop([object]$obj, [string]$name) {
  if ($null -eq $obj) { return $null }
  $p = $obj.PSObject.Properties[$name]
  if ($null -eq $p) { return $null }
  return $p.Value
}

function Parse-UtcDate([object]$value) {
  if ($null -eq $value) { return $null }
  try {
    return [DateTime]::Parse([string]$value).ToUniversalTime()
  } catch {
    return $null
  }
}

function Get-IsoWeekKey([datetime]$dt) {
  $culture = [System.Globalization.CultureInfo]::InvariantCulture
  $week = $culture.Calendar.GetWeekOfYear($dt, [System.Globalization.CalendarWeekRule]::FirstFourDayWeek, [DayOfWeek]::Monday)
  return "{0:D4}-W{1:D2}" -f $dt.Year, $week
}

function Add-Count([hashtable]$map, [string]$key, [int]$delta) {
  if (-not $key) { return }
  if ($map.ContainsKey($key)) {
    $map[$key] = [int]$map[$key] + $delta
  } else {
    $map[$key] = $delta
  }
}

function Get-EventReasonIds([object]$entry) {
  $ids = New-Object System.Collections.Generic.List[string]
  if ($null -eq $entry) { return @($ids) }

  $evProp = $entry.PSObject.Properties['decision_events']
  if ($null -eq $evProp -or $null -eq $evProp.Value) { return @($ids) }

  foreach ($ev in $evProp.Value) {
    if ($null -eq $ev) { continue }
    $native = Get-Prop -obj $ev -name 'reason_code_native'
    $fallback = Get-Prop -obj $ev -name 'reason_code'
    if ($native) {
      $ids.Add([string]$native)
    } elseif ($fallback) {
      $ids.Add([string]$fallback)
    }
  }

  return @($ids)
}

function Get-TopRows([hashtable]$counts, [int]$topN) {
  $rows = @()
  $total = 0
  foreach ($v in $counts.Values) {
    $total += [int]$v
  }

  if ($total -le 0) {
    return @()
  }

  foreach ($k in ($counts.Keys | Sort-Object)) {
    $count = [int]$counts[$k]
    $share = [math]::Round((([double]$count * 100.0) / [double]$total), 2)
    $rows += [ordered]@{
      reason_id = $k
      count = $count
      share_pct = $share
    }
  }

  return @($rows | Sort-Object -Property @{Expression='count';Descending=$true}, @{Expression='reason_id';Descending=$false} | Select-Object -First $topN)
}

function Get-ShareMap([hashtable]$counts) {
  $result = @{}
  $total = 0
  foreach ($v in $counts.Values) {
    $total += [int]$v
  }

  if ($total -le 0) {
    return $result
  }

  foreach ($k in $counts.Keys) {
    $result[$k] = [math]::Round((([double]$counts[$k] * 100.0) / [double]$total), 2)
  }
  return $result
}

$now = (Get-Date).ToUniversalTime()
$fromDate = $now.AddDays(-1 * $WeeklyWindowDays)
$currentWeekKey = Get-IsoWeekKey -dt $now
$previousWeekKey = Get-IsoWeekKey -dt $now.AddDays(-7)

$driftHistory = Read-JsonLines -path $DriftHistoryPath
$driftWindow = @($driftHistory | Select-Object -Last $WindowRuns)

$driftPass = 0
$unknownShareValues = New-Object System.Collections.Generic.List[double]
foreach ($run in $driftWindow) {
  if ([bool](Get-Prop -obj $run -name 'drift_ok')) {
    $driftPass++
  }

  $shares = Get-Prop -obj $run -name 'current_shares'
  if ($null -ne $shares) {
    $unk = $shares.PSObject.Properties['AUTO_APPLY_UNKNOWN']
    if ($null -ne $unk -and $null -ne $unk.Value) {
      try {
        $unknownShareValues.Add([double]$unk.Value)
      } catch {
      }
    }
  }
}

$driftPassRate = 100.0
if ($driftWindow.Count -gt 0) {
  $driftPassRate = [math]::Round((([double]$driftPass * 100.0) / [double]$driftWindow.Count), 2)
}

$avgUnknownShare = 0.0
if ($unknownShareValues.Count -gt 0) {
  $sumUnknown = 0.0
  foreach ($x in $unknownShareValues) { $sumUnknown += [double]$x }
  $avgUnknownShare = [math]::Round(($sumUnknown / [double]$unknownShareValues.Count), 2)
}

$m13History = Read-JsonLines -path $ExplainabilityHistoryPath

$currentWeekCounts = @{}
$previousWeekCounts = @{}
$recentEventCount = 0
$weeklySnapshots = @{}

foreach ($entry in $m13History) {
  $ts = Parse-UtcDate -value (Get-Prop -obj $entry -name 'ts_utc')
  if ($null -eq $ts) { continue }
  if ($ts -lt $fromDate) { continue }

  $ids = Get-EventReasonIds -entry $entry
  if ($ids.Count -eq 0) { continue }

  $weekKey = Get-IsoWeekKey -dt $ts
  if (-not $weeklySnapshots.ContainsKey($weekKey)) {
    $weeklySnapshots[$weekKey] = @{}
  }

  foreach ($id in $ids) {
    Add-Count -map $weeklySnapshots[$weekKey] -key $id -delta 1
    if ($weekKey -eq $currentWeekKey) {
      Add-Count -map $currentWeekCounts -key $id -delta 1
    } elseif ($weekKey -eq $previousWeekKey) {
      Add-Count -map $previousWeekCounts -key $id -delta 1
    }
    $recentEventCount++
  }
}

$topCurrentWeek = Get-TopRows -counts $currentWeekCounts -topN $TopReasonIds
$topPreviousWeek = Get-TopRows -counts $previousWeekCounts -topN $TopReasonIds

$currentShareMap = Get-ShareMap -counts $currentWeekCounts
$previousShareMap = Get-ShareMap -counts $previousWeekCounts

$keys = New-Object System.Collections.Generic.HashSet[string]
foreach ($k in $currentShareMap.Keys) { [void]$keys.Add([string]$k) }
foreach ($k in $previousShareMap.Keys) { [void]$keys.Add([string]$k) }

$wowRows = @()
foreach ($k in $keys) {
  $curShare = 0.0
  $prevShare = 0.0
  if ($currentShareMap.ContainsKey($k)) { $curShare = [double]$currentShareMap[$k] }
  if ($previousShareMap.ContainsKey($k)) { $prevShare = [double]$previousShareMap[$k] }
  $delta = [math]::Round(($curShare - $prevShare), 2)

  $wowRows += [ordered]@{
    reason_id = $k
    current_week_share_pct = $curShare
    previous_week_share_pct = $prevShare
    delta_share_pct = $delta
    abs_delta_share_pct = [math]::Round([math]::Abs($delta), 2)
  }
}

$wowTop = @($wowRows | Sort-Object -Property @{Expression='abs_delta_share_pct';Descending=$true}, @{Expression='reason_id';Descending=$false} | Select-Object -First $TopReasonIds)

$weeklyRows = @()
foreach ($wk in ($weeklySnapshots.Keys | Sort-Object)) {
  $topWeek = Get-TopRows -counts $weeklySnapshots[$wk] -topN 3
  $weeklyRows += [ordered]@{
    week = $wk
    event_count = (($weeklySnapshots[$wk].Values | Measure-Object -Sum).Sum)
    top_reason_ids = @($topWeek)
  }
}

$stateDir = Split-Path -Parent $StatePath
if ($stateDir -and -not (Test-Path -LiteralPath $stateDir)) {
  New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
}

$mdDir = Split-Path -Parent $MarkdownPath
if ($mdDir -and -not (Test-Path -LiteralPath $mdDir)) {
  New-Item -ItemType Directory -Path $mdDir -Force | Out-Null
}

$historyDir = Split-Path -Parent $DashboardHistoryPath
if ($historyDir -and -not (Test-Path -LiteralPath $historyDir)) {
  New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
}

$alerts = New-Object System.Collections.Generic.List[string]
if ($driftWindow.Count -eq 0) {
  $alerts.Add('no_m15_history')
}
if ($recentEventCount -eq 0) {
  $alerts.Add('no_recent_reason_events')
}

$state = [ordered]@{
  ts_utc = $now.ToString('o')
  drift_history_path = $DriftHistoryPath
  explainability_history_path = $ExplainabilityHistoryPath
  window_runs = $WindowRuns
  weekly_window_days = $WeeklyWindowDays
  drift_runs_analyzed = $driftWindow.Count
  drift_pass_rate_pct = $driftPassRate
  avg_unknown_share_pct = $avgUnknownShare
  recent_reason_event_count = $recentEventCount
  current_week = $currentWeekKey
  previous_week = $previousWeekKey
  top_current_week = @($topCurrentWeek)
  top_previous_week = @($topPreviousWeek)
  week_over_week_delta_top = @($wowTop)
  weekly_snapshots = @($weeklyRows)
  alerts = @($alerts)
}

$state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $StatePath -Encoding UTF8
Write-Ok "Dashboard state written: $StatePath"

$mdLines = New-Object System.Collections.Generic.List[string]
$mdLines.Add('# M15.1 reason_id SLO dashboard')
$mdLines.Add('')
$mdLines.Add("Generated: $($state.ts_utc)")
$mdLines.Add('')
$mdLines.Add('## SLO snapshot')
$mdLines.Add('')
$mdLines.Add("- Drift runs analyzed: $($state.drift_runs_analyzed)")
$mdLines.Add("- Drift pass rate: $($state.drift_pass_rate_pct)%")
$mdLines.Add("- Avg AUTO_APPLY_UNKNOWN share: $($state.avg_unknown_share_pct)%")
$mdLines.Add("- Recent reason events analyzed: $($state.recent_reason_event_count)")
$mdLines.Add('')

$mdLines.Add('## Week-over-week reason_id delta (top)')
$mdLines.Add('')
$mdLines.Add('| reason_id | current_week_share_pct | previous_week_share_pct | delta_share_pct |')
$mdLines.Add('|---|---:|---:|---:|')
if ($wowTop.Count -gt 0) {
  foreach ($row in $wowTop) {
    $mdLines.Add("| $($row.reason_id) | $($row.current_week_share_pct) | $($row.previous_week_share_pct) | $($row.delta_share_pct) |")
  }
} else {
  $mdLines.Add('| n/a | 0 | 0 | 0 |')
}
$mdLines.Add('')

$mdLines.Add('## Weekly compact snapshots')
$mdLines.Add('')
if ($weeklyRows.Count -gt 0) {
  foreach ($wk in $weeklyRows) {
    $mdLines.Add("- $($wk.week): events=$($wk.event_count)")
    foreach ($top in $wk.top_reason_ids) {
      $mdLines.Add("  - $($top.reason_id): $($top.count) ($($top.share_pct)%)")
    }
  }
} else {
  $mdLines.Add('- No data available in the selected window.')
}
$mdLines.Add('')

if ($alerts.Count -gt 0) {
  $mdLines.Add('## Alerts')
  $mdLines.Add('')
  foreach ($a in $alerts) {
    $mdLines.Add("- $a")
  }
  $mdLines.Add('')
}

$mdLines -join "`r`n" | Set-Content -LiteralPath $MarkdownPath -Encoding UTF8
Write-Ok "Dashboard markdown written: $MarkdownPath"

if (-not $NoHistoryWrite) {
  ($state | ConvertTo-Json -Compress -Depth 8) | Add-Content -LiteralPath $DashboardHistoryPath
  Write-Ok "Dashboard history appended: $DashboardHistoryPath"
}

Write-Step "drift_runs=$($state.drift_runs_analyzed) pass_rate=$($state.drift_pass_rate_pct)% weekly_events=$($state.recent_reason_event_count)"
if ($alerts.Count -gt 0) {
  foreach ($a in $alerts) { Write-Warn "Alert: $a" }
}
Write-Ok 'M15.1 reason_id SLO dashboard export complete'
