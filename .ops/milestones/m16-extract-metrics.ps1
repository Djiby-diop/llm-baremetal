param(
  [string]$ImagePath = "llm-baremetal-boot.img",
  [string]$PartitionOffset = '1M',
  [string]$OutputDir = "artifacts/m16/raw",
  [string]$OutputFilename = "",  # If empty, auto-generate with timestamp
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# Resolve repo root (this script is in .ops/milestones/)
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $repoRoot

function Write-Step([string]$msg) { if (-not $Quiet) { Write-Host "[M16-Extract] $msg" -ForegroundColor Cyan } }
function Write-Ok([string]$msg) { if (-not $Quiet) { Write-Host "[M16-Extract][OK] $msg" -ForegroundColor Green } }
function Write-Warn([string]$msg) { Write-Host "[M16-Extract][WARN] $msg" -ForegroundColor Yellow }

function Get-WslPath([string]$windowsPath) {
  $normalized = $windowsPath -replace '\\', '/'
  $p = wsl wslpath -a -u "$normalized"
  if (-not $p) { return "" }
  return $p.Trim()
}

# Ensure output directory exists
$outDir = Join-Path $repoRoot $OutputDir
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# Check if image exists
$imgPath = Join-Path $repoRoot $ImagePath
if (-not (Test-Path -LiteralPath $imgPath)) {
  Write-Warn "Image not found: $imgPath"
  exit 1
}

Write-Step "Extracting LLMK_METRICS.LOG from $ImagePath"

# Generate output filename with timestamp if not specified
if (-not $OutputFilename) {
  $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
  $OutputFilename = "metrics_$timestamp.json"
}

$outFile = Join-Path $outDir $OutputFilename


$imgWsl = Get-WslPath $imgPath
$outWsl = Get-WslPath $outFile
if (-not $imgWsl) {
  Write-Warn "Failed to resolve WSL path for image: $imgPath"
  exit 1
}
if (-not $outWsl) {
  Write-Warn "Failed to resolve WSL path for output: $outFile"
  exit 1
}

$imgMtools = "$imgWsl@@$PartitionOffset"

# Extract file using mtools mcopy
$extractCmd = "mcopy -i $imgMtools ::LLMK_METRICS.LOG $outWsl 2>&1"
$extractResult = wsl bash -lc $extractCmd

if ($LASTEXITCODE -ne 0) {
  Write-Warn "mcopy failed (exit=$LASTEXITCODE)"
  if ($extractResult) { Write-Warn $extractResult }
  exit 1
}

if (-not (Test-Path -LiteralPath $outFile)) {
  Write-Warn "Failed to extract metrics file"
  exit 1
}

Write-Ok "Extracted to: $outFile"

# Validate JSON format
try {
  $metrics = Get-Content -LiteralPath $outFile -Raw | ConvertFrom-Json
  $tokenCount = ([int]$metrics.total_prefill_tokens + [int]$metrics.total_decode_tokens)
  Write-Ok "Valid JSON - Total tokens: $tokenCount"
} catch {
  Write-Warn "Invalid JSON format in extracted metrics file"
  exit 1
}

Write-Ok "Extraction complete"
exit 0
