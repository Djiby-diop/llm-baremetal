# Build + create bootable image using WSL (single entrypoint)
param(
	[ValidateSet('repl')]
	[string]$Target = 'repl',

	# Phase 5 (Zig): metabolism profiles (build-time defaults)
	[ValidateSet('performance','balanced','survival')]
	[string]$MetabionProfile = 'balanced',

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
	[int]$ImageSizeMB = 0,

	# Skip prebuild static analysis (Neural Protector Phase 2.2)
	# Use during incremental development; recommended to enable for CI/CD.
	[switch]$SkipPrebuild
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$WorkspaceRoot = Split-Path -Parent $RepoRoot

# Be cwd-independent: run relative operations from the repo root.
Set-Location -LiteralPath $RepoRoot

Write-Host "`n[Build] Build + Image (WSL)" -ForegroundColor Cyan
Write-Host "  Target: $Target" -ForegroundColor Gray
Write-Host "  Profile: $MetabionProfile" -ForegroundColor Gray
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
	$here = Join-Path $RepoRoot $spec
	$parent = Join-Path $WorkspaceRoot $spec
	if ((Test-Path $here) -or (Test-Path $parent)) { return $true }

	# If no extension provided, accept .bin or .gguf
	if ($spec -notmatch '\.[A-Za-z0-9]+$') {
		$hereBin = Join-Path $RepoRoot ($spec + '.bin')
		$hereGguf = Join-Path $RepoRoot ($spec + '.gguf')
		$parBin = Join-Path $WorkspaceRoot ($spec + '.bin')
		$parGguf = Join-Path $WorkspaceRoot ($spec + '.gguf')
		return ((Test-Path $hereBin) -or (Test-Path $hereGguf) -or (Test-Path $parBin) -or (Test-Path $parGguf))
	}

	return $false
}

if (-not $NoModel -and -not (Test-ModelSpecPresent $ModelBin)) {
	Write-Host "" 
	Write-Host "ERROR: Missing model weights: $ModelBin" -ForegroundColor Red
	Write-Host "Place the model file in this folder or one level up, then re-run." -ForegroundColor Yellow
	Write-Host "You can also pass a base name without extension (will accept .bin or .gguf)." -ForegroundColor Yellow
	Write-Host "Tip: download weights from Hugging Face (or any direct URL) using scripts/get-weights.{ps1,sh}." -ForegroundColor Yellow
	throw "Missing model weights: $ModelBin"
}

# Validate extra model files (if any)
if (-not $NoModel) {
	foreach ($m in $ExtraModelBins) {
		if (-not $m) { continue }
		if (-not (Test-ModelSpecPresent $m)) {
			Write-Host "" 
			Write-Host "ERROR: Missing extra model weights: $m" -ForegroundColor Red
			throw "Missing extra model weights: $m"
		}
	}
}

