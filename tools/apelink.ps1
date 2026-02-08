param(
    [string]$Version = '4.0.2',
    [switch]$Bootstrap,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'

$toolDir = Join-Path $PSScriptRoot '_toolchains\apelink'
$bootstrapper = Join-Path $PSScriptRoot 'get-apelink.ps1'

function Find-Apelink([string]$dir) {
    $candidates = @(
        (Join-Path $dir 'apelink.exe'),
        (Join-Path $dir 'apelink')
    )
    foreach ($p in $candidates) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

$apelinkPath = Find-Apelink $toolDir
if (-not $apelinkPath) {
    if (-not $Bootstrap) {
        throw "apelink not installed. Run: $bootstrapper (or re-run this script with -Bootstrap)"
    }
    & $bootstrapper -Version $Version
    $apelinkPath = Find-Apelink $toolDir
}

if (-not $apelinkPath) {
    throw "apelink install completed but executable not found under $toolDir"
}

try {
    & $apelinkPath @Args
    exit $LASTEXITCODE
} catch {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        throw
    }
    $wslPath = (& wsl.exe wslpath -a $apelinkPath).Trim()
    & wsl.exe --exec $wslPath @Args
    exit $LASTEXITCODE
}
