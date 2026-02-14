param(
  [switch]$SkipPreflight,
  [switch]$SkipBuild,
  [switch]$RunQemu,
  [ValidateRange(15, 600)]
  [int]$TimeoutSec = 90,
  [ValidateSet('auto','whpx','tcg','none')]
  [string]$Accel = 'tcg',
  [ValidateRange(1024, 8192)]
  [int]$MemMB = 4096,
  [ValidateRange(0, 120000)]
  [int]$MaxModelSelectMs = 2000,
  [ValidateRange(0, 120000)]
  [int]$MaxModelPrepareMs = 12000,
  [ValidateRange(0, 100)]
  [int]$MaxHarmfulRatioPct = 40,
  [ValidateRange(1, 100)]
  [int]$MaxConsecutiveFailures = 2,
  [ValidateRange(1, 1000)]
  [int]$MinAutoApplySamples = 2,
  [ValidateRange(1, 100)]
  [int]$M11StableStreakNeeded = 3,
  [ValidateRange(1, 20)]
  [int]$M11CanaryBoots = 2,
  [ValidateRange(1, 1000)]
  [int]$M11MinSamplesForRelease = 2,
  [ValidateRange(1, 50)]
  [int]$M11M9StableWindow = 3,
  [ValidateRange(1, 50)]
  [int]$M11M10StableWindow = 3,
  [ValidateRange(0, 100)]
  [int]$M11CanaryConfThreshold = 20,
  [ValidateRange(0, 100)]
  [int]$M12EarlyThreshold = 35,
  [ValidateRange(0, 100)]
  [int]$M12WarmThreshold = 25,
  [ValidateRange(0, 100)]
  [int]$M12SteadyThreshold = 15,
  [ValidateRange(-100, 100)]
  [int]$M12AdjLatencyOptimization = -5,
  [ValidateRange(-100, 100)]
  [int]$M12AdjContextExpansion = 10,
  [ValidateRange(-100, 100)]
  [int]$M12AdjMixed = 3,
  [ValidateRange(-100, 100)]
  [int]$M12AdjUnknown = 0
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Write-Step([string]$msg) {
  Write-Host "[M8] $msg" -ForegroundColor Cyan
}

function Write-Ok([string]$msg) {
  Write-Host "[M8][OK] $msg" -ForegroundColor Green
}

function Write-Warn([string]$msg) {
  Write-Host "[M8][WARN] $msg" -ForegroundColor Yellow
}

function Assert-Pattern([string]$text, [string]$pattern, [string]$label) {
  if ($text -imatch $pattern) {
    Write-Ok "$label"
    return $true
  }
  Write-Warn "$label (missing pattern: $pattern)"
  return $false
}

$preflightScript = Join-Path $PSScriptRoot 'preflight-host.ps1'
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
$runScript = Join-Path $PSScriptRoot 'run.ps1'
$m9Script = Join-Path $PSScriptRoot 'm9-guardrails.ps1'
$m10Script = Join-Path $PSScriptRoot 'm10-quality-guardrails.ps1'
$m11Script = Join-Path $PSScriptRoot 'm11-self-heal.ps1'
$m12Script = Join-Path $PSScriptRoot 'm12-policy-curriculum.ps1'
$cfgPath = Join-Path $PSScriptRoot 'repl.cfg'
$autorunPath = Join-Path $PSScriptRoot 'llmk-autorun.txt'
$tmpDir = Join-Path $PSScriptRoot 'artifacts\m8'
$logPath = Join-Path $tmpDir 'm8-qemu-serial.log'
$errLogPath = Join-Path $tmpDir 'm8-qemu-serial.err.log'

New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

if (-not $SkipPreflight) {
  Write-Step 'Running preflight-host.ps1'
  & $preflightScript
  if ($LASTEXITCODE -ne 0) {
    throw "Preflight failed with exit code $LASTEXITCODE"
  }
  Write-Ok 'Preflight passed'
}

if (-not $SkipBuild) {
  if (-not $RunQemu) {
    Write-Step 'Running build.ps1 -NoModel'
    & $buildScript -NoModel
    if ($LASTEXITCODE -ne 0) {
      throw "Build failed with exit code $LASTEXITCODE"
    }
    Write-Ok 'Build passed'
  }
}

$sourceText = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'llama2_efi_final.c') -Raw
Write-Step 'Static marker checks for A1/A3/B1/B2/B3'

$staticChecks = @(
  @{ Label = 'A1 startup marker exists'; Pattern = '\[obs\]\[startup\]\s+model_select_ms=' },
  @{ Label = 'A3 models summary exists'; Pattern = 'summary:\s+total=' },
  @{ Label = 'B1 confidence marker exists'; Pattern = 'OK:\s+OO confidence:' },
  @{ Label = 'B2 feedback marker exists'; Pattern = 'OK:\s+OO feedback:' },
  @{ Label = 'B2 outcome log append exists'; Pattern = 'OOOUTCOME\.LOG' },
  @{ Label = 'B3 plan marker exists'; Pattern = 'OK:\s+OO plan:' },
  @{ Label = 'B3 hard-stop event exists'; Pattern = 'plan_hard_stop\s+reason=' }
)

