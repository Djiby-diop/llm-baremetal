<#
.SYNOPSIS
    OO Automated Benchmark — runs after every build, tracks tok/s + memory in CSV.

.DESCRIPTION
    Wraps bench-matrix.ps1. Parses serial log for [stats] and [zones] lines.
    Appends one row per run to bench/results.csv and prints delta vs previous run.

.EXAMPLE
    .\tools\bench-auto.ps1
    .\tools\bench-auto.ps1 -Accel whpx -MemMB 4096 -Repeat 3
    .\tools\bench-auto.ps1 -Quick -SkipBuild        # CI fast path
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('auto','whpx','tcg','none')]
    [string]$Accel = 'tcg',

    [ValidateRange(512, 8192)]
    [int]$MemMB = 2048,

    [ValidateRange(1, 10)]
    [int]$Repeat = 1,

    [switch]$Quick,
    [switch]$SkipBuild,
    [switch]$SkipBenchMatrix,    # use pre-existing serial log (debug)
    [string]$SerialLogPath = '',  # override log path

    [string]$CsvPath = ''         # defaults to bench/results.csv next to repo root
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# ── Output CSV ────────────────────────────────────────────────────────────────
if (-not $CsvPath) {
    $benchDir = Join-Path $root 'bench'
    if (-not (Test-Path $benchDir)) { New-Item -ItemType Directory -Path $benchDir | Out-Null }
    $CsvPath = Join-Path $benchDir 'results.csv'
}

$csvHeader = 'timestamp,git_sha,accel,mem_mb,mode,tokens,time_ms,tok_s,zone_total_mb,zone_free_mb,wall_sec'
if (-not (Test-Path $CsvPath)) {
    Set-Content -Path $CsvPath -Value $csvHeader -Encoding UTF8
    Write-Host "[bench-auto] Created $CsvPath" -ForegroundColor Cyan
}

# ── Git info ──────────────────────────────────────────────────────────────────
$gitSha = 'unknown'
try {
    $gitSha = (& git -C $root rev-parse --short HEAD 2>$null).Trim()
} catch {}

$ts = (Get-Date -Format 'yyyy-MM-ddTHH:mm:ss')

# ── Helper: parse serial log ──────────────────────────────────────────────────
function Parse-SerialLog([string]$logPath) {
    if (-not $logPath -or -not (Test-Path $logPath)) { return $null }
    $txt = Get-Content $logPath -Raw -ErrorAction SilentlyContinue
    if (-not $txt) { return $null }

    # [stats] tokens=53 time_ms=4200 tok_s=12.619
    $mStats = [regex]::Match($txt, '(?m)^\[stats\] tokens=(\d+) time_ms=(\d+) tok_s=([0-9.]+|inf)')
    # [zones] total_mb=XXXX free_mb=YYYY
    $mZones = [regex]::Match($txt, '(?m)total_mb=(\d+).*?free_mb=(\d+)')

    return [pscustomobject]@{
        Tokens   = if ($mStats.Success) { [int]$mStats.Groups[1].Value } else { $null }
        TimeMs   = if ($mStats.Success) { [int]$mStats.Groups[2].Value } else { $null }
        TokS     = if ($mStats.Success) { $mStats.Groups[3].Value } else { $null }
        ZoneTotalMB = if ($mZones.Success) { [int]$mZones.Groups[1].Value } else { $null }
        ZoneFreeMB  = if ($mZones.Success) { [int]$mZones.Groups[2].Value } else { $null }
    }
}

# ── Helper: last CSV row for delta ────────────────────────────────────────────
function Get-LastTokS([string]$csv, [string]$mode) {
    $rows = Import-Csv $csv -ErrorAction SilentlyContinue
    if (-not $rows) { return $null }
    $last = $rows | Where-Object { $_.mode -eq $mode } | Select-Object -Last 1
    if ($last) { return $last.tok_s } else { return $null }
}

# ── Run bench-matrix ──────────────────────────────────────────────────────────
$matrixScript = Join-Path $root 'scripts\bench-matrix.ps1'
if (-not (Test-Path $matrixScript)) {
    throw "Missing bench-matrix.ps1: $matrixScript"
}

