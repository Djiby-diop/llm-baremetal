param(
  [string]$CorpusPath = '.ops/benchmarks/m19-corpus.json',
  [string]$OutputDir = 'artifacts/m19/current',
  [string]$ResultsPath = '',
  [string]$Model = 'stories15M.q8_0.gguf',
  [string]$Profile = 'default',
  [int]$Seed = 42,
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $repoRoot

function Write-Step([string]$msg) { if (-not $Quiet) { Write-Host "[M19] $msg" -ForegroundColor Cyan } }
function Write-Ok([string]$msg) { if (-not $Quiet) { Write-Host "[M19][OK] $msg" -ForegroundColor Green } }
function Write-Warn([string]$msg) { Write-Host "[M19][WARN] $msg" -ForegroundColor Yellow }

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

function Write-JsonLines([string]$path, [object[]]$rows) {
  $dir = Split-Path -Parent $path
  if ($dir -and -not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
  }

  $buffer = @()
  foreach ($row in $rows) {
    $buffer += ($row | ConvertTo-Json -Compress -Depth 20)
  }
  Set-Content -LiteralPath $path -Value ($buffer -join "`n") -Encoding UTF8
}

function Compute-Summary([object[]]$rows) {
  $valid = @($rows | Where-Object { $_.case_id })
  if ($valid.Count -eq 0) {
    return @{
      cases = 0
      with_latency = 0
      with_decode_cpt = 0
      latency_ms_p50 = 0.0
      decode_cycles_per_token_p50 = 0.0
    }
  }

  $latencies = @($valid | Where-Object { $_.latency_ms -ne $null } | ForEach-Object { [double]$_.latency_ms } | Sort-Object)
  $decodeCpt = @(
    $valid | ForEach-Object {
      if ($_.decode_cycles -ne $null -and $_.decode_tokens -ne $null -and [double]$_.decode_tokens -gt 0) {
        [double]$_.decode_cycles / [double]$_.decode_tokens
      }
    } | Where-Object { $_ -ne $null } | Sort-Object
  )

  $latP50 = if ($latencies.Count -gt 0) { $latencies[[Math]::Min([Math]::Floor($latencies.Count * 0.5), $latencies.Count - 1)] } else { 0.0 }
  $decP50 = if ($decodeCpt.Count -gt 0) { $decodeCpt[[Math]::Min([Math]::Floor($decodeCpt.Count * 0.5), $decodeCpt.Count - 1)] } else { 0.0 }

  return @{
    cases = $valid.Count
    with_latency = $latencies.Count
    with_decode_cpt = $decodeCpt.Count
    latency_ms_p50 = [Math]::Round([double]$latP50, 2)
    decode_cycles_per_token_p50 = [Math]::Round([double]$decP50, 2)
  }
}

Write-Step 'Preparing reproducible benchmark pack'

$corpusFull = Join-Path $repoRoot $CorpusPath
if (-not (Test-Path -LiteralPath $corpusFull)) {
  throw "Corpus file not found: $corpusFull"
}

try {
  $cases = Get-Content -LiteralPath $corpusFull -Raw | ConvertFrom-Json -ErrorAction Stop
} catch {
  throw "Invalid corpus JSON: $CorpusPath"
}

if ($null -eq $cases -or $cases.Count -eq 0) {
  throw "Benchmark corpus is empty: $CorpusPath"
}

$outDirFull = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Path $outDirFull -Force | Out-Null

$commit = 'unknown'
try {
  $commit = (git rev-parse --short HEAD 2>$null)
  if (-not $commit) { $commit = 'unknown' }
} catch {
  $commit = 'unknown'
}

$timestamp = (Get-Date).ToUniversalTime().ToString('o')

$pack = @{
  milestone = 'M19'
  timestamp = $timestamp
  commit = $commit
  model = $Model
  profile = $Profile
  seed = $Seed
  corpus_path = $CorpusPath
  case_count = $cases.Count
  cases = @($cases)
}

$packPath = Join-Path $outDirFull 'benchmark-pack.json'
$pack | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $packPath -Encoding UTF8

$requests = @()
foreach ($c in $cases) {
  $requests += @{
    case_id = [string]$c.id
    category = [string]$c.category
    prompt = [string]$c.prompt
    max_new_tokens = if ($c.max_new_tokens) { [int]$c.max_new_tokens } else { 64 }
    sampling = @{
      seed = $Seed
      temperature = 0.8
      top_p = 0.95
      top_k = 40
    }
  }
}

$requestsPath = Join-Path $outDirFull 'benchmark-input.jsonl'
Write-JsonLines -path $requestsPath -rows $requests

$manifest = @()
$manifest += '# M19 Benchmark Pack'
$manifest += ''
$manifest += "- timestamp: $timestamp"
$manifest += "- commit: $commit"
$manifest += "- model: $Model"
$manifest += "- profile: $Profile"
$manifest += "- seed: $Seed"
$manifest += "- corpus: $CorpusPath"
$manifest += "- case_count: $($cases.Count)"
$manifest += ''
$manifest += '| case_id | category | max_new_tokens |'
$manifest += '|---|---:|---:|'
foreach ($c in $cases) {
  $manifest += "| $($c.id) | $($c.category) | $($c.max_new_tokens) |"
}

if ($ResultsPath) {
  $resultsFull = Join-Path $repoRoot $ResultsPath
  if (Test-Path -LiteralPath $resultsFull) {
    $rows = Read-JsonLines -path $resultsFull
    $summary = Compute-Summary -rows $rows
    $summaryPath = Join-Path $outDirFull 'benchmark-summary.json'
    $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

    $manifest += ''
    $manifest += '## Current Results Summary'
    $manifest += ''
    $manifest += "- result_rows: $($summary.cases)"
    $manifest += "- latency_ms_p50: $($summary.latency_ms_p50)"
    $manifest += "- decode_cycles_per_token_p50: $($summary.decode_cycles_per_token_p50)"
  } else {
    Write-Warn "ResultsPath not found (summary skipped): $ResultsPath"
  }
}

$manifestPath = Join-Path $outDirFull 'benchmark-manifest.md'
Set-Content -LiteralPath $manifestPath -Value ($manifest -join "`n") -Encoding UTF8

Write-Ok "Benchmark pack written: $OutputDir"
Write-Ok 'Files: benchmark-pack.json, benchmark-input.jsonl, benchmark-manifest.md'
exit 0
