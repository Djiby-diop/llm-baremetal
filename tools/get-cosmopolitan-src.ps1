param(
    [string]$Version = '4.0.2',
    [string]$Sha256 = 'e466106b18064e0c996ef64d261133af867bccd921ad14e54975d89aa17a8717',
    [string]$InstallDir = (Join-Path $PSScriptRoot '_toolchains\cosmopolitan-src'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Download-File([string]$url, [string]$outFile) {
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        # Resume partial downloads when possible; still verified by SHA256 afterwards.
        & $curl.Source '-fL' '--retry' '10' '--retry-delay' '2' '--retry-all-errors' '-C' '-' '-o' $outFile $url
        if ($LASTEXITCODE -ne 0) {
            throw "curl.exe failed downloading $url (exit=$LASTEXITCODE)"
        }
        if (-not (Test-Path $outFile)) {
            throw "curl.exe reported success but output file missing: $outFile"
        }
        return
    }

    if (Test-Path $outFile) {
        Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue
    }

    for ($attempt = 1; $attempt -le 5; $attempt++) {
        try {
            Invoke-WebRequest -Uri $url -OutFile $outFile
            return
        } catch {
            if ($attempt -ge 5) { throw }
            Start-Sleep -Seconds (2 * $attempt)
        }
    }
}

function Get-InstalledVersion([string]$dir) {
    $vf = Join-Path $dir 'VERSION.txt'
    if (Test-Path $vf) {
        return (Get-Content -LiteralPath $vf -Raw).Trim()
    }
    return $null
}

function Normalize-ExtractLayout([string]$dir) {
    $subdirs = @(Get-ChildItem -LiteralPath $dir -Directory -ErrorAction SilentlyContinue)
    $files = @(Get-ChildItem -LiteralPath $dir -File -ErrorAction SilentlyContinue)
    if ($subdirs.Count -ne 1 -or $files.Count -ne 0) {
        return
    }

    $candidate = $subdirs[0].FullName
    Get-ChildItem -LiteralPath $candidate -Force | ForEach-Object {
        Move-Item -LiteralPath $_.FullName -Destination $dir -Force
    }
    Remove-Item -LiteralPath $candidate -Recurse -Force
}

$installedVersion = Get-InstalledVersion $InstallDir
if ($installedVersion -eq $Version -and -not $Force) {
    Write-Host "cosmopolitan-src already installed: v$Version ($InstallDir)"
    exit 0
}

if ((Test-Path $InstallDir) -and -not $Force -and $installedVersion) {
    throw "cosmopolitan-src already installed (v$installedVersion). Re-run with -Force to replace with v$Version."
}

$tgzName = "cosmopolitan-$Version.tar.gz"
$downloadUrl = "https://justine.lol/cosmopolitan/$tgzName"
$tgzPath = Join-Path $env:TEMP $tgzName

Write-Host "Downloading $downloadUrl"
Download-File -url $downloadUrl -outFile $tgzPath

$hash = (Get-FileHash -LiteralPath $tgzPath -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = $Sha256.ToLowerInvariant()
if ($hash -ne $expected) {
    throw "SHA256 mismatch for $tgzName. Expected=$expected Actual=$hash"
}

if (Test-Path $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallDir | Out-Null

if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
    throw "tar not found on PATH. Install bsdtar (Windows 10+ usually has tar.exe) or run from WSL."
}

Write-Host "Extracting to $InstallDir"
tar -xf $tgzPath -C $InstallDir
if ($LASTEXITCODE -ne 0) {
    throw "tar failed (exit=$LASTEXITCODE). On Windows this is often due to path length limits. Try: ./tools/get-cosmopolitan-src.sh from WSL, enable long paths, or use a shorter -InstallDir."
}

Normalize-ExtractLayout $InstallDir

Set-Content -LiteralPath (Join-Path $InstallDir 'VERSION.txt') -Value $Version -NoNewline

Write-Host "Done. cosmopolitan sources extracted."
Write-Host "- Root: $InstallDir"
