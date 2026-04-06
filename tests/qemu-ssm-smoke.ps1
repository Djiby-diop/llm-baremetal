#!/usr/bin/env pwsh
# qemu-ssm-smoke.ps1 — Boot QEMU + run SSM inference via autorun
#
# Prerequisites:
#   - llm-baremetal-boot.img with:
#       KERNEL.EFI (BOOTX64.EFI), oo_v3.bin, gpt_neox_tokenizer.bin,
#       ssm-autorun.txt, repl.cfg (autorun_autostart=1 autorun_file=ssm-autorun.txt)
#
# Output: tests/ssm-smoke.log (serial output)
# Exit code: 0 on success (inference output detected), 1 on failure

param(
    [string]$Image   = "",
    [int]$TimeoutSec = 180,
    [switch]$Headless
)

$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent

if (-not $Image) {
    $Image = Join-Path $root "llm-baremetal-boot.img"
}

$QEMU  = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$OVMF  = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$VARS  = Join-Path $PSScriptRoot "ovmf-vars-ssm-smoke.fd"
$LOG   = Join-Path $root "tests\ssm-smoke.log"

if (-not (Test-Path $QEMU))  { Write-Error "QEMU not found: $QEMU";  exit 1 }
if (-not (Test-Path $OVMF))  { Write-Error "OVMF not found: $OVMF";  exit 1 }
if (-not (Test-Path $Image)) { Write-Error "Image not found: $Image"; exit 1 }

# Blank OVMF vars store
$bytes = New-Object byte[] 131072
[System.IO.File]::WriteAllBytes($VARS, $bytes)

Remove-Item $LOG -ErrorAction SilentlyContinue
Write-Host "[SSM-SMOKE] Image:   $Image"
Write-Host "[SSM-SMOKE] Timeout: ${TimeoutSec}s"
Write-Host "[SSM-SMOKE] Log:     $LOG"

$display = if ($Headless) { "none" } else { "sdl" }

$qargs = @(
    "-drive",   "if=pflash,format=raw,readonly=on,file=$OVMF",
    "-drive",   "if=pflash,format=raw,file=$VARS",
    "-drive",   "format=raw,file=$Image",
    "-machine", "q35",
    "-m",       "4096M",
    "-cpu",     "max",
    "-accel",   "tcg,thread=multi",
    "-display", $display,
    "-chardev", "file,id=ser0,path=$LOG",
    "-serial",  "chardev:ser0",
    "-no-reboot"
)

Write-Host "[SSM-SMOKE] Starting QEMU..."
$proc = Start-Process -FilePath $QEMU -ArgumentList $qargs -PassThru
Write-Host "[SSM-SMOKE] PID: $($proc.Id)"

$waited = 0
while ($waited -lt $TimeoutSec -and -not $proc.HasExited) {
    Start-Sleep 5
    $waited += 5
    if (($waited % 30) -eq 0) { Write-Host "[SSM-SMOKE] Waiting... ${waited}s" }
}

if (-not $proc.HasExited) {
    Write-Host "[SSM-SMOKE] Timeout reached — stopping QEMU"
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}

# Analyze log
if (Test-Path $LOG) {
    $content = Get-Content $LOG -Raw
    $lines = ($content -split "`n").Count
    Write-Host "[SSM-SMOKE] Log: $lines lines captured"

    $pass = $true
    $checks = @(
        @{ Name = "Boot";       Pattern = "OO>|REPL" },
        @{ Name = "Selftest";   Pattern = "selftest.*BPE.*OK" },
        @{ Name = "SSM Load";   Pattern = "OOSI-v3.*OK.*inference ready" },
        @{ Name = "Tokenizer";  Pattern = "Tokenizer loaded" },
        @{ Name = "Inference";  Pattern = "OOSI-v3.*tokens.*tok/s" },
        @{ Name = "No crash";   Pattern = "OOSI-v3" }
    )

    foreach ($c in $checks) {
        if ($content -match $c.Pattern) {
            Write-Host "[SSM-SMOKE] PASS: $($c.Name)"
        } else {
            Write-Host "[SSM-SMOKE] FAIL: $($c.Name) — pattern '$($c.Pattern)' not found"
            $pass = $false
        }
    }

    if ($pass) {
        Write-Host "`n[SSM-SMOKE] ALL CHECKS PASSED"
        exit 0
    } else {
        Write-Host "`n[SSM-SMOKE] SOME CHECKS FAILED — see $LOG"
        exit 1
    }
} else {
    Write-Host "[SSM-SMOKE] ERROR: no log file produced"
    exit 1
}
