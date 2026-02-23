# Helper script to run oo-guard when cargo is not in PATH
# Usage: .\run-oo-guard.ps1 check --root . --logs-dir artifacts/m14

param(
  [Parameter(ValueFromRemainingArguments=$true)]
  [string[]]$Args
)

$ErrorActionPreference = 'Stop'

$cargoPath = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'

if (-not (Test-Path -LiteralPath $cargoPath)) {
  Write-Host "ERROR: Rust not found at $cargoPath" -ForegroundColor Red
  Write-Host "Install from: https://rustup.rs" -ForegroundColor Yellow
  exit 1
}

$manifest = Join-Path $PSScriptRoot 'oo-guard\Cargo.toml'
if (-not (Test-Path -LiteralPath $manifest)) {
  Write-Host "ERROR: oo-guard not found: $manifest" -ForegroundColor Red
  exit 1
}

Write-Host "[oo-guard] Running with args: $Args" -ForegroundColor Cyan

& $cargoPath run --release --manifest-path $manifest -- @Args

exit $LASTEXITCODE
