# boot-oo.ps1 - Launch OO in QEMU (serial log mode or interactive)
# Usage:  .\boot-oo.ps1              -- serial only, 60s timeout
#         .\boot-oo.ps1 -Interactive -- open graphical window
#         .\boot-oo.ps1 -TimeoutSec 120

param(
    [int]$TimeoutSec = 60,
    [switch]$Interactive = $false
)

$QEMU     = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$OVMF     = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
# Create a blank writable vars image if not present (x86_64 has no pre-built vars.fd)
$VARS_TMP = "$env:TEMP\edk2-x86_64-oo-vars.fd"
if (-not (Test-Path $VARS_TMP)) {
    $blankVars = New-Object byte[] 3653632
    [System.IO.File]::WriteAllBytes($VARS_TMP, $blankVars)
}
$IMG      = "$PSScriptRoot\llm-baremetal-boot.img"
$UART     = "$PSScriptRoot\OO_UART.log"
$LOG      = "$PSScriptRoot\OO_BOOT.log"

if (Test-Path $UART) { try { Remove-Item $UART -Force -ErrorAction SilentlyContinue } catch {} }
if (Test-Path $LOG)  { try { Remove-Item $LOG  -Force -ErrorAction SilentlyContinue } catch {} }

$qemu_args = @(
    "-machine", "q35,accel=tcg",
    "-cpu",     "max",
    "-m",       "4096",
    "-drive",   "if=pflash,format=raw,readonly=on,file=$OVMF",
    "-drive",   "if=pflash,format=raw,file=$VARS_TMP",
    "-drive",   "format=raw,file=$IMG",
    "-serial",  "file:$UART",
    "-serial",  "file:$LOG",
    "-no-reboot"
)

Write-Host "==> OO Boot Image : $IMG" -ForegroundColor Cyan
Write-Host "    OVMF          : $OVMF" -ForegroundColor Gray
Write-Host "    UART log      : $UART"  -ForegroundColor Gray

if ($Interactive) {
    $qemu_args += @("-vga", "std")
    Write-Host "==> Launching QEMU (interactive window)..." -ForegroundColor Green
    & $QEMU @qemu_args
    $exit_code = $LASTEXITCODE
} else {
    $qemu_args += @("-nographic", "-vga", "none")
    Write-Host "==> Launching QEMU (nographic, ${TimeoutSec}s timeout)..." -ForegroundColor Green
    $p = Start-Process -FilePath $QEMU -ArgumentList $qemu_args -PassThru -NoNewWindow
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
    }
    if (-not $p.HasExited) {
        Write-Host "==> Timeout reached - stopping QEMU (PID $($p.Id))" -ForegroundColor Yellow
        $pid_to_kill = $p.Id
        Stop-Process -Id $pid_to_kill -ErrorAction SilentlyContinue
    }
    $exit_code = $p.ExitCode
}

Write-Host ""
Write-Host "==> QEMU exited (code: $exit_code)" -ForegroundColor $(if ($exit_code -eq 0) { "Green" } else { "Yellow" })
Write-Host ""
Write-Host "==> OO UART log (last 60 lines):" -ForegroundColor Cyan
if (Test-Path $UART) {
    Get-Content $UART | Select-Object -Last 60
} else {
    Write-Host "  (no UART output captured)" -ForegroundColor Gray
}


