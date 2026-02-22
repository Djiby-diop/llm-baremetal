param(
  [switch]$NewWindow,
  [switch]$Gui,
  [switch]$PassThroughExitCode,
  [switch]$NoNormalizeExitCode,
  [ValidateSet('auto','whpx','tcg','none')]
  [string]$Accel = 'auto',
  [ValidateSet('auto','host','max','qemu64')]
  [string]$Cpu = 'auto',
  [switch]$ForceAvx2,
  [ValidateSet('pc','q35')]
  [string]$Machine = 'pc',
  [ValidateRange(512, 8192)]
  [int]$MemMB = 4096,
  [string]$QemuPath,
  [string]$OvmfPath,
  [string]$ImagePath
)

# Compatibility wrapper: keep run-qemu.ps1 working, but route through run.ps1
# so QEMU/OVMF auto-detection + vars pflash + exit-code normalization stay consistent.

$run = Join-Path $PSScriptRoot 'run.ps1'
if (-not (Test-Path $run)) {
  throw "run.ps1 not found at: $run"
}

& $run @PSBoundParameters
