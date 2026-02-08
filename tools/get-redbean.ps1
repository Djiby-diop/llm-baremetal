param(
    [string]$Version = '4.0.2',
    [string]$InstallDir = (Join-Path $PSScriptRoot '_toolchains\redbean'),
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

$src = Join-Path $cosmosDir 'bin\redbean'
if (-not (Test-Path $src)) {
    throw "redbean not found under cosmos: $src"
}

$installedVersion = Get-InstalledVersion $InstallDir
if ($installedVersion -eq $Version -and -not $Force) {
    Write-Host "redbean already installed: v$Version ($InstallDir)"
    exit 0
}

if (Test-Path $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallDir | Out-Null

Copy-Item -LiteralPath $src -Destination (Join-Path $InstallDir 'redbean') -Force
Set-Content -LiteralPath (Join-Path $InstallDir 'VERSION.txt') -Value $Version -NoNewline

Write-Host "Done. redbean installed."
Write-Host "- Bin: $(Join-Path $InstallDir 'redbean')"
