[CmdletBinding(PositionalBinding = $false)]
param(
  # When set, missing Rust/cargo will fail validation.
  [switch]$Strict,

  # When set, skip OS-G host-side tests (cargo test / dplus_check).
  [switch]$SkipOsgHost,

  # When set, skip OS-G QEMU/UEFI smoke.
  [switch]$SkipOsgSmoke,

  # Pass-through to OS-G smoke runner.
  [ValidateSet('debug','release')][string]$OsgProfile = 'release',
  [int]$TimeoutSec = 180
)

$ErrorActionPreference = 'Stop'

Set-Location -LiteralPath $PSScriptRoot

function Info([string]$msg) { Write-Host $msg -ForegroundColor Cyan }
function Warn([string]$msg) { Write-Host $msg -ForegroundColor Yellow }

Info "=== Validate (llm-baremetal) ==="

# 1) Host preflight (WSL/QEMU/OVMF)
$preflight = Join-Path $PSScriptRoot 'preflight-host.ps1'
if (Test-Path -LiteralPath $preflight) {
  Info "[1/3] Host preflight"
  & $preflight
} else {
  Warn "[1/3] Host preflight: missing preflight-host.ps1 (skipping)"
}

# 2) OS-G host-side checks
if (-not $SkipOsgHost) {
  $osgRoot = Join-Path $PSScriptRoot 'OS-G (Operating System Genesis)'
  if (-not (Test-Path -LiteralPath $osgRoot)) {
    if ($Strict) { throw "OS-G folder not found: $osgRoot" }
    Warn "[2/3] OS-G host checks: OS-G folder not found (skipping)"
  } else {
    $cargo = Get-Command cargo -ErrorAction SilentlyContinue
    if (-not $cargo) {
      if ($Strict) { throw "cargo not found (install Rust toolchain)" }
      Warn "[2/3] OS-G host checks: cargo not found (skipping)"
    } else {
      Info "[2/3] OS-G host checks: cargo test --features std"
      Push-Location -LiteralPath $osgRoot
      try {
        & cargo test --features std
        if ($LASTEXITCODE -ne 0) { throw "OS-G cargo test failed ($LASTEXITCODE)" }

        $policyForSmoke = Join-Path $osgRoot 'qemu-fs\policy.dplus'
        if (-not (Test-Path -LiteralPath $policyForSmoke)) {
          throw "OS-G smoke policy not found: $policyForSmoke"
        }

        Info "[2/3] OS-G host checks: dplus_check qemu-fs/policy.dplus"
        & cargo run --quiet --features std --bin dplus_check -- $policyForSmoke
        if ($LASTEXITCODE -ne 0) { throw "dplus_check failed ($LASTEXITCODE)" }
      }
      finally {
        Pop-Location
      }
    }
  }
} else {
  Warn "[2/3] OS-G host checks: skipped (-SkipOsgHost)"
}

# 3) OS-G QEMU/UEFI smoke
if (-not $SkipOsgSmoke) {
  Info "[3/3] OS-G smoke (UEFI/QEMU)"
  $smoke = Join-Path $PSScriptRoot 'run-osg-smoke.ps1'
  if (-not (Test-Path -LiteralPath $smoke)) {
    throw "run-osg-smoke.ps1 not found: $smoke"
  }
  & $smoke -Profile $OsgProfile -TimeoutSec $TimeoutSec
  if ($LASTEXITCODE -ne 0) { throw "OS-G smoke failed ($LASTEXITCODE)" }
} else {
  Warn "[3/3] OS-G smoke: skipped (-SkipOsgSmoke)"
}

Info "Validate: OK"
