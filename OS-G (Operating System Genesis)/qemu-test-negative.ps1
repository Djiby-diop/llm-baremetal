param(
  [ValidateSet('debug','release')][string]$Profile = 'release',
  [string]$QemuPath,
  [string]$OvmfCode,
  [string]$OvmfVars,
  [int]$TimeoutSec = 120
)

$ErrorActionPreference = 'Stop'

$policy = Join-Path $PSScriptRoot 'qemu-fs\policy_fail_forbidden_while.dplus'

$args = @{
  Profile = $Profile
  TimeoutSec = $TimeoutSec
  ExpectedResult = 'fail'
  PolicySource = $policy
  RequiredRegex = 'DPLUS:\s*verify FAIL:'
}

if ($QemuPath) { $args.QemuPath = $QemuPath }
if ($OvmfCode) { $args.OvmfCode = $OvmfCode }
if ($OvmfVars) { $args.OvmfVars = $OvmfVars }

& (Join-Path $PSScriptRoot 'qemu-test.ps1') @args
