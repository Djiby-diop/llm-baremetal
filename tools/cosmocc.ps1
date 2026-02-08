param(
    [string]$Version = '4.0.2',
    [switch]$Bootstrap,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'

$toolchainDir = Join-Path $PSScriptRoot '_toolchains\cosmocc'
$bootstrapper = Join-Path $PSScriptRoot 'get-cosmocc.ps1'

function Find-Cosmocc([string]$dir) {
    $direct = @(
        (Join-Path $dir 'bin\\cosmocc.exe'),
        (Join-Path $dir 'bin\\cosmocc')
    )
    foreach ($p in $direct) {
        if (Test-Path $p) { return $p }
    }

    $nested = @(Get-ChildItem -LiteralPath $dir -Directory -ErrorAction SilentlyContinue)
    foreach ($d in $nested) {
        $candidates = @(
            (Join-Path $d.FullName 'bin\\cosmocc.exe'),
            (Join-Path $d.FullName 'bin\\cosmocc')
        )
        foreach ($p in $candidates) {
            if (Test-Path $p) { return $p }
        }
    }

    return $null
}

$cosmoccPath = Find-Cosmocc $toolchainDir
if (-not $cosmoccPath) {
    if (-not $Bootstrap) {
        throw "cosmocc not installed. Run: $bootstrapper (or re-run this script with -Bootstrap)"
    }
    & $bootstrapper -Version $Version
    $cosmoccPath = Find-Cosmocc $toolchainDir
}

if (-not $cosmoccPath) {
    throw "cosmocc install completed but bin/cosmocc was not found under $toolchainDir"
}

& $cosmoccPath @Args
exit $LASTEXITCODE
