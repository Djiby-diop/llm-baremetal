param(
  [ValidateSet('oo_smoke','oo_consult_smoke','oo_reboot_smoke','oo_outcome_smoke','smoke')]
  [string]$Mode = 'oo_smoke',
  [ValidateSet('auto','whpx','tcg','none')]
  [string]$Accel = 'tcg',
  [ValidateRange(512, 8192)]
  [int]$MemMB = 1024,
  [ValidateRange(30, 86400)]
  [int]$TimeoutSec = 480,
  [switch]$SkipBuild,
  [switch]$SkipPrebuild,
  [switch]$SkipInspect,
  [switch]$BootstrapToolchains,
  [switch]$SkipModelAssertions,
  [string]$ModelBin = '',
  [string]$BootModel = '',
  [string]$ExtraModel = '',
  [string]$ExpectedModel = '',
  [ValidateRange(1, 4096)]
  [int]$GenMaxTokens = 64,
  [ValidateRange(0.0, 2.0)]
  [double]$GenTemp = 0.2
)

$ErrorActionPreference = 'Stop'

# Forwarder: keep historical path stable.
$script = [System.IO.Path]::Combine($PSScriptRoot, 'tests', 'test-qemu-autorun.ps1')
if (-not (Test-Path $script)) {
  throw "llm-baremetal/tests/test-qemu-autorun.ps1 not found at: $script"
}

& $script @PSBoundParameters
exit $LASTEXITCODE
