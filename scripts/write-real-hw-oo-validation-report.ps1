[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$ArtifactsDir,
  [string]$OutPath,
  [string]$ModelBin = '',
  [string]$SourceLabel = '',
  [ValidateSet('consult','handoff','reboot','auto')]
  [string]$Scenario = 'auto'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$artifactsRoot = Join-Path $root 'artifacts'

if (-not $PSBoundParameters.ContainsKey('ArtifactsDir') -or -not $ArtifactsDir) {
  $latest = Get-ChildItem -LiteralPath $artifactsRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'real-hw-oo-*' -or $_.Name -like 'real-hw-handoff-*' -or $_.Name -like 'real-hw-reboot-*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if (-not $latest) {
    throw "No real-hw-oo-*, real-hw-handoff-*, or real-hw-reboot-* artifact directory found under $artifactsRoot"
  }
  $ArtifactsDir = $latest.FullName
}

if (-not [System.IO.Path]::IsPathRooted($ArtifactsDir)) {
  $repoRelative = Join-Path $root $ArtifactsDir
  if (Test-Path -LiteralPath $repoRelative) {
    $ArtifactsDir = $repoRelative
  } else {
    $ArtifactsDir = Join-Path $artifactsRoot $ArtifactsDir
  }
}

$ArtifactsDir = [System.IO.Path]::GetFullPath($ArtifactsDir)
if (-not (Test-Path -LiteralPath $ArtifactsDir)) {
  throw "Missing artifact directory: $ArtifactsDir"
}

if (-not $PSBoundParameters.ContainsKey('OutPath') -or -not $OutPath) {
  $OutPath = Join-Path $ArtifactsDir 'oo-real-validation-report.md'
}

if (-not [System.IO.Path]::IsPathRooted($OutPath)) {
  $OutPath = Join-Path $ArtifactsDir $OutPath
}

$OutPath = [System.IO.Path]::GetFullPath($OutPath)

$summaryPath = Join-Path $ArtifactsDir 'oo-artifacts-summary.txt'
$consultPath = Join-Path $ArtifactsDir 'OOCONSULT.LOG'
$jourPath = Join-Path $ArtifactsDir 'OOJOUR.LOG'
$handoffPath = Join-Path $ArtifactsDir 'OOHANDOFF.TXT'

if (-not (Test-Path -LiteralPath $summaryPath)) {
  throw "Missing summary file: $summaryPath"
}

$summary = Get-Content -LiteralPath $summaryPath -Raw
$consult = if (Test-Path -LiteralPath $consultPath) { Get-Content -LiteralPath $consultPath -Raw } else { '' }
$jour = if (Test-Path -LiteralPath $jourPath) { Get-Content -LiteralPath $jourPath -Raw } else { '' }
$handoff = if (Test-Path -LiteralPath $handoffPath) { Get-Content -LiteralPath $handoffPath -Raw } else { '' }

$summaryMap = @{}
foreach ($line in ($summary -split "`r?`n")) {
  if ($line -match '^([^:]+): present=(\d+) bytes=(\d+)$') {
    $summaryMap[$Matches[1]] = [pscustomobject]@{
      Present = ([int]$Matches[2] -ne 0)
      Bytes = [int64]$Matches[3]
    }
  }
}

$consultDecision = ''
$consultScore = ''
$consultThreshold = ''
$consultApplied = ''
if ($consult -match ' d=([^\s]+)') { $consultDecision = $Matches[1] }
if ($consult -match ' sc=([^\s]+)') { $consultScore = $Matches[1] }
if ($consult -match ' th=([^\s]+)') { $consultThreshold = $Matches[1] }
if ($consult -match ' a=([^\s]+)') { $consultApplied = $Matches[1] }

$journalEvents = @()
foreach ($line in ($jour -split "`r?`n")) {
  if ($line -match '^oo event=(.+)$') {
    $journalEvents += $Matches[1]
  }
}

$handoffMap = @{}
foreach ($line in ($handoff -split "`r?`n")) {
  if ($line -match '^([^=]+)=(.*)$') {
    $handoffMap[$Matches[1].Trim()] = $Matches[2].Trim()
  }
}

$effectiveScenario = $Scenario
if ($effectiveScenario -eq 'auto') {
  if ($handoffMap.Count -gt 0 -and -not $consult) {
    $effectiveScenario = 'handoff'
  } elseif ($journalEvents -contains 'reboot_probe_arm' -or $journalEvents -contains 'reboot_probe_verified') {
    $effectiveScenario = 'reboot'
  } else {
    $effectiveScenario = 'consult'
  }
}

