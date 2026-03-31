[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$OoHostRoot,
  [string]$ExportPath,
  [string]$UsbRoot,
  [switch]$CreateBootImage,
  [string]$BaseImagePath,
  [string]$OutImagePath,
  [switch]$NoAutoAutorun,
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

if (-not $PSBoundParameters.ContainsKey('BaseImagePath')) {
  $BaseImagePath = Join-Path $root 'llm-baremetal-boot.img'
}
$BaseImagePath = [System.IO.Path]::GetFullPath($BaseImagePath)

if (-not $PSBoundParameters.ContainsKey('OutImagePath')) {
  $OutImagePath = Join-Path $root 'llm-baremetal-boot-real-hw-handoff.img'
}
$OutImagePath = [System.IO.Path]::GetFullPath($OutImagePath)

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

function Convert-WindowsPathToWsl([string]$path) {
  $full = [System.IO.Path]::GetFullPath($path)
  $drive = $full.Substring(0, 1).ToLowerInvariant()
  $rest = $full.Substring(2) -replace '\\', '/'
  return "/mnt/$drive$rest"
}

function New-HandoffAutorunConfig([string]$path) {
  $cfg = @(
    'autorun_autostart=1',
    'autorun_shutdown_when_done=0',
    'autorun_file=llmk-autorun-real-hw-handoff-smoke.txt',
    'oo_enable=1'
  ) -join "`n"
  Set-Content -LiteralPath $path -Value $cfg -Encoding ASCII -NoNewline
}

function New-HandoffBootImage {
  if (-not (Test-Path -LiteralPath $BaseImagePath)) {
    throw "Missing base image: $BaseImagePath"
  }

  $outDir = Split-Path -Parent $OutImagePath
  if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
  }

  Copy-Item -LiteralPath $BaseImagePath -Destination $OutImagePath -Force

  $imgWsl = Convert-WindowsPathToWsl $OutImagePath
  $exportWsl = Convert-WindowsPathToWsl $ExportPath

  $cfgPath = $null
  try {
    if ($NoAutoAutorun) {
      $bashCommand = "mcopy -o -i '$imgWsl@@1M' '$exportWsl' '::sovereign_export.json'"
    } else {
      $cfgPath = Join-Path ([System.IO.Path]::GetTempPath()) 'llmk-repl-handoff-auto.cfg'
      New-HandoffAutorunConfig -path $cfgPath
      $cfgWsl = Convert-WindowsPathToWsl $cfgPath
      $bashCommand = "mcopy -o -i '$imgWsl@@1M' '$exportWsl' '::sovereign_export.json' && mcopy -o -i '$imgWsl@@1M' '$cfgWsl' '::repl.cfg'"
    }

    & wsl bash -lc $bashCommand
    if ($LASTEXITCODE -ne 0) {
      throw "failed to inject handoff files into image: $OutImagePath"
    }
  }
  finally {
    if ($cfgPath -and (Test-Path -LiteralPath $cfgPath)) {
      Remove-Item -LiteralPath $cfgPath -Force -ErrorAction SilentlyContinue
    }
  }

  Write-Host "[Handoff] Boot image ready: $OutImagePath" -ForegroundColor Green
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

if ($CreateBootImage) {
  New-HandoffBootImage
}
