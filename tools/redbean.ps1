param(
    [string]$Version = '4.0.2',
    [switch]$Bootstrap,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'

$toolDir = Join-Path $PSScriptRoot '_toolchains\redbean'
$bootstrapper = Join-Path $PSScriptRoot 'get-redbean.ps1'

function Find-Redbean([string]$dir) {
    $p = Join-Path $dir 'redbean'
    if (Test-Path $p) { return $p }
    $pExe = Join-Path $dir 'redbean.exe'
    if (Test-Path $pExe) { return $pExe }
    return $null
}

$redbeanPath = Find-Redbean $toolDir
if (-not $redbeanPath) {
    if (-not $Bootstrap) {
        throw "redbean not installed. Run: $bootstrapper (or re-run this script with -Bootstrap)"
    }
    & $bootstrapper -Version $Version
    $redbeanPath = Find-Redbean $toolDir
}

if (-not $redbeanPath) {
    throw "redbean install completed but executable not found under $toolDir"
}

try {
    & $redbeanPath @Args
    exit $LASTEXITCODE
} catch {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        throw
    }
    $wslPath = (& wsl.exe wslpath -a $redbeanPath).Trim()
    & wsl.exe --exec $wslPath @Args
    exit $LASTEXITCODE
}
