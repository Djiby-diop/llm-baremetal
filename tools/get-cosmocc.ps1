param(
    [string]$Version = '4.0.2',
    [string]$Sha256 = '85b8c37a406d862e656ad4ec14be9f6ce474c1b436b9615e91a55208aced3f44',
    [string]$InstallDir = (Join-Path $PSScriptRoot '_toolchains\cosmocc'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

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
    Write-Host "cosmocc already installed: v$Version ($InstallDir)"
    exit 0
}

if ((Test-Path $InstallDir) -and -not $Force -and $installedVersion) {
    throw "cosmocc already installed (v$installedVersion). Re-run with -Force to replace with v$Version."
}

$zipName = "cosmocc-$Version.zip"
$downloadUrl = "https://cosmo.zip/pub/cosmocc/$zipName"
$zipPath = Join-Path $env:TEMP $zipName

Write-Host "Downloading $downloadUrl"
Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath

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

$binDir = Join-Path $InstallDir 'bin'
Write-Host "Done. cosmocc toolchain installed."
Write-Host "- Bin dir: $binDir"
Write-Host "- Wrapper: $(Join-Path $PSScriptRoot 'cosmocc.ps1')"
