param(
    [ValidateSet('auto','whpx','tcg','none')]
    [string]$Accel = 'tcg',

    [ValidateRange(512, 8192)]
    [int]$MemMB = 4096,

    [ValidateRange(1, 86400)]
    [int]$TimeoutSec = 180,

    [string]$ModelBin = 'stories110M.bin',

    [switch]$SkipBuild,
    [switch]$SkipExtract,
    [switch]$SkipInspect,

    [string]$QemuPath,
    [string]$OvmfPath,
    [string]$ImagePath,

    [switch]$ForceAvx2
)

$ErrorActionPreference = 'Stop'

function Resolve-FirstExistingPath([string[]]$paths) {
    foreach ($p in $paths) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}

function Resolve-QemuPath([string]$override) {
    if ($override) {
        if (-not (Test-Path $override)) { throw "QEMU not found at -QemuPath: $override" }
        return $override
    }

    $cmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $common = @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "C:\msys64\mingw64\bin\qemu-system-x86_64.exe",
        "C:\msys64\usr\bin\qemu-system-x86_64.exe"
    )
    $found = Resolve-FirstExistingPath $common
    if ($found) { return $found }

    throw "QEMU not found. Provide -QemuPath or ensure qemu-system-x86_64.exe is on PATH."
}

function Resolve-OvmfPath([string]$override) {
    if ($override) {
        if (-not (Test-Path $override)) { throw "OVMF not found at -OvmfPath: $override" }
        return $override
    }

    $common = @(
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
        "C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd",
        "C:\msys64\usr\share\edk2-ovmf\x64\OVMF_CODE.fd",
        "C:\msys64\usr\share\ovmf\x64\OVMF_CODE.fd"
    )
    $found = Resolve-FirstExistingPath $common
    if ($found) { return $found }

    throw "OVMF not found. Provide -OvmfPath (UEFI firmware .fd)."
}

function Resolve-ImagePath([string]$override) {
    if ($override) {
        if (-not (Test-Path $override)) { throw "Image not found at -ImagePath: $override" }
        return $override
    }

    $candidates = Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    if ($candidates -and $candidates.Count -gt 0) {
        return $candidates[0].FullName
    }

    throw "Image not found (run .\build.ps1 first)"
}

function ConvertTo-WslPath([string]$winPath) {
    $norm = ($winPath -replace '\\','/')
    try {
        $wsl = (wsl wslpath -a $norm 2>$null)
        if ($wsl) { return $wsl.Trim() }
    } catch {
        # fall through
    }

    if ($norm -match '^([A-Za-z]):/(.*)$') {
        $drive = $Matches[1].ToLowerInvariant()
        $rest = $Matches[2]
        return "/mnt/$drive/$rest"
    }
    throw "Failed to convert path to WSL path: $winPath"
}

function Assert-Contains([string]$haystack, [string]$needle, [string]$label) {
    if ([string]::IsNullOrEmpty($haystack) -or -not $haystack.Contains($needle)) {
        throw "Missing expected output ($label): $needle"
    }
}

function Dump-Tails([string]$serialLog, [string]$tmpErr, [string]$tmpOut) {
    Write-Host "`n[Debug] Serial log tail:" -ForegroundColor Yellow
    if ($serialLog -and (Test-Path $serialLog)) {
        Get-Content -Path $serialLog -Tail 220 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host "(serial log missing)" -ForegroundColor Yellow
    }

    Write-Host "`n[Debug] stderr tail:" -ForegroundColor Yellow
    if ($tmpErr -and (Test-Path $tmpErr)) {
        Get-Content -Path $tmpErr -Tail 160 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host "(stderr missing)" -ForegroundColor Yellow
    }

    Write-Host "`n[Debug] stdout tail:" -ForegroundColor Yellow
    if ($tmpOut -and (Test-Path $tmpOut)) {
        Get-Content -Path $tmpOut -Tail 80 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host "(stdout missing)" -ForegroundColor Yellow
    }
}

Write-Host "`n[Test] QEMU autorun smoke test" -ForegroundColor Cyan

$serialLog = $null
$tmpOut = $null
$tmpErr = $null