$staticOk = $true
foreach ($c in $staticChecks) {
  if (-not (Assert-Pattern -text $sourceText -pattern $c.Pattern -label $c.Label)) {
    $staticOk = $false
  }
}

if (-not $RunQemu) {
  if ($staticOk) {
    Write-Ok 'M8 static reliability pass complete'
    Write-Host "[M8] Tip: rerun with -RunQemu for runtime autorun validation." -ForegroundColor Gray
    exit 0
  }
  throw 'M8 static reliability checks failed'
}

Write-Step 'Preparing runtime autorun scenario (A3 + B1/B2/B3)'

$cfgBackup = Join-Path $tmpDir 'repl.cfg.backup'
$autorunBackup = Join-Path $tmpDir 'llmk-autorun.txt.backup'
Copy-Item -LiteralPath $cfgPath -Destination $cfgBackup -Force
if (Test-Path $autorunPath) {
  Copy-Item -LiteralPath $autorunPath -Destination $autorunBackup -Force
}

try {
  $cfgLines = @(
    'boot_verbose=1',
    'overlay=1',
    'overlay_digits=1',
    'overlay_time_mode=1',
    'autorun_autostart=1',
    'autorun_shutdown_when_done=0',
    'autorun_file=llmk-autorun.txt',
    'model=stories15M.q8_0.gguf',
    'oo_enable=1',
    'oo_llm_consult=1',
    'oo_multi_actions=1',
    'oo_auto_apply=1',
    'oo_conf_gate=1',
    'oo_conf_threshold=0',
    'oo_plan_enable=1',
    'oo_plan_max_actions=2'
  )
  Set-Content -LiteralPath $cfgPath -Value ($cfgLines -join "`r`n") -Encoding ASCII

  $autorunLines = @(
    '/models',
    '/oo_consult_mock reduce ctx and reduce seq',
    '/oo_consult_mock increase context',
    '/oo_jour',
    'exit'
  )
  Set-Content -LiteralPath $autorunPath -Value ($autorunLines -join "`r`n") -Encoding ASCII

  if (-not $SkipBuild) {
    Write-Step 'Running build.ps1 for runtime scenario (embedded autorun/cfg + small model)'
    & $buildScript -ModelBin 'stories15M.q8_0.gguf'
    if ($LASTEXITCODE -ne 0) {
      throw "Runtime build failed with exit code $LASTEXITCODE"
    }
    Write-Ok 'Runtime build passed'
  } else {
    Write-Warn 'SkipBuild used: runtime uses existing image contents (autorun/cfg may not match current checks)'
  }

  if (Test-Path $logPath) {
    Remove-Item -LiteralPath $logPath -Force
  }
  if (Test-Path $errLogPath) {
    Remove-Item -LiteralPath $errLogPath -Force
  }

  Write-Step "Running QEMU scenario (timeout=${TimeoutSec}s, accel=$Accel, mem=${MemMB}MB)"
  $argList = @(
    '-NoProfile',
    '-File',
    $runScript,
    '-PassThroughExitCode',
    '-NoNormalizeExitCode',
    '-Accel', $Accel,
    '-MemMB', "$MemMB"
  )

  $proc = Start-Process -FilePath 'pwsh' -ArgumentList $argList -WorkingDirectory $PSScriptRoot `
    -RedirectStandardOutput $logPath -RedirectStandardError $errLogPath -PassThru

  $finished = $proc.WaitForExit($TimeoutSec * 1000)
  if (-not $finished) {
    Write-Warn "QEMU timeout reached (${TimeoutSec}s); stopping process"
    try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
    Get-Process -Name 'qemu-system-x86_64' -ErrorAction SilentlyContinue | ForEach-Object {
      try { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
  }

  if (-not (Test-Path $logPath)) {
    throw 'Runtime log was not produced'
  }

  if (Test-Path $errLogPath) {
    Add-Content -LiteralPath $logPath -Value "`r`n----- STDERR -----`r`n"
    Get-Content -LiteralPath $errLogPath | Add-Content -LiteralPath $logPath
  }

  $runtimeText = Get-Content -LiteralPath $logPath -Raw
  Write-Step 'Runtime marker checks'

  $runtimeChecks = @(
    @{ Label = 'A3 models summary emitted'; Pattern = 'summary:\s+total=' }
  )

  $noModelBoot = ($runtimeText -imatch 'OK:\s+REPL ready \(no model\)')
  if ($noModelBoot) {
    Write-Warn 'Runtime booted in no-model mode; skipping B1/B2/B3 runtime markers for this pass'
    $runtimeChecks += @{ Label = 'A3 no-model diagnostic emitted'; Pattern = 'Tip:\s+in no-model REPL use /models and /model_info <path>' }
  } else {
    $runtimeChecks += @(
      @{ Label = 'A1 startup marker emitted'; Pattern = '\[obs\]\[startup\]\s+model_select_ms=' },
      @{ Label = 'B1 confidence emitted'; Pattern = 'OK:\s+OO confidence:' },
      @{ Label = 'B2 feedback emitted'; Pattern = 'OK:\s+OO feedback:' },
      @{ Label = 'B3 plan emitted'; Pattern = 'OK:\s+OO plan:' }
    )
  }

  $runtimeOk = $true
  foreach ($c in $runtimeChecks) {
    if (-not (Assert-Pattern -text $runtimeText -pattern $c.Pattern -label $c.Label)) {
      $runtimeOk = $false
    }
  }

  if (-not $staticOk -or -not $runtimeOk) {
    throw "M8 reliability checks failed (see log: $logPath)"
  }

  if (-not (Test-Path -LiteralPath $m9Script)) {
    throw "M9 script not found: $m9Script"
  }

  Write-Step 'Running M9 regression guardrails'
  & $m9Script -LogPath $logPath -MaxModelSelectMs $MaxModelSelectMs -MaxModelPrepareMs $MaxModelPrepareMs -RequireOoMarkers
  if ($LASTEXITCODE -ne 0) {
    throw "M9 guardrails failed with exit code $LASTEXITCODE"
  }

  if (-not (Test-Path -LiteralPath $m10Script)) {
    throw "M10 script not found: $m10Script"
  }

  Write-Step 'Running M10 policy quality guardrails'
  & $m10Script -LogPath $logPath `
    -MaxHarmfulRatioPct $MaxHarmfulRatioPct `
    -MaxConsecutiveFailures $MaxConsecutiveFailures `
    -MinAutoApplySamples $MinAutoApplySamples `
    -RuntimeMemMB $MemMB `
    -ApplyQuarantine `
    -ConfigPath $cfgPath
  if ($LASTEXITCODE -ne 0) {
    throw "M10 guardrails failed with exit code $LASTEXITCODE"
  }

  if (-not (Test-Path -LiteralPath $m11Script)) {
    throw "M11 script not found: $m11Script"
  }

  Write-Step 'Running M11 self-healing quarantine release'
  & $m11Script -LogPath $logPath `
    -QuarantineStatePath (Join-Path $PSScriptRoot 'artifacts/m10/quarantine-state.json') `
    -M9HistoryPath (Join-Path $PSScriptRoot 'artifacts/m9/history.jsonl') `
    -M10HistoryPath (Join-Path $PSScriptRoot 'artifacts/m10/history.jsonl') `
    -ConfigPath $cfgPath `
    -StableStreakNeeded $M11StableStreakNeeded `
    -CanaryBoots $M11CanaryBoots `
    -MinSamplesForRelease $M11MinSamplesForRelease `
    -M9StableWindow $M11M9StableWindow `
    -M10StableWindow $M11M10StableWindow `
    -CanaryConfThreshold $M11CanaryConfThreshold `
    -ApplyRelease
  if ($LASTEXITCODE -ne 0) {
    throw "M11 self-heal failed with exit code $LASTEXITCODE"
  }

  if (-not (Test-Path -LiteralPath $m12Script)) {
    throw "M12 script not found: $m12Script"
  }

  Write-Step 'Running M12 policy curriculum'
  & $m12Script -LogPath $logPath `
    -M9HistoryPath (Join-Path $PSScriptRoot 'artifacts/m9/history.jsonl') `
    -M10HistoryPath (Join-Path $PSScriptRoot 'artifacts/m10/history.jsonl') `
    -ConfigPath $cfgPath `
    -EarlyThreshold $M12EarlyThreshold `
    -WarmThreshold $M12WarmThreshold `
    -SteadyThreshold $M12SteadyThreshold `
    -AdjLatencyOptimization $M12AdjLatencyOptimization `
    -AdjContextExpansion $M12AdjContextExpansion `
    -AdjMixed $M12AdjMixed `
    -AdjUnknown $M12AdjUnknown `
    -ApplyConfig
  if ($LASTEXITCODE -ne 0) {
    throw "M12 curriculum failed with exit code $LASTEXITCODE"
  }

  Write-Ok "M8 runtime reliability pass complete (log: $logPath)"
}
finally {
  if (Test-Path $cfgBackup) {
    Copy-Item -LiteralPath $cfgBackup -Destination $cfgPath -Force
  }
  if (Test-Path $autorunBackup) {
    Copy-Item -LiteralPath $autorunBackup -Destination $autorunPath -Force
  }
}
