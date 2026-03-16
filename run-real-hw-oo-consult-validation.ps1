[CmdletBinding(PositionalBinding = $false)]
param(
  [ValidateSet('prepare','collect')]
  [string]$Phase = 'prepare',
  [string]$ModelBin = 'stories110M.bin',
  [string[]]$ExtraModelBins = @(),
  [string]$UsbRoot,
  [string]$ImagePath,
  [string]$ArtifactsDir,
  [switch]$SkipPrebuild,
  [switch]$RequireDiag,
  [switch]$RequireHandoff
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$prepareScript = Join-Path $root 'prepare-real-hw-chat.ps1'
$collectScript = Join-Path $root 'collect-real-hw-oo-artifacts.ps1'
$validateScript = Join-Path $root 'validate-real-hw-oo-artifacts.ps1'
$reportScript = Join-Path $root 'write-real-hw-oo-validation-report.ps1'

foreach ($path in @($prepareScript, $collectScript, $validateScript, $reportScript)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required helper: $path"
  }
}

if ($Phase -eq 'prepare') {
  $prepareArgs = @{
    ModelBin = $ModelBin
  }
  if ($ExtraModelBins.Count -gt 0) {
    $prepareArgs['ExtraModelBins'] = $ExtraModelBins
  }
  if ($SkipPrebuild) {
    $prepareArgs['SkipPrebuild'] = $true
  }
  $prepareArgs['AutoOoConsultSmoke'] = $true

  & $prepareScript @prepareArgs

  Write-Host ''
  Write-Host '[OO Real] Next steps on the physical machine:' -ForegroundColor Cyan
  Write-Host '  1. Flash or copy the generated real-hardware chat image to the USB device.' -ForegroundColor Gray
  Write-Host '  2. Boot the target machine.' -ForegroundColor Gray
  Write-Host '  3. Let the autorun complete /oo_consult and /oo_log.' -ForegroundColor Gray
  Write-Host '  4. Mount the FAT partition again on the host.' -ForegroundColor Gray
  Write-Host '  5. Run this same script with -Phase collect -UsbRoot <drive>.' -ForegroundColor Gray
  exit 0
}

if (-not $UsbRoot -and -not $ImagePath) {
  throw 'For -Phase collect, provide -UsbRoot <mounted FAT root> or -ImagePath <boot image>.'
}

if ($UsbRoot -and $ImagePath) {
  throw 'For -Phase collect, use either -UsbRoot or -ImagePath, not both.'
}

$collectArgs = @{}
if ($ArtifactsDir) {
  $collectArgs['OutDir'] = $ArtifactsDir
}
if ($UsbRoot) {
  $collectArgs['UsbRoot'] = $UsbRoot
} else {
  $collectArgs['ImagePath'] = $ImagePath
}

& $collectScript @collectArgs

$targetArtifacts = if ($ArtifactsDir) {
  $ArtifactsDir
} else {
  $latest = Get-ChildItem -LiteralPath (Join-Path $root 'artifacts') -Directory |
    Where-Object { $_.Name -like 'real-hw-oo-*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if (-not $latest) {
    throw 'Collector completed but no artifact directory was found.'
  }
  $latest.FullName
}

$validateArgs = @{
  ArtifactsDir = $targetArtifacts
}
if ($RequireDiag) {
  $validateArgs['RequireDiag'] = $true
}
if ($RequireHandoff) {
  $validateArgs['RequireHandoff'] = $true
}

& $validateScript @validateArgs

$reportArgs = @{
  ArtifactsDir = $targetArtifacts
  ModelBin = $ModelBin
}
if ($UsbRoot) {
  $reportArgs['SourceLabel'] = [System.IO.Path]::GetFullPath($UsbRoot)
} elseif ($ImagePath) {
  $reportArgs['SourceLabel'] = [System.IO.Path]::GetFullPath($ImagePath)
}

& $reportScript @reportArgs

Write-Host ''
Write-Host "[OO Real] Validation complete: $targetArtifacts" -ForegroundColor Green