$artifactLines = foreach ($name in @('OOCONSULT.LOG','OOJOUR.LOG','OOSTATE.BIN','OORECOV.BIN','OOHANDOFF.TXT','llmk-diag.txt')) {
  $entry = $summaryMap[$name]
  if ($entry) {
    ('- {0}: present={1} bytes={2}' -f $name, ([int]$entry.Present), $entry.Bytes)
  } else {
    ('- {0}: present=0 bytes=0' -f $name)
  }
}

$eventLines = if ($journalEvents.Count -gt 0) {
  $journalEvents | ForEach-Object { "- $_" }
} else {
  @('- (no journal events parsed)')
}

$handoffLines = if ($handoffMap.Count -gt 0) {
  @(
    "- Organism ID: $(if ($handoffMap.ContainsKey('organism_id')) { $handoffMap['organism_id'] } else { 'unknown' })",
    "- Mode: $(if ($handoffMap.ContainsKey('mode')) { $handoffMap['mode'] } else { 'unknown' })",
    "- Policy enforcement: $(if ($handoffMap.ContainsKey('policy_enforcement')) { $handoffMap['policy_enforcement'] } else { 'unknown' })",
    "- Continuity epoch: $(if ($handoffMap.ContainsKey('continuity_epoch')) { $handoffMap['continuity_epoch'] } else { 'unknown' })",
    "- Last recovery reason: $(if ($handoffMap.ContainsKey('last_recovery_reason')) { $handoffMap['last_recovery_reason'] } else { 'unknown' })"
  )
} else {
  @('- (missing OOHANDOFF.TXT)')
}

$rebootArmSeen = ($journalEvents -contains 'reboot_probe_arm')
$rebootVerifiedSeen = ($journalEvents -contains 'reboot_probe_verified')
$rebootFailedSeen = ($journalEvents -contains 'reboot_probe_failed')
$rebootVerdict = if ($rebootVerifiedSeen -and -not $rebootFailedSeen) { 'pass' } elseif ($rebootFailedSeen) { 'fail' } else { 'unknown' }
$rebootLines = @(
  "- Arm marker seen: $([int]$rebootArmSeen)",
  "- Verified marker seen: $([int]$rebootVerifiedSeen)",
  "- Failed marker seen: $([int]$rebootFailedSeen)",
  "- Verdict: $rebootVerdict"
)

$report = @(
  '# OO Real Hardware Validation Report',
  '',
  "- Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ssK')",
  "- ArtifactsDir: $ArtifactsDir",
  "- Source: $(if ($SourceLabel) { $SourceLabel } else { 'unknown' })",
  "- ModelBin: $(if ($ModelBin) { $ModelBin } else { 'unknown' })",
  '',
  '## Artifact Summary',
  ''
) + $artifactLines

if ($effectiveScenario -eq 'consult' -or $consult) {
  $report += @(
  '',
  '## Consult Summary',
  '',
  "- Decision: $(if ($consultDecision) { $consultDecision } else { 'unknown' })",
  "- Applied: $(if ($consultApplied) { $consultApplied } else { 'unknown' })",
  "- Confidence score: $(if ($consultScore) { $consultScore } else { 'unknown' })",
  "- Confidence threshold: $(if ($consultThreshold) { $consultThreshold } else { 'unknown' })",
  ''
  )
}

if ($effectiveScenario -eq 'handoff' -or $handoffMap.Count -gt 0) {
  $report += @(
  '',
  '## Handoff Summary',
  ''
  ) + $handoffLines + @(
  ''
  )
}

if ($effectiveScenario -eq 'reboot') {
  $report += @(
  '',
  '## Reboot Summary',
  ''
  ) + $rebootLines + @(
  ''
  )
}

$report += @(
  '## Journal Events',
  ''
) + $eventLines

if ($effectiveScenario -eq 'consult' -or $consult) {
  $report += @(
  '',
  '## Raw Consult Log',
  '',
  '```text',
  $(if ($consult) { $consult.TrimEnd("`r", "`n") } else { '(missing OOCONSULT.LOG)' }),
  '```',
  ''
  )
}

if ($effectiveScenario -eq 'handoff' -or $handoffMap.Count -gt 0) {
  $report += @(
  '',
  '## Raw Handoff Receipt',
  '',
  '```text',
  $(if ($handoff) { $handoff.TrimEnd("`r", "`n") } else { '(missing OOHANDOFF.TXT)' }),
  '```',
  ''
  )
}

Set-Content -LiteralPath $OutPath -Value ($report -join "`n") -Encoding UTF8 -NoNewline
Write-Host "[OO Report] Wrote: $OutPath" -ForegroundColor Green