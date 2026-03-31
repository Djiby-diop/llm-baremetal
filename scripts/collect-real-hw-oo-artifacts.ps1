[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$UsbRoot,
  [string]$ImagePath,
  [string]$OutDir,
  [string[]]$FileNames = @(
    'OOCONSULT.LOG',
    'OOJOUR.LOG',
    'OOSTATE.BIN',
    'OORECOV.BIN',
    'OOHANDOFF.TXT',
    'llmk-diag.txt'
  )
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath

if (-not $UsbRoot -and -not $ImagePath) {
  throw 'Provide either -UsbRoot <mounted FAT root> or -ImagePath <boot image>.'
}

if ($UsbRoot -and $ImagePath) {
  throw 'Use either -UsbRoot or -ImagePath, not both.'
}

if (-not $PSBoundParameters.ContainsKey('OutDir')) {
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  $OutDir = Join-Path $root (Join-Path 'artifacts' "real-hw-oo-$stamp")
}

$OutDir = [System.IO.Path]::GetFullPath($OutDir)
if (-not (Test-Path -LiteralPath $OutDir)) {
  New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

function Convert-WindowsPathToWsl([string]$path) {
  $full = [System.IO.Path]::GetFullPath($path)
  $drive = $full.Substring(0, 1).ToLowerInvariant()
  $rest = $full.Substring(2) -replace '\\', '/'
  return "/mnt/$drive$rest"
}

function Copy-FromUsbRoot([string]$sourceRoot, [string]$name, [string]$destinationDir) {
  $sourcePath = Join-Path $sourceRoot $name
  if (-not (Test-Path -LiteralPath $sourcePath)) {
    return $false
  }
  Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $destinationDir $name) -Force
  return $true
}

function Copy-FromImage([string]$imagePath, [string]$name, [string]$destinationDir) {
  $imageFull = [System.IO.Path]::GetFullPath($imagePath)
  if (-not (Test-Path -LiteralPath $imageFull)) {
    throw "Missing image: $imageFull"
  }

  $dest = Join-Path $destinationDir $name
  if (Test-Path -LiteralPath $dest) {
    Remove-Item -LiteralPath $dest -Force -ErrorAction SilentlyContinue
  }

  $imageWsl = Convert-WindowsPathToWsl $imageFull
  $destWsl = Convert-WindowsPathToWsl $dest
  $bash = "rm -f '$destWsl'; mcopy -o -i '$imageWsl@@1M' '::$name' '$destWsl' >/dev/null 2>&1"
  & wsl bash -lc $bash
  if ($LASTEXITCODE -ne 0) {
    return $false
  }
  return (Test-Path -LiteralPath $dest)
}

$sourceLabel = if ($UsbRoot) { [System.IO.Path]::GetFullPath($UsbRoot) } else { [System.IO.Path]::GetFullPath($ImagePath) }
Write-Host "[OO Collect] Source: $sourceLabel" -ForegroundColor Cyan
Write-Host "[OO Collect] OutDir: $OutDir" -ForegroundColor Gray

$results = [System.Collections.Generic.List[object]]::new()

foreach ($name in $FileNames) {
  if (-not $name) { continue }
  $copied = if ($UsbRoot) {
    Copy-FromUsbRoot -sourceRoot ([System.IO.Path]::GetFullPath($UsbRoot)) -name $name -destinationDir $OutDir
  } else {
    Copy-FromImage -imagePath $ImagePath -name $name -destinationDir $OutDir
  }

  $destPath = Join-Path $OutDir $name
  $bytes = 0
  if ($copied -and (Test-Path -LiteralPath $destPath)) {
    $bytes = (Get-Item -LiteralPath $destPath).Length
    Write-Host ("[OO Collect] OK: {0} ({1} bytes)" -f $name, $bytes) -ForegroundColor Green
  } else {
    Write-Host ("[OO Collect] MISS: {0}" -f $name) -ForegroundColor Yellow
  }

  $results.Add([pscustomobject]@{
    Name = $name
    Present = [bool]$copied
    Bytes = $bytes
  }) | Out-Null
}

$summaryPath = Join-Path $OutDir 'oo-artifacts-summary.txt'
$summaryLines = [System.Collections.Generic.List[string]]::new()
$summaryLines.Add("source=$sourceLabel")
$summaryLines.Add("out_dir=$OutDir")
foreach ($item in $results) {
  $summaryLines.Add(("{0}: present={1} bytes={2}" -f $item.Name, ($item.Present ? 1 : 0), $item.Bytes))
}
Set-Content -LiteralPath $summaryPath -Value ($summaryLines -join "`n") -Encoding ASCII -NoNewline

Write-Host "[OO Collect] Summary: $summaryPath" -ForegroundColor Green