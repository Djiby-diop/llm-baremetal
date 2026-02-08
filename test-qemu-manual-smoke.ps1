param(
    [string]$ImagePath,
    [string]$OvmfPath,
    [ValidateSet('whpx','tcg','none')][string]$Accel = 'tcg',
    [int]$MemMB = 1024,
    [int]$TimeoutSec = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Set-Location -LiteralPath $PSScriptRoot

. (Join-Path $PSScriptRoot 'tools\fat83.ps1')

function Resolve-FirstExistingPath([string[]]$candidates) {
    foreach ($p in $candidates) {
        if ($p -and (Test-Path $p)) { return (Get-Item $p).FullName }
    }
    return $null
}

function Resolve-OvmfPath([string]$override) {
    if ($override) {
        if (-not (Test-Path $override)) { throw "OVMF not found at -OvmfPath: $override" }
        return (Get-Item $override).FullName
    }

    $candidates = @(
        'C:\Program Files\qemu\share\edk2-x86_64-code.fd',
        'C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd',
        'C:\msys64\usr\share\edk2-ovmf\x64\OVMF_CODE.fd',
        'C:\msys64\usr\share\ovmf\x64\OVMF_CODE.fd'
    )

    $found = Resolve-FirstExistingPath $candidates
    if ($found) { return $found }

    throw 'OVMF not found. Provide -OvmfPath (UEFI firmware .fd).'
}

function Get-QemuExe {
    $cmd = Get-Command 'qemu-system-x86_64.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $common = @(
        'C:\Program Files\qemu\qemu-system-x86_64.exe',
        'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
        'C:\msys64\mingw64\bin\qemu-system-x86_64.exe',
        'C:\msys64\usr\bin\qemu-system-x86_64.exe'
    )
    $found = Resolve-FirstExistingPath $common
    if ($found) { return $found }

    throw 'QEMU not found. Ensure qemu-system-x86_64.exe is installed or add it to PATH.'
}

function Show-SerialTail([string]$serialLog) {
    Write-Host "`n[Debug] Serial log tail:" -ForegroundColor Yellow
    if ($serialLog -and (Test-Path $serialLog)) {
        Get-Content -Path $serialLog -Tail 260 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host '(serial log missing)' -ForegroundColor Yellow
    }
}

function Get-DefaultImagePath {
    $candidates = @(Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending)
    if ($candidates -and $candidates.Length -gt 0) {
        return $candidates[0].FullName
    }
    $fallback = Join-Path $PSScriptRoot 'llm-baremetal-boot.img'
    throw "Image not found. Provide -ImagePath or build it first (expected $fallback)."
}

function ConvertTo-DriveFileArg([string]$p) {
    if (-not $p) { return '""' }
    return '"' + ($p -replace '"','""') + '"'
}

$image = if ($ImagePath) { (Get-Item $ImagePath).FullName } else { Get-DefaultImagePath }
$ovmf = Resolve-OvmfPath $OvmfPath
$qemu = Get-QemuExe

Write-Host ("[Smoke] manual boot (no injection): image={0}" -f $image) -ForegroundColor Cyan

$serialLog = Join-Path $env:TEMP ("llm-baremetal-serial-manual-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))
$tmpOut = Join-Path $env:TEMP ("llm-baremetal-qemu-manual-out-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))
$tmpErr = Join-Path $env:TEMP ("llm-baremetal-qemu-manual-err-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))

$accelArgs = @()
switch ($Accel) {
    'whpx' { $accelArgs = @('-accel','whpx') }
    'tcg'  { $accelArgs = @('-accel','tcg') }
    'none' { $accelArgs = @() }
    default { $accelArgs = @() }
}

$qemuArgs = @(
    '-m', "$MemMB",
    '-machine', 'q35',
    '-cpu', 'qemu64',
    '-snapshot',
    '-drive', ("if=ide,format=raw,file=" + (ConvertTo-DriveFileArg $image)),
    '-drive', ("if=pflash,format=raw,readonly=on,file=" + (ConvertTo-DriveFileArg $ovmf)),
    '-display', 'none',
    '-serial', "file:$serialLog"
) + $accelArgs

$needles = @(
    'OK: Djibion boot',
    'OK: Model loaded:',
    'OK: Version:'
)

Write-Host ("[Smoke] Starting QEMU (accel={0}, mem={1}MB, timeout={2}s)" -f $Accel, $MemMB, $TimeoutSec) -ForegroundColor Cyan

$proc = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -NoNewWindow -PassThru -RedirectStandardOutput $tmpOut -RedirectStandardError $tmpErr

try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $serial = ''

    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) {
            break
        }

        if (Test-Path $serialLog) {
            $serial = Get-Content -Path $serialLog -Raw -ErrorAction SilentlyContinue
        }

        $ok = $true
        foreach ($n in $needles) {
            if (-not ($serial -and $serial.Contains($n))) { $ok = $false; break }
        }

        if ($ok) {
            Write-Host '[OK] Manual boot smoke passed.' -ForegroundColor Green
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            exit 0
        }

        Start-Sleep -Milliseconds 250
    }

    Write-Host '[FAIL] Manual boot smoke failed.' -ForegroundColor Red
    Write-Host '  Expected to see all markers:' -ForegroundColor Red
    $needles | ForEach-Object { Write-Host ("   - {0}" -f $_) -ForegroundColor Red }

    if ($serial -match 'BOOTX64\.EFI returned\. You are in the UEFI shell') {
        Write-Host '  Detected UEFI shell fallback; startup.nsh may not have run.' -ForegroundColor Yellow
    }

    Show-SerialTail $serialLog

    if (Test-Path $tmpErr) {
        Write-Host "`n[Debug] QEMU stderr tail:" -ForegroundColor Yellow
        Get-Content -Path $tmpErr -Tail 120 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    }

    exit 1
} finally {
    try { if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } } catch {}
}
