# Build + create bootable image using WSL (single entrypoint)
param(
	[ValidateSet('repl')]
	[string]$Target = 'repl',

	# Default unchanged (110M) unless overridden.
	[string]$ModelBin = 'stories110M.bin',

	# Optional additional models to bundle into the image (copied to /models on the FAT partition).
	# Example:
	#   .\build.ps1 -ModelBin stories110M.bin -ExtraModelBins @('my-instruct.bin','another.bin')
	[string[]]$ExtraModelBins = @()
)

$ErrorActionPreference = 'Stop'

Write-Host "`n[Build] Build + Image (WSL)" -ForegroundColor Cyan
Write-Host "  Target: $Target" -ForegroundColor Gray
Write-Host "  Model:  $ModelBin" -ForegroundColor Gray

if ($ExtraModelBins.Count -gt 0) {
	Write-Host "  Extra:  $($ExtraModelBins -join ', ')" -ForegroundColor Gray
}

# Fail fast with a helpful message when weights are not present.
$modelHere = Join-Path $PSScriptRoot $ModelBin
$modelParent = Join-Path (Split-Path $PSScriptRoot -Parent) $ModelBin
if (-not (Test-Path $modelHere) -and -not (Test-Path $modelParent)) {
	Write-Host "" 
	Write-Host "❌ Missing model weights: $ModelBin" -ForegroundColor Red
	Write-Host "Place the model file in this folder or one level up, then re-run." -ForegroundColor Yellow
	Write-Host "Tip: download the model from GitHub Releases (recommended) instead of committing weights." -ForegroundColor Yellow
	throw "Missing model weights: $ModelBin"
}

# Validate extra model files (if any)
foreach ($m in $ExtraModelBins) {
	if (-not $m) { continue }
	$mHere = Join-Path $PSScriptRoot $m
	$mParent = Join-Path (Split-Path $PSScriptRoot -Parent) $m
	if (-not (Test-Path $mHere) -and -not (Test-Path $mParent)) {
		Write-Host "" 
		Write-Host "❌ Missing extra model weights: $m" -ForegroundColor Red
		throw "Missing extra model weights: $m"
	}
}

function ConvertTo-WslPath([string]$winPath) {
	# Normalize to forward slashes first.
	$norm = ($winPath -replace '\\','/')
	try {
		$wsl = (wsl wslpath -a $norm 2>$null)
		if ($wsl) { return $wsl.Trim() }
	} catch {
		# fall through
	}

	# Fallback: C:/foo/bar -> /mnt/c/foo/bar
	if ($norm -match '^([A-Za-z]):/(.*)$') {
		$drive = $Matches[1].ToLowerInvariant()
		$rest = $Matches[2]
		return "/mnt/$drive/$rest"
	}
	throw "Failed to convert path to WSL path: $winPath"
}

$wslRepo = ConvertTo-WslPath $PSScriptRoot

$extra = ($ExtraModelBins | Where-Object { $_ -and $_.Trim().Length -gt 0 }) -join ';'

# Avoid brittle quoting by writing a temporary script and running it from WSL.
$tmpSh = Join-Path $env:TEMP ("llmk-build-{0}.sh" -f ([Guid]::NewGuid().ToString('N')))
$tmpShWsl = ConvertTo-WslPath $tmpSh

$scriptBody = @(
	'#!/usr/bin/env bash'
	'set -e'
	("cd '{0}'" -f $wslRepo)
	'chmod +x create-boot-mtools.sh'
	'make clean'
	'make repl'
	("MODEL_BIN='{0}' EXTRA_MODELS='{1}' ./create-boot-mtools.sh" -f $ModelBin, $extra)
) -join "\n"

Set-Content -Path $tmpSh -Value $scriptBody -Encoding ASCII

try {
	wsl bash "$tmpShWsl"
} finally {
	Remove-Item -Force -ErrorAction SilentlyContinue $tmpSh
}

if ($LASTEXITCODE -ne 0) {
	throw "WSL build failed with exit code $LASTEXITCODE"
}

$img = Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
	Sort-Object LastWriteTime -Descending |
	Select-Object -First 1

if ($img) {
	Write-Host "`n[OK] Done: $($img.Name)" -ForegroundColor Green
} else {
	Write-Host "`n[OK] Done" -ForegroundColor Green
}
