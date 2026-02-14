param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [string]$M9HistoryPath = "artifacts/m9/history.jsonl",
  [string]$M10HistoryPath = "artifacts/m10/history.jsonl",
  [string]$ConfigPath = "repl.cfg",
  [ValidateSet('auto','early','warm','steady')]
  [string]$BootPhase = 'auto',
  [ValidateSet('auto','latency_optimization','context_expansion','mixed','unknown')]
  [string]$WorkloadClass = 'auto',
  [ValidateRange(0, 100)]
  [int]$EarlyThreshold = 35,
  [ValidateRange(0, 100)]
  [int]$WarmThreshold = 25,
  [ValidateRange(0, 100)]
  [int]$SteadyThreshold = 15,
  [ValidateRange(-100, 100)]
  [int]$AdjLatencyOptimization = -5,
  [ValidateRange(-100, 100)]
  [int]$AdjContextExpansion = 10,
  [ValidateRange(-100, 100)]
  [int]$AdjMixed = 3,
  [ValidateRange(-100, 100)]
  [int]$AdjUnknown = 0,
  [ValidateRange(1, 50)]
  [int]$OutcomeAdaptWindow = 5,
  [ValidateRange(1, 20)]
  [int]$OutcomeAdaptStep = 2,
  [switch]$NoOutcomeAdaptation,
  [switch]$ApplyConfig,
  [string]$StatePath = "artifacts/m12/curriculum-state.json",
  [string]$HistoryPath = "artifacts/m12/history.jsonl",
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M12] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M12][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M12][WARN] $msg" -ForegroundColor Yellow }

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

function Get-BootPhase([string]$forcedPhase, [object[]]$m9History) {
  if ($forcedPhase -ne 'auto') { return $forcedPhase }

  $count = 0
  if ($m9History) {
    $count = $m9History.Count
  }

  if ($count -le 2) { return 'early' }
  if ($count -le 6) { return 'warm' }
  return 'steady'
}

function Get-WorkloadClass([string]$forcedClass, [string]$logText) {
  if ($forcedClass -ne 'auto') { return $forcedClass }

  $reduceHits = ([regex]::Matches($logText, '/oo_consult_mock\s+.*?(reduce|decrease|lower)')).Count
  $increaseHits = ([regex]::Matches($logText, '/oo_consult_mock\s+.*?(increase|expand|raise)')).Count

  if ($reduceHits -gt 0 -and $increaseHits -gt 0) { return 'mixed' }
  if ($reduceHits -gt 0) { return 'latency_optimization' }
  if ($increaseHits -gt 0) { return 'context_expansion' }
  return 'unknown'
}

function Get-EffectiveThreshold([string]$phase,
                                [string]$workload,
                                [int]$early,
                                [int]$warm,
                                [int]$steady,
                                [int]$adjLatency,
                                [int]$adjExpansion,
                                [int]$adjMixed,
                                [int]$adjUnknown) {
  $base = $warm
  if ($phase -eq 'early') {
    $base = $early
  } elseif ($phase -eq 'steady') {
    $base = $steady
  }

  $adj = $adjUnknown
  if ($workload -eq 'latency_optimization') {
    $adj = $adjLatency
  } elseif ($workload -eq 'context_expansion') {
    $adj = $adjExpansion
  } elseif ($workload -eq 'mixed') {
    $adj = $adjMixed
  }

  $effective = $base + $adj
  if ($effective -lt 0) { $effective = 0 }
  if ($effective -gt 95) { $effective = 95 }

  return [ordered]@{
    base = $base
    adjustment = $adj
    effective = $effective
  }
}

function Clamp-Threshold([int]$value) {
  if ($value -lt 0) { return 0 }
  if ($value -gt 95) { return 95 }
  return $value
}

