# Build + create bootable image using WSL (single entrypoint)
param(
	[ValidateSet('repl')]
	[string]$Target = 'repl',

	# Build an image without embedding any model weights.
	# Useful for release artifacts and for users who want to copy their own model later.
	[switch]$NoModel,

	# Default unchanged (110M) unless overridden.
	# NOTE: despite the name, this now supports:
	#   - a full filename (stories110M.bin, my-instruct.gguf)
	#   - a base name without extension (stories110M, my-instruct)
	# In the base-name case, the image builder will copy .bin and/or .gguf if present.
	[string]$ModelBin = 'stories110M.bin',

	# Optional additional models to bundle into the image (copied to /models on the FAT partition).
	# Example:
	#   .\build.ps1 -ModelBin stories110M.bin -ExtraModelBins @('my-instruct.bin','another.bin')
	[string[]]$ExtraModelBins = @(),

	# Optional: force a specific image size (MiB). Defaults to auto-sizing.
	# Example: create a >1GB image for testing
	#   .\build.ps1 -NoModel -ImageSizeMB 1200
	[ValidateRange(0, 65536)]
	[int]$ImageSizeMB = 0
)

$ErrorActionPreference = 'Stop'

# Be cwd-independent: always run relative operations from the script folder.
Set-Location -LiteralPath $PSScriptRoot

Write-Host "`n[Build] Build + Image (WSL)" -ForegroundColor Cyan
Write-Host "  Target: $Target" -ForegroundColor Gray
if ($NoModel) {
	Write-Host "  Model:  (no-model image)" -ForegroundColor Gray
} else {
	Write-Host "  Model:  $ModelBin" -ForegroundColor Gray
}

if ($ExtraModelBins.Count -gt 0) {
	Write-Host "  Extra:  $($ExtraModelBins -join ', ')" -ForegroundColor Gray
}

if ($ImageSizeMB -gt 0) {
	Write-Host "  Image:  ${ImageSizeMB}MB (forced)" -ForegroundColor Gray
}

# Fail fast with a helpful message when weights are not present.
function Test-ModelSpecPresent([string]$spec) {
	if (-not $spec) { return $false }
	$here = Join-Path $PSScriptRoot $spec
	$parent = Join-Path (Split-Path $PSScriptRoot -Parent) $spec
	if ((Test-Path $here) -or (Test-Path $parent)) { return $true }

	# If no extension provided, accept .bin or .gguf
	if ($spec -notmatch '\.[A-Za-z0-9]+$') {
		$hereBin = Join-Path $PSScriptRoot ($spec + '.bin')
		$hereGguf = Join-Path $PSScriptRoot ($spec + '.gguf')
		$parBin = Join-Path (Split-Path $PSScriptRoot -Parent) ($spec + '.bin')
		$parGguf = Join-Path (Split-Path $PSScriptRoot -Parent) ($spec + '.gguf')
		return ((Test-Path $hereBin) -or (Test-Path $hereGguf) -or (Test-Path $parBin) -or (Test-Path $parGguf))
	}

	return $false
}

if (-not $NoModel -and -not (Test-ModelSpecPresent $ModelBin)) {
	Write-Host "" 
	Write-Host "❌ Missing model weights: $ModelBin" -ForegroundColor Red
	Write-Host "Place the model file in this folder or one level up, then re-run." -ForegroundColor Yellow
	Write-Host "You can also pass a base name without extension (will accept .bin or .gguf)." -ForegroundColor Yellow
	Write-Host "Tip: download the model from GitHub Releases (recommended) instead of committing weights." -ForegroundColor Yellow
	throw "Missing model weights: $ModelBin"
}

# Validate extra model files (if any)
if (-not $NoModel) {
	foreach ($m in $ExtraModelBins) {
		if (-not $m) { continue }
		if (-not (Test-ModelSpecPresent $m)) {
			Write-Host "" 
			Write-Host "❌ Missing extra model weights: $m" -ForegroundColor Red
			throw "Missing extra model weights: $m"
		}
	}
}

