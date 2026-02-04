[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('auto','whpx','tcg','none')]
    [string]$Accel = 'tcg',

    [ValidateRange(512, 8192)]
    [int]$MemMB = 4096,

    [ValidateRange(1, 86400)]
    [Alias('TimeoutSeconds')]
    [int]$TimeoutSec = 300,

    # If omitted, the script will auto-select a model file.
    [string]$ModelBin = '',

    # Optional: override repl.cfg model=<...> (e.g. 'models\\stories15M.q8_0.gguf')
    # without changing which model build.ps1 bundles into the image.
    [string]$BootModel = '',

    # Optional: copy an extra model file into ::models/ in the boot image for the test.
    # Can be an absolute path or a filename relative to this script folder.
    [string]$ExtraModel = '',

    # Optional: expected loaded model name/path in serial output.
    # Useful when BootModel is a GGUF that may fall back to a bundled .bin.
    [string]$ExpectedModel = '',

    # If set, skips strict checks for the "OK: Model loaded:" line.
    # Useful while iterating on GGUF probing/fallback behaviors.
    [switch]$SkipModelAssertions,

    [switch]$SkipBuild,
    [switch]$SkipExtract,
    [switch]$SkipInspect,

    [string]$QemuPath,
    [string]$OvmfPath,
    [string]$ImagePath,

    [switch]$ForceAvx2,

    [ValidateSet('smoke','q8bench','ram','gen','custom','gguf_smoke')]
    [string]$Mode = 'smoke',

    # Optional repl.cfg overrides injected into the boot image for the duration of the test.
    # These map to keys parsed by llama2_efi_final.c.
    [ValidateRange(0, 32768)]
    [int]$CtxLen = 0,

    # -1 means "do not set" (leave default/image behavior). 0/1 explicitly set in repl.cfg.
    [ValidateSet(-1,0,1)]
    [int]$ModelPicker = -1,

    # Controls GOP loading overlay timing display (parsed by interface.h).
    # 0=off, 1=delta/total ms, 2=ETA/elapsed seconds.
    [ValidateSet(0,1,2)]
    [int]$OverlayTimeMode = 1,

    # If set, runs the test twice: once with overlay_time_mode=1 and once with =2.
    # Useful to keep both overlay paths healthy.
    [switch]$OverlayTimeMatrix,

    # Used when -Mode custom. Provide the whole script as a single string.
    # This avoids array/quoting issues when invoking via `powershell -File ...`.
    # Lines are split on newlines (CRLF or LF).
    [string]$AutorunScript = '',

    # Used when -Mode custom. Each entry becomes one REPL input line.
    [string[]]$AutorunLines = @(),

    # Optional: in -Mode custom, assert that a prompt triggered generation.
    # This checks for common REPL markers like "AI:" or "[gen] tokens=".
    [switch]$ExpectGeneration,
    # -Mode gen tuning knobs (for more stable tok/s comparisons)
    [ValidateRange(1, 256)]
    [int]$GenMaxTokens = 64,

    [ValidateRange(0.0, 2.0)]
    [double]$GenTemp = 0.2,

    # If set, -Mode gen fails unless it generates exactly GenMaxTokens.
    # Useful for apples-to-apples tok/s comparisons.
    [switch]$GenRequireFullTokens,

    [string]$GenPrompt = 'Repeat the word apple 200 times separated by spaces. No punctuation.'
)

$ErrorActionPreference = 'Stop'

function Resolve-DefaultModelSpec {
    $preferred = @(
        # Stable dev model (requested)
        'stories110M.bin'
    )

    $dirs = @(
        $PSScriptRoot,
        (Split-Path $PSScriptRoot -Parent)
    )

    foreach ($name in $preferred) {
        foreach ($d in $dirs) {
            if ($d -and (Test-Path (Join-Path $d $name))) { return $name }
        }
    }

    foreach ($d in $dirs) {
        if (-not $d) { continue }

        $bin = Get-ChildItem -Path $d -Filter '*.bin' -ErrorAction SilentlyContinue |
            Sort-Object Name |
            Select-Object -First 1
        if ($bin) { return $bin.Name }

        $gguf = Get-ChildItem -Path $d -Filter '*.gguf' -ErrorAction SilentlyContinue |
            Sort-Object Name |
            Select-Object -First 1
        if ($gguf) { return $gguf.Name }
    }

    throw "No model weights found next to the script (or one level up). Pass -ModelBin <file> (supports .bin or .gguf)."
}

if ((-not $PSBoundParameters.ContainsKey('ModelBin')) -and (-not $ModelBin)) {
    $ModelBin = Resolve-DefaultModelSpec
}

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
        'C:\Program Files\qemu\qemu-system-x86_64.exe',
        'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
        'C:\msys64\mingw64\bin\qemu-system-x86_64.exe',
        'C:\msys64\usr\bin\qemu-system-x86_64.exe'
    )
    $found = Resolve-FirstExistingPath $common
    if ($found) { return $found }

    throw 'QEMU not found. Provide -QemuPath or ensure qemu-system-x86_64.exe is on PATH.'
}

function Resolve-OvmfPath([string]$override) {
    if ($override) {
        if (-not (Test-Path $override)) { throw "OVMF not found at -OvmfPath: $override" }
        return $override
    }

    $common = @(
        'C:\Program Files\qemu\share\edk2-x86_64-code.fd',
        'C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd',
        'C:\msys64\usr\share\edk2-ovmf\x64\OVMF_CODE.fd',
        'C:\msys64\usr\share\ovmf\x64\OVMF_CODE.fd'
    )
    $found = Resolve-FirstExistingPath $common
    if ($found) { return $found }

    throw 'OVMF not found. Provide -OvmfPath (UEFI firmware .fd).'
}

function Resolve-ImagePath([string]$override) {
    if ($override) {
        if (-not (Test-Path $override)) { throw "Image not found at -ImagePath: $override" }
        return $override
    }

    # Prefer the most recently written image (supports timestamped images when an older one is locked by QEMU).
    $candidates = Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    if ($candidates -and $candidates.Count -gt 0) {
        return $candidates[0].FullName
    }

    $fallback = Join-Path $PSScriptRoot 'llm-baremetal-boot.img'
    throw "Image not found: $fallback (run .\\build.ps1 first)"
}

