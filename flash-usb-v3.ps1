# flash-usb-v3.ps1 — Flash oo_usb_v3.img to USB drive with Rufus (DD mode)
# Prerequisites: Rufus installed, 64GB USB drive inserted

param(
    [Parameter(Mandatory=$false)]
    [string]$ImagePath = "C:\Temp\oo_usb_v3.img",

    [Parameter(Mandatory=$false)]
    [string]$RufusPath = "C:\Program Files\Rufus\rufus.exe"
)

Write-Host "=== OO USB Flash Tool ===" -ForegroundColor Cyan
Write-Host ""

# Verify image
if (-not (Test-Path $ImagePath)) {
    Write-Error "Image not found: $ImagePath"
    exit 1
}
$img = Get-Item $ImagePath
Write-Host "Image  : $ImagePath" -ForegroundColor Yellow
Write-Host "Size   : $([math]::Round($img.Length/1GB, 2)) GB" -ForegroundColor Yellow
Write-Host ""

# List available USB drives
Write-Host "=== Available USB Drives ===" -ForegroundColor Cyan
$disks = Get-Disk | Where-Object { $_.BusType -eq 'USB' }
if ($disks.Count -eq 0) {
    Write-Error "No USB drives found. Insert your 64GB USB drive and try again."
    exit 1
}
$disks | Format-Table DiskNumber,FriendlyName,Size,BusType

# Safety confirmation
Write-Host ""
Write-Host "WARNING: This will ERASE ALL DATA on the selected USB drive!" -ForegroundColor Red
Write-Host "The image contains:" -ForegroundColor Yellow
Write-Host "  - EFI/BOOT/BOOTX64.EFI (UEFI boot application, 27MB)" -ForegroundColor Yellow
Write-Host "  - oo_v3.bin (Mamba-2.8B OOSI v3 weights, 2.73GB)" -ForegroundColor Yellow
Write-Host "  - gpt_neox_tokenizer.bin (GPT-NeoX vocabulary, 714KB)" -ForegroundColor Yellow
Write-Host "  - repl.cfg + llmk-autorun.txt (boot config)" -ForegroundColor Yellow
Write-Host ""
Write-Host "Boot sequence on PC:" -ForegroundColor Green
Write-Host "  1. Press F12/F2/DEL at boot → select USB drive" -ForegroundColor Green
Write-Host "  2. OO system boots automatically" -ForegroundColor Green
Write-Host "  3. autorun: /ssm_load oo_v3.bin (~60s to load 2.73GB)" -ForegroundColor Green
Write-Host "  4. /ssm_infer runs full Mamba-2.8B SSM inference" -ForegroundColor Green
Write-Host ""

# Method 1: Rufus (GUI, preferred)
if (Test-Path $RufusPath) {
    Write-Host "=== Option 1: Rufus (RECOMMENDED) ===" -ForegroundColor Green
    Write-Host "  1. Launch Rufus"
    Write-Host "  2. Device: select your 64GB USB drive"
    Write-Host "  3. Boot selection: click SELECT → choose $ImagePath"
    Write-Host "  4. Image option: DD Image (NOT ISO Image!)"
    Write-Host "  5. Click START"
    Write-Host ""
    $launch = Read-Host "Launch Rufus now? (y/n)"
    if ($launch -eq 'y') {
        Start-Process $RufusPath
    }
} else {
    Write-Host "Rufus not found at $RufusPath" -ForegroundColor Yellow
    Write-Host "Download: https://rufus.ie" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Option 2: Windows (PowerShell Admin) ===" -ForegroundColor Cyan
Write-Host "  Run as Administrator:"
Write-Host "  diskpart"
Write-Host "  > list disk       (find USB disk number)"
Write-Host "  > select disk N   (N = your USB disk number)"
Write-Host "  > clean"
Write-Host "  > exit"
Write-Host "  Then:"
$disknum = $disks[0].DiskNumber
Write-Host "  dd if=$($ImagePath.Replace('\','\\')) of=\\.\PhysicalDrive$disknum bs=4M" -ForegroundColor Yellow
Write-Host "  (use WSL: dd if=/mnt/c/Temp/oo_usb_v3.img of=/dev/sdX bs=4M status=progress)" -ForegroundColor Yellow
Write-Host ""
Write-Host "=== System Requirements for USB Boot ===" -ForegroundColor Cyan
Write-Host "  CPU: x86_64 with AVX2 (2013+, Intel Haswell / AMD Ryzen 1000+)"
Write-Host "  RAM: 16GB minimum (needs 3.5GB for weights + SSM state)"
Write-Host "  Boot: UEFI (not legacy BIOS)"
Write-Host "  Secure Boot: DISABLE in BIOS/UEFI settings"
Write-Host ""
Write-Host "=== QEMU Test First ===" -ForegroundColor Green
Write-Host "  .\test-qemu-v3.ps1"
Write-Host "  (uses 4GB RAM, validates inference before real hardware)"
