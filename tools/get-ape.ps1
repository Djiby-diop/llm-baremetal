param(
    [string]$Version = '4.0.2',
    [string]$InstallDir = (Join-Path $PSScriptRoot '_toolchains\ape'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$cosmosDir = Join-Path $PSScriptRoot '_toolchains\cosmos'
$getCosmos = Join-Path $PSScriptRoot 'get-cosmos.ps1'

function Get-InstalledVersion([string]$dir) {
    $vf = Join-Path $dir 'VERSION.txt'
    if (Test-Path $vf) {
        return (Get-Content -LiteralPath $vf -Raw).Trim()
    }
    return $null
}

if (-not (Test-Path $cosmosDir)) {
    & $getCosmos -Version $Version
}

$binDir = Join-Path $cosmosDir 'bin'
if (-not (Test-Path $binDir)) {
    throw "cosmos bin dir not found: $binDir (try re-running $getCosmos -Force)"
}

$installedVersion = Get-InstalledVersion $InstallDir
if ($installedVersion -eq $Version -and -not $Force) {
    Write-Host "ape already installed: v$Version ($InstallDir)"
    exit 0
}

if (Test-Path $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallDir | Out-Null

$apes = @(Get-ChildItem -LiteralPath $binDir -File -Filter 'ape-*' -ErrorAction SilentlyContinue)
if ($apes.Count -eq 0) {
    throw "No ape-* artifacts found under $binDir"
}

foreach ($f in $apes) {
    Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $InstallDir $f.Name) -Force
}

Set-Content -LiteralPath (Join-Path $InstallDir 'VERSION.txt') -Value $Version -NoNewline

Write-Host "Done. ape artifacts installed."
Write-Host "- Dir: $InstallDir"
