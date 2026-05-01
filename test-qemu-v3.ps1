# test-qemu-v3.ps1 — Test OOSI v3 full Mamba SSM inference in QEMU
# Requires: OVMF.fd, oo_usb_v3.img (4GB), qemu-system-x86_64
# RAM: 4096MB (needs to hold 2.73GB weights in WEIGHTS arena)

$QEMU   = "qemu-system-x86_64"
$OVMF   = "C:\Temp\ovmf.fd"
$IMG    = "C:\Temp\oo_usb_v3.img"
$LOG    = "C:\Temp\oo_v3_test.log"

if (-not (Test-Path $OVMF)) {
    Write-Error "OVMF not found at $OVMF. Download from https://github.com/tianocore/edk2"
    exit 1
}
if (-not (Test-Path $IMG)) {
    Write-Error "USB image not found at $IMG. Run mk_usb_img.sh first."
    exit 1
}

Write-Host "=== OO OOSI v3 QEMU test ===" -ForegroundColor Cyan
Write-Host "  Image  : $IMG" -ForegroundColor Yellow
Write-Host "  Log    : $LOG" -ForegroundColor Yellow
Write-Host "  RAM    : 4096MB (needed for 2.73GB weights + 20MB SSM state)" -ForegroundColor Yellow
Write-Host ""
Write-Host "Expected boot sequence:" -ForegroundColor Green
Write-Host "  1. UEFI boot → OO REPL" -ForegroundColor Green
Write-Host "  2. autorun: /ssm_load oo_v3.bin" -ForegroundColor Green
Write-Host "  3. [OOSI] Detected v3 format (full standalone Mamba)" -ForegroundColor Green
Write-Host "  4. Allocating zones: WEIGHTS=2730MB KV=25MB SCRATCH=20MB" -ForegroundColor Green
Write-Host "  5. /ssm_infer The future of AI is" -ForegroundColor Green
Write-Host "  6. [OOSI-v3] Generating with full SSM recurrence..." -ForegroundColor Green
Write-Host "  7. /quit" -ForegroundColor Green
Write-Host ""

$args = @(
    "-machine", "q35",
    "-cpu", "max",
    "-m", "4096M",
    "-drive", "if=pflash,format=raw,readonly=on,file=$OVMF",
    "-drive", "format=raw,file=$IMG",
    "-serial", "tcp:127.0.0.1:4444,server,nowait",
    "-nographic",
    "-no-reboot"
)

Write-Host "Running: $QEMU $($args -join ' ')" -ForegroundColor DarkGray
Write-Host ""

& $QEMU @args

if (Test-Path $LOG) {
    Write-Host ""
    Write-Host "=== Serial log (last 40 lines) ===" -ForegroundColor Cyan
    Get-Content $LOG | Select-Object -Last 40
}