$since = Get-Date

if (-not $SkipBenchMatrix) {
    Write-Host "[bench-auto] Running bench-matrix (accel=$Accel mem=${MemMB}MB quick=$Quick)..." -ForegroundColor Cyan
    $mxArgs = @('-Accel', $Accel, '-MemMB', $MemMB, '-Repeat', $Repeat)
    if ($Quick)      { $mxArgs += '-Quick' }
    if ($SkipBuild)  { $mxArgs += '-SkipBuild' }

    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $matrixScript @mxArgs
    $mxExit = $LASTEXITCODE
    $ErrorActionPreference = $oldEap

    if ($mxExit -ne 0) {
        Write-Warning "[bench-auto] bench-matrix exited $mxExit — recording failure row"
    }
}

# ── Find serial log ───────────────────────────────────────────────────────────
if (-not $SerialLogPath) {
    $tmp = [System.IO.Path]::GetTempPath()
    $cand = Get-ChildItem -Path $tmp -Filter 'llm-baremetal-serial-autorun-*.txt' -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -gt $since } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
    if ($cand) { $SerialLogPath = $cand.FullName }
}

# ── Parse and record ──────────────────────────────────────────────────────────
$parsed = Parse-SerialLog $SerialLogPath
$wallSec = [Math]::Round((Get-Date).Subtract($since).TotalSeconds, 1)

$mode = if ($Quick) { 'gen' } else { 'q8bench' }

$row = [pscustomobject]@{
    timestamp    = $ts
    git_sha      = $gitSha
    accel        = $Accel
    mem_mb       = $MemMB
    mode         = $mode
    tokens       = if ($parsed) { $parsed.Tokens }      else { '' }
    time_ms      = if ($parsed) { $parsed.TimeMs }      else { '' }
    tok_s        = if ($parsed) { $parsed.TokS }        else { '' }
    zone_total_mb = if ($parsed) { $parsed.ZoneTotalMB } else { '' }
    zone_free_mb  = if ($parsed) { $parsed.ZoneFreeMB }  else { '' }
    wall_sec     = $wallSec
}

$row | Export-Csv -Path $CsvPath -Append -NoTypeInformation -Encoding UTF8

# ── Delta report ──────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "═══ OO Bench Result ═══════════════════════════════" -ForegroundColor Cyan
Write-Host "  sha      : $gitSha"
Write-Host "  accel    : $Accel   mem: ${MemMB}MB"
if ($parsed -and $parsed.TokS) {
    Write-Host "  tok/s    : $($parsed.TokS)   tokens=$($parsed.Tokens)  time=$($parsed.TimeMs)ms" -ForegroundColor Green

    $prev = Get-LastTokS $CsvPath $mode
    # prev is now the SECOND-to-last row (we already appended current)
    $rows2 = Import-Csv $CsvPath -ErrorAction SilentlyContinue |
             Where-Object { $_.mode -eq $mode -and $_.tok_s -ne '' }
    if ($rows2 -and $rows2.Count -ge 2) {
        $prevTokS = ($rows2 | Select-Object -Last 2)[0].tok_s
        try {
            $delta = [double]$parsed.TokS - [double]$prevTokS
            $sign  = if ($delta -ge 0) { '+' } else { '' }
            $color = if ($delta -ge 0) { 'Green' } else { 'Red' }
            Write-Host "  delta    : ${sign}$([Math]::Round($delta,3)) tok/s vs previous ($prevTokS)" -ForegroundColor $color
        } catch {}
    }
} else {
    Write-Host "  tok/s    : (no stats found in serial log)" -ForegroundColor Yellow
}
if ($parsed -and $parsed.ZoneTotalMB) {
    $usedMB = $parsed.ZoneTotalMB - $parsed.ZoneFreeMB
    Write-Host "  memory   : ${usedMB}MB used / $($parsed.ZoneTotalMB)MB total"
}
Write-Host "  csv      : $CsvPath"
Write-Host "═══════════════════════════════════════════════════" -ForegroundColor Cyan