function Test-FileUnlockedForWrite([string]$path) {
    if (-not $path -or -not (Test-Path $path)) { return $false }
    try {
        $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        $fs.Dispose()
        return $true
    } catch {
        return $false
    }
}

function Get-TempImageCopy([string]$srcPath) {
    if (-not $srcPath -or -not (Test-Path $srcPath)) {
        throw "Image not found: $srcPath"
    }
    $tmp = Join-Path $env:TEMP ("llm-baremetal-boot-test-{0}.img" -f ([Guid]::NewGuid().ToString('n')))
    Copy-Item -Path $srcPath -Destination $tmp -Force
    return $tmp
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

function Get-EfiSystemPartitionOffsetBytes([string]$imagePath) {
    if (-not $imagePath -or -not (Test-Path $imagePath)) {
        throw "Image not found: $imagePath"
    }

    $ESP_TYPE = [Guid]'C12A7328-F81F-11D2-BA4B-00A0C93EC93B'
    $SECTOR = 512

    $fs = [System.IO.File]::Open($imagePath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $hdr = New-Object byte[] $SECTOR
        $fs.Position = $SECTOR
        $n = $fs.Read($hdr, 0, $hdr.Length)
        if ($n -ne $SECTOR) { throw 'Failed to read GPT header' }

        $sig = [System.Text.Encoding]::ASCII.GetString($hdr, 0, 8)
        if ($sig -ne 'EFI PART') { throw 'Not a GPT image (missing EFI PART signature)' }

        function Read-U32([byte[]]$b, [int]$o) { return [BitConverter]::ToUInt32($b, $o) }
        function Read-U64([byte[]]$b, [int]$o) { return [BitConverter]::ToUInt64($b, $o) }

        $partEntriesLba = Read-U64 $hdr 72
        $numEntries = Read-U32 $hdr 80
        $entrySize = Read-U32 $hdr 84

        if ($numEntries -lt 1 -or $entrySize -lt 128) { throw 'Invalid GPT partition entry metadata' }

        $entriesBytes = [UInt64]$numEntries * [UInt64]$entrySize
        if ($entriesBytes -gt 16MB) { throw "GPT partition array too large ($entriesBytes bytes)" }

        $entries = New-Object byte[] ([int]$entriesBytes)
        $fs.Position = [Int64]($partEntriesLba * $SECTOR)
        $rn = $fs.Read($entries, 0, $entries.Length)
        if ($rn -ne $entries.Length) { throw 'Failed to read GPT partition entries' }

        for ($i = 0; $i -lt $numEntries; $i++) {
            $off = $i * $entrySize
            $typeBytes = New-Object byte[] 16
            [Array]::Copy($entries, $off + 0, $typeBytes, 0, 16)
            $typeGuid = [Guid]::new($typeBytes)
            if ($typeGuid -eq $ESP_TYPE) {
                $firstLba = [BitConverter]::ToUInt64($entries, $off + 32)
                return [Int64]($firstLba * $SECTOR)
            }
        }

        throw 'EFI System Partition not found in GPT'
    } finally {
        $fs.Dispose()
    }
}

function Assert-Contains([string]$haystack, [string]$needle, [string]$label) {
    if ([string]::IsNullOrEmpty($haystack) -or -not $haystack.Contains($needle)) {
        throw "Missing expected output ($label): $needle"
    }
}

function Assert-Match([string]$haystack, [string]$pattern, [string]$label) {
    if ([string]::IsNullOrEmpty($haystack) -or -not ([regex]::IsMatch($haystack, $pattern))) {
        throw "Missing expected output ($label): /$pattern/"
    }
}

function Show-Tails([string]$serialLog, [string]$tmpErr, [string]$tmpOut) {
    Write-Host "`n[Debug] Serial log tail:" -ForegroundColor Yellow
    if ($serialLog -and (Test-Path $serialLog)) {
        Get-Content -Path $serialLog -Tail 220 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host '(serial log missing)' -ForegroundColor Yellow
    }

    Write-Host "`n[Debug] stderr tail:" -ForegroundColor Yellow
    if ($tmpErr -and (Test-Path $tmpErr)) {
        Get-Content -Path $tmpErr -Tail 160 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host '(stderr missing)' -ForegroundColor Yellow
    }

    Write-Host "`n[Debug] stdout tail:" -ForegroundColor Yellow
    if ($tmpOut -and (Test-Path $tmpOut)) {
        Get-Content -Path $tmpOut -Tail 80 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host '(stdout missing)' -ForegroundColor Yellow
    }
}

Write-Host "`n[Test] QEMU autorun test (mode=$Mode, model=$ModelBin)" -ForegroundColor Cyan

if ($OverlayTimeMatrix) {
    Write-Host "[Test] Overlay matrix: running modes 1 and 2" -ForegroundColor Cyan

    # Build once (if requested), then re-run self twice with -SkipBuild.
    if (-not $SkipBuild) {
        Write-Host '[Test] Building + creating image (WSL)...' -ForegroundColor Cyan
        & (Join-Path $PSScriptRoot 'build.ps1') -ModelBin $ModelBin
        if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
    }

    foreach ($m in @(1,2)) {
        Write-Host "`n[Test] OverlayTimeMode=$m" -ForegroundColor Cyan

        $p = @{
            Accel = $Accel
            MemMB = $MemMB
            TimeoutSec = $TimeoutSec
            ModelBin = $ModelBin
            SkipBuild = $true
            Mode = $Mode
            OverlayTimeMode = $m
        }
        if ($SkipExtract) { $p.SkipExtract = $true }
        if ($SkipInspect) { $p.SkipInspect = $true }
        if ($ForceAvx2) { $p.ForceAvx2 = $true }
        if ($QemuPath) { $p.QemuPath = $QemuPath }
        if ($OvmfPath) { $p.OvmfPath = $OvmfPath }
        if ($ImagePath) { $p.ImagePath = $ImagePath }
        if ($Mode -eq 'custom') {
            if ($AutorunScript) { $p.AutorunScript = $AutorunScript }
            if ($AutorunLines -and $AutorunLines.Count -gt 0) { $p.AutorunLines = $AutorunLines }
            if ($ExpectGeneration) { $p.ExpectGeneration = $true }
        }

        & $PSCommandPath @p
        if ($LASTEXITCODE -ne 0) {
            throw "Overlay matrix run failed for OverlayTimeMode=$m"
        }
    }

    Write-Host "`n[OK] Overlay matrix passed (1 and 2)." -ForegroundColor Green
    exit 0
}

$serialLog = $null
$tmpOut = $null
$tmpErr = $null

$IMAGE = $null
$fatOffset = $null
$didModifyImage = $false
$imageBackups = @()
$isTempImage = $false

# When -ExtraModel is injected, mtools copy uses a randomized temp name; the final file on the FAT volume is that temp basename.
$extraInjectedName = $null

# If invoked via `powershell -File`, passing a string[] can be finicky.
# Allow a single string containing newline-separated commands.
if ($Mode -eq 'custom' -and $AutorunScript) {
    $lines = ($AutorunScript -split "`r?`n") | ForEach-Object { $_.TrimEnd() } | Where-Object { $_ -ne '' }
    if ($lines -and $lines.Count -gt 0) {
        $AutorunLines = @($lines)
    }
}

$tmpCfgPath = Join-Path $env:TEMP ("llm-baremetal-replcfg-test-{0}.cfg" -f ([Guid]::NewGuid().ToString('n')))
$autorunPath = Join-Path $env:TEMP ("llm-baremetal-autorun-test-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))

function Backup-ImageFile {
    param(
        [Parameter(Mandatory=$true)][string]$ImagePath,
        [Parameter(Mandatory=$true)][int]$FatOffsetBytes,
        [Parameter(Mandatory=$true)][string]$ImageFile,
        [Parameter(Mandatory=$true)][string]$Label
    )

    $tmp = Join-Path $env:TEMP ("llm-baremetal-imgbk-{0}-{1}" -f $Label, ([Guid]::NewGuid().ToString('n')))
    $wslImg = ConvertTo-WslPath $ImagePath
    $wslTmp = ConvertTo-WslPath $tmp

    # mcopy returns non-zero if the file doesn't exist; that's fine.
    wsl bash -lc ("mcopy -o -i '{0}@@{1}' '::{2}' '{3}'" -f $wslImg, $FatOffsetBytes, $ImageFile, $wslTmp)
    $exists = ($LASTEXITCODE -eq 0) -and (Test-Path $tmp)

    if (-not $exists) {
        try { if (Test-Path $tmp) { Remove-Item -Force -ErrorAction SilentlyContinue $tmp } } catch {}
        return [pscustomobject]@{ ImageFile = $ImageFile; BackupPath = $null; Existed = $false }
    }

    return [pscustomobject]@{ ImageFile = $ImageFile; BackupPath = $tmp; Existed = $true }
}

function Restore-ImageFile {
    param(
        [Parameter(Mandatory=$true)][string]$ImagePath,
        [Parameter(Mandatory=$true)][int]$FatOffsetBytes,
        [Parameter(Mandatory=$true)][pscustomobject]$Entry
    )

    $wslImg = ConvertTo-WslPath $ImagePath
    if ($Entry.Existed -and $Entry.BackupPath -and (Test-Path $Entry.BackupPath)) {
        $wslTmp = ConvertTo-WslPath $Entry.BackupPath
        wsl bash -lc ("set -e && mcopy -o -i '{0}@@{1}' '{2}' '::{3}'" -f $wslImg, $FatOffsetBytes, $wslTmp, $Entry.ImageFile)
    } else {
        wsl bash -lc ("(mdel -i '{0}@@{1}' '::{2}' 2>/dev/null || true)" -f $wslImg, $FatOffsetBytes, $Entry.ImageFile)
    }
}

$autorunLinesEffective = @()
switch ($Mode) {
    'smoke' {
        $autorunLinesEffective = @(
            '# llmk autorun smoke test',
            '/version',
            '/oo_new smoke-test-entity',
            '/oo_plan 1 +2 first action',
            '/oo_plan 1 p=1 second action',
            '/oo_agenda 1',
            '/compat_status',
            '/calib_status',
            '/orch_status',
            '/orch_clear',
            '/orch_add /compat_status; /calib_status; /orch_status',
            '/orch_start 1',
            '/oo_next 1',
            '/oo_done 1 1',
            '/oo_save oo-test.bin'
        )
    }
    'q8bench' {
        $autorunLinesEffective = @(
            '# llmk autorun q8 bench',
            '/cfg',
            '/q8_matvec wq 0 30',
            '/q8_matvec w2 0 30'
        )
    }
    'ram' {
        $autorunLinesEffective = @(
            '# llmk autorun ram test',
            '/version',
            '/cfg',
            '/ram'
        )
    }
    'gen' {
        $autorunLinesEffective = @(
            '# llmk autorun gen smoke test',
            '/version',
            ("/max_tokens {0}" -f $GenMaxTokens),
            '/stats 1',
            '/stop_you 0',
            '/stop_nl 0',
            '/seed 123',
            ("/temp {0}" -f $GenTemp),
            $GenPrompt
        )
    }
    'gguf_smoke' {
        $autorunLinesEffective = @(
            '# llmk autorun gguf smoke test (injected model)',
            '/version',
            '/cfg',
            ("/max_tokens {0}" -f $GenMaxTokens),
            '/stats 1',
            '/stop_you 0',
            '/stop_nl 0',
            '/seed 123',
            ("/temp {0}" -f $GenTemp),
            'Hello! Give me one short sentence.'
        )
    }
    'custom' {
        if (-not $AutorunLines -or $AutorunLines.Count -eq 0) {
            throw '-Mode custom requires -AutorunLines or -AutorunScript'
        }
        $autorunLinesEffective = $AutorunLines
    }
}

[System.IO.File]::WriteAllText($autorunPath, ($autorunLinesEffective -join "`n") + "`n", [System.Text.Encoding]::ASCII)

# q8bench requires a Q8_0 GGUF in blob mode. Default to the bundled Q8_0 model
# if the caller didn't explicitly specify -BootModel.
if (-not $PSBoundParameters.ContainsKey('BootModel')) {
    if ($Mode -eq 'q8bench') {
        $BootModel = 'models\stories15M.q8_0.gguf'
    }
}

$tmpCfg = @(
    '# test-qemu-autorun.ps1 temporary config',
    "model=$ModelBin",
    'boot_verbose=1',
    "overlay_time_mode=$OverlayTimeMode",
    'autorun_autostart=1',
    'autorun_shutdown_when_done=1',
    'autorun_file=llmk-autorun.txt'
)

if ($Mode -eq 'q8bench') {
    $tmpCfg += 'gguf_q8_blob=1'
}

# Apply optional boot-time model override for repl.cfg.
# If an ExtraModel is injected into ::models/, allow the caller to refer to it as models\<basename>
# and rewrite to the actual injected filename.
$bootModelEffective = $BootModel
if ($ExtraModel -and $extraInjectedName) {
    $extraBase = $extraBaseName
    if (-not $bootModelEffective -and $Mode -eq 'gguf_smoke') {
        $bootModelEffective = ("models\\{0}" -f $extraInjectedName)
    } elseif ($bootModelEffective) {
        $bmNorm = $bootModelEffective -replace '/','\\'
        if ($bmNorm -match '^(models\\)(.+)$') {
            $tail = $Matches[2]
            if ($tail -ieq $extraBase) {
                $bootModelEffective = ("models\\{0}" -f $extraInjectedName)
            }
        }
    }

    # If expected model was not specified, infer it for injected runs.
    if (-not $ExpectedModel -and ($Mode -eq 'gguf_smoke' -or $bootModelEffective -match '^models\\')) {
        $ExpectedModel = ("models\\{0}" -f $extraInjectedName)
    }
}

if ($bootModelEffective) {
    $tmpCfg[1] = "model=$bootModelEffective"
}

if ($CtxLen -gt 0) {
    $tmpCfg += "ctx_len=$CtxLen"
}

if ($ModelPicker -ge 0) {
    $tmpCfg += "model_picker=$ModelPicker"
}

$tmpCfg = $tmpCfg -join "`n"

[System.IO.File]::WriteAllText($tmpCfgPath, $tmpCfg + "`n", [System.Text.Encoding]::ASCII)

try {
    if (-not $SkipBuild) {
        Write-Host '[Test] Building + creating image (WSL)...' -ForegroundColor Cyan
        & (Join-Path $PSScriptRoot 'build.ps1') -ModelBin $ModelBin
    }

    $QEMU = Resolve-QemuPath $QemuPath
    $OVMF = Resolve-OvmfPath $OvmfPath
    $IMAGE = Resolve-ImagePath $ImagePath

    # If we're injecting an ExtraModel, always operate on a temp copy.
    # This avoids leaving the primary image modified and sidesteps file-lock edge cases.
    if ($ExtraModel) {
        Write-Host "[Test] ExtraModel injection requested; copying image to temp for this run..." -ForegroundColor Yellow
        $IMAGE = Get-TempImageCopy $IMAGE
        $isTempImage = $true
        Write-Host "[Test] Using temp image: $IMAGE" -ForegroundColor Yellow
    }

    # If a QEMU GUI/manual run still has the image open, WSL+mtools can hang.
    # Detect that and operate on a temp copy instead.
    if (-not (Test-FileUnlockedForWrite $IMAGE)) {
        Write-Host "[Test] Image appears locked/in-use; copying to temp for this run..." -ForegroundColor Yellow
        $IMAGE = Get-TempImageCopy $IMAGE
        $isTempImage = $true
        Write-Host "[Test] Using temp image: $IMAGE" -ForegroundColor Yellow
    }

    $fatOffset = Get-EfiSystemPartitionOffsetBytes $IMAGE

    # Verify the image actually contains the EFI we just built.
    # This prevents false debugging sessions when QEMU runs a stale image/kernel.
    $localEfi = Join-Path $PSScriptRoot 'llama2.efi'
    if (-not (Test-Path $localEfi)) {
        throw "Local EFI not found: $localEfi (run .\\build.ps1 first)"
    }
    $localHash = (Get-FileHash -Path $localEfi -Algorithm SHA256).Hash.ToLowerInvariant()
    $tmpKernel = Join-Path $env:TEMP ("llm-baremetal-kernel-from-img-{0}.efi" -f ([Guid]::NewGuid().ToString('n')))
    $tmpBootx64 = Join-Path $env:TEMP ("llm-baremetal-bootx64-from-img-{0}.efi" -f ([Guid]::NewGuid().ToString('n')))
    $wslImgVerify = ConvertTo-WslPath $IMAGE
    $wslTmpKernel = ConvertTo-WslPath $tmpKernel
    $wslTmpBootx64 = ConvertTo-WslPath $tmpBootx64
    $bashExtractEfi = "set -e && mcopy -o -i '$wslImgVerify@@$fatOffset' ::KERNEL.EFI '$wslTmpKernel' && mcopy -o -i '$wslImgVerify@@$fatOffset' ::EFI/BOOT/BOOTX64.EFI '$wslTmpBootx64'"
    wsl bash -lc $bashExtractEfi

    $imgKernelHash = (Get-FileHash -Path $tmpKernel -Algorithm SHA256).Hash.ToLowerInvariant()
    $imgBootx64Hash = (Get-FileHash -Path $tmpBootx64 -Algorithm SHA256).Hash.ToLowerInvariant()
    try { Remove-Item -Force -ErrorAction SilentlyContinue $tmpKernel } catch {}
    try { Remove-Item -Force -ErrorAction SilentlyContinue $tmpBootx64 } catch {}

    if ($localHash -ne $imgKernelHash) {
        throw "Image/kernel mismatch: image KERNEL.EFI sha256=$imgKernelHash, local llama2.efi sha256=$localHash. Re-run .\\build.ps1 (and ensure QEMU is not holding the image open)."
    }
    if ($localHash -ne $imgBootx64Hash) {
        throw "Image/boot mismatch: image EFI/BOOT/BOOTX64.EFI sha256=$imgBootx64Hash, local llama2.efi sha256=$localHash. Re-run .\\build.ps1 (and ensure QEMU is not holding the image open)."
    }

    # Backup files we are going to modify so manual runs aren't left in autorun mode.
    if (-not $isTempImage) {
        $imageBackups = @(
            (Backup-ImageFile -ImagePath $IMAGE -FatOffsetBytes $fatOffset -ImageFile 'repl.cfg' -Label 'replcfg'),
            (Backup-ImageFile -ImagePath $IMAGE -FatOffsetBytes $fatOffset -ImageFile 'llmk-autorun.txt' -Label 'autorun'),
            (Backup-ImageFile -ImagePath $IMAGE -FatOffsetBytes $fatOffset -ImageFile 'NvVars' -Label 'nvvars'),
            (Backup-ImageFile -ImagePath $IMAGE -FatOffsetBytes $fatOffset -ImageFile 'oo-test.bin' -Label 'ootest'),
            (Backup-ImageFile -ImagePath $IMAGE -FatOffsetBytes $fatOffset -ImageFile 'oo-test.bin.bak' -Label 'ootestbak')
        )
    } else {
        $imageBackups = @()
    }

    # If requested, copy an extra model into the image under ::models/.
    $extraLocalPath = $null
    $extraBaseName = $null
    $extraImagePath = $null
    if ($ExtraModel) {
        $cand = $ExtraModel
        if (-not (Test-Path $cand)) {
            $cand = Join-Path $PSScriptRoot $ExtraModel
        }
        if (-not (Test-Path $cand)) {
            throw "ExtraModel not found: $ExtraModel (tried '$cand')"
        }
        $extraLocalPath = (Resolve-Path $cand).Path
        $extraBaseName = [System.IO.Path]::GetFileName($extraLocalPath)
        $extraImagePath = ("models/{0}" -f $extraBaseName)
        if (-not $isTempImage) {
            $imageBackups += (Backup-ImageFile -ImagePath $IMAGE -FatOffsetBytes $fatOffset -ImageFile $extraImagePath -Label 'extramodel')
        }
    }

    Write-Host '[Test] Injecting repl.cfg + llmk-autorun.txt into image (WSL+mtools)...' -ForegroundColor Cyan
    $wslImg = ConvertTo-WslPath $IMAGE
    $wslImgOriginal = $wslImg
    $wslCfg = ConvertTo-WslPath $tmpCfgPath
    $wslAr = ConvertTo-WslPath $autorunPath

    function Invoke-WslStep([string]$label, [string]$cmd, [int]$timeoutSec = 60) {
        Write-Host ("[Test] WSL: {0} (timeout={1}s)" -f $label, $timeoutSec) -ForegroundColor DarkCyan

        # Avoid brittle nested quoting by writing the command to a temporary bash script.
        $tmpSh = Join-Path $env:TEMP ("llmk-wsl-step-{0}.sh" -f ([Guid]::NewGuid().ToString('n')))
        $script = @(
            '#!/usr/bin/env bash',
            'set -e',
            'export MTOOLS_SKIP_CHECK=1',
            $cmd
        ) -join "`n"
        [System.IO.File]::WriteAllText(
            $tmpSh,
            $script + "`n",
            (New-Object System.Text.UTF8Encoding -ArgumentList @($false))
        )

        $wslSh = ConvertTo-WslPath $tmpSh
        $full = ("set -e; timeout --foreground {0}s bash '{1}'" -f $timeoutSec, $wslSh)
        try {
            # Tee-Object streams output while keeping it available for error reporting.
            $out = & wsl bash -lc $full 2>&1 | Tee-Object -Variable out
            if ($LASTEXITCODE -ne 0) {
                if ($out) {
                    Write-Host "[WSL] Output:" -ForegroundColor Yellow
                    $out | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
                }
                throw "WSL step failed ($label), exit=$LASTEXITCODE"
            }
        } finally {
            try { Remove-Item -Force -ErrorAction SilentlyContinue $tmpSh } catch {}
        }
    }

    # mtools writes against /mnt/c can occasionally hang. Stage a WSL-local copy for all write steps.
    $wslImgWork = ("/tmp/llmk-imgwork-{0}.img" -f ([Guid]::NewGuid().ToString('n')))
    Invoke-WslStep 'stage image into WSL temp' ("cp -f '{0}' '{1}'" -f $wslImgOriginal, $wslImgWork) 120
    $wslImg = $wslImgWork

    $extraCopyCmd = ''
    if ($extraLocalPath) {
        $wslExtra = ConvertTo-WslPath $extraLocalPath
        $extraInjectedName = $extraBaseName
        $extraCopyCmd = (
            "echo '[mtools] preflight root'; " +
            "mdir -i '$wslImg@@$fatOffset' ::/ >/dev/null; " +
            "if mdir -i '$wslImg@@$fatOffset' ::models >/dev/null 2>&1; then " +
                "echo '[mtools] ::models exists'; " +
            "else " +
                "echo '[mtools] creating ::models (timeout 20s)'; " +
                "timeout --foreground 20s mmd -i '$wslImg@@$fatOffset' ::models || true; " +
            "fi; " +
            "echo '[mtools] copying extra model into ::models/'; " +
            "ls -lh '$wslExtra' || true; " +
            "date; " +
            "timeout --foreground 240s mcopy -o -i '$wslImg@@$fatOffset' '$wslExtra' ::models/; " +
            "date; " +
            "echo '[mtools] extra model copy done'"
        )
    }

    $didModifyImage = $true

    # 1) Clear volatile files that can interfere with tests.
    Invoke-WslStep 'clear volatile files' (
        "(mdel -i '$wslImg@@$fatOffset' ::NvVars 2>/dev/null || true); " +
        "(mdel -i '$wslImg@@$fatOffset' ::oo-test.bin 2>/dev/null || true); " +
        "(mdel -i '$wslImg@@$fatOffset' ::oo-test.bin.bak 2>/dev/null || true)"
    ) 30

    # 2) Optional: copy an extra model under ::models/.
    if ($extraCopyCmd) {
        Invoke-WslStep 'copy ExtraModel into ::models/' $extraCopyCmd 300
    }

    # 3) Inject repl.cfg + autorun file.
    Invoke-WslStep 'write repl.cfg' ("mcopy -o -i '$wslImg@@$fatOffset' '$wslCfg' ::repl.cfg") 30
    Invoke-WslStep 'write llmk-autorun.txt' ("mcopy -o -i '$wslImg@@$fatOffset' '$wslAr' ::llmk-autorun.txt") 30

    # 4) Optional inspection (can be noisy); keep it skippable.
    if (-not $SkipInspect) {
        Invoke-WslStep 'inspect image root' ("mdir -i '$wslImg@@$fatOffset' ::/") 30
        Invoke-WslStep 'print repl.cfg' ("echo '--- repl.cfg ---'; mtype -i '$wslImg@@$fatOffset' ::repl.cfg") 30
        Invoke-WslStep 'print llmk-autorun.txt' ("echo '--- llmk-autorun.txt ---'; mtype -i '$wslImg@@$fatOffset' ::llmk-autorun.txt") 30
    }

    # Sync staged WSL-local image back to the Windows image path used by QEMU.
    Invoke-WslStep 'sync image back to Windows' ("cp -f '{0}' '{1}'" -f $wslImgWork, $wslImgOriginal) 120
    Invoke-WslStep 'remove WSL temp image' ("rm -f '{0}'" -f $wslImgWork) 30

    $serialLog = Join-Path $env:TEMP ("llm-baremetal-serial-autorun-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))
    $tmpOut = Join-Path $env:TEMP ("llm-baremetal-qemu-out-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))
    $tmpErr = Join-Path $env:TEMP ("llm-baremetal-qemu-err-{0}.txt" -f ([Guid]::NewGuid().ToString('n')))

    $accelArgs = @()
    switch ($Accel) {
        'whpx' { $accelArgs = @('-accel','whpx') }
        'tcg'  { $accelArgs = @('-accel','tcg') }
        'none' { $accelArgs = @() }
        default { $accelArgs = @() }
    }

    $cpu = 'qemu64'
    if ($ForceAvx2) {
        $cpu = 'qemu64,avx2=on'
    }

    function ConvertTo-DriveFileArg([string]$p) {
        if (-not $p) { return '""' }
        return '"' + ($p -replace '"','""') + '"'
    }

    $qemuArgs = @(
        '-m', "$MemMB",
        '-machine', 'q35',
        '-cpu', $cpu,
        '-snapshot',
        '-drive', ("if=ide,format=raw,file=" + (ConvertTo-DriveFileArg $IMAGE)),
        '-drive', ("if=pflash,format=raw,readonly=on,file=" + (ConvertTo-DriveFileArg $OVMF)),
        '-display', 'none',
        '-serial', "file:$serialLog"
    ) + $accelArgs

    Write-Host "[Test] Starting QEMU (accel=$Accel, mem=${MemMB}MB)..." -ForegroundColor Cyan

    $proc = Start-Process -FilePath $QEMU -ArgumentList $qemuArgs -NoNewWindow -PassThru -RedirectStandardOutput $tmpOut -RedirectStandardError $tmpErr

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $sawShutdown = $false
    $sawReplExit = $false
    $sawBootFailure = $false
    $bootFailureReason = ''

    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (Test-Path $serialLog) {
            $serial = Get-Content -Path $serialLog -Raw -ErrorAction SilentlyContinue
            if ($serial -and $serial.Contains('[autorun] shutting down')) {
                $sawShutdown = $true
                break
            }

            # In custom mode, the script may intentionally exit the REPL ("exit"/"quit")
            # which returns to the UEFI shell and blocks on a keypress. Treat that as completion.
            if ($Mode -eq 'custom' -and $serial) {
                if ($serial -match '(?m)^\s*\[7/7\]\s+Entering chat loop\.{3}\s*$' -and ($serial -match '(?i)\bGoodbye!\b' -or $serial -match '(?i)Press any key to exit')) {
                    $sawReplExit = $true
                    break
                }
            }

            # If the payload returns to the UEFI shell, we won't see the shutdown marker.
            # Detect common fatal boot failures early to avoid long timeouts.
            if ($serial) {
                if ($serial -match 'BOOTX64\.EFI returned\. You are in the UEFI shell') {
                    $sawBootFailure = $true
                    $bootFailureReason = 'Returned to UEFI shell'
                    break
                }
                if ($serial -match 'ERROR: llmk_zones_init failed') {
                    $sawBootFailure = $true
                    $bootFailureReason = 'llmk_zones_init failed'
                    break
                }
                if ($serial -match 'Out of Resources') {
                    $sawBootFailure = $true
                    if (-not $bootFailureReason) { $bootFailureReason = 'Out of Resources' }
                    break
                }
            }
        }
        if ($proc.HasExited) { break }
    }

    if (-not $proc.HasExited) {
        if ($sawBootFailure) {
            Write-Host "[Fail] Boot failure detected: $bootFailureReason" -ForegroundColor Red
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            throw "Boot failure detected: $bootFailureReason"
        }
        if ($sawShutdown -or $sawReplExit) {
            Write-Host '[Warn] Shutdown marker observed but QEMU did not exit; stopping QEMU process.' -ForegroundColor Yellow
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
        } else {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            throw "Timeout: QEMU did not exit within ${TimeoutSec}s"
        }
    }

    if (-not (Test-Path $serialLog)) {
        throw "Missing serial log: $serialLog"
    }

    $serial = Get-Content -Path $serialLog -Raw -ErrorAction SilentlyContinue
    if (-not $serial) {
        throw 'Failed to obtain serial log output from QEMU.'
    }

    # If the GGUF parser emits any COMPROMISED_DATA diagnostics, print them for quick copy/paste.
    $ggufDiag = [regex]::Matches($serial, 'GGUF: COMPROMISED_DATA:[^\r\n]*')
    if ($ggufDiag -and $ggufDiag.Count -gt 0) {
        Write-Host "[Diag] GGUF diagnostics:" -ForegroundColor Yellow
        foreach ($m in ($ggufDiag | Select-Object -First 6)) {
            Write-Host ("  " + $m.Value) -ForegroundColor Yellow
        }
    }

    # Also print a small set of GGUF-related lines when we are booting a GGUF override.
    $bootModelEffective = $ModelBin
    if ($BootModel) { $bootModelEffective = $BootModel }
    if ($bootModelEffective -match '\.gguf$') {
        $ggufLines = [regex]::Matches($serial, '(?m)^(GGUF:.*|GGUF detected:.*|NOTE: GGUF inference unsupported.*|OK: using \.bin fallback:.*)$')
        if ($ggufLines -and $ggufLines.Count -gt 0) {
            Write-Host "[Diag] GGUF log highlights:" -ForegroundColor Yellow
            foreach ($m in ($ggufLines | Select-Object -First 12)) {
                Write-Host ("  " + $m.Value) -ForegroundColor Yellow
            }
        } else {
            Write-Host "[Diag] No GGUF log lines matched (boot model override is .gguf)." -ForegroundColor Yellow
        }
    }

    $wantGenDiag = ($Mode -eq 'custom' -and $ExpectGeneration) -or ($Mode -eq 'gen')
    if ($wantGenDiag) {
        $genLines = [regex]::Matches($serial, '(?m)^(AI:.*|.*\[gen\] tokens=.*|.*\[stats\] tokens=.*|.*\[stop\].*)$')
        if ($genLines -and $genLines.Count -gt 0) {
            Write-Host "[Diag] Generation highlights:" -ForegroundColor Yellow
            foreach ($m in ($genLines | Select-Object -First 12)) {
                Write-Host ("  " + $m.Value) -ForegroundColor Yellow
            }
        } else {
            Write-Host "[Diag] No generation markers matched in serial output." -ForegroundColor Yellow
        }
    }

    if ($wantGenDiag) {
        $mStop = [regex]::Match($serial, '(?m)\[stop\] reason=.*$')
        if ($mStop.Success) {
            Write-Host ("[Diag] Stop reason: " + $mStop.Value) -ForegroundColor Yellow
        } else {
            Write-Host "[Diag] No [stop] reason line found in serial output." -ForegroundColor Yellow
        }
    }

    if ($Mode -eq 'gen') {
        $mGen = [regex]::Match($serial, '\[gen\] tokens=(\d+)')
        if ($mGen.Success) {
            $got = [int]$mGen.Groups[1].Value
            if ($got -lt $GenMaxTokens) {
                $msg = "[Warn] gen: model stopped early (tokens=$got < GenMaxTokens=$GenMaxTokens). Consider raising -GenTemp or changing -GenPrompt."
                if ($GenRequireFullTokens) { throw $msg }
                Write-Host $msg -ForegroundColor Yellow
            }
        }
    }

    # Determine which model we expect to be actually loaded.
    $expectedLoaded = $ExpectedModel
    if (-not $expectedLoaded) {
        if ($BootModel) {
            $expectedLoaded = $BootModel
        } else {
            $expectedLoaded = $ModelBin
        }
    }

    # RAM mode is primarily a memory-layout/allocator test.
    # Unless the caller explicitly requests strict model assertions, don't fail RAM runs
    # just because the image bundles a different default model.
    $skipModelAssertionsEffective = $SkipModelAssertions
    if (-not $PSBoundParameters.ContainsKey('SkipModelAssertions')) {
        if ($Mode -eq 'ram' -and -not $ExpectedModel) {
            $skipModelAssertionsEffective = $true
        }
    }

    try {
        Assert-Contains $serial '[autorun] loaded' 'autorun loaded'

        # Validate model name is correct both at boot and in /version.
        # When iterating on GGUF fallback/injection behavior, these checks can be skipped.
        if (-not $skipModelAssertionsEffective) {
            Assert-Contains $serial ("OK: Model loaded: $expectedLoaded") 'model loaded line'
            Assert-Contains $serial ("model=$expectedLoaded") 'version shows model'
        }

        # Build id is useful but should not be a hard failure: some environments/log sinks
        # can reformat or omit it in the captured serial output.
        if ([regex]::IsMatch($serial, 'build=', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            Assert-Match $serial 'build=20\d\d-\d\d-\d\dT\d\d:\d\d:\d\dZ' 'version shows build id'
        } else {
            Write-Host '[Warn] /version build id not found in serial output.' -ForegroundColor Yellow
        }

        if ($Mode -eq 'smoke') {
            Assert-Contains $serial 'You (autorun): /version' 'autorun ran /version'
            Assert-Contains $serial 'You (autorun): /compat_status' 'autorun ran /compat_status'
            Assert-Contains $serial '[Compatibilion]' 'compatibilion status printed'

            Assert-Contains $serial 'You (autorun): /calib_status' 'autorun ran /calib_status'
            Assert-Contains $serial '[Calibrion]' 'calibrion status printed'

            Assert-Contains $serial 'You (autorun): /orch_status' 'autorun ran /orch_status'
            Assert-Contains $serial '[Orchestrion]' 'orchestrion status printed'

            Assert-Contains $serial 'OK: created entity id=' 'OO entity created'
            Assert-Contains $serial 'OK: wrote oo-test.bin' 'OO saved'
        } elseif ($Mode -eq 'q8bench') {
            Assert-Contains $serial 'You (autorun): /cfg' 'autorun ran /cfg'
            Assert-Contains $serial 'You (autorun): /q8_matvec wq 0 30' 'autorun ran q8_matvec wq'
            Assert-Contains $serial 'Q8 matvec (wq layer=0)' 'q8 matvec wq printed'
            Assert-Contains $serial 'You (autorun): /q8_matvec w2 0 30' 'autorun ran q8_matvec w2'
            Assert-Contains $serial 'Q8 matvec (w2 layer=0)' 'q8 matvec w2 printed'
            Assert-Contains $serial 'Scalar:' 'q8 matvec scalar cycles printed'
        } elseif ($Mode -eq 'ram') {
            Assert-Contains $serial 'You (autorun): /cfg' 'autorun ran /cfg'
            Assert-Contains $serial 'You (autorun): /ram' 'autorun ran /ram'
            Assert-Contains $serial 'RAM budget (Zone B):' 'ram budget header printed'
            Assert-Contains $serial 'model_picker=' 'cfg shows model_picker'
            Assert-Contains $serial 'ctx_len_cfg=' 'cfg shows ctx_len_cfg'

            # Strict RAM formatting assertions (catch regressions in zone reporting).
            Assert-Match $serial '(?m)^\s*WEIGHTS:\s*used=\d+\s*MB\s*free=\d+\s*MB\s*total=\d+\s*MB\s*$' 'ram weights line'
            Assert-Match $serial '(?m)^\s*KV:\s*used=\d+\s*MB\s*free=\d+\s*MB\s*total=\d+\s*MB\s*$' 'ram kv line'
            Assert-Match $serial '(?m)^\s*SCRATCH:\s*used=\d+\s*MB\s*free=\d+\s*MB\s*total=\d+\s*MB\s*$' 'ram scratch line'
            Assert-Match $serial '(?m)^\s*ACTS:\s*used=\d+\s*MB\s*free=\d+\s*MB\s*total=\d+\s*MB\s*$' 'ram acts line'
            Assert-Match $serial '(?m)^\s*ZONEC:\s*used=\d+\s*MB\s*free=\d+\s*MB\s*total=\d+\s*MB\s*$' 'ram zonec line'
        } elseif ($Mode -eq 'gen') {
            Assert-Match $serial 'You \(autorun\): /max_tokens \d+' 'autorun set max_tokens'
            Assert-Contains $serial 'You (autorun): /stats 1' 'autorun enabled stats'
            Assert-Contains $serial 'You (autorun): /stop_you 0' 'autorun disabled stop_you'
            Assert-Contains $serial 'You (autorun): /stop_nl 0' 'autorun disabled stop_nl'
            Assert-Contains $serial 'You (autorun): /seed 123' 'autorun set seed'
            Assert-Match $serial 'You \(autorun\): /temp [0-9]+(\.[0-9]+)?' 'autorun set temp'
            Assert-Match $serial 'AI:|\[gen\] tokens=|\[stats\] tokens=' 'generation markers'
        } elseif ($Mode -eq 'custom') {
            if ($ExpectGeneration) {
                Assert-Match $serial 'AI:|\[gen\] tokens=|\[stats\] tokens=' 'generation markers'
            }
        }

        if ($Mode -eq 'custom') {
            # Custom scripts may intentionally exit the REPL, which returns to UEFI shell and waits for a key.
            # Accept either an explicit shutdown marker or a clean REPL exit.
            if (-not ($serial -and $serial.Contains('[autorun] shutting down'))) {
                Assert-Match $serial '(?i)\bGoodbye!\b|Press any key to exit' 'custom autorun completed'
            }
        } else {
            Assert-Contains $serial '[autorun] shutting down' 'autorun shutdown'
        }
    } catch {
        Show-Tails $serialLog $tmpErr $tmpOut
        throw
    }

    if ($Mode -ne 'custom') {
        if (-not $sawShutdown) {
            Write-Host '[Warn] Did not observe shutdown marker during polling; found it in final log.' -ForegroundColor Yellow
        }
        Write-Host '[OK] Autorun completed and system shut down.' -ForegroundColor Green
    } else {
        if ($serial -and $serial.Contains('[autorun] shutting down')) {
            Write-Host '[OK] Custom autorun completed and system shut down.' -ForegroundColor Green
        } else {
            Write-Host '[OK] Custom autorun completed (REPL exited).' -ForegroundColor Green
        }
    }
    exit 0
} catch {
    Write-Host "`n[FAIL] Autorun test failed." -ForegroundColor Red
    if ($serialLog -or $tmpErr -or $tmpOut) {
        Write-Host '[Debug] Logs:' -ForegroundColor Yellow
        Show-Tails $serialLog $tmpErr $tmpOut
    }
    throw
} finally {
    try { if (Test-Path $tmpCfgPath) { Remove-Item -Force $tmpCfgPath -ErrorAction SilentlyContinue } } catch {}
    try { if (Test-Path $autorunPath) { Remove-Item -Force $autorunPath -ErrorAction SilentlyContinue } } catch {}

    if ((-not $isTempImage) -and $didModifyImage -and $IMAGE -and $fatOffset -and $imageBackups -and $imageBackups.Count -gt 0) {
        try {
            Write-Host '[Test] Restoring image files modified during autorun injection...' -ForegroundColor Cyan
            foreach ($e in $imageBackups) {
                Restore-ImageFile -ImagePath $IMAGE -FatOffsetBytes $fatOffset -Entry $e
            }
        } catch {
            Write-Host '[Warn] Failed to fully restore injected image files. Re-run .\build.ps1 to regenerate a clean image.' -ForegroundColor Yellow
        }
    }

    if ($isTempImage -and $IMAGE -and (Test-Path $IMAGE)) {
        try {
            Remove-Item -Force -ErrorAction SilentlyContinue $IMAGE
        } catch {}
    }

    foreach ($e in $imageBackups) {
        try {
            if ($e -and $e.Existed -and $e.BackupPath -and (Test-Path $e.BackupPath)) {
                Remove-Item -Force -ErrorAction SilentlyContinue $e.BackupPath
            }
        } catch {}
    }
}
