[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Write-Check {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Detail
    )

    $mark = if ($Ok) { '[OK] ' } else { '[FAIL]' }
    $color = if ($Ok) { 'Green' } else { 'Red' }
    Write-Host ("{0} {1}: {2}" -f $mark, $Name, $Detail) -ForegroundColor $color
}

function Find-Wsl {
    $cmd = Get-Command wsl -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) { return $cmd.Source }

    $candidates = @(
        (Join-Path $env:SystemRoot 'System32\wsl.exe'),
        (Join-Path $env:SystemRoot 'Sysnative\wsl.exe'),
        'C:\Windows\System32\wsl.exe'
    )
    foreach ($path in $candidates) {
        if ($path -and (Test-Path $path)) { return $path }
    }
    return $null
}

$allOk = $true

Write-Host "=== LLM Baremetal Host Preflight ===" -ForegroundColor Cyan
Write-Host ("Host: {0} | PowerShell: {1}" -f $env:COMPUTERNAME, $PSVersionTable.PSVersion)

$wslExe = Find-Wsl
if (-not $wslExe) {
    Write-Check -Name 'WSL executable' -Ok $false -Detail 'wsl.exe not found in PATH/System32'
    $allOk = $false
} else {
    Write-Check -Name 'WSL executable' -Ok $true -Detail $wslExe
}

$fwVirtEnabled = $false
try {
    $sys = systeminfo 2>$null | Out-String
    $fwVirtEnabled = [bool]($sys -match '(?im)^\s*Virtualization Enabled In Firmware:\s*Yes\s*$')
} catch {
    $fwVirtEnabled = $false
}

$fwVirtDetail = if ($fwVirtEnabled) {
    'enabled in BIOS/UEFI'
} else {
    'disabled in BIOS/UEFI (WSL2 may fail with 0x80370102)'
}
Write-Check -Name 'Firmware virtualization' -Ok $fwVirtEnabled -Detail $fwVirtDetail

$wslListRaw = $null
$wslListExit = 1
$hasWslDistro = $false
$hasWsl2 = $false
$wslDistroName = $null
if ($wslExe) {
    try {
        $wslNames = & $wslExe -l -q 2>&1
        $namesExit = $LASTEXITCODE
        if ($namesExit -eq 0) {
            $nameLines = @($wslNames | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -and $_ -notmatch '^Windows Subsystem for Linux' })
            $hasWslDistro = ($nameLines.Count -gt 0)
            if ($hasWslDistro) {
                $wslDistroName = $nameLines[0]
            }
        }

        $wslListRaw = & $wslExe -l -v 2>&1
        $wslListExit = $LASTEXITCODE
        $text = ($wslListRaw | Out-String)
        if ($hasWslDistro) {
            if ($wslListExit -eq 0) {
                # Locale-agnostic parse: any non-header row ending with version "2".
                if ($text -match '(?m)^\s*\*?\s*.+\s+\S+\s+2\s*$') {
                    $hasWsl2 = $true
                }
            }
        }
    } catch {
        $wslListRaw = @($_.Exception.Message)
    }

    $wslDistroDetail = if ($hasWslDistro) { 'at least one distro installed' } else { 'no distro detected' }
    Write-Check -Name 'WSL distro' -Ok $hasWslDistro -Detail $wslDistroDetail
    if (-not $hasWslDistro) { $allOk = $false }

    $wsl2Detail = if ($hasWsl2) { 'version 2 detected' } else { 'no WSL2 distro detected' }
    Write-Check -Name 'WSL2' -Ok $hasWsl2 -Detail $wsl2Detail
}

$qemuCmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
if ($qemuCmd) {
    Write-Check -Name 'QEMU' -Ok $true -Detail $qemuCmd.Source
} else {
    Write-Check -Name 'QEMU' -Ok $false -Detail 'qemu-system-x86_64.exe not found in PATH'
    $allOk = $false
}

$ovmfCandidates = @(
    'C:\Program Files\qemu\share\edk2-x86_64-code.fd',
    'C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd',
    'C:\msys64\usr\share\edk2-ovmf\x64\OVMF_CODE.fd',
    'C:\msys64\usr\share\ovmf\x64\OVMF_CODE.fd'
)
$ovmfFound = $ovmfCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$ovmfDetail = if ($ovmfFound) { $ovmfFound } else { 'not found in common locations' }
Write-Check -Name 'OVMF firmware' -Ok ([bool]$ovmfFound) -Detail $ovmfDetail
if (-not $ovmfFound) { $allOk = $false }

if ($wslExe -and $hasWslDistro -and $wslDistroName) {
    try {
        $pkgCheck = & $wslExe -d $wslDistroName -- bash -lc 'for p in mtools parted dosfstools; do command -v $p >/dev/null 2>&1 || echo MISSING:$p; done' 2>&1
        $missing = @($pkgCheck | Where-Object { $_ -like 'MISSING:*' })
        $pkgOk = ($missing.Count -eq 0)
        $pkgDetail = if ($pkgOk) { 'mtools/parted/dosfstools OK' } else { ($missing -join ', ') }
        Write-Check -Name 'WSL packages' -Ok $pkgOk -Detail $pkgDetail
        if (-not $pkgOk) { $allOk = $false }
    } catch {
        Write-Check -Name 'WSL packages' -Ok $false -Detail 'unable to query distro packages'
        $allOk = $false
    }
}

Write-Host ''
if ($allOk) {
    Write-Host 'Preflight: READY ✅' -ForegroundColor Green
    Write-Host 'Next:' -ForegroundColor Cyan
    Write-Host '  .\test-qemu-autorun.ps1 -Mode smoke'
    exit 0
}

Write-Host 'Preflight: NOT READY ❌' -ForegroundColor Red
Write-Host 'Suggested fixes:' -ForegroundColor Yellow
Write-Host '  1) Install/enable WSL2 and reboot if requested:'
Write-Host '     wsl --install'
Write-Host '  2) Install a distro and set version 2:'
Write-Host '     wsl --install -d Ubuntu'
Write-Host '     wsl --set-default-version 2'
Write-Host '  3) In WSL, install packages:'
Write-Host '     sudo apt-get update && sudo apt-get install -y build-essential gnu-efi mtools parted dosfstools grub-pc-bin'
Write-Host '  4) Ensure QEMU is installed and in PATH.'
exit 1