function Update-SplashBmpFromPng-BestEffort {
	# The UEFI splash renderer only supports 24-bit uncompressed BMP (splash.bmp).
	# If llm2.png exists locally, generate splash.bmp automatically.
	$src = Join-Path $RepoRoot 'llm2.png'
	$dst = Join-Path $RepoRoot 'splash.bmp'
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

	$py = Join-Path $WorkspaceRoot '.venv\Scripts\python.exe'
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

function Get-WslFileSha256([string]$winPath) {
	$wslPath = ConvertTo-WslPath $winPath
	$py = @"
import hashlib
from pathlib import Path
p = Path(r'$wslPath')
if not p.exists():
    raise SystemExit(3)
print(hashlib.sha256(p.read_bytes()).hexdigest())
"@
	$hash = & wsl python3 -c $py
	if ($LASTEXITCODE -eq -1) {
		# WSL unavailable (e.g. OneDrive lock, WSL instance not running).
		# Return $null to signal "skip check" - the build will proceed.
		return $null
	}
	if ($LASTEXITCODE -ne 0) {
		throw "WSL could not hash file: $winPath (exit=$LASTEXITCODE)"
	}
	return ($hash | Out-String).Trim()
}

function Assert-WslSourceConsistency([string[]]$relativePaths) {
	foreach ($rel in $relativePaths) {
		$winPath = Join-Path $RepoRoot $rel
		if (-not (Test-Path -LiteralPath $winPath)) { continue }
		$winHash = (Get-FileHash -LiteralPath $winPath -Algorithm SHA256).Hash.ToLowerInvariant()
		$wslHash = Get-WslFileSha256 $winPath
		if ($null -eq $wslHash) {
			# WSL unavailable (exit=-1): skip consistency check for this file.
			Write-Host "  [WARN] WSL unavailable - skipping source consistency check for: $rel" -ForegroundColor Yellow
			continue
		}
		if (-not $wslHash) {
			throw "WSL returned an empty hash for: $rel"
		}
		if ($winHash -ne $wslHash.ToLowerInvariant()) {
			throw @"
WSL source desync detected for '$rel'.
Windows SHA256: $winHash
WSL SHA256:     $wslHash

The build would use stale content from /mnt/c (common with OneDrive-backed folders).
Resave/sync the file, or move the repo out of OneDrive for deterministic WSL builds.
"@
		}
	}
}

$wslRepo = ConvertTo-WslPath $RepoRoot

Assert-WslSourceConsistency @(
	'engine/llama2/llama2_efi_final.c',
	'Makefile',
	'scripts/create-boot-mtools.sh',
	'test-qemu-autorun.ps1',
	'llmk-autorun-handoff-smoke.txt',
	'llmk-autorun-oo-smoke.txt'
)

$extra = ($ExtraModelBins | Where-Object { $_ -and $_.Trim().Length -gt 0 }) -join ';'

$buildStartUtc = (Get-Date).ToUniversalTime()

# === Phase 2.2: Neural Protector prebuild check ===
# Run static analysis on C sources before compilation.
# Detects risky patterns: small buffers, malloc/free imbalance, unbounded loops.
if (-not $SkipPrebuild) {
	Write-Host "`n[Neural Protector] Running prebuild static analysis..." -ForegroundColor Cyan
	$ooGuardExe = Join-Path $RepoRoot 'oo-guard\target\release\oo-guard.exe'
	if (Test-Path $ooGuardExe) {
		$sourceFile = Join-Path $RepoRoot 'engine\llama2\llama2_efi_final.c'
		& $ooGuardExe prebuild --root $RepoRoot --quiet $sourceFile
		if ($LASTEXITCODE -eq 2) {
			Write-Host "`n[Neural Protector] VIOLATION: C source contains critical risk patterns" -ForegroundColor Red
			Write-Host "Re-run without --quiet to see details: .\run-oo-guard.ps1 prebuild engine\\llama2\\llama2_efi_final.c" -ForegroundColor Yellow
			Write-Host "To bypass (not recommended): .\build.ps1 -SkipPrebuild" -ForegroundColor Gray
			throw "oo-guard prebuild check failed (exit code 2)"
		} elseif ($LASTEXITCODE -ne 0) {
			Write-Host "`n[Neural Protector] WARNING: prebuild check failed with exit code $LASTEXITCODE" -ForegroundColor Yellow
		} else {
			Write-Host "[Neural Protector] Prebuild check passed" -ForegroundColor Green
		}
	} else {
		Write-Host "[Neural Protector] oo-guard not built; skipping prebuild check" -ForegroundColor Yellow
		Write-Host "  To enable: cd oo-guard; cargo build --release" -ForegroundColor Gray
	}
} else {
	Write-Host "`n[Neural Protector] Prebuild check skipped (-SkipPrebuild)" -ForegroundColor Gray
}

# Build + image creation in WSL (single shot). Using -lc avoids temp-script pitfalls.
$noModelFlag = if ($NoModel) { '1' } else { '0' }
$modelSpec = if ($NoModel) { 'nomodel' } else { $ModelBin }
$imgMbClause = if ($ImageSizeMB -gt 0) { ("IMG_MB='{0}'" -f $ImageSizeMB) } else { '' }
$bash = @(
	'set -e'
	("cd '{0}'" -f $wslRepo)
	'chmod +x scripts/create-boot-mtools.sh'
	'make clean'
	("make repl METABION_PROFILE='{0}'" -f $MetabionProfile)
	# Force the EFI payload used by the image builder to be the freshly built one.
	# Force NO_MODEL explicitly to avoid inheriting from the user's environment.
	("{3} NO_MODEL='{0}' EFI_BIN='llama2.efi' MODEL='{1}' MODEL_BIN='{1}' EXTRA_MODELS='{2}' ./scripts/create-boot-mtools.sh" -f $noModelFlag, $modelSpec, $extra, $imgMbClause)
) -join '; '

wsl bash -lc $bash

if ($LASTEXITCODE -ne 0) {
	throw "WSL build failed with exit code $LASTEXITCODE"
}

# Sanity check: ensure llama2.efi was actually produced/updated by this run.
$efiOut = Join-Path $RepoRoot 'llama2.efi'
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

$img = Get-ChildItem -Path $RepoRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
	Sort-Object LastWriteTime -Descending |
	Select-Object -First 1

if ($img) {
	Write-Host "`n[OK] Done: $($img.Name)" -ForegroundColor Green

	# Prune older images to avoid accidentally running a stale image.
	# Best-effort: if an older image is locked (e.g., by QEMU), deletion may fail.
	$allImgs = @(Get-ChildItem -Path $RepoRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
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
