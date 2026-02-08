param(
    [string]$Version = '4.0.2',
    [string]$InstallDir = (Join-Path $PSScriptRoot '_toolchains\apelink'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$cosmoccDir = Join-Path $PSScriptRoot '_toolchains\cosmocc'
$getCosmocc = Join-Path $PSScriptRoot 'get-cosmocc.ps1'

function Get-InstalledVersion([string]$dir) {
    $vf = Join-Path $dir 'VERSION.txt'
    if (Test-Path $vf) {
        return (Get-Content -LiteralPath $vf -Raw).Trim()
    }
    return $null
}

if (-not (Test-Path $cosmoccDir)) {
    & $getCosmocc -Version $Version
}

$srcCandidates = @(
    (Join-Path $cosmoccDir 'bin\apelink.exe'),
    (Join-Path $cosmoccDir 'bin\apelink')
)

$src = $null
foreach ($c in $srcCandidates) {
    if (Test-Path $c) { $src = $c; break }
}

if (-not $src) {
    throw "apelink not found under cosmocc toolchain: $(Join-Path $cosmoccDir 'bin')"
}

$installedVersion = Get-InstalledVersion $InstallDir
if ($installedVersion -eq $Version -and -not $Force) {
    Write-Host "apelink already installed: v$Version ($InstallDir)"
    exit 0
}

if (Test-Path $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallDir | Out-Null

$dstName = [IO.Path]::GetFileName($src)
Copy-Item -LiteralPath $src -Destination (Join-Path $InstallDir $dstName) -Force
Set-Content -LiteralPath (Join-Path $InstallDir 'VERSION.txt') -Value $Version -NoNewline

Write-Host "Done. apelink installed."
Write-Host "- Bin: $(Join-Path $InstallDir $dstName)"
