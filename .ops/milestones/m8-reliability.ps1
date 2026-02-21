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
  [int]$M12AdjUnknown = 0,
  [switch]$M14RequireJournalParity,
  [switch]$M14SkipJournalExtract,
  [string]$M14ImagePath,
  [ValidateRange(1, 50)]
  [int]$M15BaselineWindow = 5,
  [ValidateRange(1, 100)]
  [int]$M15MinSamplesForDrift = 5,
  [ValidateRange(1, 200)]
  [int]$M15MaxShareDriftPct = 80,
  [ValidateRange(1, 100)]
  [int]$M15MaxUnknownSharePct = 30,
  [ValidateRange(5, 500)]
  [int]$M151WindowRuns = 20,
  [ValidateRange(7, 365)]
  [int]$M151WeeklyWindowDays = 28,
  [ValidateRange(1, 20)]
  [int]$M151TopReasonIds = 5,
  [switch]$M16ExtractMetrics,
  [switch]$M16SkipExtract,
  [ValidateRange(3, 100)]
  [int]$M16WindowRuns = 10,
  [ValidateRange(0.05, 2.0)]
  [double]$M16DriftThresholdPct = 0.20,
  [switch]$M16UpdateBaseline,
  [switch]$M16RejectOnDrift,
  [switch]$M17EnableCIReport,
  [ValidateRange(0.05, 2.0)]
  [double]$M17WarnThresholdPct = 0.15,
  [ValidateRange(0.05, 2.0)]
  [double]$M17FailThresholdPct = 0.30,
  [switch]$M17FailOnDrift,
  [switch]$M19EnableBenchmarkPack,
  [ValidateRange(0.01, 2.0)]
  [double]$M19RegressionThresholdPct = 0.15,
  [switch]$M19FailOnRegression,
  [string]$M19BaselineResultsPath = 'artifacts/m19/baseline/results.jsonl',
  [string]$M19CurrentResultsPath = 'artifacts/m19/current/results.jsonl'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $repoRoot

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

$preflightScript = Join-Path $repoRoot 'preflight-host.ps1'
$buildScript = Join-Path $repoRoot 'build.ps1'
$runScript = Join-Path $repoRoot 'run.ps1'
$m9Script = Join-Path $PSScriptRoot 'm9-guardrails.ps1'
$m10Script = Join-Path $PSScriptRoot 'm10-quality-guardrails.ps1'
$m11Script = Join-Path $PSScriptRoot 'm11-self-heal.ps1'
$m12Script = Join-Path $PSScriptRoot 'm12-policy-curriculum.ps1'
$m13Script = Join-Path $PSScriptRoot 'm13-explainability.ps1'
$m14Script = Join-Path $PSScriptRoot 'm14-explainability-coverage.ps1'
$m141ExtractScript = Join-Path $PSScriptRoot 'm14-extract-oojournal.ps1'
$m15Script = Join-Path $PSScriptRoot 'm15-reasonid-drift.ps1'
$m151Script = Join-Path $PSScriptRoot 'm15-slo-dashboard.ps1'
$m16ExtractScript = Join-Path $PSScriptRoot 'm16-extract-metrics.ps1'
$m16AggregateScript = Join-Path $PSScriptRoot 'm16-metrics-aggregate.ps1'
$m17ReportScript = Join-Path $PSScriptRoot 'm17-ci-metrics-report.ps1'
$m19PackScript = Join-Path $PSScriptRoot 'm19-benchmark-pack.ps1'
$m19CompareScript = Join-Path $PSScriptRoot 'm19-benchmark-compare.ps1'
$m191AutorunScript = Join-Path $PSScriptRoot 'm19.1-benchmark-autorun.ps1'
$m191ExtractScript = Join-Path $PSScriptRoot 'm19.1-extract-bench.ps1'
$cfgPath = Join-Path $repoRoot 'repl.cfg'
$autorunPath = Join-Path $repoRoot 'llmk-autorun.txt'
$autorunBenchPath = Join-Path $repoRoot 'llmk-autorun-bench.txt'
$tmpDir = Join-Path $repoRoot 'artifacts\m8'
$logPath = Join-Path $tmpDir 'm8-qemu-serial.log'
$errLogPath = Join-Path $tmpDir 'm8-qemu-serial.err.log'
$m14JournalPath = Join-Path $repoRoot 'artifacts/m14/OOJOUR.LOG'

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

