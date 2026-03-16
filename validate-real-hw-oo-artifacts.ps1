[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$ArtifactsDir,
  [switch]$RequireDiag,
  [switch]$RequireHandoff,
  [switch]$AllowNoConsult
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

function Assert-Condition([bool]$condition, [string]$message) {
  if (-not $condition) {
    throw $message
  }
}

function Test-Present([string]$path) {
  return (Test-Path -LiteralPath $path)
}

$summaryPath = Join-Path $ArtifactsDir 'oo-artifacts-summary.txt'
$consultPath = Join-Path $ArtifactsDir 'OOCONSULT.LOG'
$jourPath = Join-Path $ArtifactsDir 'OOJOUR.LOG'
$statePath = Join-Path $ArtifactsDir 'OOSTATE.BIN'
$recovPath = Join-Path $ArtifactsDir 'OORECOV.BIN'
$handoffPath = Join-Path $ArtifactsDir 'OOHANDOFF.TXT'
$diagPath = Join-Path $ArtifactsDir 'llmk-diag.txt'

Assert-Condition (Test-Present $summaryPath) "Missing summary file: $summaryPath"
Assert-Condition (Test-Present $statePath) "Missing OOSTATE.BIN"
Assert-Condition (Test-Present $recovPath) "Missing OORECOV.BIN"

$summary = Get-Content -LiteralPath $summaryPath -Raw
$jourPresent = Test-Present $jourPath
$consultPresent = Test-Present $consultPath
$handoffPresent = Test-Present $handoffPath
$diagPresent = Test-Present $diagPath

Assert-Condition ($summary -match 'OOSTATE\.BIN: present=1') 'Summary missing OOSTATE.BIN present=1'
Assert-Condition ($summary -match 'OORECOV\.BIN: present=1') 'Summary missing OORECOV.BIN present=1'

if ($RequireHandoff) {
  Assert-Condition $handoffPresent 'OOHANDOFF.TXT is required but missing'
}
if ($RequireDiag) {
  Assert-Condition $diagPresent 'llmk-diag.txt is required but missing'
}

Assert-Condition $jourPresent 'Missing OOJOUR.LOG'
Assert-Condition ($summary -match 'OOJOUR\.LOG: present=1') 'Summary missing OOJOUR.LOG present=1'

$jour = Get-Content -LiteralPath $jourPath -Raw
Assert-Condition ($jour -match 'oo event=') 'OOJOUR.LOG has no journal events'

if ($consultPresent) {
  Assert-Condition ($summary -match 'OOCONSULT\.LOG: present=1') 'Summary missing OOCONSULT.LOG present=1'
  $consult = Get-Content -LiteralPath $consultPath -Raw
  Assert-Condition ($consult -match 'consult b=') 'OOCONSULT.LOG missing consult record'
  Assert-Condition ($consult -match ' d=') 'OOCONSULT.LOG missing decision field'
  Assert-Condition ($consult -match ' sc=') 'OOCONSULT.LOG missing confidence score field'
  Assert-Condition ($jour -match 'oo event=cmd=oo_consult') 'OOJOUR.LOG missing oo_consult command marker'
  Assert-Condition ($jour -match 'oo event=consult|oo event=consult_multi') 'OOJOUR.LOG missing consult completion marker'
} elseif (-not $AllowNoConsult) {
  throw 'Missing OOCONSULT.LOG (use -AllowNoConsult to permit this)'
}

Write-Host "[OO Validate] Artifacts: $ArtifactsDir" -ForegroundColor Cyan
Write-Host ("[OO Validate] OOSTATE.BIN  : {0} bytes" -f (Get-Item -LiteralPath $statePath).Length) -ForegroundColor Gray
Write-Host ("[OO Validate] OORECOV.BIN  : {0} bytes" -f (Get-Item -LiteralPath $recovPath).Length) -ForegroundColor Gray
Write-Host ("[OO Validate] OOJOUR.LOG   : {0}" -f ($(if ($jourPresent) { (Get-Item -LiteralPath $jourPath).Length } else { 0 }))) -ForegroundColor Gray
Write-Host ("[OO Validate] OOCONSULT.LOG: {0}" -f ($(if ($consultPresent) { (Get-Item -LiteralPath $consultPath).Length } else { 0 }))) -ForegroundColor Gray
Write-Host ("[OO Validate] OOHANDOFF.TXT: {0}" -f ($(if ($handoffPresent) { 'present' } else { 'absent' }))) -ForegroundColor Gray
Write-Host ("[OO Validate] llmk-diag.txt: {0}" -f ($(if ($diagPresent) { 'present' } else { 'absent' }))) -ForegroundColor Gray
Write-Host '[OO Validate] PASS' -ForegroundColor Green