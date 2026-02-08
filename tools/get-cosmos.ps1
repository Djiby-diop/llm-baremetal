param(
    [string]$Version = '4.0.2',
    [string]$Sha256 = '494ecbd87c2f2f622f91066d4fe5d9ffc1aaaa13de02db1714dd84d014ed398f',
    [string]$InstallDir = (Join-Path $PSScriptRoot '_toolchains\cosmos'),
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
    $binDir = Join-Path $dir 'bin'
    if (Test-Path $binDir) {
        return
    }

    $subdirs = @(Get-ChildItem -LiteralPath $dir -Directory -ErrorAction SilentlyContinue)
    if ($subdirs.Count -ne 1) {
        return
    }

    $candidate = $subdirs[0].FullName
    if (-not (Test-Path (Join-Path $candidate 'bin'))) {
        return
    }

    Get-ChildItem -LiteralPath $candidate -Force | ForEach-Object {
        Move-Item -LiteralPath $_.FullName -Destination $dir -Force
    }
    Remove-Item -LiteralPath $candidate -Recurse -Force
}

$installedVersion = Get-InstalledVersion $InstallDir
if ($installedVersion -eq $Version -and -not $Force) {
    Write-Host "cosmos already installed: v$Version ($InstallDir)"
    exit 0
}

if ((Test-Path $InstallDir) -and -not $Force -and $installedVersion) {
    throw "cosmos already installed (v$installedVersion). Re-run with -Force to replace with v$Version."
}

$zipName = "cosmos-$Version.zip"
$downloadUrl = "https://justine.lol/cosmopolitan/$zipName"
$zipPath = Join-Path $env:TEMP $zipName

Write-Host "Downloading $downloadUrl"
Download-File -url $downloadUrl -outFile $zipPath

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = $Sha256.ToLowerInvariant()
if ($hash -ne $expected) {
    throw "SHA256 mismatch for $zipName. Expected=$expected Actual=$hash"
}

if (Test-Path $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallDir | Out-Null

Write-Host "Extracting to $InstallDir"
Expand-Archive -LiteralPath $zipPath -DestinationPath $InstallDir -Force

Normalize-ExtractLayout $InstallDir

Set-Content -LiteralPath (Join-Path $InstallDir 'VERSION.txt') -Value $Version -NoNewline

Write-Host "Done. cosmos installed."
Write-Host "- Root: $InstallDir"
Write-Host "- Bin:  $(Join-Path $InstallDir 'bin')"
