[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$ArtifactsDir,
  [string]$OutPath,
  [string]$ModelBin = '',
  [string]$SourceLabel = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$artifactsRoot = Join-Path $root 'artifacts'

if (-not $PSBoundParameters.ContainsKey('ArtifactsDir') -or -not $ArtifactsDir) {
  $latest = Get-ChildItem -LiteralPath $artifactsRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'real-hw-oo-*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if (-not $latest) {
    throw "No real-hw-oo-* artifact directory found under $artifactsRoot"
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

if (-not (Test-Path -LiteralPath $summaryPath)) {
  throw "Missing summary file: $summaryPath"
}

$summary = Get-Content -LiteralPath $summaryPath -Raw
$consult = if (Test-Path -LiteralPath $consultPath) { Get-Content -LiteralPath $consultPath -Raw } else { '' }
$jour = if (Test-Path -LiteralPath $jourPath) { Get-Content -LiteralPath $jourPath -Raw } else { '' }

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
) + $artifactLines + @(
  '',
  '## Consult Summary',
  '',
  "- Decision: $(if ($consultDecision) { $consultDecision } else { 'unknown' })",
  "- Applied: $(if ($consultApplied) { $consultApplied } else { 'unknown' })",
  "- Confidence score: $(if ($consultScore) { $consultScore } else { 'unknown' })",
  "- Confidence threshold: $(if ($consultThreshold) { $consultThreshold } else { 'unknown' })",
  '',
  '## Journal Events',
  ''
) + $eventLines + @(
  '',
  '## Raw Consult Log',
  '',
  '```text',
  $(if ($consult) { $consult.TrimEnd("`r", "`n") } else { '(missing OOCONSULT.LOG)' }),
  '```',
  ''
)

Set-Content -LiteralPath $OutPath -Value ($report -join "`n") -Encoding UTF8 -NoNewline
Write-Host "[OO Report] Wrote: $OutPath" -ForegroundColor Green