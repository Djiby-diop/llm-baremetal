# run-qemu.ps1 — Boot llama2.efi in QEMU on Windows with UEFI (EDK2/OVMF)
# Usage: .\run-qemu.ps1 [options]
#   -Serial       : also launch oo-guard watch on serial output (default: $false)
#   -Interactive  : keep QEMU window open (default: $false = serial-only mode)
#   -TailUart     : tail OO_UART.log in real-time (default: $false)

param(
    [switch]$Serial = $false,
    [switch]$Interactive = $false,
    [switch]$TailUart = $false
)

$ErrorActionPreference = "Stop"

$QEMU   = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$OVMF   = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$VARS   = "C:\Program Files\qemu\share\edk2-x86_64-secure-code.fd"  # vars (writable copy)
$IMG    = "$PSScriptRoot\..\..\llm-baremetal\llm-baremetal-boot.img"
$GUARD  = "$PSScriptRoot\oo-warden\guard\target\release\oo-guard"

# OO UART bridge output (captured from COM1 via QEMU -serial)
$SERIAL   = "$env:TEMP\oo-serial.txt"
$OO_UART  = "$PSScriptRoot\..\..\llm-baremetal\OO_UART.log"

# Resolve relative paths
$IMG = (Resolve-Path $IMG).Path

# Sanity checks
if (-not (Test-Path $QEMU))  { Write-Error "QEMU not found at $QEMU" }
if (-not (Test-Path $OVMF))  { Write-Error "OVMF not found at $OVMF" }
if (-not (Test-Path $IMG))   { Write-Error "Boot image not found: $IMG. Run 'make test' in WSL first." }

# Copy VARS to temp so UEFI can write EFI variables without touching original
$VARS_TMP = "$env:TEMP\edk2-x86_64-vars-copy.fd"
if (-not (Test-Path $VARS_TMP)) {
    Copy-Item $VARS $VARS_TMP
}

# Clear previous UART log
if (Test-Path $OO_UART) { Remove-Item $OO_UART -Force }

Write-Host "==> QEMU UEFI boot: $IMG" -ForegroundColor Cyan
Write-Host "    OVMF: $OVMF" -ForegroundColor Gray
Write-Host "    Serial → $SERIAL" -ForegroundColor Gray
Write-Host "    OO UART → $OO_UART" -ForegroundColor Gray

# Build QEMU args
# COM1 (0x3F8) captured to OO_UART.log; legacy serial to SERIAL
$qemu_args = @(
    "-machine", "q35,accel=tcg",
    "-cpu", "max",
    "-m", "8192",
    "-drive", "if=pflash,format=raw,readonly=on,file=$OVMF",
    "-drive", "if=pflash,format=raw,file=$VARS_TMP",
    "-drive", "format=raw,file=$IMG",
    "-serial", "file:$OO_UART",       # COM1 → OO UART bridge log
    "-serial", "file:$SERIAL",        # COM2 → legacy serial log
    "-monitor", "none"
)

if (-not $Interactive) {
    $qemu_args += @("-nographic", "-vga", "none")
} else {
    $qemu_args += @("-vga", "std")
}

# Optionally tail OO_UART.log in real-time in background
$tail_job = $null
if ($TailUart) {
    Write-Host "==> Tailing OO UART bridge: $OO_UART" -ForegroundColor Cyan
    $tail_job = Start-Job -ScriptBlock {
        param($logpath)
        $pos = 0
        while ($true) {
            Start-Sleep -Milliseconds 250
            if (Test-Path $logpath) {
                $lines = Get-Content $logpath
                if ($lines.Count -gt $pos) {
                    $lines[$pos..($lines.Count - 1)] | ForEach-Object {
                        if ($_ -match '\[oo-event\]') {
                            Write-Host "[UART] $_" -ForegroundColor Yellow
                        }
                    }
                    $pos = $lines.Count
                }
            }
        }
    } -ArgumentList $OO_UART
}

# Launch oo-guard watch in background if requested
$guard_proc = $null
if ($Serial -and (Test-Path $GUARD)) {
    Write-Host "==> Launching oo-guard watch..." -ForegroundColor Cyan
    $guard_proc = Start-Process -FilePath $GUARD -ArgumentList "watch --serial `"$SERIAL`"" -PassThru -NoNewWindow
}

# Launch QEMU
Write-Host "==> Starting QEMU..." -ForegroundColor Green
& $QEMU @qemu_args
$exit_code = $LASTEXITCODE

Write-Host ""
Write-Host "==> QEMU exited (code: $exit_code)" -ForegroundColor $(if ($exit_code -eq 0) { "Green" } else { "Yellow" })

# Stop tail job
if ($tail_job) {
    Stop-Job -Job $tail_job
    Remove-Job -Job $tail_job -Force
}

# Stop oo-guard if running
if ($guard_proc -and -not $guard_proc.HasExited) {
    Stop-Process -Id $guard_proc.Id
}

# Show OO UART bridge events
if (Test-Path $OO_UART) {
    Write-Host ""
    Write-Host "==> OO UART Bridge events (all):" -ForegroundColor Cyan
    Get-Content $OO_UART | Where-Object { $_ -match '\[oo-event\]' } | ForEach-Object {
        Write-Host "  $_" -ForegroundColor Yellow
    }
}

# Show last 30 lines of legacy serial log
if (Test-Path $SERIAL) {
    Write-Host ""
    Write-Host "==> Serial output (last 30 lines):" -ForegroundColor Cyan
    Get-Content $SERIAL | Select-Object -Last 30
}

exit $exit_code
