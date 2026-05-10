Param(
    [ValidateSet('debug', 'release')]
    [string]$Configuration = 'debug',
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot
Push-Location $repoRoot
try {
    $cargoArgs = @('run', '--features', 'std', '--bin', 'dplus_audit', '--')
    if ($Configuration -eq 'release') {
        $cargoArgs = @('run', '--release', '--features', 'std', '--bin', 'dplus_audit', '--')
    }

    if (-not $SkipBuild) {
        $buildArgs = @('build', '--features', 'std', '--bin', 'dplus_audit')
        if ($Configuration -eq 'release') {
            $buildArgs = @('build', '--release', '--features', 'std', '--bin', 'dplus_audit')
        }
        & cargo @buildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "cargo build failed with code $LASTEXITCODE"
        }
    }

    $policyStrict = 'policies/policy-strict.dplus'

    # 1) Summary smoke path must produce summary lines.
    $summaryOutput = (& cargo @cargoArgs $policyStrict '--summary' '--runs' '4' | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "summary smoke command failed with code $LASTEXITCODE"
    }
    if ($summaryOutput -notmatch 'summary policy=' -or $summaryOutput -notmatch 'summary verdict=') {
        throw 'summary output missing expected markers'
    }

    # 2) JSONL smoke path must produce parseable JSON lines.
    $jsonlOutput = (& cargo @cargoArgs $policyStrict '--jsonl' '--limit' '3' '--tail' | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "jsonl smoke command failed with code $LASTEXITCODE"
    }

    $jsonLines = $jsonlOutput -split "`r?`n" | Where-Object { $_.Trim().Length -gt 0 }
    if ($jsonLines.Count -eq 0) {
        throw 'jsonl output produced no lines'
    }
    foreach ($line in $jsonLines) {
        $obj = $line | ConvertFrom-Json
        if (-not $obj.action -or -not $obj.verdict) {
            throw "jsonl line missing expected fields: $line"
        }
    }

    # 2b) File output path must create and append correctly.
    $tmpOut = Join-Path $env:TEMP ('dplus-audit-output-' + [Guid]::NewGuid().ToString() + '.jsonl')
    & cargo @cargoArgs $policyStrict '--jsonl' '--limit' '1' '--output' $tmpOut | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "file output command failed with code $LASTEXITCODE"
    }
    & cargo @cargoArgs $policyStrict '--jsonl' '--limit' '1' '--output' $tmpOut '--append' | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "file append command failed with code $LASTEXITCODE"
    }
    $writtenLines = Get-Content -Path $tmpOut | Where-Object { $_.Trim().Length -gt 0 }
    Remove-Item -Path $tmpOut -ErrorAction SilentlyContinue
    if ($writtenLines.Count -lt 2) {
        throw 'file output smoke expected at least two lines after append'
    }

    # 3) Strict mode positive path (should pass under permissive threshold).
    & cargo @cargoArgs $policyStrict '--max-divergence-rate' '1.0' '--fail-on-verdict' 'emergency' | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "strict mode positive path unexpectedly failed with code $LASTEXITCODE"
    }

    # 4) Strict mode negative path (should fail on FORBID verdict for rust LANG block).
    $tmpPolicy = Join-Path $env:TEMP ('dplus-audit-smoke-' + [Guid]::NewGuid().ToString() + '.dplus')
    @"
@[LANG] rust {
  fn blocked() {}
}
"@ | Set-Content -Path $tmpPolicy -NoNewline

    & cargo @cargoArgs $tmpPolicy '--fail-on-verdict' 'forbid' | Out-Null
    $strictCode = $LASTEXITCODE
    Remove-Item -Path $tmpPolicy -ErrorAction SilentlyContinue

    if ($strictCode -eq 0) {
        throw 'strict mode negative path expected failure but command succeeded'
    }

    Write-Host "dplus-audit smoke: OK (configuration=$Configuration)"
}
finally {
    Pop-Location
}
