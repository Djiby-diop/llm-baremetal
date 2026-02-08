# Build a stable release image and produce compressed artifacts + checksums.
# Intentionally does NOT create a GitHub Release automatically (auth varies).
param(
	# Optional: directory where artifacts are copied (defaults to ./release-artifacts)
	[string]$OutDir = "",

	# Build a no-model image suitable for releases.
	[switch]$NoModel = $true,

	# Primary model to embed (when -NoModel is not set).
	# Supports full filename (.bin/.gguf) or a base name without extension.
	[string]$ModelBin = 'stories110M.bin',

	# Optional additional models to bundle into the image.
	[string[]]$ExtraModelBins = @(),

	# XZ compression level (1-9). 9 is smallest, slower.
	[ValidateRange(1,9)]
	[int]$XzLevel = 9,

	# Optional: force a specific image size (MiB). Defaults to auto-sizing.
	[ValidateRange(0, 65536)]
	[int]$ImageSizeMB = 0
)

$ErrorActionPreference = 'Stop'

if (-not $OutDir) {
	$OutDir = Join-Path $PSScriptRoot 'release-artifacts'
}

Write-Host "`n[Release] Build artifacts" -ForegroundColor Cyan
Write-Host "  OutDir:  $OutDir" -ForegroundColor Gray
Write-Host "  NoModel: $NoModel" -ForegroundColor Gray
if (-not $NoModel) {
	Write-Host "  Model:   $ModelBin" -ForegroundColor Gray
	if ($ExtraModelBins.Count -gt 0) {
		Write-Host "  Extra:   $($ExtraModelBins -join ', ')" -ForegroundColor Gray
	}
}
Write-Host "  XzLevel: $XzLevel" -ForegroundColor Gray
if ($ImageSizeMB -gt 0) {
	Write-Host "  Image:  ${ImageSizeMB}MB (forced)" -ForegroundColor Gray
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 1) Build image
$buildArgs = @{
	NoModel = [bool]$NoModel
}
if (-not $NoModel) {
	$buildArgs.ModelBin = $ModelBin
	if ($ExtraModelBins.Count -gt 0) {
		$buildArgs.ExtraModelBins = $ExtraModelBins
	}
}
if ($ImageSizeMB -gt 0) {
	$buildArgs.ImageSizeMB = $ImageSizeMB
}
& (Join-Path $PSScriptRoot 'build.ps1') @buildArgs

# Find newest image
$img = Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction Stop |
	Sort-Object LastWriteTime -Descending |
	Select-Object -First 1

if (-not $img) {
	throw "No boot image found after build (expected llm-baremetal-boot*.img)."
}

$imgName = $img.Name
$imgFull = $img.FullName
Write-Host "`n[Release] Built image: $imgName" -ForegroundColor Green

# 2) Compress and compute checksums inside WSL for consistent tooling
function ConvertTo-WslPath([string]$winPath) {
	$norm = ($winPath -replace '\\','/')
	if ($norm -match '^([A-Za-z]):/(.*)$') {
		$drive = $Matches[1].ToLowerInvariant()
		$rest = $Matches[2]
		return "/mnt/$drive/$rest"
	}
	throw "Failed to convert path to WSL path: $winPath (normalized=$norm)"
}

$wslRepo = ConvertTo-WslPath $PSScriptRoot
$wslOut = ConvertTo-WslPath $OutDir

# Name artifacts deterministically
$base = if ($NoModel) { 'llm-baremetal-boot-nomodel' } else { 'llm-baremetal-boot' }
$imgOut = "$base.img"
$xzOut = "$base.img.xz"
$shaOut = 'SHA256SUMS.txt'

$bash = @(
	'set -e'
	("cd '{0}'" -f $wslRepo)
	# Copy/rename the image into the output directory
	("cp -f '{0}' '{1}/{2}'" -f $imgName, $wslOut, $imgOut)
	# Compress (single stream for reproducibility)
	("xz -T0 -{0} -c '{1}/{2}' > '{1}/{3}'" -f $XzLevel, $wslOut, $imgOut, $xzOut)
	# Checksums
	("cd '{0}'" -f $wslOut)
	("sha256sum '{0}' '{1}' > '{2}'" -f $imgOut, $xzOut, $shaOut)
) -join '; '

wsl bash -lc $bash
if ($LASTEXITCODE -ne 0) {
	throw "WSL artifact generation failed with exit code $LASTEXITCODE"
}

Write-Host "`n[Release] Artifacts ready:" -ForegroundColor Green
Write-Host "  - $(Join-Path $OutDir $imgOut)" -ForegroundColor Gray
Write-Host "  - $(Join-Path $OutDir $xzOut)" -ForegroundColor Gray
Write-Host "  - $(Join-Path $OutDir $shaOut)" -ForegroundColor Gray

Write-Host "`nNext: create a GitHub Release and upload the .xz + SHA256SUMS.txt (recommended)." -ForegroundColor Cyan
