[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$OoHostRoot,
  [string]$ExportPath,
  [string]$UsbRoot,
  [switch]$SkipExport
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$workspaceRoot = Split-Path -Parent $root
$handoffScript = Join-Path $root 'llmk-autorun-real-hw-handoff-smoke.txt'

if (-not $PSBoundParameters.ContainsKey('OoHostRoot')) {
  if ($env:OO_HOST_ROOT) {
    $OoHostRoot = $env:OO_HOST_ROOT
  } else {
    $OoHostRoot = Join-Path $workspaceRoot 'oo-host'
  }
}

$ooHostRoot = [System.IO.Path]::GetFullPath($OoHostRoot)
$dataDir = Join-Path $ooHostRoot 'data'

if (-not (Test-Path -LiteralPath $ooHostRoot)) {
  throw "Missing oo-host workspace: $ooHostRoot"
}
if (-not (Test-Path -LiteralPath $handoffScript)) {
  throw "Missing handoff autorun script: $handoffScript"
}

if (-not $PSBoundParameters.ContainsKey('ExportPath')) {
  $ExportPath = Join-Path $dataDir 'sovereign_export.json'
}
$ExportPath = [System.IO.Path]::GetFullPath($ExportPath)

function Write-HandoffExport {
  if ($SkipExport) { return }

  $ooHostExe = Join-Path $ooHostRoot 'target\debug\oo-host.exe'
  if (Test-Path -LiteralPath $ooHostExe) {
    & $ooHostExe --data-dir $dataDir export sovereign --out $ExportPath
    if ($LASTEXITCODE -ne 0) { throw "oo-host export failed ($LASTEXITCODE)" }
    return
  }

  $cargo = Get-Command cargo -ErrorAction SilentlyContinue
  if (-not $cargo) {
    throw "cargo not found and oo-host.exe missing; cannot generate sovereign export"
  }

  Push-Location -LiteralPath $ooHostRoot
  try {
    & cargo run --quiet --bin oo-host -- --data-dir $dataDir export sovereign --out $ExportPath
    if ($LASTEXITCODE -ne 0) { throw "cargo run export failed ($LASTEXITCODE)" }
  }
  finally {
    Pop-Location
  }
}

Write-HandoffExport

if (-not (Test-Path -LiteralPath $ExportPath)) {
  throw "Missing sovereign export: $ExportPath"
}

Write-Host "[Handoff] Export ready: $ExportPath" -ForegroundColor Green

if ($PSBoundParameters.ContainsKey('UsbRoot') -and $UsbRoot) {
  $usbRootFull = [System.IO.Path]::GetFullPath($UsbRoot)
  if (-not (Test-Path -LiteralPath $usbRootFull)) {
    throw "Missing USB root path: $usbRootFull"
  }

  $usbExportPath = Join-Path $usbRootFull 'sovereign_export.json'
  $usbScriptPath = Join-Path $usbRootFull 'llmk-autorun-real-hw-handoff-smoke.txt'

  Copy-Item -LiteralPath $ExportPath -Destination $usbExportPath -Force
  Copy-Item -LiteralPath $handoffScript -Destination $usbScriptPath -Force

  Write-Host "[Handoff] Copied export: $usbExportPath" -ForegroundColor Green
  Write-Host "[Handoff] Copied autorun helper: $usbScriptPath" -ForegroundColor Green
}