function Get-OutcomeAdaptation([object[]]$m10History, [int]$window, [int]$step) {
  $result = [ordered]@{
    samples = 0
    helpful = 0
    harmful = 0
    neutral = 0
    score = 0
    delta = 0
    direction = 'neutral'
  }

  if (-not $m10History -or $m10History.Count -eq 0) {
    return $result
  }

  $tail = @($m10History | Select-Object -Last $window)
  $result.samples = $tail.Count

  foreach ($entry in $tail) {
    $qualityOk = $false
    if ($null -ne $entry.PSObject.Properties['quality_ok']) {
      $qualityOk = [bool]$entry.quality_ok
    }

    $harmfulRatio = 100.0
    if ($null -ne $entry.PSObject.Properties['harmful_ratio_pct']) {
      $harmfulRatio = [double]$entry.harmful_ratio_pct
    }

    $isHelpful = ($qualityOk -and $harmfulRatio -le 20.0)
    $isHarmful = ((-not $qualityOk) -or $harmfulRatio -ge 60.0)

    if ($isHelpful) {
      $result.helpful++
    } elseif ($isHarmful) {
      $result.harmful++
    } else {
      $result.neutral++
    }
  }

  $result.score = [int]$result.helpful - [int]$result.harmful
  if ($result.score -gt 0) {
    $result.direction = 'more_permissive'
    $result.delta = -1 * ([Math]::Min(20, [Math]::Abs($result.score) * $step))
  } elseif ($result.score -lt 0) {
    $result.direction = 'more_conservative'
    $result.delta = [Math]::Min(20, [Math]::Abs($result.score) * $step)
  }

  return $result
}

if (-not (Test-Path -LiteralPath $LogPath)) {
  throw "M12 log not found: $LogPath"
}

$log = Get-Content -LiteralPath $LogPath -Raw
$m9History = Read-History -path $M9HistoryPath
$m10History = Read-History -path $M10HistoryPath

$phase = Get-BootPhase -forcedPhase $BootPhase -m9History $m9History
$workload = Get-WorkloadClass -forcedClass $WorkloadClass -logText $log

$outcomeAdaptation = [ordered]@{
  samples = 0
  helpful = 0
  harmful = 0
  neutral = 0
  score = 0
  delta = 0
  direction = 'neutral'
}

if (-not $NoOutcomeAdaptation) {
  $outcomeAdaptation = Get-OutcomeAdaptation -m10History $m10History -window $OutcomeAdaptWindow -step $OutcomeAdaptStep
}

$adaptiveEarlyThreshold = Clamp-Threshold ($EarlyThreshold + [int]$outcomeAdaptation.delta)
$adaptiveWarmThreshold = Clamp-Threshold ($WarmThreshold + [int]$outcomeAdaptation.delta)
$adaptiveSteadyThreshold = Clamp-Threshold ($SteadyThreshold + [int]$outcomeAdaptation.delta)

$adaptiveAdjLatencyOptimization = $AdjLatencyOptimization
$adaptiveAdjContextExpansion = $AdjContextExpansion
$adaptiveAdjMixed = $AdjMixed
$adaptiveAdjUnknown = $AdjUnknown

$cellDelta = [int][Math]::Round(([double]$outcomeAdaptation.delta) / 2.0)
if (-not $NoOutcomeAdaptation) {
  if ($workload -eq 'latency_optimization') {
    $adaptiveAdjLatencyOptimization = $AdjLatencyOptimization + $cellDelta
  } elseif ($workload -eq 'context_expansion') {
    $adaptiveAdjContextExpansion = $AdjContextExpansion + $cellDelta
  } elseif ($workload -eq 'mixed') {
    $adaptiveAdjMixed = $AdjMixed + $cellDelta
  } else {
    $adaptiveAdjUnknown = $AdjUnknown + $cellDelta
  }
}

