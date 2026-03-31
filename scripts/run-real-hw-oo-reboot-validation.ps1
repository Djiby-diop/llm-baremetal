[CmdletBinding(PositionalBinding = $false)]
param(
  [ValidateSet('prepare','collect')]
  [string]$Phase = 'prepare',
  [string]$UsbRoot,
  [string]$ImagePath,
  [string]$ArtifactsDir,
  [switch]$SkipPrebuild,
  [switch]$RequireDiag
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$prepareScript = Join-Path $root 'prepare-real-hw-reboot.ps1'
$collectScript = Join-Path $root 'collect-real-hw-oo-artifacts.ps1'
$validateScript = Join-Path $root 'validate-real-hw-oo-artifacts.ps1'
$reportScript = Join-Path $root 'write-real-hw-oo-validation-report.ps1'

foreach ($path in @($prepareScript, $collectScript, $validateScript, $reportScript)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required helper: $path"
  }
}

function Resolve-LatestArtifacts([string]$pattern) {
  $latest = Get-ChildItem -LiteralPath (Join-Path $root 'artifacts') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like $pattern } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if (-not $latest) {
    throw 'Collector completed but no artifact directory was found.'
  }
  return $latest.FullName
}

if ($Phase -eq 'prepare') {
  $prepareArgs = @{}
  if ($SkipPrebuild) {
    $prepareArgs['SkipPrebuild'] = $true
  }

  & $prepareScript @prepareArgs

  Write-Host ''
  Write-Host '[OO Reboot] Next steps on the physical machine:' -ForegroundColor Cyan
  Write-Host '  1. Flash or copy the generated reboot image to the USB device.' -ForegroundColor Gray
  Write-Host '  2. Boot the target machine.' -ForegroundColor Gray
  Write-Host '  3. Let the autorun arm /oo_reboot_probe, reboot, then continue verification on the next boot.' -ForegroundColor Gray
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

$targetArtifacts = if ($ArtifactsDir) {
  if ([System.IO.Path]::IsPathRooted($ArtifactsDir)) {
    [System.IO.Path]::GetFullPath($ArtifactsDir)
  } else {
    [System.IO.Path]::GetFullPath((Join-Path $root $ArtifactsDir))
  }
} else {
  Join-Path (Join-Path $root 'artifacts') ("real-hw-reboot-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
}

$collectArgs = @{
  OutDir = $targetArtifacts
}
if ($UsbRoot) {
  $collectArgs['UsbRoot'] = $UsbRoot
} else {
  $collectArgs['ImagePath'] = $ImagePath
}

& $collectScript @collectArgs

if (-not (Test-Path -LiteralPath $targetArtifacts)) {
  $targetArtifacts = Resolve-LatestArtifacts 'real-hw-reboot-*'
}

$validateArgs = @{
  ArtifactsDir = $targetArtifacts
  AllowNoConsult = $true
  RequireRebootProbe = $true
}
if ($RequireDiag) {
  $validateArgs['RequireDiag'] = $true
}

& $validateScript @validateArgs

$reportArgs = @{
  ArtifactsDir = $targetArtifacts
  ModelBin = ''
  Scenario = 'reboot'
}
if ($UsbRoot) {
  $reportArgs['SourceLabel'] = [System.IO.Path]::GetFullPath($UsbRoot)
} elseif ($ImagePath) {
  $reportArgs['SourceLabel'] = [System.IO.Path]::GetFullPath($ImagePath)
}

& $reportScript @reportArgs

Write-Host ''
Write-Host "[OO Reboot] Validation complete: $targetArtifacts" -ForegroundColor Green