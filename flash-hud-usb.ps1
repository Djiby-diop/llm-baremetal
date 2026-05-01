# flash-hud-usb.ps1 — Flash OO HUD v2 boot image to USB drive
# Works on Windows 10/11 without admin rights (uses Rufus in CLI mode)
#
# Usage:
#   .\flash-hud-usb.ps1               # auto-builds + flashes
#   .\flash-hud-usb.ps1 -SkipBuild    # skip build, flash existing image
#   .\flash-hud-usb.ps1 -BuildOnly    # build image only, don't flash
#   .\flash-hud-usb.ps1 -TestQemu     # test in QEMU instead of flashing
#
# Requirements:
#   - WSL2 with Ubuntu (for building)
#   - QEMU (for testing): C:\Program Files\qemu\
#   - Rufus (for USB flash): optional

param(
    [switch]$SkipBuild   = $false,
    [switch]$BuildOnly   = $false,
    [switch]$TestQemu    = $false,
    [string]$ImagePath   = "",
    [int]$QemuMemMB      = 512
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$REPO = Split-Path -Parent $PSScriptRoot
$IMAGE_DEFAULT = Join-Path $REPO "llm-baremetal-hud.img"
if ($ImagePath -eq "") { $ImagePath = $IMAGE_DEFAULT }
$SCRIPT = "/mnt/c" + ($REPO -replace "^C:", "" -replace "\\", "/") + "/scripts/create-hud-boot.sh"

Write-Host ""
Write-Host "══════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  OO HUD v2 — Boot Image Toolkit" -ForegroundColor Cyan
Write-Host "══════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# ── BUILD ─────────────────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Write-Host "[BUILD] Building oo_hud_v2.efi + creating boot image..." -ForegroundColor Yellow

    # Check WSL
    $wsl = Get-Command wsl -ErrorAction SilentlyContinue
    if (-not $wsl) {
        Write-Error "WSL not found. Install WSL2 with Ubuntu: wsl --install"
        exit 1
    }

    # Run build script in WSL from repo root (cd to Windows path via /mnt/c)
    $wsl_repo = "/mnt/c" + ($REPO -replace "^C:", "" -replace "\\", "/")
    $result = wsl -e bash -c "cd '$wsl_repo' && bash scripts/create-hud-boot.sh" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host $result -ForegroundColor Red
        Write-Error "Build failed (exit $LASTEXITCODE)"
        exit 1
    }
    Write-Host $result
    Write-Host "[BUILD] Done." -ForegroundColor Green
} else {
    Write-Host "[BUILD] Skipped (--SkipBuild)" -ForegroundColor Gray
}

# ── VERIFY IMAGE ──────────────────────────────────────────────────────────────
if (-not (Test-Path $ImagePath)) {
    Write-Error "Image not found: $ImagePath`n  Run without -SkipBuild to build it first."
    exit 1
}
$imgItem = Get-Item $ImagePath
$imgMB   = [math]::Round($imgItem.Length / 1MB, 1)
Write-Host ""
Write-Host "  Image : $ImagePath" -ForegroundColor White
Write-Host "  Size  : ${imgMB} MB" -ForegroundColor White
Write-Host ""

if ($BuildOnly) {
    Write-Host "[DONE] Build-only mode. Image ready at:" -ForegroundColor Green
    Write-Host "  $ImagePath" -ForegroundColor Cyan
    exit 0
}

