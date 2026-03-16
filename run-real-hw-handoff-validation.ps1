[CmdletBinding(PositionalBinding = $false)]
param(
  [ValidateSet('prepare','collect')]
  [string]$Phase = 'prepare',
  [string]$OoHostRoot,
  [string]$UsbRoot,
  [string]$ImagePath,
  [string]$ArtifactsDir,
  [string]$BaseImagePath,
  [string]$OutImagePath,
  [switch]$SkipExport,
  [switch]$RequireDiag,
  [switch]$SkipSyncCheck
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$workspaceRoot = Split-Path -Parent $root
$prepareScript = Join-Path $root 'prepare-real-hw-handoff.ps1'
$collectScript = Join-Path $root 'collect-real-hw-oo-artifacts.ps1'
$validateScript = Join-Path $root 'validate-real-hw-oo-artifacts.ps1'
$reportScript = Join-Path $root 'write-real-hw-oo-validation-report.ps1'

foreach ($path in @($prepareScript, $collectScript, $validateScript, $reportScript)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required helper: $path"
  }
}

if (-not $PSBoundParameters.ContainsKey('OoHostRoot')) {
  if ($env:OO_HOST_ROOT) {
    $OoHostRoot = $env:OO_HOST_ROOT
  } else {
    $OoHostRoot = Join-Path $workspaceRoot 'oo-host'
  }
}

$resolvedOoHostRoot = $null
if ($OoHostRoot) {
  $resolvedOoHostRoot = [System.IO.Path]::GetFullPath($OoHostRoot)
}

function Resolve-LatestArtifacts([string]$pattern) {
  $latest = Get-ChildItem -LiteralPath (Join-Path $root 'artifacts') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like $pattern } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if (-not $latest) {
    throw 'Collector completed but no artifact directory was found.'
  }
  return $latest.FullName
}

function Invoke-OoBotSyncCheck([string]$ooHostRoot, [string]$workspacePath, [string]$exportPath, [string]$receiptPath, [string]$outPath) {
  if (-not (Test-Path -LiteralPath $ooHostRoot)) {
    Write-Host "[OO Handoff] WARN: oo-host workspace missing, skipping sync-check: $ooHostRoot" -ForegroundColor Yellow
    return
  }

  if (-not (Test-Path -LiteralPath $exportPath)) {
    Write-Host "[OO Handoff] WARN: sovereign export missing, skipping sync-check: $exportPath" -ForegroundColor Yellow
    return
  }

  if (-not (Test-Path -LiteralPath $receiptPath)) {
    Write-Host "[OO Handoff] WARN: handoff receipt missing, skipping sync-check: $receiptPath" -ForegroundColor Yellow
    return
  }

  $ooBotExe = Join-Path $ooHostRoot 'target\debug\oo-bot.exe'
  $syncOutput = $null

  if (Test-Path -LiteralPath $ooBotExe) {
    $syncOutput = & $ooBotExe --data-dir (Join-Path $ooHostRoot 'data') sync-check --workspace $workspacePath --export $exportPath --receipt $receiptPath 2>&1
    if ($LASTEXITCODE -ne 0) {
      throw "oo-bot sync-check failed ($LASTEXITCODE)"
    }
  } else {
    $cargo = Get-Command cargo -ErrorAction SilentlyContinue
    if (-not $cargo) {
      Write-Host '[OO Handoff] WARN: cargo not found and oo-bot.exe missing; skipping sync-check' -ForegroundColor Yellow
      return
    }

    Push-Location -LiteralPath $ooHostRoot
    try {
      $syncOutput = & cargo run --quiet --bin oo-bot -- --data-dir (Join-Path $ooHostRoot 'data') sync-check --workspace $workspacePath --export $exportPath --receipt $receiptPath 2>&1
      if ($LASTEXITCODE -ne 0) {
        throw "cargo run oo-bot sync-check failed ($LASTEXITCODE)"
      }
    }
    finally {
      Pop-Location
    }
  }

  Set-Content -LiteralPath $outPath -Value (($syncOutput | Out-String).TrimEnd()) -Encoding UTF8
  Write-Host "[OO Handoff] Sync-check: $outPath" -ForegroundColor Green
}

