Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $repoRoot
try {
    $out = Join-Path $repoRoot 'oo-sim.exe'

    $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($cl) {
        & $cl.Source /nologo /W4 /O2 /Fe:$out src\oo_sim_main.c | Out-Host
        exit 0
    }

    $gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
    if ($gcc) {
        & $gcc.Source -std=c11 -Wall -Wextra -O2 -o $out src/oo_sim_main.c | Out-Host
        exit 0
    }

    $cc = Get-Command cc.exe -ErrorAction SilentlyContinue
    if ($cc) {
        & $cc.Source -std=c11 -Wall -Wextra -O2 -o $out src/oo_sim_main.c | Out-Host
        exit 0
    }

    throw 'No C compiler found. Install Visual Studio Build Tools (cl.exe) or MinGW/LLVM (gcc/cc).'
}
finally {
    Pop-Location
}
