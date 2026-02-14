param(
  [string]$ImagePath = "",
  [string]$OutputPath = "artifacts/m14/OOJOUR.LOG",
  [string]$StatePath = "artifacts/m14/extract-state.json",
  [string]$HistoryPath = "artifacts/m14/extract-history.jsonl",
  [switch]$FailIfMissing,
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M14.1] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M14.1][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M14.1][WARN] $msg" -ForegroundColor Yellow }

function Resolve-ImagePath([string]$overridePath) {
  if ($overridePath) {
    return $overridePath
  }

  $candidates = Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending
  if ($candidates -and $candidates.Count -gt 0) {
    return $candidates[0].FullName
  }

  return (Join-Path $PSScriptRoot 'llm-baremetal-boot.img')
}

function Get-WslPath([string]$windowsPath) {
  if (-not $windowsPath) { return $null }

  $full = $windowsPath
  if (-not [System.IO.Path]::IsPathRooted($full)) {
    $full = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $full))
  }

  $normalized = $full -replace '\\', '/'
  if ($normalized -match '^([A-Za-z]):/(.*)$') {
    $drive = $matches[1].ToLowerInvariant()
    $rest = $matches[2]
    return "/mnt/$drive/$rest"
  }

  return $null
}

$image = Resolve-ImagePath -overridePath $ImagePath
if (-not (Test-Path -LiteralPath $image)) {
  throw "M14.1 image not found: $image"
}

$outDir = Split-Path -Parent $OutputPath
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$stateDir = Split-Path -Parent $StatePath
if ($stateDir -and -not (Test-Path -LiteralPath $stateDir)) {
  New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
}

$historyDir = Split-Path -Parent $HistoryPath
if ($historyDir -and -not (Test-Path -LiteralPath $historyDir)) {
  New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
}

if (Test-Path -LiteralPath $OutputPath) {
  Remove-Item -LiteralPath $OutputPath -Force
}

$outputAbs = if ([System.IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutputPath)) }

$imageWsl = Get-WslPath -windowsPath $image
$outWsl = Get-WslPath -windowsPath $outputAbs

if (-not $imageWsl -or -not $outWsl) {
  throw 'M14.1 failed to convert paths to WSL form'
}

Write-Step "Extracting OOJOUR from image: $image"

$extracted = $false
$attempts = @('OOJOUR.LOG', 'oojour.log')
$lastError = ''

foreach ($entryName in $attempts) {
  $cmd = "set -e; mcopy -n -i '$imageWsl' '::$entryName' '$outWsl'"
  & wsl bash -lc $cmd 2>$null
  if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $OutputPath)) {
    $extracted = $true
    break
  }
  $lastError = "mcopy failed for entry $entryName"
}

$fileSize = 0
if ($extracted) {
  $fileSize = (Get-Item -LiteralPath $OutputPath).Length
  Write-Ok "OOJOUR extracted: $OutputPath ($fileSize bytes)"
} else {
  Write-Warn "OOJOUR not found in image (or extract failed): $lastError"
}

$state = [ordered]@{
  ts_utc = (Get-Date).ToUniversalTime().ToString('o')
  image_path = $image
  output_path = $OutputPath
  extracted = $extracted
  file_size_bytes = $fileSize
  fail_if_missing = [bool]$FailIfMissing
}

$state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $StatePath -Encoding UTF8
Write-Ok "Extract state written: $StatePath"

if (-not $NoHistoryWrite) {
  ($state | ConvertTo-Json -Compress) | Add-Content -LiteralPath $HistoryPath
  Write-Ok "Extract history appended: $HistoryPath"
}

if ($FailIfMissing -and (-not $extracted)) {
  throw 'M14.1 OOJOUR extraction failed (required)'
}

Write-Ok 'M14.1 OOJOUR extraction complete'