if ($Phase -eq 'prepare') {
  $prepareArgs = @{
    CreateBootImage = $true
  }
  if ($resolvedOoHostRoot) {
    $prepareArgs['OoHostRoot'] = $resolvedOoHostRoot
  }
  if ($BaseImagePath) {
    $prepareArgs['BaseImagePath'] = $BaseImagePath
  }
  if ($OutImagePath) {
    $prepareArgs['OutImagePath'] = $OutImagePath
  }
  if ($SkipExport) {
    $prepareArgs['SkipExport'] = $true
  }

  & $prepareScript @prepareArgs

  Write-Host ''
  Write-Host '[OO Handoff] Next steps on the physical machine:' -ForegroundColor Cyan
  Write-Host '  1. Flash or copy the generated handoff image to the USB device.' -ForegroundColor Gray
  Write-Host '  2. Boot the target machine.' -ForegroundColor Gray
  Write-Host '  3. Let the autorun complete /oo_handoff_info, /oo_handoff_apply, and /oo_handoff_receipt.' -ForegroundColor Gray
  Write-Host '  4. Mount the FAT partition again on the host.' -ForegroundColor Gray
  Write-Host '  5. Run this same script with -Phase collect -UsbRoot <drive>.' -ForegroundColor Gray
  exit 0
}

if (-not $UsbRoot -and -not $ImagePath) {
  throw 'For -Phase collect, provide -UsbRoot <mounted FAT root> or -ImagePath <boot image>.'
}

if ($UsbRoot -and $ImagePath) {
  throw 'For -Phase collect, use either -UsbRoot or -ImagePath, not both.'
}

$targetArtifacts = if ($ArtifactsDir) {
  if ([System.IO.Path]::IsPathRooted($ArtifactsDir)) {
    [System.IO.Path]::GetFullPath($ArtifactsDir)
  } else {
    [System.IO.Path]::GetFullPath((Join-Path $root $ArtifactsDir))
  }
} else {
  Join-Path (Join-Path $root 'artifacts') ("real-hw-handoff-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
}

$collectArgs = @{
  OutDir = $targetArtifacts
}
if ($UsbRoot) {
  $collectArgs['UsbRoot'] = $UsbRoot
} else {
  $collectArgs['ImagePath'] = $ImagePath
}

& $collectScript @collectArgs

if (-not (Test-Path -LiteralPath $targetArtifacts)) {
  $targetArtifacts = Resolve-LatestArtifacts 'real-hw-handoff-*'
}

$validateArgs = @{
  ArtifactsDir = $targetArtifacts
  RequireHandoff = $true
  AllowNoConsult = $true
}
if ($RequireDiag) {
  $validateArgs['RequireDiag'] = $true
}

& $validateScript @validateArgs

$reportArgs = @{
  ArtifactsDir = $targetArtifacts
  ModelBin = ''
  Scenario = 'handoff'
}
if ($UsbRoot) {
  $reportArgs['SourceLabel'] = [System.IO.Path]::GetFullPath($UsbRoot)
} elseif ($ImagePath) {
  $reportArgs['SourceLabel'] = [System.IO.Path]::GetFullPath($ImagePath)
}

& $reportScript @reportArgs

if (-not $SkipSyncCheck) {
  $exportPath = Join-Path (Join-Path $resolvedOoHostRoot 'data') 'sovereign_export.json'
  $receiptPath = Join-Path $targetArtifacts 'OOHANDOFF.TXT'
  $syncPath = Join-Path $targetArtifacts 'sync-check.txt'
  Invoke-OoBotSyncCheck -ooHostRoot $resolvedOoHostRoot -workspacePath $root -exportPath $exportPath -receiptPath $receiptPath -outPath $syncPath
}

Write-Host ''
Write-Host "[OO Handoff] Validation complete: $targetArtifacts" -ForegroundColor Green