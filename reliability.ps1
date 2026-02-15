# M16: Unified reliability orchestrator (public interface)
# Delegates to internal milestone pipeline in .ops/milestones/
param(
  [switch]$SkipPreflight,
  [switch]$SkipBuild,
  [switch]$RunQemu,
  [ValidateRange(15, 600)]
  [int]$TimeoutSec = 90,
  [ValidateSet('auto','whpx','tcg','none')]
  [string]$Accel = 'tcg',
  [ValidateRange(1024, 8192)]
  [int]$MemMB = 4096,
  [ValidateRange(0, 120000)]
  [int]$MaxModelSelectMs = 2000,
  [ValidateRange(0, 120000)]
  [int]$MaxModelPrepareMs = 12000,
  [ValidateRange(0, 100)]
  [int]$MaxHarmfulRatioPct = 40,
  [ValidateRange(1, 100)]
  [int]$MaxConsecutiveFailures = 2,
  [ValidateRange(1, 1000)]
  [int]$MinAutoApplySamples = 2,
  [ValidateRange(1, 100)]
  [int]$M11StableStreakNeeded = 3,
  [ValidateRange(1, 20)]
  [int]$M11CanaryBoots = 2,
  [ValidateRange(1, 1000)]
  [int]$M11MinSamplesForRelease = 2,
  [ValidateRange(1, 50)]
  [int]$M11M9StableWindow = 3,
  [ValidateRange(1, 50)]
  [int]$M11M10StableWindow = 3,
  [ValidateRange(0, 100)]
  [int]$M11CanaryConfThreshold = 20,
  [ValidateRange(0, 100)]
  [int]$M12EarlyThreshold = 35,
  [ValidateRange(0, 100)]
  [int]$M12WarmThreshold = 25,
  [ValidateRange(0, 100)]
  [int]$M12SteadyThreshold = 15,
  [ValidateRange(-100, 100)]
  [int]$M12AdjLatencyOptimization = -5,
  [ValidateRange(-100, 100)]
  [int]$M12AdjContextExpansion = 10,
  [ValidateRange(-100, 100)]
  [int]$M12AdjMixed = 3,
  [ValidateRange(-100, 100)]
  [int]$M12AdjUnknown = 0,
  [switch]$M14RequireJournalParity,
  [switch]$M14SkipJournalExtract,
  [string]$M14ImagePath,
  [ValidateRange(1, 50)]
  [int]$M15BaselineWindow = 5,
  [ValidateRange(1, 100)]
  [int]$M15MinSamplesForDrift = 5,
  [ValidateRange(1, 200)]
  [int]$M15MaxShareDriftPct = 80,
  [ValidateRange(1, 100)]
  [int]$M15MaxUnknownSharePct = 30,
  [ValidateRange(5, 500)]
  [int]$M151WindowRuns = 20,
  [ValidateRange(7, 365)]
  [int]$M151WeeklyWindowDays = 28,
  [ValidateRange(1, 20)]
  [int]$M151TopReasonIds = 5,
  [switch]$M16ExtractMetrics,
  [switch]$M16SkipExtract,
  [ValidateRange(3, 100)]
  [int]$M16WindowRuns = 10,
  [ValidateRange(0.05, 2.0)]
  [double]$M16DriftThresholdPct = 0.20,
  [switch]$M16UpdateBaseline,
  [switch]$M16RejectOnDrift,
  [switch]$M17EnableCIReport,
  [ValidateRange(0.05, 2.0)]
  [double]$M17WarnThresholdPct = 0.15,
  [ValidateRange(0.05, 2.0)]
  [double]$M17FailThresholdPct = 0.30,
  [switch]$M17FailOnDrift,
  [switch]$M19EnableBenchmarkPack,
  [ValidateRange(0.01, 2.0)]
  [double]$M19RegressionThresholdPct = 0.15,
  [switch]$M19FailOnRegression,
  [string]$M19BaselineResultsPath = 'artifacts/m19/baseline/results.jsonl',
  [string]$M19CurrentResultsPath = 'artifacts/m19/current/results.jsonl'
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

$orchestratorScript = Join-Path $PSScriptRoot '.ops\milestones\m8-reliability.ps1'

if (-not (Test-Path -LiteralPath $orchestratorScript)) {
  throw "Internal orchestrator not found: $orchestratorScript"
}

$argHash = @{
  TimeoutSec = $TimeoutSec
  Accel = $Accel
  MemMB = $MemMB
  MaxModelSelectMs = $MaxModelSelectMs
  MaxModelPrepareMs = $MaxModelPrepareMs
  MaxHarmfulRatioPct = $MaxHarmfulRatioPct
  MaxConsecutiveFailures = $MaxConsecutiveFailures
  MinAutoApplySamples = $MinAutoApplySamples
  M11StableStreakNeeded = $M11StableStreakNeeded
  M11CanaryBoots = $M11CanaryBoots
  M11MinSamplesForRelease = $M11MinSamplesForRelease
  M11M9StableWindow = $M11M9StableWindow
  M11M10StableWindow = $M11M10StableWindow
  M11CanaryConfThreshold = $M11CanaryConfThreshold
  M12EarlyThreshold = $M12EarlyThreshold
  M12WarmThreshold = $M12WarmThreshold
  M12SteadyThreshold = $M12SteadyThreshold
  M12AdjLatencyOptimization = $M12AdjLatencyOptimization
  M12AdjContextExpansion = $M12AdjContextExpansion
  M12AdjMixed = $M12AdjMixed
  M12AdjUnknown = $M12AdjUnknown
  M15BaselineWindow = $M15BaselineWindow
  M15MinSamplesForDrift = $M15MinSamplesForDrift
  M15MaxShareDriftPct = $M15MaxShareDriftPct
  M15MaxUnknownSharePct = $M15MaxUnknownSharePct
  M151WindowRuns = $M151WindowRuns
  M151WeeklyWindowDays = $M151WeeklyWindowDays
  M151TopReasonIds = $M151TopReasonIds
  M16WindowRuns = $M16WindowRuns
  M16DriftThresholdPct = $M16DriftThresholdPct
  M17WarnThresholdPct = $M17WarnThresholdPct
  M17FailThresholdPct = $M17FailThresholdPct
  M19RegressionThresholdPct = $M19RegressionThresholdPct
  M19BaselineResultsPath = $M19BaselineResultsPath
  M19CurrentResultsPath = $M19CurrentResultsPath
}

if ($SkipPreflight) { $argHash['SkipPreflight'] = $true }
if ($SkipBuild) { $argHash['SkipBuild'] = $true }
if ($RunQemu) { $argHash['RunQemu'] = $true }
if ($M14RequireJournalParity) { $argHash['M14RequireJournalParity'] = $true }
if ($M14SkipJournalExtract) { $argHash['M14SkipJournalExtract'] = $true }
if ($M14ImagePath) { $argHash['M14ImagePath'] = $M14ImagePath }
if ($M16ExtractMetrics) { $argHash['M16ExtractMetrics'] = $true }
if ($M16SkipExtract) { $argHash['M16SkipExtract'] = $true }
if ($M16UpdateBaseline) { $argHash['M16UpdateBaseline'] = $true }
if ($M16RejectOnDrift) { $argHash['M16RejectOnDrift'] = $true }
if ($M17EnableCIReport) { $argHash['M17EnableCIReport'] = $true }
if ($M17FailOnDrift) { $argHash['M17FailOnDrift'] = $true }
if ($M19EnableBenchmarkPack) { $argHash['M19EnableBenchmarkPack'] = $true }
if ($M19FailOnRegression) { $argHash['M19FailOnRegression'] = $true }

& $orchestratorScript @argHash
exit $LASTEXITCODE
