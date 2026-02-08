[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('auto','whpx','tcg','none')]
    [string]$Accel = 'tcg',

    [ValidateRange(512, 8192)]
    [int]$MemMB = 1024,

    [ValidateRange(1, 10)]
    [int]$Repeat = 2,

    [ValidateRange(60, 86400)]
    [int]$TimeoutSec = 1200,

    # If omitted, test-qemu-autorun.ps1 will auto-select.
    [string]$ModelBin = '',

    # GGUF model used for injected smoke; default is stories15M.q8_0.gguf next to this script.
    [string]$GgufPath = '',

    [switch]$SkipBuild,

    # Optional: bootstrap pinned toolchains (downloaded into tools/_toolchains/, ignored by git)
    # before running any build steps.
    [switch]$BootstrapToolchains,

    # If set, runs only the quick modes (ram/gen/gguf_smoke) to keep iteration fast.
    [switch]$Quick,

    # If set, prints the tail of each serial log path used.
    [switch]$VerboseLogs,
    # By default, matrix runs focus on functional markers + perf.
    # Enable this to enforce strict "OK: Model loaded:" and "model=..." assertions.
    [switch]$StrictModelAssertions
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$autorun = Join-Path $root 'test-qemu-autorun.ps1'
if (-not (Test-Path $autorun)) {
    throw "Missing autorun harness: $autorun"
}

function Get-NewSerialLog([datetime]$since) {
    $tmp = [System.IO.Path]::GetTempPath()
    $cands = Get-ChildItem -Path $tmp -Filter 'llm-baremetal-serial-autorun-*.txt' -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -gt $since } |
        Sort-Object LastWriteTime -Descending
    return $cands | Select-Object -First 1
}

function Parse-Stats([string]$serialText) {
    if (-not $serialText) { return $null }

    $m = [regex]::Matches($serialText, '(?m)^\[stats\] tokens=(\d+) time_ms=(\d+) tok_s=([0-9]+\.[0-9]{3}|inf)\s*$')
    if ($m -and $m.Count -gt 0) {
        $last = $m[$m.Count - 1]
        return [pscustomobject]@{
            Tokens = [int]$last.Groups[1].Value
            TimeMs = [int]$last.Groups[2].Value
            TokS = $last.Groups[3].Value
        }
    }
    return $null
}

function Run-Mode([string]$mode, [hashtable]$extraArgs) {
    $since = Get-Date
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    $args = @{
        Mode = $mode
        Accel = $Accel
        MemMB = $MemMB
        SkipInspect = $true
        TimeoutSec = $TimeoutSec
    }
    if ($BootstrapToolchains) { $args.BootstrapToolchains = $true }
    if ($SkipBuild) { $args.SkipBuild = $true }
    if ($ModelBin) { $args.ModelBin = $ModelBin }

    # Most matrix modes should not fail due to model fallback (e.g. .bin missing -> bundled GGUF).
    # gguf_smoke is the exception: we explicitly validate the injected model path.
    if (-not $StrictModelAssertions -and $mode -ne 'gguf_smoke') {
        $args.SkipModelAssertions = $true
    }

    foreach ($k in $extraArgs.Keys) { $args[$k] = $extraArgs[$k] }

    $kv = @()
    foreach ($k in $args.Keys) {
        $v = $args[$k]
        if ($v -is [switch] -or $v -is [bool]) {
            if ($v) { $kv += @("-$k") }
        } else {
            $kv += @("-$k", "$v")
        }
    }

    # Capture output so it doesn't leak into the function pipeline (which would pollute $rows).
    # The harness may emit benign stderr (e.g. missing optional backup files); don't let that
    # become a terminating NativeCommandError under $ErrorActionPreference='Stop'.
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $procOut = & powershell -NoProfile -ExecutionPolicy Bypass -File $autorun @kv 2>&1
        $exit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldEap
    }

    if ($exit -ne 0 -and $procOut) {
        Write-Host "[Matrix] $mode harness output tail:" -ForegroundColor Yellow
        $procOut | Select-Object -Last 80 | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
    }

    $sw.Stop()
    $serialFile = Get-NewSerialLog -since $since
    $serialText = $null
    if ($serialFile -and (Test-Path $serialFile.FullName)) {
        $serialText = Get-Content -Path $serialFile.FullName -Raw -ErrorAction SilentlyContinue
    }

    $stats = Parse-Stats $serialText

    if ($VerboseLogs -and $serialFile) {
        Write-Host "[Matrix] $mode serial: $($serialFile.FullName)" -ForegroundColor DarkGray
        Get-Content -Path $serialFile.FullName -Tail 40 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ -ForegroundColor DarkGray }
    }

    return [pscustomobject]@{
        Mode = $mode
        Exit = $exit
        WallSec = [Math]::Round($sw.Elapsed.TotalSeconds, 2)
        Tokens = if ($stats) { $stats.Tokens } else { $null }
        TokS = if ($stats) { $stats.TokS } else { $null }
        Serial = if ($serialFile) { $serialFile.FullName } else { $null }
    }
}

