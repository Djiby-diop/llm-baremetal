[CmdletBinding(PositionalBinding = $false)]
param(
  [ValidateSet('smoke','consult','reboot','handoff','all-core')]
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
$autorunScript = Join-Path $root 'test-qemu-autorun.ps1'
$handoffScript = Join-Path $root 'test-qemu-handoff.ps1'

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
  $args = @{
    Mode = $autorunMode
    Accel = $Accel
    MemMB = $MemMB
    TimeoutSec = $TimeoutSec
  }
  if ($SkipPrebuild) {
    $args['SkipPrebuild'] = $true
  }
  if ($autorunMode -eq 'oo_consult_smoke') {
    $args['ModelBin'] = $ModelBin
  }
  & $autorunScript @args
  if ($LASTEXITCODE -ne 0) {
    throw "test-qemu-autorun.ps1 failed for mode=$autorunMode ($LASTEXITCODE)"
  }
}

function Invoke-HandoffMode {
  $args = @{
    Accel = $Accel
    MemMB = $MemMB
    TimeoutSec = $TimeoutSec
  }
  if ($SkipPrebuild) {
    $args['SkipPrebuild'] = $true
  }
  if ($OoHostRoot) {
    $args['OoHostRoot'] = $OoHostRoot
  }
  & $handoffScript @args
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
  'handoff' {
    Invoke-Step 'Host to sovereign handoff smoke' { Invoke-HandoffMode }
  }
  'all-core' {
    Invoke-Step 'No-model OO smoke' { Invoke-AutorunMode 'oo_smoke' }
    Invoke-Step 'OO reboot continuity smoke' { Invoke-AutorunMode 'oo_reboot_smoke' }
    Invoke-Step 'Host to sovereign handoff smoke' { Invoke-HandoffMode }
    Invoke-Step 'Model-backed OO consult smoke' { Invoke-AutorunMode 'oo_consult_smoke' }
  }
}

Write-Host '[QEMU OO] PASS' -ForegroundColor Green