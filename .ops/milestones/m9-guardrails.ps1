param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [ValidateRange(0, 120000)]
  [int]$MaxModelSelectMs = 2000,
  [ValidateRange(0, 120000)]
  [int]$MaxModelPrepareMs = 12000,
  [switch]$RequireOoMarkers,
  [string]$HistoryPath = "artifacts/m9/history.jsonl",
  [ValidateRange(2, 50)]
  [int]$DriftWindow = 5,
  [ValidateRange(1, 1000)]
  [int]$MaxSelectDriftPct = 250,
  [ValidateRange(1, 1000)]
  [int]$MaxPrepareDriftPct = 250,
  [switch]$NoDriftCheck,
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Ok([string]$msg) {
  Write-Host "[M9][OK] $msg" -ForegroundColor Green
}

function Write-Warn([string]$msg) {
  Write-Host "[M9][WARN] $msg" -ForegroundColor Yellow
}

function Write-Step([string]$msg) {
  Write-Host "[M9] $msg" -ForegroundColor Cyan
}

function Assert-Pattern([string]$text, [string]$pattern, [string]$label) {
  if ($text -imatch $pattern) {
    Write-Ok $label
    return $true
  }
  Write-Warn "$label (missing pattern: $pattern)"
  return $false
}

function Assert-Max([int]$value, [int]$max, [string]$label) {
  if ($value -le $max) {
    Write-Ok "$label ($value <= $max)"
    return $true
  }
  Write-Warn "$label ($value > $max)"
  return $false
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

function Mean-Int([object[]]$vals) {
  if (-not $vals -or $vals.Count -eq 0) { return 0.0 }
  $sum = 0.0
  foreach ($v in $vals) { $sum += [double]$v }
  return ($sum / [double]$vals.Count)
}

function Assert-DriftPct([double]$current, [double]$baseline, [int]$maxPct, [string]$label) {
  if ($baseline -le 0.0) {
    Write-Step "$label skipped (baseline <= 0)"
    return $true
  }
  $pct = (($current - $baseline) / $baseline) * 100.0
  $pctRounded = [math]::Round($pct, 1)
  if ($pct -le [double]$maxPct) {
    Write-Ok "$label (drift=${pctRounded}% <= ${maxPct}%)"
    return $true
  }
  Write-Warn "$label (drift=${pctRounded}% > ${maxPct}%)"
  return $false
}

if (-not (Test-Path -LiteralPath $LogPath)) {
  throw "M9 guardrail log not found: $LogPath"
}

$log = Get-Content -LiteralPath $LogPath -Raw
Write-Step "Parsing guardrails from $LogPath"

$ok = $true
$selectMs = 0
$prepareMs = 0
$hasOoConfidence = $false
$hasOoFeedback = $false
$hasOoPlan = $false

$mandatory = @(
  @{ Label = 'Startup marker present'; Pattern = '\[obs\]\[startup\]\s+model_select_ms=\d+\s+model_prepare_ms=\d+' },
  @{ Label = 'Models summary present'; Pattern = 'summary:\s+total=' }
)

foreach ($m in $mandatory) {
  if (-not (Assert-Pattern -text $log -pattern $m.Pattern -label $m.Label)) {
    $ok = $false
  }
}

$mm = [regex]::Match($log, '\[obs\]\[startup\]\s+model_select_ms=(\d+)\s+model_prepare_ms=(\d+)')
if ($mm.Success) {
  $selectMs = [int]$mm.Groups[1].Value
  $prepareMs = [int]$mm.Groups[2].Value

  if (-not (Assert-Max -value $selectMs -max $MaxModelSelectMs -label 'model_select_ms budget')) {
    $ok = $false
  }
  if (-not (Assert-Max -value $prepareMs -max $MaxModelPrepareMs -label 'model_prepare_ms budget')) {
    $ok = $false
  }
} else {
  Write-Warn 'Could not parse startup latency marker'
  $ok = $false
}

if ($RequireOoMarkers) {
  $ooChecks = @(
    @{ Label = 'OO confidence marker present'; Pattern = 'OK:\s+OO confidence:' },
    @{ Label = 'OO feedback marker present'; Pattern = 'OK:\s+OO feedback:' },
    @{ Label = 'OO plan marker present'; Pattern = 'OK:\s+OO plan:' }
  )

  foreach ($o in $ooChecks) {
    $present = ($log -imatch $o.Pattern)
    if (-not $present) {
      Write-Warn "$($o.Label) (missing pattern: $($o.Pattern))"
      $ok = $false
    } else {
      Write-Ok $o.Label
    }
    if ($o.Label -eq 'OO confidence marker present') { $hasOoConfidence = $present }
    if ($o.Label -eq 'OO feedback marker present') { $hasOoFeedback = $present }
    if ($o.Label -eq 'OO plan marker present') { $hasOoPlan = $present }
  }
}

$historyDir = Split-Path -Parent $HistoryPath
if ($historyDir -and -not (Test-Path -LiteralPath $historyDir)) {
  New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
}

$history = Read-History -path $HistoryPath

if (-not $NoDriftCheck -and $history.Count -ge 1) {
  $tail = @($history | Select-Object -Last $DriftWindow)
  if ($tail.Count -gt 0) {
    $baselineSelect = Mean-Int ($tail | ForEach-Object { [int]$_.model_select_ms })
    $baselinePrepare = Mean-Int ($tail | ForEach-Object { [int]$_.model_prepare_ms })

    Write-Step "Drift check against last $($tail.Count) run(s)"
    if (-not (Assert-DriftPct -current $selectMs -baseline $baselineSelect -maxPct $MaxSelectDriftPct -label 'model_select_ms drift')) {
      $ok = $false
    }
    if (-not (Assert-DriftPct -current $prepareMs -baseline $baselinePrepare -maxPct $MaxPrepareDriftPct -label 'model_prepare_ms drift')) {
      $ok = $false
    }
  }
} elseif (-not $NoDriftCheck) {
  Write-Step 'Drift check skipped (insufficient history; need >=1 prior run)'
}

if (-not $NoHistoryWrite) {
  $entry = [ordered]@{
    ts_utc = (Get-Date).ToUniversalTime().ToString('o')
    log_path = $LogPath
    model_select_ms = $selectMs
    model_prepare_ms = $prepareMs
    oo_confidence = $hasOoConfidence
    oo_feedback = $hasOoFeedback
    oo_plan = $hasOoPlan
    max_model_select_ms = $MaxModelSelectMs
    max_model_prepare_ms = $MaxModelPrepareMs
    require_oo_markers = [bool]$RequireOoMarkers
    pass = $ok
  }

  ($entry | ConvertTo-Json -Compress) | Add-Content -LiteralPath $HistoryPath
  Write-Ok "History appended: $HistoryPath"
}

if (-not $ok) {
  throw "M9 guardrails failed for $LogPath"
}

Write-Ok 'M9 guardrails passed'
