param(
  [string]$LogPath = "artifacts/m8/m8-qemu-serial.log",
  [ValidateRange(0, 120000)]
  [int]$MaxModelSelectMs = 2000,
  [ValidateRange(0, 120000)]
  [int]$MaxModelPrepareMs = 12000,
  [switch]$RequireOoMarkers
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Ok([string]$msg) {
  Write-Host "[M9][OK] $msg" -ForegroundColor Green
}

function Write-Warn([string]$msg) {
  Write-Host "[M9][WARN] $msg" -ForegroundColor Yellow
}

function Write-Step([string]$msg) {
  Write-Host "[M9] $msg" -ForegroundColor Cyan
}

function Assert-Pattern([string]$text, [string]$pattern, [string]$label) {
  if ($text -imatch $pattern) {
    Write-Ok $label
    return $true
  }
  Write-Warn "$label (missing pattern: $pattern)"
  return $false
}

function Assert-Max([int]$value, [int]$max, [string]$label) {
  if ($value -le $max) {
    Write-Ok "$label ($value <= $max)"
    return $true
  }
  Write-Warn "$label ($value > $max)"
  return $false
}

if (-not (Test-Path -LiteralPath $LogPath)) {
  throw "M9 guardrail log not found: $LogPath"
}

$log = Get-Content -LiteralPath $LogPath -Raw
Write-Step "Parsing guardrails from $LogPath"

$ok = $true

$mandatory = @(
  @{ Label = 'Startup marker present'; Pattern = '\[obs\]\[startup\]\s+model_select_ms=\d+\s+model_prepare_ms=\d+' },
  @{ Label = 'Models summary present'; Pattern = 'summary:\s+total=' }
)

foreach ($m in $mandatory) {
  if (-not (Assert-Pattern -text $log -pattern $m.Pattern -label $m.Label)) {
    $ok = $false
  }
}

$mm = [regex]::Match($log, '\[obs\]\[startup\]\s+model_select_ms=(\d+)\s+model_prepare_ms=(\d+)')
if ($mm.Success) {
  $selectMs = [int]$mm.Groups[1].Value
  $prepareMs = [int]$mm.Groups[2].Value

  if (-not (Assert-Max -value $selectMs -max $MaxModelSelectMs -label 'model_select_ms budget')) {
    $ok = $false
  }
  if (-not (Assert-Max -value $prepareMs -max $MaxModelPrepareMs -label 'model_prepare_ms budget')) {
    $ok = $false
  }
} else {
  Write-Warn 'Could not parse startup latency marker'
  $ok = $false
}

if ($RequireOoMarkers) {
  $ooChecks = @(
    @{ Label = 'OO confidence marker present'; Pattern = 'OK:\s+OO confidence:' },
    @{ Label = 'OO feedback marker present'; Pattern = 'OK:\s+OO feedback:' },
    @{ Label = 'OO plan marker present'; Pattern = 'OK:\s+OO plan:' }
  )

  foreach ($o in $ooChecks) {
    if (-not (Assert-Pattern -text $log -pattern $o.Pattern -label $o.Label)) {
      $ok = $false
    }
  }
}

if (-not $ok) {
  throw "M9 guardrails failed for $LogPath"
}

Write-Ok 'M9 guardrails passed'
