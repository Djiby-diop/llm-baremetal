[CmdletBinding(PositionalBinding = $false)]
param(
  [switch]$SkipPrebuild,
  [string]$OutImagePath
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$buildScript = Join-Path $root 'build.ps1'
$generatedImage = Join-Path $root 'llm-baremetal-boot.img'
$rebootSmokeScript = Join-Path $root 'llmk-autorun-real-hw-oo-reboot-smoke.txt'
$replCfgPath = Join-Path $root 'repl.cfg'

if (-not (Test-Path -LiteralPath $buildScript)) {
  throw "Missing build script: $buildScript"
}
if (-not (Test-Path -LiteralPath $rebootSmokeScript)) {
  throw "Missing OO reboot smoke script: $rebootSmokeScript"
}

if (-not $PSBoundParameters.ContainsKey('OutImagePath')) {
  $OutImagePath = Join-Path $root 'llm-baremetal-boot-real-hw-oo-reboot.img'
}
$OutImagePath = [System.IO.Path]::GetFullPath($OutImagePath)

function New-GeneratedReplCfg {
  $lines = [System.Collections.Generic.List[string]]::new()
  $lines.Add('oo_enable=1')
  $lines.Add('autorun_autostart=1')
  $lines.Add('autorun_shutdown_when_done=0')
  $lines.Add('autorun_file=llmk-autorun-real-hw-oo-reboot-smoke.txt')
  return ($lines -join "`n")
}

$existingReplCfg = $null
$hadExistingReplCfg = Test-Path -LiteralPath $replCfgPath
if ($hadExistingReplCfg) {
  $existingReplCfg = Get-Content -LiteralPath $replCfgPath -Raw
}

try {
  Set-Content -LiteralPath $replCfgPath -Value (New-GeneratedReplCfg) -Encoding ASCII -NoNewline

  Write-Host '[RebootPrep] Building no-model reboot image' -ForegroundColor Cyan
  Write-Host '  OO:      enabled (reboot continuity smoke)' -ForegroundColor Gray
  Write-Host '  Autorun: llmk-autorun-real-hw-oo-reboot-smoke.txt' -ForegroundColor Gray

  $buildArgs = @(
    '-NoProfile',
    '-ExecutionPolicy','Bypass',
    '-File', $buildScript,
    '-NoModel'
  )
  if ($SkipPrebuild) {
    $buildArgs += '-SkipPrebuild'
  }

  & powershell @buildArgs
  if ($LASTEXITCODE -ne 0) {
    throw "build.ps1 failed ($LASTEXITCODE)"
  }

  if (-not (Test-Path -LiteralPath $generatedImage)) {
    throw "Expected build output missing: $generatedImage"
  }

  $outDir = Split-Path -Parent $OutImagePath
  if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
  }

  Copy-Item -LiteralPath $generatedImage -Destination $OutImagePath -Force
  Write-Host "[RebootPrep] Ready: $OutImagePath" -ForegroundColor Green
}
finally {
  if ($hadExistingReplCfg) {
    Set-Content -LiteralPath $replCfgPath -Value $existingReplCfg -Encoding ASCII -NoNewline
  } elseif (Test-Path -LiteralPath $replCfgPath) {
    Remove-Item -LiteralPath $replCfgPath -Force -ErrorAction SilentlyContinue
  }
}