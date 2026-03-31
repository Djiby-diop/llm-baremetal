#!/usr/bin/env pwsh
# qemu-smoke.ps1 — boot llama2.efi in QEMU TCG + capture serial to log
# Usage: .\qemu-smoke.ps1
# Output: qemu-boot.log (serial output)

$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

$QEMU   = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$OVMF   = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$VARS   = Join-Path $PSScriptRoot "ovmf-vars-smoke.fd"
$IMAGE  = Join-Path $PSScriptRoot "llm-baremetal-boot.img"
$LOG    = Join-Path $PSScriptRoot "qemu-boot.log"

# Create blank vars store
if (-not (Test-Path $VARS)) {
    $bytes = New-Object byte[] 131072
    [System.IO.File]::WriteAllBytes($VARS, $bytes)
}

Remove-Item $LOG -ErrorAction SilentlyContinue
Write-Host "[SMOKE] Booting $IMAGE"
Write-Host "[SMOKE] Serial log: $LOG"
Write-Host "[SMOKE] QEMU will run for 30s then auto-stop (no-reboot + timeout)"

# -chardev file captures UEFI ConOut via serial redirect
# -device isa-serial routes it to COM1 which OVMF forwards from ConOut
$qargs = @(
    "-drive",   "if=pflash,format=raw,readonly=on,file=$OVMF",
    "-drive",   "if=pflash,format=raw,file=$VARS",
    "-drive",   "format=raw,file=$IMAGE",
    "-machine", "pc",
    "-m",       "512M",
    "-cpu",     "max",
    "-accel",   "tcg,thread=multi",
    "-display", "sdl",
    "-chardev", "file,id=ser0,path=$LOG",
    "-serial",  "chardev:ser0",
    "-no-reboot"
)

Write-Host "[SMOKE] Starting QEMU..."
$proc = Start-Process -FilePath $QEMU -ArgumentList $qargs -PassThru
Write-Host "[SMOKE] PID: $($proc.Id)"

# Wait up to 40s
$waited = 0
while ($waited -lt 40 -and -not $proc.HasExited) {
    Start-Sleep 2
    $waited += 2
    if (Test-Path $LOG) {
        $sz = (Get-Item $LOG).Length
        Write-Host "  t=$waited`s log=$sz bytes"
    }
}

# Kill if still running
if (-not $proc.HasExited) {
    Write-Host "[SMOKE] Timeout — killing QEMU PID $($proc.Id)"
    $proc.Kill()
}

Write-Host ""
Write-Host "=== BOOT LOG (serial) ==="
if (Test-Path $LOG) {
    $content = Get-Content $LOG -Encoding UTF8 -ErrorAction SilentlyContinue
    if ($content) {
        $content | Select-Object -First 100
    } else {
        # Try raw bytes (UEFI may use UTF-16)
        $bytes = [System.IO.File]::ReadAllBytes($LOG)
        $text  = [System.Text.Encoding]::Unicode.GetString($bytes)
        $text -split "`n" | Select-Object -First 60
    }
} else {
    Write-Host "(no serial log — ConOut may be VGA only)"
    Write-Host "Check the QEMU window for visual output."
}
