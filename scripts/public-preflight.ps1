param(
  [switch]$Strict
)

$ErrorActionPreference = 'Stop'

function Test-Command {
  param([string]$Name)
  return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

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

  # 1) Git clean state
  $status = git status --porcelain
  $isClean = [string]::IsNullOrWhiteSpace(($status -join "`n"))
  $gitDetails = "uncommitted changes present"
  if ($isClean) { $gitDetails = "working tree clean" }
  Write-Check -Name "Git clean" -Ok $isClean -Details $gitDetails
  if (-not $isClean) { $failed = $true }

  # 2) Required docs
  $requiredDocs = @(
    "README.md",
    "LICENSE",
    "NOTICE",
    "docs/PUBLIC_RELEASE_PLAYBOOK.md",
    "docs/PUBLIC_SCOPE.md",
    "docs/THIRD_PARTY_LICENSE_AUDIT.md",
    "docs/TRANSLATIONS.md"
  )
  foreach ($doc in $requiredDocs) {
    $exists = Test-Path -LiteralPath (Join-Path $repoRoot $doc)
    $docDetails = "missing"
    if ($exists) { $docDetails = "present" }
    Write-Check -Name "Doc: $doc" -Ok $exists -Details $docDetails
    if (-not $exists) { $failed = $true }
  }

  # 3) Tracked build artifacts policy
  $trackedArtifacts = @(git ls-files "*.o" "*.so" "*.efi" "*.img" "*.fd" "*.log")
  $artifactOk = ($trackedArtifacts.Count -eq 0)
  $artifactDetails = "$($trackedArtifacts.Count) tracked"
  if ($artifactOk) { $artifactDetails = "none tracked" }
  Write-Check -Name "Tracked build artifacts" -Ok $artifactOk -Details $artifactDetails
  if (-not $artifactOk) {
    $trackedArtifacts | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    if ($Strict) { $failed = $true }
  }

  # 4) Basic secret scan
  $patterns = @(
    'ghp_[A-Za-z0-9]{20,}',
    'sk-[A-Za-z0-9]{20,}',
    'BEGIN (RSA|OPENSSH|EC|DSA) PRIVATE KEY',
    'Authorization:\s*Bearer\s+[A-Za-z0-9\-_\.]{10,}',
    'api[_-]?key\s*[:=]\s*["''][^"'']+["'']',
    'password\s*[:=]\s*["''][^"'']+["'']'
  )

  $hits = @()
  if (Test-Command rg) {
    foreach ($p in $patterns) {
      $out = rg -n --hidden --glob "!.git" --glob "!*.png" --glob "!*.jpg" --glob "!*.jpeg" --glob "!*.gif" --glob "!*.webp" --glob "!*.bin" --glob "!*.gguf" -e $p . 2>$null
      if ($LASTEXITCODE -eq 0 -and $out) {
        $hits += $out
      }
    }
  } else {
    foreach ($p in $patterns) {
      $out = git --no-pager grep -n -E $p -- . 2>$null
      if ($LASTEXITCODE -eq 0 -and $out) {
        $hits += $out
      }
    }
  }

  $secretOk = ($hits.Count -eq 0)
  $secretDetails = "$($hits.Count) potential match(es)"
  if ($secretOk) { $secretDetails = "no obvious secrets found" }
  Write-Check -Name "Secret scan" -Ok $secretOk -Details $secretDetails
  if (-not $secretOk) {
    $hits | Select-Object -First 20 | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    $failed = $true
  }

  # 5) Public scope check
  $scopeArgs = @()
  if ($Strict) { $scopeArgs += "-Strict" }
  & (Join-Path $PSScriptRoot "public-scope-check.ps1") @scopeArgs
  $scopeOk = ($LASTEXITCODE -eq 0)
  Write-Check -Name "Public scope" -Ok $scopeOk -Details "see scope checker output"
  if (-not $scopeOk) { $failed = $true }

  # 6) Summary
  if ($failed) {
    Write-Host "`nPreflight result: FAIL" -ForegroundColor Red
    exit 1
  } else {
    Write-Host "`nPreflight result: PASS" -ForegroundColor Green
    exit 0
  }
}
finally {
  Pop-Location
}
