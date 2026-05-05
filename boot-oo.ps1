# boot-oo.ps1 - Launch OO in QEMU
# Usage:  .\boot-oo.ps1                      -- headless, 90s timeout, shows UART on exit
#         .\boot-oo.ps1 -Interactive          -- graphical window (type in UEFI console)
#         .\boot-oo.ps1 -TimeoutSec 180
#
# Boot image: llm-baremetal-boot.img (512MB GPT/FAT32)
#   - EFI/BOOT/BOOTX64.EFI  (llama2.efi  ~28MB)
#   - models/stories15M.q8_0.gguf (~15MB)
#   - tokenizer.bin          (~0.4MB)
#
# To rebuild:  wsl -e bash tools/scripts/make-boot-img.sh

param(
    [int]$TimeoutSec = 90,
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

$qemu_args = @(
    "-machine", "q35,accel=tcg",
    "-cpu",     "max",
    "-m",       "2048",
    "-drive",   "if=pflash,format=raw,readonly=on,file=$OVMF",
    "-drive",   "if=pflash,format=raw,file=$VARS_TMP",
    "-drive",   "format=raw,file=$IMG,if=ide",
    "-serial",  "file:$UART",
    "-no-reboot"
)

Write-Host "==> OO Boot Image : $IMG" -ForegroundColor Cyan
Write-Host "    OVMF          : $OVMF" -ForegroundColor Gray
Write-Host "    UART log      : $UART"  -ForegroundColor Gray

if ($Interactive) {
    $qemu_args += @("-vga", "std")
    Write-Host "==> Launching QEMU (interactive graphical window)..." -ForegroundColor Green
    Write-Host "    Type commands in the QEMU window UEFI console." -ForegroundColor Gray
    $p = Start-Process -FilePath $QEMU -ArgumentList $qemu_args -PassThru
    Write-Host "    PID $($p.Id). Close window or press Ctrl+C to stop."
    $p.WaitForExit()
    $exit_code = $p.ExitCode
} else {
    $qemu_args += @("-display", "none")
    Write-Host "==> Launching QEMU (headless, ${TimeoutSec}s timeout)..." -ForegroundColor Green
    $p = Start-Process -FilePath $QEMU -ArgumentList $qemu_args -PassThru -NoNewWindow
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $spinner = @('|','/','-','\')
    $i = 0
    while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        Write-Host -NoNewline "`r    Booting $($spinner[$i % 4]) "
        $i++
    }
    Write-Host ""
    if (-not $p.HasExited) {
        Write-Host "==> Timeout reached - stopping QEMU (PID $($p.Id))" -ForegroundColor Yellow
        Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
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


