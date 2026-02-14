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
  [int]$M151TopReasonIds = 5
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
}

if ($SkipPreflight) { $argHash['SkipPreflight'] = $true }
if ($SkipBuild) { $argHash['SkipBuild'] = $true }
if ($RunQemu) { $argHash['RunQemu'] = $true }
if ($M14RequireJournalParity) { $argHash['M14RequireJournalParity'] = $true }
if ($M14SkipJournalExtract) { $argHash['M14SkipJournalExtract'] = $true }
if ($M14ImagePath) { $argHash['M14ImagePath'] = $M14ImagePath }

& $orchestratorScript @argHash
exit $LASTEXITCODE
