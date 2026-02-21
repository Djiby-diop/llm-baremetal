param(
  [string]$RawMetricsDir = "artifacts/m16/raw",
  [string]$HistoryPath = "artifacts/m16/metrics-history.jsonl",
  [string]$BaselinePath = "artifacts/m16/baseline.json",
  [string]$StatePath = "artifacts/m16/aggregate-state.json",
  [string]$DashboardPath = "artifacts/m16/dashboard.md",
  [ValidateRange(3, 100)]
  [int]$WindowRuns = 10,
  [ValidateRange(0.05, 2.0)]
  [double]$DriftThresholdPct = 0.20,  # 20% increase = drift alert
  [switch]$UpdateBaseline,
  [switch]$RejectOnDrift,
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# Resolve repo root (this script is in .ops/milestones/)
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $repoRoot

function Write-Step([string]$msg) { if (-not $Quiet) { Write-Host "[M16.2] $msg" -ForegroundColor Cyan } }
function Write-Ok([string]$msg) { if (-not $Quiet) { Write-Host "[M16.2][OK] $msg" -ForegroundColor Green } }
function Write-Warn([string]$msg) { Write-Host "[M16.2][WARN] $msg" -ForegroundColor Yellow }
function Write-Fail([string]$msg) { Write-Host "[M16.2][FAIL] $msg" -ForegroundColor Red }

function Read-JsonLines([string]$path) {
  $items = @()
  if (-not (Test-Path -LiteralPath $path)) { return $items }
  
  $lines = Get-Content -LiteralPath $path -ErrorAction SilentlyContinue
  foreach ($line in $lines) {
    if (-not $line) { continue }
    try {
      $obj = $line | ConvertFrom-Json -ErrorAction Stop
      if ($null -ne $obj) { $items += $obj }
    } catch { }
  }
  return $items
}

function Write-JsonLine([string]$path, [object]$obj) {
  $dir = Split-Path -Parent $path
  if ($dir -and -not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
  }
  $json = ($obj | ConvertTo-Json -Compress -Depth 10)
  Add-Content -LiteralPath $path -Value $json -Encoding UTF8
}

function Get-Percentile([double[]]$sorted, [double]$pct) {
  if ($sorted.Count -eq 0) { return 0.0 }
  $idx = [Math]::Floor($sorted.Count * $pct)
  if ($idx -ge $sorted.Count) { $idx = $sorted.Count - 1 }
  return $sorted[$idx]
}

function Compute-Stats([double[]]$values) {
  if ($values.Count -eq 0) {
    return @{
      count = 0
      mean = 0.0
      p50 = 0.0
      p95 = 0.0
      p99 = 0.0
      min = 0.0
      max = 0.0
    }
  }
  
  $sorted = $values | Sort-Object
  $sum = ($sorted | Measure-Object -Sum).Sum
  $mean = $sum / $sorted.Count
  
  return @{
    count = $sorted.Count
    mean = [Math]::Round($mean, 2)
    p50 = [Math]::Round((Get-Percentile $sorted 0.50), 2)
    p95 = [Math]::Round((Get-Percentile $sorted 0.95), 2)
    p99 = [Math]::Round((Get-Percentile $sorted 0.99), 2)
    min = [Math]::Round($sorted[0], 2)
    max = [Math]::Round($sorted[-1], 2)
  }
}

function Parse-MetricsFile([string]$path) {
  try {
    $content = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    return @{
      valid = $true
      session_start_cycles = [uint64]$content.session_start_cycles
      total_prefill_cycles = [uint64]$content.total_prefill_cycles
      total_decode_cycles = [uint64]$content.total_decode_cycles
      total_prefill_tokens = [uint64]$content.total_prefill_tokens
      total_decode_tokens = [uint64]$content.total_decode_tokens
      total_prefill_calls = [uint64]$content.total_prefill_calls
      total_decode_calls = [uint64]$content.total_decode_calls
      kv_cache_resets = [uint64]$content.kv_cache_resets
      generation_count = [uint64]$content.generation_count
      sentinel_violations_total = [uint64]$content.sentinel_violations_total
    }
  } catch {
    return @{ valid = $false }
  }
}

# ============================================================================
# Main execution
# ============================================================================

Write-Step "M16.2 Metrics Aggregation + Drift Detection"

# Ensure artifacts directory exists
$artifactsDir = Join-Path $repoRoot "artifacts/m16"
if (-not (Test-Path -LiteralPath $artifactsDir)) {
  New-Item -ItemType Directory -Path $artifactsDir -Force | Out-Null
}

# 1. Load all raw metrics files
$rawDir = Join-Path $repoRoot $RawMetricsDir
if (-not (Test-Path -LiteralPath $rawDir)) {
  Write-Warn "Raw metrics directory not found: $rawDir"
  Write-Warn "Run m16-extract-metrics.ps1 first to collect metrics from QEMU runs"
  exit 1
}

$metricsFiles = Get-ChildItem -LiteralPath $rawDir -Filter "*.json" -File | Sort-Object LastWriteTime
if ($metricsFiles.Count -eq 0) {
  Write-Warn "No metrics files found in $rawDir"
  exit 1
}

Write-Step "Found $($metricsFiles.Count) metrics file(s)"

# 2. Parse all metrics and build aggregates
$allMetrics = @()
$prefillCyclesPerToken = @()
$decodeCyclesPerToken = @()
$generationCounts = @()
$kvResets = @()
$sentinelViolations = @()

foreach ($file in $metricsFiles) {
  $parsed = Parse-MetricsFile $file.FullName
  if (-not $parsed.valid) {
    Write-Warn "Skipping invalid metrics file: $($file.Name)"
    continue
  }
  
  $allMetrics += $parsed
  
  # Calculate per-token cycles (avoid division by zero)
  if ($parsed.total_prefill_tokens -gt 0) {
    $prefillCyclesPerToken += ($parsed.total_prefill_cycles / $parsed.total_prefill_tokens)
  }
  if ($parsed.total_decode_tokens -gt 0) {
    $decodeCyclesPerToken += ($parsed.total_decode_cycles / $parsed.total_decode_tokens)
  }
  
  $generationCounts += [double]$parsed.generation_count
  $kvResets += [double]$parsed.kv_cache_resets
  $sentinelViolations += [double]$parsed.sentinel_violations_total
}

if ($allMetrics.Count -eq 0) {
  Write-Warn "No valid metrics to aggregate"
  exit 1
}

Write-Ok "Parsed $($allMetrics.Count) valid metrics file(s)"

# 3. Compute statistics
$prefillStats = Compute-Stats $prefillCyclesPerToken
$decodeStats = Compute-Stats $decodeCyclesPerToken
$genStats = Compute-Stats $generationCounts
$kvResetStats = Compute-Stats $kvResets
$sentinelStats = Compute-Stats $sentinelViolations

$aggregateState = @{
  timestamp = (Get-Date).ToUniversalTime().ToString("o")
  run_count = $allMetrics.Count
  prefill_cycles_per_token = $prefillStats
  decode_cycles_per_token = $decodeStats
  generation_count = $genStats
  kv_cache_resets = $kvResetStats
  sentinel_violations = $sentinelStats
}

# 4. Load baseline for drift detection
$baseline = $null
$baselinePath = Join-Path $repoRoot $BaselinePath
if (Test-Path -LiteralPath $baselinePath) {
  try {
    $baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
    Write-Step "Loaded baseline from $BaselinePath"
  } catch {
    Write-Warn "Failed to parse baseline file"
  }
}

# 5. Detect drift
$driftDetected = $false
$driftReasons = @()

if ($null -ne $baseline) {
  $prefillDrift = 0.0
  $decodeDrift = 0.0
  
  if ($baseline.prefill_cycles_per_token.p50 -gt 0) {
    $prefillDrift = ($prefillStats.p50 - $baseline.prefill_cycles_per_token.p50) / $baseline.prefill_cycles_per_token.p50
  }
  if ($baseline.decode_cycles_per_token.p50 -gt 0) {
    $decodeDrift = ($decodeStats.p50 - $baseline.decode_cycles_per_token.p50) / $baseline.decode_cycles_per_token.p50
  }
  
  if ($prefillDrift -gt $DriftThresholdPct) {
    $driftDetected = $true
    $pctStr = "{0:P1}" -f $prefillDrift
    $driftReasons += "Prefill cycles/token P50 increased by $pctStr (threshold: {0:P0})" -f $DriftThresholdPct
  }
  if ($decodeDrift -gt $DriftThresholdPct) {
    $driftDetected = $true
    $pctStr = "{0:P1}" -f $decodeDrift
    $driftReasons += "Decode cycles/token P50 increased by $pctStr (threshold: {0:P0})" -f $DriftThresholdPct
  }
  
  $aggregateState.drift_detected = $driftDetected
  $aggregateState.prefill_drift_pct = [Math]::Round($prefillDrift * 100, 2)
  $aggregateState.decode_drift_pct = [Math]::Round($decodeDrift * 100, 2)
  $aggregateState.drift_reasons = $driftReasons
} else {
  Write-Step "No baseline - skipping drift detection"
  $aggregateState.drift_detected = $false
}

# 6. Write aggregate state
$statePathFull = Join-Path $repoRoot $StatePath
$aggregateState | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $statePathFull -Encoding UTF8
Write-Ok "Wrote aggregate state to $StatePath"

# 7. Append to history
$historyPathFull = Join-Path $repoRoot $HistoryPath
Write-JsonLine $historyPathFull $aggregateState
Write-Ok "Appended to history: $HistoryPath"

# 8. Update baseline if requested
if ($UpdateBaseline) {
  $newBaseline = @{
    timestamp = (Get-Date).ToUniversalTime().ToString("o")
    run_count = $allMetrics.Count
    prefill_cycles_per_token = $prefillStats
    decode_cycles_per_token = $decodeStats
  }
  $newBaseline | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $baselinePath -Encoding UTF8
  Write-Ok "Updated baseline: $BaselinePath"
}

# 9. Generate Markdown dashboard
$dashPathFull = Join-Path $repoRoot $DashboardPath
$md = @"
# M16.2 Runtime Metrics Dashboard

**Generated**: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss") UTC  
**Runs analyzed**: $($allMetrics.Count)  
**Window**: Last $WindowRuns runs  

## Performance Summary

### Prefill Cycles/Token
- **Mean**: $($prefillStats.mean)
- **P50**: $($prefillStats.p50)
- **P95**: $($prefillStats.p95)
- **P99**: $($prefillStats.p99)
- **Range**: [$($prefillStats.min), $($prefillStats.max)]

### Decode Cycles/Token
- **Mean**: $($decodeStats.mean)
- **P50**: $($decodeStats.p50)
- **P95**: $($decodeStats.p95)
- **P99**: $($decodeStats.p99)
- **Range**: [$($decodeStats.min), $($decodeStats.max)]

## Operational Metrics

### Generations
- **Mean**: $($genStats.mean)
- **P50**: $($genStats.p50)
- **Max**: $($genStats.max)

### KV Cache Resets
- **Mean**: $($kvResetStats.mean)
- **P50**: $($kvResetStats.p50)
- **Max**: $($kvResetStats.max)

### Sentinel Violations
- **Mean**: $($sentinelStats.mean)
- **P50**: $($sentinelStats.p50)
- **Max**: $($sentinelStats.max)

"@

if ($null -ne $baseline) {
  $thresholdStr = "{0:P0}" -f $DriftThresholdPct
  $md += "`n## Drift Detection`n`n"
  $md += "**Baseline timestamp**: $($baseline.timestamp)  `n"
  $md += "**Drift threshold**: $thresholdStr`n`n"

  if ($driftDetected) {
    $md += "### DRIFT DETECTED`n`n"
    foreach ($reason in $driftReasons) {
      $md += "- $reason`n"
    }
  } else {
    $md += "### No drift detected`n`n"
    $md += "- Prefill drift: {0:+0.0;-0.0}%`n" -f $aggregateState.prefill_drift_pct
    $md += "- Decode drift: {0:+0.0;-0.0}%`n" -f $aggregateState.decode_drift_pct
  }
}

$thresholdStr2 = "{0:P0}" -f $DriftThresholdPct
$baselineModeStr = if ($UpdateBaseline) { 'UPDATE' } else { 'CHECK' }
$md += "`n---`n`n"
$md += "**Configuration**:`n"
$md += "- Window runs: $WindowRuns`n"
$md += "- Drift threshold: $thresholdStr2`n"
$md += "- Baseline mode: $baselineModeStr`n"

Set-Content -LiteralPath $dashPathFull -Value $md -Encoding UTF8
Write-Ok "Generated dashboard: $DashboardPath"

# 10. Exit with appropriate code
if ($driftDetected) {
  Write-Warn "Performance drift detected"
  foreach ($reason in $driftReasons) {
    Write-Warn "  - $reason"
  }
  
  if ($RejectOnDrift) {
    Write-Fail "Rejecting due to performance drift (use -RejectOnDrift:`$false to disable)"
    exit 1
  } else {
    Write-Warn "Drift detected but not rejecting (use -RejectOnDrift to enforce)"
  }
}

Write-Ok "M16.2 aggregation complete"
exit 0
