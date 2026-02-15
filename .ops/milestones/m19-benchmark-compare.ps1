param(
  [string]$BaselineResultsPath = 'artifacts/m19/baseline/results.jsonl',
  [string]$CurrentResultsPath = 'artifacts/m19/current/results.jsonl',
  [string]$OutputPath = 'artifacts/m19/compare/benchmark-compare.md',
  [ValidateRange(0.01, 2.0)]
  [double]$RegressionThresholdPct = 0.15,
  [switch]$FailOnRegression,
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $repoRoot

function Write-Step([string]$msg) { if (-not $Quiet) { Write-Host "[M19-COMPARE] $msg" -ForegroundColor Cyan } }
function Write-Ok([string]$msg) { if (-not $Quiet) { Write-Host "[M19-COMPARE][OK] $msg" -ForegroundColor Green } }
function Write-Warn([string]$msg) { Write-Host "[M19-COMPARE][WARN] $msg" -ForegroundColor Yellow }
function Write-Fail([string]$msg) { Write-Host "[M19-COMPARE][FAIL] $msg" -ForegroundColor Red }

function Read-JsonLines([string]$path) {
  $rows = @()
  if (-not (Test-Path -LiteralPath $path)) { return $rows }
  foreach ($line in (Get-Content -LiteralPath $path -ErrorAction SilentlyContinue)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    try {
      $obj = $line | ConvertFrom-Json -ErrorAction Stop
      if ($null -ne $obj) { $rows += $obj }
    } catch { }
  }
  return $rows
}

function P50([double[]]$values) {
  if ($values.Count -eq 0) { return 0.0 }
  $sorted = $values | Sort-Object
  $idx = [Math]::Min([Math]::Floor($sorted.Count * 0.5), $sorted.Count - 1)
  return [double]$sorted[$idx]
}

function Build-CaseMetrics([object]$row) {
  $decodeCpt = $null
  if (-not [object]::ReferenceEquals($null, $row.decode_cycles) -and -not [object]::ReferenceEquals($null, $row.decode_tokens) -and [double]$row.decode_tokens -gt 0) {
    $decodeCpt = [double]$row.decode_cycles / [double]$row.decode_tokens
  }

  $latMs = $null
  if (-not [object]::ReferenceEquals($null, $row.latency_ms)) {
    $latMs = [double]$row.latency_ms
  }

  return @{
    case_id = [string]$row.case_id
    status = [string]$row.status
    latency_ms = $latMs
    decode_cpt = $decodeCpt
  }
}

Write-Step 'Comparing benchmark runs'

$basePath = Join-Path $repoRoot $BaselineResultsPath
$currPath = Join-Path $repoRoot $CurrentResultsPath

if (-not (Test-Path -LiteralPath $basePath)) {
  throw "Baseline results not found: $BaselineResultsPath"
}
if (-not (Test-Path -LiteralPath $currPath)) {
  throw "Current results not found: $CurrentResultsPath"
}

$baselineRows = Read-JsonLines -path $basePath
$currentRows = Read-JsonLines -path $currPath

if ($baselineRows.Count -eq 0 -or $currentRows.Count -eq 0) {
  throw 'Baseline or current results are empty'
}

$baseMap = @{}
foreach ($r in $baselineRows) {
  if ($r.case_id) { $baseMap[[string]$r.case_id] = Build-CaseMetrics -row $r }
}
$currMap = @{}
foreach ($r in $currentRows) {
  if ($r.case_id) { $currMap[[string]$r.case_id] = Build-CaseMetrics -row $r }
}

$caseIds = @($baseMap.Keys | Where-Object { $currMap.ContainsKey($_) } | Sort-Object)
if ($caseIds.Count -eq 0) {
  throw 'No overlapping case_id between baseline and current results'
}

$rows = @()
$regressions = @()

foreach ($id in $caseIds) {
  $b = $baseMap[$id]
  $c = $currMap[$id]

  $latDeltaPct = $null
  if (-not [object]::ReferenceEquals($null, $b.latency_ms) -and [double]$b.latency_ms -gt 0 -and -not [object]::ReferenceEquals($null, $c.latency_ms)) {
    $latDeltaPct = (([double]$c.latency_ms - [double]$b.latency_ms) / [double]$b.latency_ms)
  }

  $decDeltaPct = $null
  if (-not [object]::ReferenceEquals($null, $b.decode_cpt) -and [double]$b.decode_cpt -gt 0 -and -not [object]::ReferenceEquals($null, $c.decode_cpt)) {
    $decDeltaPct = (([double]$c.decode_cpt - [double]$b.decode_cpt) / [double]$b.decode_cpt)
  }

  $isRegression = $false
  if (-not [object]::ReferenceEquals($null, $latDeltaPct) -and [double]$latDeltaPct -gt $RegressionThresholdPct) { $isRegression = $true }
  if (-not [object]::ReferenceEquals($null, $decDeltaPct) -and [double]$decDeltaPct -gt $RegressionThresholdPct) { $isRegression = $true }

  if ($isRegression) {
    $regressions += $id
  }

  $rows += [pscustomobject]@{
    case_id = $id
    baseline_latency_ms = if (-not [object]::ReferenceEquals($null, $b.latency_ms)) { [Math]::Round([double]$b.latency_ms, 2) } else { $null }
    current_latency_ms = if (-not [object]::ReferenceEquals($null, $c.latency_ms)) { [Math]::Round([double]$c.latency_ms, 2) } else { $null }
    latency_delta_pct = if (-not [object]::ReferenceEquals($null, $latDeltaPct)) { [Math]::Round([double]$latDeltaPct * 100, 1) } else { $null }
    baseline_decode_cpt = if (-not [object]::ReferenceEquals($null, $b.decode_cpt)) { [Math]::Round([double]$b.decode_cpt, 2) } else { $null }
    current_decode_cpt = if (-not [object]::ReferenceEquals($null, $c.decode_cpt)) { [Math]::Round([double]$c.decode_cpt, 2) } else { $null }
    decode_cpt_delta_pct = if (-not [object]::ReferenceEquals($null, $decDeltaPct)) { [Math]::Round([double]$decDeltaPct * 100, 1) } else { $null }
    regression = $isRegression
  }
}

$latBaseP50 = P50 @($rows | Where-Object { -not [object]::ReferenceEquals($null, $_.baseline_latency_ms) } | ForEach-Object { [double]$_.baseline_latency_ms })
$latCurrP50 = P50 @($rows | Where-Object { -not [object]::ReferenceEquals($null, $_.current_latency_ms) } | ForEach-Object { [double]$_.current_latency_ms })
$decBaseP50 = P50 @($rows | Where-Object { -not [object]::ReferenceEquals($null, $_.baseline_decode_cpt) } | ForEach-Object { [double]$_.baseline_decode_cpt })
$decCurrP50 = P50 @($rows | Where-Object { -not [object]::ReferenceEquals($null, $_.current_decode_cpt) } | ForEach-Object { [double]$_.current_decode_cpt })

$outFull = Join-Path $repoRoot $OutputPath
$outDir = Split-Path -Parent $outFull
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$md = @()
$md += '# M19 Benchmark Compare'
$md += ''
$md += "- threshold_regression_pct: $([Math]::Round($RegressionThresholdPct * 100, 1))"
$md += "- compared_cases: $($rows.Count)"
$md += "- regressions: $($regressions.Count)"
$md += ''
$md += '## Aggregate (P50)'
$md += ''
$md += '| metric | baseline | current |'
$md += '|---|---:|---:|'
$md += "| latency_ms | $([Math]::Round($latBaseP50, 2)) | $([Math]::Round($latCurrP50, 2)) |"
$md += "| decode_cycles_per_token | $([Math]::Round($decBaseP50, 2)) | $([Math]::Round($decCurrP50, 2)) |"
$md += ''
$md += '## Case Matrix'
$md += ''
$md += '| case_id | lat_base | lat_curr | lat_delta% | dec_base | dec_curr | dec_delta% | regression |'
$md += '|---|---:|---:|---:|---:|---:|---:|---|'
foreach ($r in $rows) {
  $md += "| $($r.case_id) | $($r.baseline_latency_ms) | $($r.current_latency_ms) | $($r.latency_delta_pct) | $($r.baseline_decode_cpt) | $($r.current_decode_cpt) | $($r.decode_cpt_delta_pct) | $($r.regression) |"
}

if ($regressions.Count -gt 0) {
  $md += ''
  $md += '## Regressions'
  foreach ($id in $regressions) {
    $md += "- $id"
  }
}

Set-Content -LiteralPath $outFull -Value ($md -join "`n") -Encoding UTF8

$summary = @{
  compared_cases = $rows.Count
  regression_threshold_pct = $RegressionThresholdPct
  regressions = $regressions
  regression_count = $regressions.Count
  aggregate = @{
    baseline_latency_p50 = [Math]::Round($latBaseP50, 2)
    current_latency_p50 = [Math]::Round($latCurrP50, 2)
    baseline_decode_cpt_p50 = [Math]::Round($decBaseP50, 2)
    current_decode_cpt_p50 = [Math]::Round($decCurrP50, 2)
  }
}

$summaryPath = Join-Path $outDir 'benchmark-compare.json'
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

if ($env:GITHUB_STEP_SUMMARY) {
  $jobSummary = @()
  $jobSummary += '## M19 Benchmark Compare'
  $jobSummary += ''
  $jobSummary += "- Compared cases: $($rows.Count)"
  $jobSummary += "- Regressions: $($regressions.Count)"
  $jobSummary += "- Threshold: $([Math]::Round($RegressionThresholdPct * 100, 1))%"
  $jobSummary += ''
  $jobSummary += "Report: $OutputPath"
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ($jobSummary -join "`n") -Encoding UTF8
}

if ($regressions.Count -gt 0 -and $FailOnRegression) {
  Write-Fail "Detected $($regressions.Count) regression(s) above threshold"
  exit 1
}

if ($regressions.Count -gt 0) {
  Write-Warn "Detected $($regressions.Count) regression(s) (non-blocking)"
} else {
  Write-Ok 'No regressions detected'
}

Write-Ok "Comparison report written: $OutputPath"
exit 0
