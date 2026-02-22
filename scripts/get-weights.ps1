[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Url,

  [string]$OutName = '',

  [string]$Sha256 = '',

  [string]$DestDir = 'models',

  [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)

function Fail([string]$msg) {
  Write-Host "ERROR: $msg" -ForegroundColor Red
  exit 1
}

try {
  $uri = [System.Uri]$Url
} catch {
  Fail "Invalid URL: $Url"
}

if (-not $OutName) {
  $leaf = Split-Path -Leaf $uri.AbsolutePath
  if (-not $leaf) {
    Fail "Cannot infer OutName from URL path; pass -OutName"
  }
  $OutName = $leaf
}

if ($OutName -match '[\\/]' ) {
  Fail "OutName must be a file name, not a path: $OutName"
}

if (-not (Test-Path -LiteralPath $DestDir)) {
  New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
}

$outPath = Join-Path $DestDir $OutName

if ((Test-Path -LiteralPath $outPath) -and (-not $Force)) {
  Write-Host "[OK] Exists: $outPath" -ForegroundColor Green
  Write-Host "Use -Force to re-download." -ForegroundColor Gray
  exit 0
}

$tmpPath = $outPath + '.download'
if (Test-Path -LiteralPath $tmpPath) {
  Remove-Item -LiteralPath $tmpPath -Force -ErrorAction SilentlyContinue
}

Write-Host "Downloading: $Url" -ForegroundColor Cyan
Write-Host "To:          $outPath" -ForegroundColor Gray

try {
  Invoke-WebRequest -Uri $Url -OutFile $tmpPath -UseBasicParsing | Out-Null
} catch {
  Fail "Download failed: $($_.Exception.Message)"
}

if (-not (Test-Path -LiteralPath $tmpPath)) {
  Fail "Download did not produce expected file: $tmpPath"
}

if ($Sha256) {
  $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $tmpPath).Hash.ToLowerInvariant()
  $expected = $Sha256.Trim().ToLowerInvariant()
  if ($actual -ne $expected) {
    Remove-Item -LiteralPath $tmpPath -Force -ErrorAction SilentlyContinue
    Fail "SHA256 mismatch for $OutName (expected $expected, got $actual)"
  }
  Write-Host "[OK] SHA256 verified" -ForegroundColor Green
}

Move-Item -LiteralPath $tmpPath -Destination $outPath -Force
Write-Host "[OK] Downloaded: $outPath" -ForegroundColor Green
