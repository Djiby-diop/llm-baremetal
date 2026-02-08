param(
    [ValidateSet('x86_64','aarch64','arm64')]
    [string]$Arch = 'x86_64',
    [string]$Version = '4.0.2',
    [switch]$Bootstrap,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'

$toolDir = Join-Path $PSScriptRoot '_toolchains\ape'
$bootstrapper = Join-Path $PSScriptRoot 'get-ape.ps1'

function Resolve-ApePath([string]$dir, [string]$arch) {
    $candidates = @()
    switch ($arch) {
        'x86_64' { $candidates += 'ape-x86_64.elf' }
        'aarch64' { $candidates += 'ape-aarch64.elf' }
        'arm64' { $candidates += 'ape-arm64.elf'; $candidates += 'ape-aarch64.elf' }
    }

    foreach ($n in $candidates) {
        $p = Join-Path $dir $n
        if (Test-Path $p) { return $p }
    }
    return $null
}

$apePath = Resolve-ApePath $toolDir $Arch
if (-not $apePath) {
    if (-not $Bootstrap) {
        throw "ape not installed. Run: $bootstrapper (or re-run this script with -Bootstrap)"
    }
    & $bootstrapper -Version $Version
    $apePath = Resolve-ApePath $toolDir $Arch
}

if (-not $apePath) {
    throw "ape artifact not found under $toolDir for Arch=$Arch"
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "wsl.exe not found. Use ./tools/ape.sh from WSL/Linux."
}

$wslPath = (& wsl.exe wslpath -a $apePath).Trim()
if (-not $wslPath) {
    throw "Failed to translate path to WSL path: $apePath"
}

& wsl.exe --exec $wslPath @Args
exit $LASTEXITCODE