function Update-SplashBmpFromPng-BestEffort {
	# The UEFI splash renderer only supports 24-bit uncompressed BMP (splash.bmp).
	# If llm2.png exists locally, generate splash.bmp automatically.
	$src = Join-Path $PSScriptRoot 'llm2.png'
	$dst = Join-Path $PSScriptRoot 'splash.bmp'
	if (-not (Test-Path $src)) { return }

	$need = $false
	if (-not (Test-Path $dst)) {
		$need = $true
	} else {
		try {
			$srcTime = (Get-Item $src).LastWriteTimeUtc
			$dstTime = (Get-Item $dst).LastWriteTimeUtc
			if ($srcTime -gt $dstTime) { $need = $true }
		} catch {
			$need = $true
		}
	}

	if (-not $need) { return }

	$py = Join-Path (Split-Path $PSScriptRoot -Parent) '.venv\Scripts\python.exe'
	if (-not (Test-Path $py)) {
		$cmd = Get-Command python.exe -ErrorAction SilentlyContinue
		if ($cmd) { $py = $cmd.Source }
	}
	if (-not $py -or -not (Test-Path $py)) {
		Write-Host "[Build] Splash: python not found; skipping llm2.png -> splash.bmp" -ForegroundColor Yellow
		return
	}

	Write-Host "[Build] Splash: generating splash.bmp from llm2.png" -ForegroundColor Gray
	try {
		& $py -c "from PIL import Image; import os; src=r'$src'; dst=r'$dst'; im=Image.open(src).convert('RGB'); im=im.resize((1024,1024), Image.Resampling.LANCZOS); im.save(dst, format='BMP')"
	} catch {
		Write-Host "[Build] Splash: conversion failed (need 'pillow'): $($_.Exception.Message)" -ForegroundColor Yellow
		return
	}
}

Update-SplashBmpFromPng-BestEffort

function ConvertTo-WslPath([string]$winPath) {
	# Deterministic conversion (avoids occasional unreliable `wslpath` output).
	$norm = ($winPath -replace '\\','/')
	if ($norm -match '^([A-Za-z]):/(.*)$') {
		$drive = $Matches[1].ToLowerInvariant()
		$rest = $Matches[2]
		return "/mnt/$drive/$rest"
	}
	throw "Failed to convert path to WSL path: $winPath (normalized=$norm)"
}

$wslRepo = ConvertTo-WslPath $PSScriptRoot

$extra = ($ExtraModelBins | Where-Object { $_ -and $_.Trim().Length -gt 0 }) -join ';'

$buildStartUtc = (Get-Date).ToUniversalTime()

# Build + image creation in WSL (single shot). Using -lc avoids temp-script pitfalls.
$noModelFlag = if ($NoModel) { '1' } else { '0' }
$modelSpec = if ($NoModel) { 'nomodel' } else { $ModelBin }
$imgMbClause = if ($ImageSizeMB -gt 0) { ("IMG_MB='{0}'" -f $ImageSizeMB) } else { '' }
$bash = @(
	'set -e'
	("cd '{0}'" -f $wslRepo)
	'chmod +x create-boot-mtools.sh'
	'make clean'
	'make repl'
	# Force the EFI payload used by the image builder to be the freshly built one.
	# Force NO_MODEL explicitly to avoid inheriting from the user's environment.
	("{3} NO_MODEL='{0}' EFI_BIN='llama2.efi' MODEL='{1}' MODEL_BIN='{1}' EXTRA_MODELS='{2}' ./create-boot-mtools.sh" -f $noModelFlag, $modelSpec, $extra, $imgMbClause)
) -join '; '

wsl bash -lc $bash

if ($LASTEXITCODE -ne 0) {
	throw "WSL build failed with exit code $LASTEXITCODE"
}

# Sanity check: ensure llama2.efi was actually produced/updated by this run.
$efiOut = Join-Path $PSScriptRoot 'llama2.efi'
if (-not (Test-Path $efiOut)) {
	throw "Build did not produce $efiOut"
}
try {
	$efiTime = (Get-Item $efiOut).LastWriteTimeUtc
	if ($efiTime -lt $buildStartUtc.AddSeconds(-2)) {
		throw "Build may have been skipped: llama2.efi timestamp ($efiTime) is older than build start ($buildStartUtc)."
	}
} catch {
	throw
}

$img = Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
	Sort-Object LastWriteTime -Descending |
	Select-Object -First 1

if ($img) {
	Write-Host "`n[OK] Done: $($img.Name)" -ForegroundColor Green

	# Prune older images to avoid accidentally running a stale image.
	# Best-effort: if an older image is locked (e.g., by QEMU), deletion may fail.
	$allImgs = @(Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
		Sort-Object LastWriteTime -Descending)
	if ($allImgs -and $allImgs.Count -gt 1) {
		foreach ($old in ($allImgs | Select-Object -Skip 1)) {
			try {
				Remove-Item -Force -ErrorAction Stop $old.FullName
			} catch {
				Write-Host "[Build] Prune: could not delete $($old.Name) (maybe in use)" -ForegroundColor Yellow
			}
		}
	}
} else {
	Write-Host "`n[OK] Done" -ForegroundColor Green
}
