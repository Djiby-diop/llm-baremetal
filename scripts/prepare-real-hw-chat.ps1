[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$ModelBin = 'stories110M.bin',
  [string[]]$ExtraModelBins = @(),
  [ValidateSet('you_ai','llama2','chatml','alpaca','raw')]
  [string]$ChatFormat = 'you_ai',
  [string]$SystemPrompt = 'You are a concise offline sovereign assistant running on bare metal.',
  [ValidateRange(64, 4096)]
  [int]$CtxLen = 256,
  [ValidateRange(1, 256)]
  [int]$MaxTokens = 96,
  [ValidateRange(0.0, 2.0)]
  [double]$Temperature = 0.75,
  [ValidateRange(0.0, 1.0)]
  [double]$TopP = 0.95,
  [ValidateRange(0, 512)]
  [int]$TopK = 80,
  [ValidateRange(1.0, 2.0)]
  [double]$RepeatPenalty = 1.15,
  [switch]$EnableOo,
  [switch]$EnableOoConsult,
  [switch]$AutoSmoke,
  [switch]$AutoOoConsultSmoke,
  [switch]$SkipPrebuild,
  [string]$OutImagePath
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$buildScript = Join-Path $root 'build.ps1'
$generatedImage = Join-Path $root 'llm-baremetal-boot.img'
$smokeScript = Join-Path $root 'llmk-autorun-real-hw-model-chat-smoke.txt'
$ooConsultSmokeScript = Join-Path $root 'llmk-autorun-real-hw-oo-consult-smoke.txt'
$replCfgPath = Join-Path $root 'repl.cfg'

if (-not (Test-Path -LiteralPath $buildScript)) {
  throw "Missing build script: $buildScript"
}
if (-not (Test-Path -LiteralPath $smokeScript)) {
  throw "Missing model chat smoke script: $smokeScript"
}
if (-not (Test-Path -LiteralPath $ooConsultSmokeScript)) {
  throw "Missing OO consult smoke script: $ooConsultSmokeScript"
}

if (-not $PSBoundParameters.ContainsKey('OutImagePath')) {
  $OutImagePath = Join-Path $root 'llm-baremetal-boot-real-hw-chat.img'
}
$OutImagePath = [System.IO.Path]::GetFullPath($OutImagePath)

function Get-SystemPromptText([string]$value) {
  if (-not $value) { return '' }
  return (($value -replace '[\r\n]+', ' ') -replace '\s{2,}', ' ').Trim()
}

function New-GeneratedReplCfg {
  $prompt = Get-SystemPromptText $SystemPrompt
  $lines = [System.Collections.Generic.List[string]]::new()
  $enableOoEffective = $EnableOo -or $EnableOoConsult -or $AutoOoConsultSmoke
  $lines.Add("chat_format=$ChatFormat")
  if ($prompt) {
    $lines.Add("system_prompt=$prompt")
  }
  $lines.Add("ctx_len=$CtxLen")
  $lines.Add("max_tokens=$MaxTokens")
  $lines.Add(("temperature={0}" -f $Temperature.ToString([System.Globalization.CultureInfo]::InvariantCulture)))
  $lines.Add(("top_p={0}" -f $TopP.ToString([System.Globalization.CultureInfo]::InvariantCulture)))
  $lines.Add("top_k=$TopK")
  $lines.Add(("repeat_penalty={0}" -f $RepeatPenalty.ToString([System.Globalization.CultureInfo]::InvariantCulture)))
  $lines.Add('stats=1')
  $lines.Add('stop_you=1')
  if ($enableOoEffective) {
    $lines.Add('oo_enable=1')
  }
  if ($EnableOoConsult -or $AutoOoConsultSmoke) {
    $lines.Add('oo_llm_consult=1')
  }
  if ($AutoOoConsultSmoke) {
    $lines.Add('autorun_autostart=1')
    $lines.Add('autorun_shutdown_when_done=0')
    $lines.Add('autorun_file=llmk-autorun-real-hw-oo-consult-smoke.txt')
  } elseif ($AutoSmoke) {
    $lines.Add('autorun_autostart=1')
    $lines.Add('autorun_shutdown_when_done=0')
    $lines.Add('autorun_file=llmk-autorun-real-hw-model-chat-smoke.txt')
  }
  return ($lines -join "`n")
}

$existingReplCfg = $null
$hadExistingReplCfg = Test-Path -LiteralPath $replCfgPath
if ($hadExistingReplCfg) {
  $existingReplCfg = Get-Content -LiteralPath $replCfgPath -Raw
}

try {
  $generatedCfg = New-GeneratedReplCfg
  Set-Content -LiteralPath $replCfgPath -Value $generatedCfg -Encoding ASCII -NoNewline

  Write-Host "[ChatPrep] Building model-backed image" -ForegroundColor Cyan
  Write-Host "  Model: $ModelBin" -ForegroundColor Gray
  Write-Host "  Chat:  $ChatFormat (ctx=$CtxLen, max_tokens=$MaxTokens)" -ForegroundColor Gray
  if ($AutoOoConsultSmoke) {
    Write-Host "  OO:    enabled (LLM consult smoke)" -ForegroundColor Gray
    Write-Host "  Autorun: llmk-autorun-real-hw-oo-consult-smoke.txt" -ForegroundColor Gray
  } elseif ($EnableOoConsult) {
    Write-Host "  OO:    enabled (LLM consult interactive)" -ForegroundColor Gray
    Write-Host "  Autorun: disabled (interactive boot)" -ForegroundColor Gray
  } elseif ($AutoSmoke) {
    Write-Host "  Autorun: llmk-autorun-real-hw-model-chat-smoke.txt" -ForegroundColor Gray
  } elseif ($EnableOo) {
    Write-Host "  OO:    enabled (interactive)" -ForegroundColor Gray
    Write-Host "  Autorun: disabled (interactive boot)" -ForegroundColor Gray
  } else {
    Write-Host "  Autorun: disabled (interactive boot)" -ForegroundColor Gray
  }

  $buildArgs = @(
    '-NoProfile',
    '-ExecutionPolicy','Bypass',
    '-File', $buildScript,
    '-ModelBin', $ModelBin
  )
  if ($ExtraModelBins.Count -gt 0) {
    $buildArgs += '-ExtraModelBins'
    $buildArgs += $ExtraModelBins
  }
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
  Write-Host "[ChatPrep] Ready: $OutImagePath" -ForegroundColor Green
}
finally {
  if ($hadExistingReplCfg) {
    Set-Content -LiteralPath $replCfgPath -Value $existingReplCfg -Encoding ASCII -NoNewline
  } elseif (Test-Path -LiteralPath $replCfgPath) {
    Remove-Item -LiteralPath $replCfgPath -Force -ErrorAction SilentlyContinue
  }
}