# ── QEMU TEST ─────────────────────────────────────────────────────────────────
if ($TestQemu) {
    $qemu = "C:\Program Files\qemu\qemu-system-x86_64.EXE"
    if (-not (Test-Path $qemu)) {
        Write-Error "QEMU not found at $qemu. Install from https://qemu.weilnetz.de/"
    }
    $ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    if (-not (Test-Path $ovmf)) {
        Write-Error "OVMF not found at $ovmf"
    }

    # Create fresh empty vars
    $vars_tmp = Join-Path $env:TEMP "oo_hud_vars.fd"
    [System.IO.File]::WriteAllBytes($vars_tmp, (New-Object byte[] 131072))

    Write-Host "[QEMU] Launching OO HUD v2 in QEMU..." -ForegroundColor Cyan
    Write-Host "       Memory: ${QemuMemMB}MB | Accel: tcg,thread=multi" -ForegroundColor Gray
    Write-Host "       Press Ctrl+C to stop" -ForegroundColor Gray
    Write-Host ""

    & $qemu `
        -accel tcg,thread=multi `
        -display sdl `
        -serial stdio `
        -drive "if=pflash,format=raw,readonly=on,file=$ovmf" `
        -drive "if=pflash,format=raw,file=$vars_tmp" `
        -drive "format=raw,file=$ImagePath" `
        -machine pc `
        -m "${QemuMemMB}M" `
        -cpu max `
        -smp 2 `
        -monitor none

    Remove-Item $vars_tmp -ErrorAction SilentlyContinue
    exit 0
}

# ── USB FLASH ─────────────────────────────────────────────────────────────────
Write-Host "══════════════════════════════════════════════" -ForegroundColor Yellow
Write-Host "  Flash to USB Drive" -ForegroundColor Yellow
Write-Host "══════════════════════════════════════════════" -ForegroundColor Yellow
Write-Host ""

# List USB drives
$usbDisks = Get-Disk | Where-Object { $_.BusType -eq 'USB' } | Sort-Object Number
if ($usbDisks.Count -eq 0) {
    Write-Host "[INFO] No USB drives detected." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Insert a USB drive (4GB minimum) and re-run, or:" -ForegroundColor White
    Write-Host "  Use Rufus (recommended for Windows):" -ForegroundColor White
    Write-Host "    1. Download Rufus: https://rufus.ie" -ForegroundColor Cyan
    Write-Host "    2. Open Rufus" -ForegroundColor Cyan
    Write-Host "    3. Device: select your USB drive" -ForegroundColor Cyan
    Write-Host "    4. SELECT -> $ImagePath" -ForegroundColor Cyan
    Write-Host "    5. Partition scheme: GPT" -ForegroundColor Cyan
    Write-Host "    6. Target system: UEFI (non CSM)" -ForegroundColor Cyan
    Write-Host "    7. Click START (will ask for DD mode - click YES)" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  Or test in QEMU:" -ForegroundColor White
    Write-Host "    .\flash-hud-usb.ps1 -TestQemu" -ForegroundColor Cyan
    exit 0
}

Write-Host "  Available USB drives:" -ForegroundColor White
foreach ($disk in $usbDisks) {
    $diskGB = [math]::Round($disk.Size / 1GB, 1)
    $parts  = Get-Partition -DiskNumber $disk.Number -ErrorAction SilentlyContinue
    $labels = ($parts | ForEach-Object {
        try { (Get-Volume -Partition $_ -ErrorAction SilentlyContinue).FileSystemLabel } catch { "" }
    }) -join ", "
    Write-Host "    Disk $($disk.Number): $diskGB GB  [$($disk.FriendlyName)] $labels" -ForegroundColor Cyan
}
Write-Host ""

# Safety confirmation
Write-Host "⚠️  WARNING: This will ERASE all data on the selected USB drive!" -ForegroundColor Red
Write-Host ""
$diskNum = Read-Host "Enter disk number to flash (or ENTER to cancel)"
if ($diskNum -eq "") {
    Write-Host "Cancelled." -ForegroundColor Yellow
    exit 0
}

$target = $usbDisks | Where-Object { $_.Number -eq [int]$diskNum }
if (-not $target) {
    Write-Error "Disk $diskNum not found or not a USB drive."
    exit 1
}
$targetGB = [math]::Round($target.Size / 1GB, 1)
Write-Host ""
Write-Host "  Target : Disk $diskNum ($targetGB GB) — $($target.FriendlyName)" -ForegroundColor Red
Write-Host "  Image  : $ImagePath ($imgMB MB)" -ForegroundColor Yellow
Write-Host ""
$confirm = Read-Host "Type YES to confirm flash (anything else cancels)"
if ($confirm -ne "YES") {
    Write-Host "Cancelled." -ForegroundColor Yellow
    exit 0
}

# Flash via WSL dd (most reliable on Windows for raw image writing)
Write-Host ""
Write-Host "[FLASH] Writing image to disk $diskNum via WSL dd..." -ForegroundColor Yellow
Write-Host "        This may take 1-3 minutes..." -ForegroundColor Gray

$wsl_img = "/mnt/c" + ($ImagePath -replace "^C:", "" -replace "\\", "/")
$wsl_dev = "/dev/sd$( [char]([int][char]'a' + $diskNum) )"

# Use diskpart to offline the disk first
$diskpartScript = @"
select disk $diskNum
offline disk
"@
$dpFile = Join-Path $env:TEMP "dp_offline.txt"
$diskpartScript | Out-File $dpFile -Encoding ASCII
diskpart /s $dpFile | Out-Null
Remove-Item $dpFile -ErrorAction SilentlyContinue

# Flash with wsl dd
wsl -e bash -c "sudo dd if='$wsl_img' of='$wsl_dev' bs=4M status=progress && sync"
if ($LASTEXITCODE -ne 0) {
    Write-Error "dd failed. Try using Rufus instead."
    exit 1
}

# Re-online the disk
$diskpartScript2 = @"
select disk $diskNum
online disk
"@
$dpFile2 = Join-Path $env:TEMP "dp_online.txt"
$diskpartScript2 | Out-File $dpFile2 -Encoding ASCII
diskpart /s $dpFile2 | Out-Null
Remove-Item $dpFile2 -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "══════════════════════════════════════════════" -ForegroundColor Green
Write-Host "  USB Flash Complete!" -ForegroundColor Green
Write-Host "══════════════════════════════════════════════" -ForegroundColor Green
Write-Host ""
Write-Host "  Boot your PC from this USB drive." -ForegroundColor White
Write-Host "  Make sure UEFI Secure Boot is DISABLED." -ForegroundColor Yellow
Write-Host "  Set boot order: USB first in BIOS/UEFI." -ForegroundColor Yellow
Write-Host ""
Write-Host "  On most PCs: press F12/F2/DEL at boot to access boot menu." -ForegroundColor Gray
Write-Host ""