# Resolve GGUF path for injected runs
if (-not $GgufPath) {
    $cand = Join-Path $root 'stories15M.q8_0.gguf'
    if (Test-Path $cand) { $GgufPath = $cand }
}

$hasGguf = ($GgufPath -and (Test-Path $GgufPath))
$ggufLeaf = if ($hasGguf) { Split-Path -Leaf $GgufPath } else { $null }

$modeList = @()
if ($Quick) {
    $modeList = @('ram','gen')
} else {
    $modeList = @('smoke','q8bench','ram','gen')
}

if ($hasGguf) {
    $modeList += 'gguf_smoke'
} else {
    Write-Host "[Matrix] Note: skipping gguf_smoke (GGUF not found; pass -GgufPath <path>)" -ForegroundColor DarkGray
}

if (-not $SkipBuild) {
    Write-Host "[Matrix] Build once upfront..." -ForegroundColor Cyan
    $bp = @()
    if ($BootstrapToolchains) { $bp += '-BootstrapToolchains' }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1') @bp
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed with exit=$LASTEXITCODE" }
}

$rows = @()
for ($i = 1; $i -le $Repeat; $i++) {
    Write-Host "\n[Matrix] Run $i/$Repeat (accel=$Accel mem=${MemMB}MB)" -ForegroundColor Cyan

    foreach ($m in $modeList) {
        $extra = @{}
        if ($m -eq 'gen') {
            $extra.GenMaxTokens = 64
            $extra.GenTemp = 0.2
        }
        if ($m -eq 'gguf_smoke') {
            if (-not $hasGguf -or -not $ggufLeaf) {
                throw "Internal error: gguf_smoke selected but GGUF path is missing"
            }
            $extra.BootModel = ("models\\{0}" -f $ggufLeaf)
            $extra.ExtraModel = $GgufPath
            $extra.ExpectedModel = ("models\\{0}" -f $ggufLeaf)
            $extra.GenMaxTokens = 16
            $extra.GenTemp = 0.8
        }

        $r = Run-Mode $m $extra
        $rows += $r

        $ok = ($r.Exit -eq 0)
        $color = if ($ok) { 'Green' } else { 'Red' }
        $stats = if ($r.TokS) { " tok_s=$($r.TokS)" } else { '' }
        Write-Host ("[Matrix] {0,-10} exit={1} wall={2}s{3}" -f $r.Mode, $r.Exit, $r.WallSec, $stats) -ForegroundColor $color

        if (-not $ok) {
            throw "Matrix failed in mode=$($r.Mode) (exit=$($r.Exit)). Serial: $($r.Serial)"
        }
    }
}

# Summarize
Write-Host "\n[Matrix] Summary" -ForegroundColor Cyan
$rows |
    Group-Object Mode |
    ForEach-Object {
        $g = $_.Group
        [pscustomobject]@{
            Mode = $_.Name
            Runs = $g.Count
            WallSec_Avg = [Math]::Round(($g | Measure-Object WallSec -Average).Average, 2)
            TokS_Last = ($g | Select-Object -Last 1).TokS
            Tokens_Last = ($g | Select-Object -Last 1).Tokens
        }
    } |
    Sort-Object Mode |
    Format-Table -AutoSize
