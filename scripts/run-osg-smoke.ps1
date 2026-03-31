[CmdletBinding(PositionalBinding = $false)]
param(
  [ValidateSet('debug','release')][string]$Profile = 'release',
  [ValidateSet('pass','fail')][string]$ExpectedResult = 'pass',
  [string]$RequiredRegex,
  [string]$PolicySource,
  [int]$TimeoutSec = 120,
  [string]$QemuPath,
  [string]$OvmfCode,
  [string]$OvmfVars
)

$ErrorActionPreference = 'Stop'

$osgRoot = Join-Path $PSScriptRoot 'OS-G (Operating System Genesis)'
$osgScript = Join-Path $osgRoot 'qemu-test.ps1'

if (-not (Test-Path -LiteralPath $osgScript)) {
  throw "OS-G smoke runner not found. Expected: $osgScript"
}

$psArgs = @(
  '-NoProfile',
  '-ExecutionPolicy','Bypass',
  '-File', $osgScript,
  '-Profile', $Profile,
  '-ExpectedResult', $ExpectedResult,
  '-TimeoutSec', $TimeoutSec
)

if ($RequiredRegex) { $psArgs += @('-RequiredRegex', $RequiredRegex) }
if ($PolicySource)  { $psArgs += @('-PolicySource',  $PolicySource) }
if ($QemuPath)      { $psArgs += @('-QemuPath',      $QemuPath) }
if ($OvmfCode)      { $psArgs += @('-OvmfCode',      $OvmfCode) }
if ($OvmfVars)      { $psArgs += @('-OvmfVars',      $OvmfVars) }

Write-Host "[OS-G] Running QEMU smoke from: $osgRoot" -ForegroundColor Cyan

# Run in a clean pwsh process to avoid parent session state.
& pwsh @psArgs
exit $LASTEXITCODE
