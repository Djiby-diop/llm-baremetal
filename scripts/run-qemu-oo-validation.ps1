[CmdletBinding(PositionalBinding = $false)]
param(
  [ValidateSet('smoke','consult','reboot','outcome','handoff','all-core')]
  [string]$Mode = 'all-core',

  [ValidateSet('auto','whpx','tcg','none')]
  [string]$Accel = 'tcg',

  [ValidateRange(512, 8192)]
  [int]$MemMB = 1024,

  [ValidateRange(30, 86400)]
  [int]$TimeoutSec = 480,

  [string]$ModelBin = 'stories15M.q8_0.gguf',
  [switch]$SkipPrebuild,
  [string]$OoHostRoot
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $root

$autorunCandidates = @(
  (Join-Path $repoRoot 'tests\test-qemu-autorun.ps1'),
  (Join-Path $repoRoot 'test-qemu-autorun.ps1')
)

$handoffCandidates = @(
  (Join-Path $repoRoot 'tests\test-qemu-handoff.ps1'),
  (Join-Path $repoRoot 'test-qemu-handoff.ps1')
)

$autorunScript = $autorunCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$handoffScript = $handoffCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

foreach ($path in @($autorunScript, $handoffScript)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required script: $path"
  }
}

function Invoke-Step([string]$label, [scriptblock]$action) {
  Write-Host "[QEMU OO] $label" -ForegroundColor Cyan
  & $action
  Write-Host "[QEMU OO] OK: $label" -ForegroundColor Green
}

function Invoke-AutorunMode([string]$autorunMode) {
  $callParams = @{
    Mode = $autorunMode
    Accel = $Accel
    MemMB = $MemMB
    TimeoutSec = $TimeoutSec
  }
  if ($SkipPrebuild) {
    $callParams['SkipPrebuild'] = $true
  }
  if ($autorunMode -eq 'oo_consult_smoke') {
    $callParams['ModelBin'] = $ModelBin
  }
  & $autorunScript @callParams
  if ($LASTEXITCODE -ne 0) {
    throw "test-qemu-autorun.ps1 failed for mode=$autorunMode ($LASTEXITCODE)"
  }
}

function Invoke-HandoffMode {
  $callParams = @{
    Accel = $Accel
    MemMB = $MemMB
    TimeoutSec = $TimeoutSec
  }
  if ($SkipPrebuild) {
    $callParams['SkipPrebuild'] = $true
  }
  if ($OoHostRoot) {
    $callParams['OoHostRoot'] = $OoHostRoot
  }
  & $handoffScript @callParams
  if ($LASTEXITCODE -ne 0) {
    throw "test-qemu-handoff.ps1 failed ($LASTEXITCODE)"
  }
}

switch ($Mode) {
  'smoke' {
    Invoke-Step 'No-model OO smoke' { Invoke-AutorunMode 'oo_smoke' }
  }
  'consult' {
    Invoke-Step 'Model-backed OO consult smoke' { Invoke-AutorunMode 'oo_consult_smoke' }
  }
  'reboot' {
    Invoke-Step 'OO reboot continuity smoke' { Invoke-AutorunMode 'oo_reboot_smoke' }
  }
  'outcome' {
    Invoke-Step 'OO outcome feedback smoke' { Invoke-AutorunMode 'oo_outcome_smoke' }
  }
  'handoff' {
    Invoke-Step 'Host to sovereign handoff smoke' { Invoke-HandoffMode }
  }
  'all-core' {
    Invoke-Step 'No-model OO smoke' { Invoke-AutorunMode 'oo_smoke' }
    Invoke-Step 'OO reboot continuity smoke' { Invoke-AutorunMode 'oo_reboot_smoke' }
    Invoke-Step 'OO outcome feedback smoke' { Invoke-AutorunMode 'oo_outcome_smoke' }
    Invoke-Step 'Host to sovereign handoff smoke' { Invoke-HandoffMode }
    Invoke-Step 'Model-backed OO consult smoke' { Invoke-AutorunMode 'oo_consult_smoke' }
  }
}

Write-Host '[QEMU OO] PASS' -ForegroundColor Green