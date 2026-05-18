param(
  [switch]$Strict
)

$ErrorActionPreference = 'Stop'

function Write-Check {
  param([string]$Name, [bool]$Ok, [string]$Details)
  if ($Ok) {
    Write-Host "[PASS] $Name - $Details" -ForegroundColor Green
  } else {
    Write-Host "[FAIL] $Name - $Details" -ForegroundColor Red
  }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
  $failed = $false

  $outOfScopeDirs = @(
    'oo-host',
    'oo-model',
    'oo-system',
    'oo-model-repo'
  )

  $presentOutOfScopeDirs = @()
  foreach ($d in $outOfScopeDirs) {
    $tracked = @(git ls-files "$d/*")
    if ($tracked.Count -gt 0) {
      $presentOutOfScopeDirs += $d
    }
  }

  $dirOk = ($presentOutOfScopeDirs.Count -eq 0)
  $dirDetails = "$($presentOutOfScopeDirs.Count) out-of-scope root dir(s)"
  if ($dirOk) { $dirDetails = "no out-of-scope root dirs found" }
  Write-Check -Name "Out-of-scope root dirs" -Ok $dirOk -Details $dirDetails
  if (-not $dirOk) {
    $presentOutOfScopeDirs | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    if ($Strict) { $failed = $true }
  }

  $leakPatterns = @(
    '\.\./oo-host',
    '\.\./oo-model',
    '\.\./oo-system',
    'Operating Organism'
  )

  $leaks = @()
  foreach ($p in $leakPatterns) {
    $out = git --no-pager grep -n -E $p -- README.md docs 2>$null
    if ($LASTEXITCODE -eq 0 -and $out) {
      $leaks += $out
    }
  }

  $leakOk = ($leaks.Count -eq 0)
  $leakDetails = "$($leaks.Count) scope leak marker(s)"
  if ($leakOk) { $leakDetails = "no private-scope markers in public docs" }
  Write-Check -Name "Public docs scope" -Ok $leakOk -Details $leakDetails
  if (-not $leakOk) {
    $leaks | Select-Object -First 20 | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    if ($Strict) { $failed = $true }
  }

  if ($failed) {
    Write-Host "`nScope check result: FAIL" -ForegroundColor Red
    exit 1
  }

  Write-Host "`nScope check result: PASS" -ForegroundColor Green
  exit 0
}
finally {
  Pop-Location
}
