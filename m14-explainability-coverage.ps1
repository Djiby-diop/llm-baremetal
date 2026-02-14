param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [string]$JournalLogPath = "artifacts/m14/OOJOUR.LOG",
  [string]$StatePath = "artifacts/m14/coverage-state.json",
  [string]$HistoryPath = "artifacts/m14/history.jsonl",
  [switch]$FailOnCoverageGap,
  [switch]$RequireJournalParity,
  [switch]$NoHistoryWrite
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "[M14] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg) { Write-Host "[M14][OK] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "[M14][WARN] $msg" -ForegroundColor Yellow }

if (-not (Test-Path -LiteralPath $LogPath)) {
  throw "M14 log not found: $LogPath"
}

$logText = Get-Content -LiteralPath $LogPath -Raw
$logLines = $logText -split "`r?`n"

$confidenceLines = @($logLines | Where-Object { $_ -match 'OK:\s+OO confidence:' })
$planLines = @($logLines | Where-Object { $_ -match 'OK:\s+OO plan:' })
$autoApplyLines = @($logLines | Where-Object { $_ -match 'OK:\s+OO auto-apply:|ERROR:\s+OO auto-apply verification failed:' })

$confidenceWithReasonId = @($confidenceLines | Where-Object { $_ -match 'reason_id=' })
$planWithReasonId = @($planLines | Where-Object { $_ -match 'reason_id=' })
$autoApplyWithReasonId = @($autoApplyLines | Where-Object { $_ -match 'reason_id=' })

$coverage = [ordered]@{
  confidence_total = $confidenceLines.Count
  confidence_with_reason_id = $confidenceWithReasonId.Count
  plan_total = $planLines.Count
  plan_with_reason_id = $planWithReasonId.Count
  auto_apply_total = $autoApplyLines.Count
  auto_apply_with_reason_id = $autoApplyWithReasonId.Count
}

$coverageOk = $true
if ($coverage.confidence_total -gt 0 -and $coverage.confidence_with_reason_id -lt $coverage.confidence_total) {
  $coverageOk = $false
}
if ($coverage.plan_total -gt 0 -and $coverage.plan_with_reason_id -lt $coverage.plan_total) {
  $coverageOk = $false
}
if ($coverage.auto_apply_total -gt 0 -and $coverage.auto_apply_with_reason_id -lt $coverage.auto_apply_total) {
  $coverageOk = $false
}

Write-Step "coverage confidence=$($coverage.confidence_with_reason_id)/$($coverage.confidence_total) plan=$($coverage.plan_with_reason_id)/$($coverage.plan_total) auto_apply=$($coverage.auto_apply_with_reason_id)/$($coverage.auto_apply_total)"

$journalPresent = Test-Path -LiteralPath $JournalLogPath
$journalParityOk = $true
$journalParityChecked = $false
$journalLogAutoApplyCount = 0
$journalLogAutoApplyWithReasonIdCount = 0

if ($journalPresent) {
  $journalText = Get-Content -LiteralPath $JournalLogPath -Raw
  $journalLines = $journalText -split "`r?`n"
  $journalAutoApply = @($journalLines | Where-Object { $_ -match 'auto_apply\s+action=.*result=(success|failed)' })
  $journalAutoApplyWithReasonId = @($journalAutoApply | Where-Object { $_ -match 'reason_id=' })

  $journalLogAutoApplyCount = $journalAutoApply.Count
  $journalLogAutoApplyWithReasonIdCount = $journalAutoApplyWithReasonId.Count
  $journalParityChecked = $true

  if ($journalLogAutoApplyWithReasonIdCount -lt $journalLogAutoApplyCount) {
    $journalParityOk = $false
  }

  if ($journalLogAutoApplyCount -gt 0 -and $autoApplyLines.Count -gt 0 -and $journalLogAutoApplyCount -ne $autoApplyLines.Count) {
    $journalParityOk = $false
  }

  Write-Step "journal parity auto_apply log=$($autoApplyLines.Count) journal=$journalLogAutoApplyCount with_reason_id=$journalLogAutoApplyWithReasonIdCount"
} else {
  Write-Warn "Journal log not found for parity check: $JournalLogPath"
  if ($RequireJournalParity) {
    $journalParityOk = $false
  }
}

$overallOk = ($coverageOk -and $journalParityOk)

$stateDir = Split-Path -Parent $StatePath
if ($stateDir -and -not (Test-Path -LiteralPath $stateDir)) {
  New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
}

$historyDir = Split-Path -Parent $HistoryPath
if ($historyDir -and -not (Test-Path -LiteralPath $historyDir)) {
  New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
}

$state = [ordered]@{
  ts_utc = (Get-Date).ToUniversalTime().ToString('o')
  log_path = $LogPath
  journal_log_path = $JournalLogPath
  coverage = $coverage
  coverage_ok = $coverageOk
  journal_parity_checked = $journalParityChecked
  journal_parity_ok = $journalParityOk
  journal_present = $journalPresent
  journal_auto_apply_total = $journalLogAutoApplyCount
  journal_auto_apply_with_reason_id = $journalLogAutoApplyWithReasonIdCount
  overall_ok = $overallOk
}

$state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $StatePath -Encoding UTF8
Write-Ok "Coverage state written: $StatePath"

if (-not $NoHistoryWrite) {
  ($state | ConvertTo-Json -Compress -Depth 6) | Add-Content -LiteralPath $HistoryPath
  Write-Ok "Coverage history appended: $HistoryPath"
}

if (-not $coverageOk) {
  Write-Warn 'Reason ID coverage gap detected in runtime markers'
}
if (-not $journalParityOk) {
  Write-Warn 'Journal parity gap detected'
}

if ($FailOnCoverageGap -and (-not $coverageOk)) {
  throw 'M14 coverage check failed'
}
if ($RequireJournalParity -and (-not $journalParityOk)) {
  throw 'M14 journal parity check failed'
}

Write-Ok 'M14 explainability coverage check complete'