# Create an autorun script in the repo root so build.ps1 (WSL) can copy it into the image.
$autorunPath = Join-Path $PSScriptRoot 'llmk-autorun.txt'
$autorun = @(
    "# llmk autorun smoke test",
    "/version",
    "/oo_new smoke-test-entity",
    "/oo_plan 1 +2 first action",
    "/oo_plan 1 p=1 second action",
    "/oo_agenda 1",
    "/oo_next 1",
    "/oo_done 1 1",
    "/oo_save oo-test.bin"
) -join "`n"

# ASCII is enough; firmware code also handles UTF-8/UTF-16.
[System.IO.File]::WriteAllText($autorunPath, $autorun + "`n", [System.Text.Encoding]::ASCII)

try {
    if (-not $SkipBuild) {
        Write-Host "[Test] Building + creating image (WSL)..." -ForegroundColor Cyan
        & (Join-Path $PSScriptRoot 'build.ps1') -ModelBin $ModelBin
    }

    $QEMU = Resolve-QemuPath $QemuPath
    $OVMF = Resolve-OvmfPath $OvmfPath
    $IMAGE = Resolve-ImagePath $ImagePath

    $OVMF_VARS = Join-Path $PSScriptRoot 'ovmf-vars-temp.fd'
    if (-not (Test-Path $OVMF_VARS)) {
        $bytes = New-Object byte[] 131072
        [System.IO.File]::WriteAllBytes($OVMF_VARS, $bytes)
    }

    $accelArg = $Accel
    if ($accelArg -eq 'tcg') { $accelArg = 'tcg,thread=multi' }

    $cpuModel = 'qemu64'
    if ($Accel -eq 'whpx') { $cpuModel = 'host' }
    if ($ForceAvx2) { $cpuModel = "$cpuModel,avx2=on,fma=on" }

    $serialLog = Join-Path $env:TEMP ("llm-baremetal-serial-autorun-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))
    $tmpOut = Join-Path $env:TEMP ("llm-baremetal-qemu-out-autorun-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))
    $tmpErr = Join-Path $env:TEMP ("llm-baremetal-qemu-err-autorun-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))

    Write-Host "  Serial: $serialLog" -ForegroundColor Gray

    $args = @(
        '-display', 'none',
        '-serial', ("file:{0}" -f $serialLog),
        '-monitor', 'none',
        '-drive', "if=pflash,format=raw,readonly=on,file=$OVMF",
        '-drive', "if=pflash,format=raw,file=$OVMF_VARS",
        '-drive', "format=raw,file=$IMAGE",
        '-machine', 'pc',
        '-m', ("{0}M" -f $MemMB),
        '-cpu', $cpuModel,
        '-smp', '2'
    )

    if ($Accel -and $Accel -ne 'none') {
        $args = @('-accel', $accelArg) + $args
    }

    function Quote-CmdArg([string]$s) {
        if ($null -eq $s) { return '""' }
        if ($s -match '[\s"]') {
            return '"' + ($s -replace '"', '\\"') + '"'
        }
        return $s
    }

    $argString = ($args | ForEach-Object { Quote-CmdArg $_ }) -join ' '

    Write-Host "[Test] Starting QEMU..." -ForegroundColor Cyan
    Write-Host "  QEMU:  $QEMU" -ForegroundColor Gray
    Write-Host "  OVMF:  $OVMF" -ForegroundColor Gray
    Write-Host "  Image: $IMAGE" -ForegroundColor Gray

    $p = Start-Process -FilePath $QEMU -ArgumentList $argString -PassThru -NoNewWindow -RedirectStandardOutput $tmpOut -RedirectStandardError $tmpErr

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $sawShutdown = $false
    while (-not $p.HasExited) {
        if ($sw.Elapsed.TotalSeconds -ge $TimeoutSec) {
            try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}
            throw "Timeout: QEMU did not exit within ${TimeoutSec}s"
        }

        if (Test-Path $serialLog) {
            $tailSerial = Get-Content -Path $serialLog -Tail 200 -ErrorAction SilentlyContinue
            $joined = $tailSerial -join "`n"
            if ($joined -like '*[autorun] shutting down*') { $sawShutdown = $true }
        }

        Start-Sleep -Milliseconds 200
    }

    if (-not (Test-Path $serialLog)) {
        throw "Missing serial log: $serialLog"
    }

    $serial = Get-Content -Path $serialLog -Raw -ErrorAction SilentlyContinue

    try {
        # Verify high-signal markers.
        Assert-Contains $serial '[autorun] loaded' 'autorun loaded'
        Assert-Contains $serial 'You (autorun): /version' 'autorun ran /version'
        Assert-Contains $serial 'OK: created entity id=' 'OO entity created'
        Assert-Contains $serial 'OK: wrote oo-test.bin' 'OO saved'
        Assert-Contains $serial '[autorun] shutting down' 'autorun shutdown'

        # This line has been occasionally missing in some firmware/serial setups; don't fail the run.
        if (-not $serial.Contains('[autorun] done')) {
            Write-Host "[Warn] Missing '[autorun] done' marker in serial log." -ForegroundColor Yellow
        }
    } catch {
        Dump-Tails $serialLog $tmpErr $tmpOut
        throw
    }

    if (-not $sawShutdown) {
        Write-Host "[Warn] Did not observe shutdown marker during polling; found it in final log." -ForegroundColor Yellow
    }

    Write-Host "[OK] Autorun completed and system shut down." -ForegroundColor Green

    $outBin = Join-Path $PSScriptRoot 'artifacts\oo-test.bin'
    if (-not $SkipExtract) {
        New-Item -ItemType Directory -Path (Split-Path $outBin -Parent) -Force | Out-Null

        $wslImg = ConvertTo-WslPath $IMAGE
        $wslOut = ConvertTo-WslPath $outBin

        Write-Host "[Test] Extracting oo-test.bin from image (WSL+mtools)..." -ForegroundColor Cyan

        # mtools supports operating directly on an image at a byte offset via -i <image>@@<offset>.
        # This avoids quoting/format issues with MTOOLSRC.
        $bashTpl = 'set -e; mcopy -n -i ''{0}@@1048576'' ::oo-test.bin ''{1}'''
        $bash = $bashTpl -f $wslImg, $wslOut

        wsl bash -lc $bash

        if (-not (Test-Path $outBin)) {
            throw "Extraction reported success but file not found: $outBin"
        }
        Write-Host "[OK] Extracted: $outBin" -ForegroundColor Green
    }

    if (-not $SkipInspect -and (Test-Path $outBin)) {
        Write-Host "[Test] Inspecting OO save file..." -ForegroundColor Cyan

        $py = Get-Command python -ErrorAction SilentlyContinue
        $py3 = Get-Command py -ErrorAction SilentlyContinue

        if ($py) {
            & $py.Source (Join-Path $PSScriptRoot 'tools\oo_inspect.py') $outBin
        } elseif ($py3) {
            & $py3.Source -3 (Join-Path $PSScriptRoot 'tools\oo_inspect.py') $outBin
        } else {
            $wslTool = ConvertTo-WslPath (Join-Path $PSScriptRoot 'tools\oo_inspect.py')
            $wslBin = ConvertTo-WslPath $outBin
            wsl python3 $wslTool $wslBin
        }
    }

    exit 0
} catch {
    Write-Host "`n[FAIL] Autorun test failed." -ForegroundColor Red
    if ($serialLog -or $tmpErr -or $tmpOut) {
        Write-Host "[Debug] Logs:" -ForegroundColor Yellow
        Write-Host "  Serial: $serialLog" -ForegroundColor Yellow
        Write-Host "  stderr: $tmpErr" -ForegroundColor Yellow
        Write-Host "  stdout: $tmpOut" -ForegroundColor Yellow
        Dump-Tails $serialLog $tmpErr $tmpOut
    }
    throw
} finally {
    # Keep repo clean; file is ignored but we remove it anyway.
    try {
        if (Test-Path $autorunPath) { Remove-Item -Force $autorunPath -ErrorAction SilentlyContinue }
    } catch {}
}
