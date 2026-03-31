# Prebuild Violation Reporter
# Shows prebuild violations with context for remediation

param(
    [string]$SourceFile = "llama2_efi_final.c",
    [int]$ContextLines = 2
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

Write-Host "`n[Prebuild Reporter] Analyzing $SourceFile..." -ForegroundColor Cyan

# Run prebuild check to get violations
$output = & .\run-oo-guard.ps1 prebuild $SourceFile 2>&1 | Out-String

# Parse violations
$violations = $output -split "`n" | Where-Object { $_ -match 'small buffer \[(\d+)\]' }

if (-not $violations -or $violations.Count -eq 0) {
    Write-Host "`n✅ No violations found!" -ForegroundColor Green
    exit 0
}

Write-Host "`nFound $($violations.Count) violations:" -ForegroundColor Yellow
Write-Host "=" * 80

$sourceContent = Get-Content $SourceFile

foreach ($violation in $violations) {
    if ($violation -match '(.+):(\d+): small buffer \[(\d+)\]') {
        $file = $matches[1]
        $lineNum = [int]$matches[2]
        $size = $matches[3]
        
        Write-Host "`n📍 Line $lineNum (buffer size: $size bytes)" -ForegroundColor Cyan
        Write-Host "-" * 80
        
        # Show context
        $start = [Math]::Max(1, $lineNum - $ContextLines)
        $end = [Math]::Min($sourceContent.Length, $lineNum + $ContextLines)
        
        for ($i = $start; $i -le $end; $i++) {
            $prefix = if ($i -eq $lineNum) { ">>>" } else { "   " }
            $color = if ($i -eq $lineNum) { "Yellow" } else { "Gray" }
            Write-Host ("{0} {1,5}: {2}" -f $prefix, $i, $sourceContent[$i - 1]) -ForegroundColor $color
        }
        
        # Suggest fix
        Write-Host "`n💡 Remediation options:" -ForegroundColor Green
        if ($size -lt 64) {
            Write-Host "   1. Add justification: // SAFE: <reason buffer size is correct>" -ForegroundColor Gray
            Write-Host "   2. Increase size: change [$size] to [64] or larger if needed" -ForegroundColor Gray
            Write-Host "   3. Use dynamic allocation if size is variable" -ForegroundColor Gray
        }
    }
}

Write-Host "`n" + ("=" * 80)
Write-Host "`nTotal: $($violations.Count) violations require review" -ForegroundColor Yellow
Write-Host "Run '.\run-oo-guard.ps1 prebuild $SourceFile' to re-check after fixes" -ForegroundColor Gray
