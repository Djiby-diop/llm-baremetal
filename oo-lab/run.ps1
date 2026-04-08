Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $repoRoot 'build.ps1')

$exe = Join-Path $repoRoot 'oo-lab.exe'
if (!(Test-Path $exe)) {
    throw "Build succeeded but '$exe' not found."
}

& $exe @args
