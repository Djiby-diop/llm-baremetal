param(
  [string]$InputJsonl = 'artifacts/m19/current/benchmark-input.jsonl',
  [string]$OutputPath = 'llmk-autorun-bench.txt',
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $repoRoot

function Write-Step([string]$msg) { if (-not $Quiet) { Write-Host "[M19.1] $msg" -ForegroundColor Cyan } }
function Write-Ok([string]$msg) { if (-not $Quiet) { Write-Host "[M19.1][OK] $msg" -ForegroundColor Green } }
function Write-Warn([string]$msg) { Write-Host "[M19.1][WARN] $msg" -ForegroundColor Yellow }

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

$inputFull = Join-Path $repoRoot $InputJsonl
if (-not (Test-Path -LiteralPath $inputFull)) {
  throw "Input JSONL not found: $InputJsonl"
}

Write-Step "Generating benchmark autorun from $InputJsonl"
$cases = Read-JsonLines -path $inputFull
if ($null -eq $cases -or $cases.Count -eq 0) {
  throw "Benchmark input is empty: $InputJsonl"
}

# Sampling defaults are taken from the first case (pack uses a single shared profile).
$seed = 42
$temperature = 0.8
$top_p = 0.95
$top_k = 40

$first = $cases[0]
if ($first.sampling) {
  if ($first.sampling.seed -ne $null) { $seed = [int]$first.sampling.seed }
  if ($first.sampling.temperature -ne $null) { $temperature = [double]$first.sampling.temperature }
  if ($first.sampling.top_p -ne $null) { $top_p = [double]$first.sampling.top_p }
  if ($first.sampling.top_k -ne $null) { $top_k = [int]$first.sampling.top_k }
}

$lines = @()
$lines += '/bench_begin LLMK_BEN.JNL'
$lines += ("/seed {0}" -f $seed)
$lines += ("/temp {0}" -f ([Math]::Round($temperature, 3)))
$lines += ("/top_p {0}" -f ([Math]::Round($top_p, 3)))
$lines += ("/top_k {0}" -f $top_k)

foreach ($c in $cases) {
  $caseId = [string]$c.case_id
  $category = [string]$c.category
  $maxNew = if ($c.max_new_tokens) { [int]$c.max_new_tokens } else { 64 }
  $prompt = [string]$c.prompt

  if ([string]::IsNullOrWhiteSpace($caseId) -or [string]::IsNullOrWhiteSpace($category) -or [string]::IsNullOrWhiteSpace($prompt)) {
    Write-Warn "Skipping invalid case (missing fields)"
    continue
  }

  # Autorun is line-based: ensure single-line prompt.
  $prompt = $prompt -replace "\r\n|\n|\r", ' '
  if ($prompt.StartsWith('/')) {
    $prompt = ' ' + $prompt
  }

  $lines += ("/bench_case {0} {1} {2} {3}" -f $caseId, $category, $maxNew, $prompt)
}

$lines += '/bench_end'

$outFull = Join-Path $repoRoot $OutputPath
$dir = Split-Path -Parent $outFull
if ($dir -and -not (Test-Path -LiteralPath $dir)) {
  New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

Set-Content -LiteralPath $outFull -Value ($lines -join "`r`n") -Encoding ASCII
Write-Ok "Wrote autorun: $OutputPath"
Write-Ok "Cases: $($cases.Count)"
exit 0