$sourceText = Get-Content -LiteralPath (Join-Path $repoRoot 'llama2_efi_final.c') -Raw
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
    if ($M19EnableBenchmarkPack) {
      # M19 can run in static mode (no QEMU required)
      if (-not (Test-Path -LiteralPath $m19PackScript)) {
        Write-Warn "M19 benchmark pack script not found: $m19PackScript"
      } else {
        Write-Step 'Running M19 benchmark pack generation (static mode)'
        & $m19PackScript -OutputDir 'artifacts/m19/current' -ResultsPath $M19CurrentResultsPath
        if ($LASTEXITCODE -ne 0) {
          throw "M19 benchmark pack generation failed with exit code $LASTEXITCODE"
        }
        Write-Ok 'M19 benchmark pack generated (static mode)'
      }

      if (-not (Test-Path -LiteralPath $m19CompareScript)) {
        Write-Warn "M19 benchmark compare script not found: $m19CompareScript"
      } else {
        $baselineFull = Join-Path $repoRoot $M19BaselineResultsPath
        $currentFull = Join-Path $repoRoot $M19CurrentResultsPath

        if ((Test-Path -LiteralPath $baselineFull) -and (Test-Path -LiteralPath $currentFull)) {
          Write-Step 'Running M19 benchmark comparison (static mode)'
          $m19Args = @{
            BaselineResultsPath = $M19BaselineResultsPath
            CurrentResultsPath = $M19CurrentResultsPath
            OutputPath = 'artifacts/m19/compare/benchmark-compare.md'
            RegressionThresholdPct = $M19RegressionThresholdPct
          }
          if ($M19FailOnRegression) {
            $m19Args.FailOnRegression = $true
          }

          & $m19CompareScript @m19Args
          if ($LASTEXITCODE -ne 0) {
            if ($M19FailOnRegression) {
              throw "M19 benchmark compare failed with exit code $LASTEXITCODE"
            } else {
              Write-Warn 'M19 benchmark compare detected regressions (non-blocking)'
            }
          } else {
            Write-Ok 'M19 benchmark comparison complete (static mode)'
          }
        } else {
          Write-Warn "M19 compare skipped: missing baseline or current results JSONL"
          Write-Warn "Expected: $M19BaselineResultsPath and $M19CurrentResultsPath"
        }
      }
    }

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
  if ($M19EnableBenchmarkPack) {
    if (-not (Test-Path -LiteralPath $m19PackScript)) {
      Write-Warn "M19 benchmark pack script not found: $m19PackScript"
    } else {
      Write-Step 'Preparing M19.1 benchmark input pack (for autorun)'
      & $m19PackScript -OutputDir 'artifacts/m19/current' -Quiet
      if ($LASTEXITCODE -ne 0) {
        throw "M19 benchmark pack generation (pre-run) failed with exit code $LASTEXITCODE"
      }
    }

    if (-not (Test-Path -LiteralPath $m191AutorunScript)) {
      Write-Warn "M19.1 autorun generator script not found: $m191AutorunScript"
    } else {
      Write-Step 'Generating llmk-autorun-bench.txt (M19.1)'
      & $m191AutorunScript -InputJsonl 'artifacts/m19/current/benchmark-input.jsonl' -OutputPath 'llmk-autorun-bench.txt'
      if ($LASTEXITCODE -ne 0) {
        throw "M19.1 autorun generation failed with exit code $LASTEXITCODE"
      }
      Write-Ok 'M19.1 benchmark autorun ready'
    }
  }

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
    '/oo_jour'
  )

  if ($M19EnableBenchmarkPack -and (Test-Path -LiteralPath $autorunBenchPath)) {
    # Chain benchmark capture from the main reliability autorun.
    # Use --shutdown to terminate the VM cleanly when benchmarks finish.
    $autorunLines += '/autorun --shutdown llmk-autorun-bench.txt'
  } else {
    $autorunLines += 'exit'
  }

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
    '-ImagePath', 'llm-baremetal-boot.img',
    '-Accel', $Accel,
    '-MemMB', "$MemMB"
  )

  $proc = Start-Process -FilePath 'pwsh' -ArgumentList $argList -WorkingDirectory $repoRoot `
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

  if ($M19EnableBenchmarkPack) {
    if (-not (Test-Path -LiteralPath $m191ExtractScript)) {
      Write-Warn "M19.1 extract script not found: $m191ExtractScript"
    } else {
      Write-Step 'Extracting M19.1 benchmark results from image'
      $currentFull = Join-Path $repoRoot $M19CurrentResultsPath
      if (Test-Path -LiteralPath $currentFull) {
        Remove-Item -LiteralPath $currentFull -Force -ErrorAction SilentlyContinue
      }
      & $m191ExtractScript -ImagePath 'llm-baremetal-boot.img' -PartitionOffset '1M' -BenchFilename 'LLMK_BEN.JNL' -OutputPath $M19CurrentResultsPath
      if ($LASTEXITCODE -ne 0) {
        throw "M19.1 benchmark extraction failed with exit code $LASTEXITCODE"
      }
      Write-Ok 'M19.1 benchmark results extracted'

      # Re-run M19 compare immediately so the report matches the freshly extracted results.
      if (Test-Path -LiteralPath $m19CompareScript) {
        $baselineFull = Join-Path $repoRoot $M19BaselineResultsPath
        if ((Test-Path -LiteralPath $baselineFull) -and (Test-Path -LiteralPath $currentFull)) {
          Write-Step 'Running M19 benchmark comparison (post-extract)'
          $m19Args = @{
            BaselineResultsPath = $M19BaselineResultsPath
            CurrentResultsPath = $M19CurrentResultsPath
            OutputPath = 'artifacts/m19/compare/benchmark-compare.md'
            RegressionThresholdPct = $M19RegressionThresholdPct
          }
          if ($M19FailOnRegression) {
            $m19Args.FailOnRegression = $true
          }
          & $m19CompareScript @m19Args
          if ($LASTEXITCODE -ne 0) {
            if ($M19FailOnRegression) {
              throw "M19 benchmark compare failed with exit code $LASTEXITCODE"
            }
            Write-Warn 'M19 benchmark compare reported regressions (non-blocking)'
          } else {
            Write-Ok 'M19 benchmark comparison complete (post-extract)'
          }
        }
      }
    }
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
    -QuarantineStatePath (Join-Path $repoRoot 'artifacts/m10/quarantine-state.json') `
    -M9HistoryPath (Join-Path $repoRoot 'artifacts/m9/history.jsonl') `
    -M10HistoryPath (Join-Path $repoRoot 'artifacts/m10/history.jsonl') `
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
    -M9HistoryPath (Join-Path $repoRoot 'artifacts/m9/history.jsonl') `
    -M10HistoryPath (Join-Path $repoRoot 'artifacts/m10/history.jsonl') `
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

  if (-not (Test-Path -LiteralPath $m13Script)) {
    throw "M13 script not found: $m13Script"
  }

  Write-Step 'Running M13 explainability pack'
  & $m13Script -LogPath $logPath `
    -M10StatePath (Join-Path $repoRoot 'artifacts/m10/quarantine-state.json') `
    -M11StatePath (Join-Path $repoRoot 'artifacts/m11/release-state.json') `
    -M12StatePath (Join-Path $repoRoot 'artifacts/m12/curriculum-state.json')
  if ($LASTEXITCODE -ne 0) {
    throw "M13 explainability failed with exit code $LASTEXITCODE"
  }

  if (-not (Test-Path -LiteralPath $m14Script)) {
    throw "M14 script not found: $m14Script"
  }

  if (-not $M14SkipJournalExtract) {
    if (-not (Test-Path -LiteralPath $m141ExtractScript)) {
      throw "M14.1 extract script not found: $m141ExtractScript"
    }

    Write-Step 'Running M14.1 OOJOUR extraction'
    $m14Image = if ($M14ImagePath) { $M14ImagePath } else { (Join-Path $repoRoot 'llm-baremetal-boot.img') }
    if ($M14RequireJournalParity) {
      & $m141ExtractScript -OutputPath $m14JournalPath -ImagePath $m14Image -FailIfMissing
    } else {
      & $m141ExtractScript -OutputPath $m14JournalPath -ImagePath $m14Image
    }
    if ($LASTEXITCODE -ne 0) {
      throw "M14.1 OOJOUR extraction failed with exit code $LASTEXITCODE"
    }
  } else {
    Write-Warn 'M14.1 journal extraction skipped by flag'
  }

  Write-Step 'Running M14 explainability coverage check'
  if ($M14RequireJournalParity) {
    & $m14Script -LogPath $logPath -JournalLogPath $m14JournalPath -FailOnCoverageGap -RequireJournalParity
  } else {
    & $m14Script -LogPath $logPath -JournalLogPath $m14JournalPath -FailOnCoverageGap
  }
  if ($LASTEXITCODE -ne 0) {
    throw "M14 coverage failed with exit code $LASTEXITCODE"
  }

  if (-not (Test-Path -LiteralPath $m15Script)) {
    throw "M15 script not found: $m15Script"
  }

  Write-Step 'Running M15 reason_id drift guardrails'
  & $m15Script -LogPath $logPath `
    -HistoryPath (Join-Path $repoRoot 'artifacts/m13/history.jsonl') `
    -BaselineWindow $M15BaselineWindow `
    -MinSamplesForDrift $M15MinSamplesForDrift `
    -MaxShareDriftPct $M15MaxShareDriftPct `
    -MaxUnknownSharePct $M15MaxUnknownSharePct `
    -FailOnDrift
  if ($LASTEXITCODE -ne 0) {
    throw "M15 drift guardrails failed with exit code $LASTEXITCODE"
  }

  if (-not (Test-Path -LiteralPath $m151Script)) {
    throw "M15.1 script not found: $m151Script"
  }

  Write-Step 'Running M15.1 reason_id SLO dashboard export'
  & $m151Script -DriftHistoryPath (Join-Path $repoRoot 'artifacts/m15/history.jsonl') `
    -ExplainabilityHistoryPath (Join-Path $repoRoot 'artifacts/m13/history.jsonl') `
    -WindowRuns $M151WindowRuns `
    -WeeklyWindowDays $M151WeeklyWindowDays `
    -TopReasonIds $M151TopReasonIds
  if ($LASTEXITCODE -ne 0) {
    throw "M15.1 dashboard export failed with exit code $LASTEXITCODE"
  }

  # M16.1/M16.2: Runtime metrics extraction + aggregation
  if ($M16ExtractMetrics -and -not $M16SkipExtract) {
    if (-not (Test-Path -LiteralPath $m16ExtractScript)) {
      Write-Warn "M16 extract script not found: $m16ExtractScript"
    } else {
      Write-Step 'Running M16 runtime metrics extraction from image'
      $imagePath = if ($M14ImagePath) { $M14ImagePath } else { 'llm-baremetal-boot.img' }
      
      & $m16ExtractScript -ImagePath $imagePath -OutputDir 'artifacts/m16/raw'
      if ($LASTEXITCODE -ne 0) {
        Write-Warn "M16 metrics extraction failed (metrics may not exist in image yet)"
      } else {
        Write-Ok 'M16 metrics extracted successfully'
      }
    }
  }

  if ($M16ExtractMetrics) {
    if (-not (Test-Path -LiteralPath $m16AggregateScript)) {
      Write-Warn "M16.2 aggregate script not found: $m16AggregateScript"
    } else {
      Write-Step 'Running M16.2 runtime metrics aggregation + drift detection'
      $m16Args = @{
        WindowRuns = $M16WindowRuns
        DriftThresholdPct = $M16DriftThresholdPct
      }
      if ($M16UpdateBaseline) {
        $m16Args.UpdateBaseline = $true
      }
      if ($M16RejectOnDrift) {
        $m16Args.RejectOnDrift = $true
      }

      & $m16AggregateScript @m16Args
      if ($LASTEXITCODE -ne 0) {
        if ($M16RejectOnDrift) {
          throw "M16.2 metrics aggregation failed with exit code $LASTEXITCODE"
        } else {
          Write-Warn "M16.2 metrics aggregation failed (non-blocking)"
        }
      } else {
        Write-Ok 'M16.2 metrics aggregation complete'
      }
    }
  }

  # M17: CI metrics reporting
  if ($M17EnableCIReport) {
    # Check if we have extracted metrics to report on
    $latestMetrics = Join-Path $repoRoot 'artifacts/m16/raw/metrics_ci_latest.json'
    if (-not (Test-Path -LiteralPath $latestMetrics)) {
      # Try to find most recent metrics file
      $rawMetricsDir = Join-Path $repoRoot 'artifacts/m16/raw'
      if (Test-Path -LiteralPath $rawMetricsDir) {
        $latestFile = Get-ChildItem -LiteralPath $rawMetricsDir -Filter '*.json' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($latestFile) {
          $latestMetrics = $latestFile.FullName
        }
      }
    }

    if (Test-Path -LiteralPath $latestMetrics) {
      if (-not (Test-Path -LiteralPath $m17ReportScript)) {
        Write-Warn "M17 CI report script not found: $m17ReportScript"
      } else {
        Write-Step 'Running M17 CI metrics report generation'
        $m17Args = @{
          MetricsFile = $latestMetrics
          OutputPath = 'artifacts/m17/ci-metrics-report.txt'
          WarnThresholdPct = $M17WarnThresholdPct
          FailThresholdPct = $M17FailThresholdPct
        }
        if ($M17FailOnDrift) {
          $m17Args.FailOnDrift = $true
        }

        & $m17ReportScript @m17Args
        if ($LASTEXITCODE -ne 0) {
          if ($M17FailOnDrift) {
            throw "M17 CI metrics report failed with exit code $LASTEXITCODE"
          } else {
            Write-Warn "M17 CI metrics report detected drift (non-blocking)"
          }
        } else {
          Write-Ok 'M17 CI metrics report complete'
        }
      }
    } else {
      Write-Warn "M17 CI report requested but no metrics file found"
      Write-Warn "Ensure M16ExtractMetrics is enabled to collect runtime metrics"
    }
  }

  # M19: Reproducible benchmark pack + commit-to-commit matrix
  if ($M19EnableBenchmarkPack) {
    if (-not (Test-Path -LiteralPath $m19PackScript)) {
      Write-Warn "M19 benchmark pack script not found: $m19PackScript"
    } else {
      Write-Step 'Running M19 benchmark pack generation'
      & $m19PackScript -OutputDir 'artifacts/m19/current' -ResultsPath $M19CurrentResultsPath
      if ($LASTEXITCODE -ne 0) {
        throw "M19 benchmark pack generation failed with exit code $LASTEXITCODE"
      }
      Write-Ok 'M19 benchmark pack generated'
    }

    if (-not (Test-Path -LiteralPath $m19CompareScript)) {
      Write-Warn "M19 benchmark compare script not found: $m19CompareScript"
    } else {
      $baselineFull = Join-Path $repoRoot $M19BaselineResultsPath
      $currentFull = Join-Path $repoRoot $M19CurrentResultsPath

      if ((Test-Path -LiteralPath $baselineFull) -and (Test-Path -LiteralPath $currentFull)) {
        Write-Step 'Running M19 benchmark comparison'
        $m19Args = @{
          BaselineResultsPath = $M19BaselineResultsPath
          CurrentResultsPath = $M19CurrentResultsPath
          OutputPath = 'artifacts/m19/compare/benchmark-compare.md'
          RegressionThresholdPct = $M19RegressionThresholdPct
        }
        if ($M19FailOnRegression) {
          $m19Args.FailOnRegression = $true
        }

        & $m19CompareScript @m19Args
        if ($LASTEXITCODE -ne 0) {
          if ($M19FailOnRegression) {
            throw "M19 benchmark compare failed with exit code $LASTEXITCODE"
          } else {
            Write-Warn 'M19 benchmark compare detected regressions (non-blocking)'
          }
        } else {
          Write-Ok 'M19 benchmark comparison complete'
        }
      } else {
        Write-Warn "M19 compare skipped: missing baseline or current results JSONL"
        Write-Warn "Expected: $M19BaselineResultsPath and $M19CurrentResultsPath"
      }
    }
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