$thresholdPack = Get-EffectiveThreshold -phase $phase `
                                       -workload $workload `
                                       -early $adaptiveEarlyThreshold `
                                       -warm $adaptiveWarmThreshold `
                                       -steady $adaptiveSteadyThreshold `
                                       -adjLatency $adaptiveAdjLatencyOptimization `
                                       -adjExpansion $adaptiveAdjContextExpansion `
                                       -adjMixed $adaptiveAdjMixed `
                                       -adjUnknown $adaptiveAdjUnknown

Write-Step "phase=$phase workload=$workload base=$($thresholdPack.base) adj=$($thresholdPack.adjustment) effective=$($thresholdPack.effective)"
Write-Step "outcome_adapt direction=$($outcomeAdaptation.direction) delta=$($outcomeAdaptation.delta) samples=$($outcomeAdaptation.samples) helpful=$($outcomeAdaptation.helpful) harmful=$($outcomeAdaptation.harmful)"

$configApplied = $false
if ($ApplyConfig) {
  if (-not (Test-Path -LiteralPath $ConfigPath)) {
    Write-Warn "Config not found: $ConfigPath"
  } else {
    Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_conf_gate' -value '1'
    Set-Or-Append-CfgKey -path $ConfigPath -key 'oo_conf_threshold' -value "$($thresholdPack.effective)"
    $configApplied = $true
    Write-Ok "Curriculum applied to $ConfigPath (oo_conf_threshold=$($thresholdPack.effective))"
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

$state = [ordered]@{
  ts_utc = (Get-Date).ToUniversalTime().ToString('o')
  log_path = $LogPath
  m9_history_path = $M9HistoryPath
  m10_history_path = $M10HistoryPath
  m9_history_count = if ($m9History) { $m9History.Count } else { 0 }
  m10_history_count = if ($m10History) { $m10History.Count } else { 0 }
  phase = $phase
  workload_class = $workload
  early_threshold = $EarlyThreshold
  warm_threshold = $WarmThreshold
  steady_threshold = $SteadyThreshold
  adj_latency_optimization = $AdjLatencyOptimization
  adj_context_expansion = $AdjContextExpansion
  adj_mixed = $AdjMixed
  adj_unknown = $AdjUnknown
  outcome_adaptation_enabled = (-not $NoOutcomeAdaptation)
  outcome_adapt_window = $OutcomeAdaptWindow
  outcome_adapt_step = $OutcomeAdaptStep
  outcome_adapt_samples = [int]$outcomeAdaptation.samples
  outcome_adapt_helpful = [int]$outcomeAdaptation.helpful
  outcome_adapt_harmful = [int]$outcomeAdaptation.harmful
  outcome_adapt_neutral = [int]$outcomeAdaptation.neutral
  outcome_adapt_score = [int]$outcomeAdaptation.score
  outcome_adapt_direction = $outcomeAdaptation.direction
  outcome_adapt_delta = [int]$outcomeAdaptation.delta
  adaptive_early_threshold = $adaptiveEarlyThreshold
  adaptive_warm_threshold = $adaptiveWarmThreshold
  adaptive_steady_threshold = $adaptiveSteadyThreshold
  adaptive_adj_latency_optimization = $adaptiveAdjLatencyOptimization
  adaptive_adj_context_expansion = $adaptiveAdjContextExpansion
  adaptive_adj_mixed = $adaptiveAdjMixed
  adaptive_adj_unknown = $adaptiveAdjUnknown
  base_threshold = $thresholdPack.base
  workload_adjustment = $thresholdPack.adjustment
  effective_threshold = $thresholdPack.effective
  config_applied = $configApplied
  config_path = $ConfigPath
}

$state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $StatePath -Encoding UTF8
Write-Ok "Curriculum state written: $StatePath"

if (-not $NoHistoryWrite) {
  ($state | ConvertTo-Json -Compress) | Add-Content -LiteralPath $HistoryPath
  Write-Ok "Curriculum history appended: $HistoryPath"
}

Write-Ok 'M12 policy curriculum evaluation complete'
