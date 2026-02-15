param(
  [string]$MetricsFile = "artifacts/m16/raw/metrics_latest.json",
  [string]$BaselinePath = "artifacts/m16/baseline.json",
  [string]$OutputPath = "",  # If empty, output to console only
  [ValidateRange(0.05, 2.0)]
  [double]$WarnThresholdPct = 0.15,
  [ValidateRange(0.05, 2.0)]
  [double]$FailThresholdPct = 0.30,
  [switch]$FailOnDrift,
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# Resolve repo root (this script is in .ops/milestones/)
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $repoRoot

function Write-Step([string]$msg) { if (-not $Quiet) { Write-Host "[M17-CI] $msg" -ForegroundColor Cyan } }
function Write-Ok([string]$msg) { if (-not $Quiet) { Write-Host "[M17-CI][OK] $msg" -ForegroundColor Green } }
function Write-Warn([string]$msg) { Write-Host "[M17-CI][WARN] $msg" -ForegroundColor Yellow }
function Write-Fail([string]$msg) { Write-Host "[M17-CI][FAIL] $msg" -ForegroundColor Red }

function Format-Cycles([double]$cycles) {
  if ($cycles -ge 1000000) {
    return "{0:N2}M" -f ($cycles / 1000000)
  } elseif ($cycles -ge 1000) {
    return "{0:N2}k" -f ($cycles / 1000)
  } else {
    return "{0:N0}" -f $cycles
  }
}

# Check if metrics file exists
$metricsPath = Join-Path $repoRoot $MetricsFile
if (-not (Test-Path -LiteralPath $metricsPath)) {
  Write-Warn "Metrics file not found: $metricsPath"
  Write-Warn "Run /metrics command in REPL and extract with m16-extract-metrics.ps1"
  exit 1
}

Write-Step "Parsing metrics from: $MetricsFile"

# Parse metrics
try {
  $metrics = Get-Content -LiteralPath $metricsPath -Raw | ConvertFrom-Json
} catch {
  Write-Fail "Failed to parse metrics JSON: $_"
  exit 1
}

# Calculate derived metrics
$prefillCyclesPerToken = if ($metrics.total_prefill_tokens -gt 0) {
  $metrics.total_prefill_cycles / $metrics.total_prefill_tokens
} else { 0 }

$decodeCyclesPerToken = if ($metrics.total_decode_tokens -gt 0) {
  $metrics.total_decode_cycles / $metrics.total_decode_tokens
} else { 0 }

$totalTokens = [uint64]$metrics.total_prefill_tokens + [uint64]$metrics.total_decode_tokens
$totalCycles = [uint64]$metrics.total_prefill_cycles + [uint64]$metrics.total_decode_cycles

# Build report
$report = @()
$report += "=" * 70
$report += "M17 CI RUNTIME METRICS REPORT"
$report += "=" * 70
$report += ""
$report += "Performance Summary:"
$report += "  Prefill cycles/token:  $(Format-Cycles $prefillCyclesPerToken)"
$report += "  Decode cycles/token:   $(Format-Cycles $decodeCyclesPerToken)"
$report += "  Total tokens:          $totalTokens"
$report += "  Total cycles:          $(Format-Cycles $totalCycles)"
$report += ""
$report += "Operational Metrics:"
$report += "  Generations:           $($metrics.generation_count)"
$report += "  KV cache resets:       $($metrics.kv_cache_resets)"
$report += "  Sentinel violations:   $($metrics.sentinel_violations_total)"
$report += "  Prefill calls:         $($metrics.total_prefill_calls)"
$report += "  Decode calls:          $($metrics.total_decode_calls)"
$report += ""

# Load baseline and check drift
$baselinePath = Join-Path $repoRoot $BaselinePath
$driftStatus = "NO_BASELINE"
$prefillDrift = 0.0
$decodeDrift = 0.0
$shouldFail = $false

if (Test-Path -LiteralPath $baselinePath) {
  try {
    $baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
    
    if ($baseline.prefill_cycles_per_token.p50 -gt 0) {
      $prefillDrift = ($prefillCyclesPerToken - $baseline.prefill_cycles_per_token.p50) / $baseline.prefill_cycles_per_token.p50
    }
    if ($baseline.decode_cycles_per_token.p50 -gt 0) {
      $decodeDrift = ($decodeCyclesPerToken - $baseline.decode_cycles_per_token.p50) / $baseline.decode_cycles_per_token.p50
    }
    
    $report += "Drift vs Baseline:"
    $report += "  Baseline date:         $($baseline.timestamp)"
    $report += "  Prefill drift:         {0:+0.0;-0.0}% (baseline: $(Format-Cycles $baseline.prefill_cycles_per_token.p50))" -f ($prefillDrift * 100)
    $report += "  Decode drift:          {0:+0.0;-0.0}% (baseline: $(Format-Cycles $baseline.decode_cycles_per_token.p50))" -f ($decodeDrift * 100)
    $report += ""
    
    # Check thresholds
    if ($prefillDrift -ge $FailThresholdPct -or $decodeDrift -ge $FailThresholdPct) {
      $driftStatus = "FAIL"
      $shouldFail = $true
      $report += "  Status: ❌ FAIL - Drift exceeds {0:P0} threshold" -f $FailThresholdPct
    } elseif ($prefillDrift -ge $WarnThresholdPct -or $decodeDrift -ge $WarnThresholdPct) {
      $driftStatus = "WARN"
      $report += "  Status: ⚠️  WARNING - Drift exceeds {0:P0} threshold" -f $WarnThresholdPct
    } else {
      $driftStatus = "PASS"
      $report += "  Status: ✅ PASS - Performance within acceptable range"
    }
  } catch {
    Write-Warn "Failed to parse baseline: $_"
    $driftStatus = "BASELINE_ERROR"
  }
} else {
  $report += "Drift vs Baseline: (no baseline available)"
  $report += "  Run: .\reliability.ps1 -RunQemu -M16ExtractMetrics -M16UpdateBaseline"
  $report += "  To establish performance baseline"
}

$report += ""
$report += "=" * 70

# Output report
$reportText = $report -join "`n"
Write-Host $reportText

# Write to file if requested
if ($OutputPath) {
  $outputPathFull = Join-Path $repoRoot $OutputPath
  $outputDir = Split-Path -Parent $outputPathFull
  if ($outputDir -and -not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
  }
  Set-Content -LiteralPath $outputPathFull -Value $reportText -Encoding UTF8
  Write-Ok "Report written to: $OutputPath"
}

# Generate GitHub Actions job summary if in CI
if ($env:GITHUB_STEP_SUMMARY) {
  $mdReport = @"
## M17 Runtime Metrics Report

### Performance Summary
| Metric | Value |
|--------|-------|
| Prefill cycles/token | $(Format-Cycles $prefillCyclesPerToken) |
| Decode cycles/token | $(Format-Cycles $decodeCyclesPerToken) |
| Total tokens | $totalTokens |
| Total cycles | $(Format-Cycles $totalCycles) |

### Operational Metrics
| Metric | Value |
|--------|-------|
| Generations | $($metrics.generation_count) |
| KV cache resets | $($metrics.kv_cache_resets) |
| Sentinel violations | $($metrics.sentinel_violations_total) |

"@

  if (Test-Path -LiteralPath $baselinePath) {
    $statusEmoji = switch ($driftStatus) {
      "PASS" { "✅" }
      "WARN" { "⚠️" }
      "FAIL" { "❌" }
      default { "ℹ️" }
    }
    
    $mdReport += @"
### Drift Analysis $statusEmoji

| Metric | Drift | Baseline |
|--------|-------|----------|
| Prefill | {0:+0.0;-0.0}% | $(Format-Cycles $baseline.prefill_cycles_per_token.p50) |
| Decode | {1:+0.0;-0.0}% | $(Format-Cycles $baseline.decode_cycles_per_token.p50) |

**Status**: $driftStatus  
**Thresholds**: Warn at {2:P0}, Fail at {3:P0}

"@ -f ($prefillDrift * 100), ($decodeDrift * 100), $WarnThresholdPct, $FailThresholdPct
  }
  
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value $mdReport -Encoding UTF8
  Write-Ok "GitHub Actions job summary updated"
}

# Exit with appropriate code
if ($shouldFail -and $FailOnDrift) {
  Write-Fail "Performance drift exceeds failure threshold"
  exit 1
} elseif ($shouldFail) {
  Write-Warn "Performance drift detected but not failing (use -FailOnDrift to enforce)"
}

Write-Ok "M17 CI metrics report complete (status: $driftStatus)"
exit 0
