param(
  [string]$ImagePath = 'llm-baremetal-boot.img',
  [string]$BenchFilename = 'LLMK_BEN.JNL',
  [string]$PartitionOffset = '1M',
  [string]$OutputPath = 'artifacts/m19/current/results.jsonl',
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $repoRoot

function Write-Step([string]$msg) { if (-not $Quiet) { Write-Host "[M19.1-Extract] $msg" -ForegroundColor Cyan } }
function Write-Ok([string]$msg) { if (-not $Quiet) { Write-Host "[M19.1-Extract][OK] $msg" -ForegroundColor Green } }
function Write-Warn([string]$msg) { Write-Host "[M19.1-Extract][WARN] $msg" -ForegroundColor Yellow }

function Get-WslPath([string]$windowsPath) {
  $normalized = $windowsPath -replace '\\', '/'
  $p = wsl wslpath -a -u "$normalized"
  if (-not $p) { return "" }
  return $p.Trim()
}

$imgFull = Join-Path $repoRoot $ImagePath
if (-not (Test-Path -LiteralPath $imgFull)) {
  Write-Warn "Image not found: $ImagePath"
  exit 1
}

$outFull = Join-Path $repoRoot $OutputPath
$outDir = Split-Path -Parent $outFull
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# Ensure extraction is non-interactive: if the output file exists, remove it first.
if (Test-Path -LiteralPath $outFull) {
  Remove-Item -LiteralPath $outFull -Force -ErrorAction SilentlyContinue
}

Write-Step "Extracting $BenchFilename from $ImagePath"

$imgWsl = Get-WslPath $imgFull
$outWsl = Get-WslPath $outFull
if (-not $imgWsl) {
  Write-Warn "Failed to resolve WSL path for image: $imgFull"
  exit 1
}
if (-not $outWsl) {
  Write-Warn "Failed to resolve WSL path for output: $outFull"
  exit 1
}

$imgMtools = "$imgWsl@@$PartitionOffset"

$extractCmd = "mcopy -o -i $imgMtools ::$BenchFilename $outWsl 2>&1"
$extractResult = wsl bash -lc $extractCmd

if ($LASTEXITCODE -ne 0) {
  Write-Warn "mcopy failed (exit=$LASTEXITCODE)"
  if ($extractResult) { Write-Warn $extractResult }
  exit 1
}

if (-not (Test-Path -LiteralPath $outFull)) {
  Write-Warn "Failed to extract: $BenchFilename"
  if ($extractResult) { Write-Warn $extractResult }
  exit 1
}

# Validate JSONL (best-effort): ensure each non-empty line parses.
$ok = 0
$bad = 0
foreach ($line in (Get-Content -LiteralPath $outFull -ErrorAction SilentlyContinue)) {
  if ([string]::IsNullOrWhiteSpace($line)) { continue }
  try {
    $obj = $line | ConvertFrom-Json -ErrorAction Stop
    if ($obj -and $obj.case_id) { $ok++ } else { $bad++ }
  } catch {
    $bad++
  }
}

if ($ok -eq 0 -or $bad -gt 0) {
  Write-Warn "Extracted file has parse issues (ok=$ok bad=$bad): $OutputPath"
  exit 1
}

Write-Ok "Extracted to: $OutputPath"
Write-Ok "Rows: $ok"
exit 0
