[CmdletBinding(PositionalBinding = $false)]
param(
  # Minimal mode set: we only guarantee oo_smoke for no-model iteration.
  [ValidateSet('oo_smoke','oo_consult_smoke','oo_reboot_smoke','oo_outcome_smoke','smoke')]
  [string]$Mode = 'oo_smoke',

  [ValidateSet('auto','whpx','tcg','none')]
  [string]$Accel = 'tcg',

  [ValidateRange(512, 8192)]
  [int]$MemMB = 1024,

  [ValidateRange(30, 86400)]
  [int]$TimeoutSec = 480,

  # If set, skips rebuilding the image.
  [switch]$SkipBuild,

  # If set, skips oo-guard prebuild check during image rebuild.
  # Useful during incremental development; leave unset for CI-like safety.
  [switch]$SkipPrebuild,

  # Compatibility flag: ignored (kept so bench-matrix wrappers can pass it).
  [switch]$SkipInspect,

  # Compatibility flags: accepted but not implemented in this minimal harness.
  [switch]$BootstrapToolchains,
  [switch]$SkipModelAssertions,

  # Reserved for future compatibility.
  [string]$ModelBin = '',
  [string]$BootModel = '',
  [string]$ExtraModel = '',
  [string]$ExpectedModel = '',
  [ValidateRange(1, 4096)]
  [int]$GenMaxTokens = 64,
  [ValidateRange(0.0, 2.0)]
  [double]$GenTemp = 0.2
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$build = Join-Path $root 'build.ps1'
$run = Join-Path $root 'run.ps1'

if (-not (Test-Path $build)) { throw "Missing: $build" }
if (-not (Test-Path $run)) { throw "Missing: $run" }

# Normalize mode alias
if ($Mode -eq 'smoke') { $Mode = 'oo_smoke' }

function Backup-File([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return $null }
  $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
  Copy-Item -LiteralPath $path -Destination $tmp -Force
  return $tmp
}

function Restore-File([string]$path, [string]$backup) {
  if ($backup -and (Test-Path -LiteralPath $backup)) {
    Copy-Item -LiteralPath $backup -Destination $path -Force
    Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
  } else {
    if (Test-Path -LiteralPath $path) {
      Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
  }
}

$replCfg = Join-Path $root 'repl.cfg'
$autorunTxt = Join-Path $root 'llmk-autorun.txt'

$replBak = Backup-File $replCfg
$autorunBak = Backup-File $autorunTxt

# Serial log path: bench-matrix expects this pattern.
$serialPath = Join-Path ([System.IO.Path]::GetTempPath()) ("llm-baremetal-serial-autorun-{0:yyyyMMdd-HHmmss}-{1}.txt" -f (Get-Date), $Mode)
$serialErrPath = "$serialPath.err"

try {
  $cfgLines = [System.Collections.Generic.List[string]]::new()
  $cfgLines.Add('autorun_autostart=1')
  $cfgLines.Add('autorun_shutdown_when_done=1')
  $cfgLines.Add('autorun_file=llmk-autorun.txt')
  $cfgLines.Add('oo_enable=1')

  if ($Mode -eq 'oo_consult_smoke') {
    $cfgLines.Add('oo_llm_consult=1')
  }
  if ($Mode -eq 'oo_outcome_smoke') {
    $cfgLines.Add('oo_llm_consult=1')
    $cfgLines.Add('oo_auto_apply=1')
    $cfgLines.Add('oo_conf_gate=0')
    $cfgLines.Add('ctx_len=512')
    $cfgLines.Add('seq_len=1024')
  }

  $cfg = $cfgLines -join "`n"
  Set-Content -LiteralPath $replCfg -Value $cfg -Encoding ASCII -NoNewline

  $scriptName = switch ($Mode) {
    'oo_smoke' { 'llmk-autorun-oo-smoke.txt' }
    'oo_consult_smoke' { 'llmk-autorun-oo-consult-smoke.txt' }
    'oo_reboot_smoke' { 'llmk-autorun-oo-reboot-smoke.txt' }
    'oo_outcome_smoke' { 'llmk-autorun-oo-outcome-smoke.txt' }
    default { throw "Unsupported -Mode '$Mode' in minimal harness. Supported: oo_smoke, oo_consult_smoke, oo_reboot_smoke, oo_outcome_smoke" }
  }

  $scriptPath = Join-Path $root $scriptName
  if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Missing autorun script: $scriptPath"
  }
  Copy-Item -LiteralPath $scriptPath -Destination $autorunTxt -Force

  if (-not $SkipBuild) {
    $buildArgs = @(
      '-NoProfile',
      '-ExecutionPolicy','Bypass',
      '-File', $build
    )
    if ($Mode -eq 'oo_consult_smoke') {
      $resolvedModel = if ($ModelBin) { $ModelBin } elseif ($BootModel) { $BootModel } else { 'stories15M.q8_0.gguf' }
      Write-Host "[Autorun] Build (Model=$resolvedModel)" -ForegroundColor Cyan
      $buildArgs += @('-ModelBin', $resolvedModel)
    } else {
      Write-Host "[Autorun] Build (NoModel)" -ForegroundColor Cyan
      $buildArgs += '-NoModel'
    }
    if ($SkipPrebuild) { $buildArgs += '-SkipPrebuild' }
    & powershell @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed with exit=$LASTEXITCODE" }
  }

  Write-Host "[Autorun] Run QEMU (accel=$Accel mem=${MemMB}MB)" -ForegroundColor Cyan
  Write-Host "[Autorun] Serial log: $serialPath" -ForegroundColor DarkGray

  $psArgs = @(
    '-NoProfile',
    '-ExecutionPolicy','Bypass',
    '-File', $run,
    '-Accel', $Accel,
    '-MemMB', $MemMB
  )

  if (Test-Path -LiteralPath $serialPath) { Remove-Item -LiteralPath $serialPath -Force -ErrorAction SilentlyContinue }
  if (Test-Path -LiteralPath $serialErrPath) { Remove-Item -LiteralPath $serialErrPath -Force -ErrorAction SilentlyContinue }

  $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList $psArgs -WorkingDirectory $root -NoNewWindow -PassThru -RedirectStandardOutput $serialPath -RedirectStandardError $serialErrPath
  $ok = $proc.WaitForExit($TimeoutSec * 1000)
  if (-not $ok) {
    try {
      # Best-effort kill tree: stop children first.
      $kids = Get-CimInstance Win32_Process -Filter ("ParentProcessId={0}" -f $proc.Id) -ErrorAction SilentlyContinue
      foreach ($k in $kids) {
        try { Stop-Process -Id $k.ProcessId -Force -ErrorAction SilentlyContinue } catch {}
      }
      Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    } catch {}
    throw "Timeout after ${TimeoutSec}s. Serial: $serialPath"
  }

  $exit = $proc.ExitCode
  # Merge stderr into stdout log for easier debugging.
  if (Test-Path -LiteralPath $serialErrPath) {
    try {
      Add-Content -LiteralPath $serialPath -Value "`n--- STDERR ---`n" -Encoding ASCII
      Get-Content -LiteralPath $serialErrPath -ErrorAction SilentlyContinue | Add-Content -LiteralPath $serialPath -Encoding ASCII
    } catch {}
  }

  $serial = if (Test-Path -LiteralPath $serialPath) { Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue } else { '' }

  if ($exit -ne 0) {
    Write-Host "[Autorun] QEMU wrapper exit=$exit" -ForegroundColor Yellow
  }

  # Assertions (keep these minimal + stable)
  if ($serial -notmatch '(?m)^\[autorun\] done\s*$') { throw "Missing '[autorun] done'. Serial: $serialPath" }

  if ($Mode -eq 'oo_consult_smoke') {
    if ($serial -notmatch '(?m)^OK: Model loaded: ') { throw "Missing model loaded marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^OK: REPL ready \(/help\)\s*$') { throw "Missing REPL ready marker. Serial: $serialPath" }
  } else {
    if ($serial -notmatch '(?m)^OK: REPL ready \(no model\)\.') { throw "Missing no-model REPL marker. Serial: $serialPath" }
  }

  if ($Mode -eq 'oo_smoke') {
    if ($serial -notmatch '/wasm_info') { throw "Missing /wasm_info invocation in autorun. Serial: $serialPath" }
    if ($serial -notmatch '/wasm_apply') { throw "Missing /wasm_apply invocation in autorun. Serial: $serialPath" }
    $guard = [regex]::Matches($serial, 'No model loaded\. Use /models then set repl\.cfg: model=<file> and reboot\.').Count
    if ($guard -lt 2) { throw "Missing expected no-model guard response for wasm commands (count=$guard). Serial: $serialPath" }
    if ($serial -notmatch '(?m)^OK: created entity id=') { throw "Missing OO create marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[OO Persistence\]\s*$') { throw "Missing OO persistence block. Serial: $serialPath" }
    if ($serial -notmatch 'OOSTATE\.BIN\s+present=1') { throw "Missing OOSTATE.BIN presence marker. Serial: $serialPath" }
    if ($serial -notmatch 'OOJOUR\.LOG\s+present=1') { throw "Missing OOJOUR.LOG presence marker. Serial: $serialPath" }
    if ($serial -notmatch 'continuity=no_receipt') { throw "Missing no-receipt continuity marker in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_continuity\] receipt\.present=0\s*$') { throw "Missing /oo_continuity_status receipt marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_continuity\] reason=no_receipt\s*$') { throw "Missing /oo_continuity_status no_receipt reason. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^oo\s+(event=)?cmd=oo_new(\s|$)') { throw "Missing ultra-minimal journal entry (oo cmd=oo_new). Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_infermini\] out:\s*') { throw "Missing /oo_infermini output marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_infermini\] ok hash=\d+\s*$') { throw "Missing /oo_infermini success hash. Serial: $serialPath" }
  }

  if ($Mode -eq 'oo_consult_smoke') {
    if ($serial -notmatch '\[oo_consult\] Consulting LLM for system status adaptation') { throw "Missing /oo_consult start marker. Serial: $serialPath" }
    if ($serial -notmatch '\[obs\]\[oo\] consult_start mode=') { throw "Missing consult_start marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO LLM suggested: ') { throw "Missing LLM suggestion marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO confidence: score=') { throw "Missing OO confidence marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO consult logged to OOCONSULT\.LOG') { throw "Missing OOCONSULT.LOG write marker. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_log\] OOCONSULT\.LOG tail:') { throw "Missing /oo_log marker. Serial: $serialPath" }
    if ($serial -notmatch 'OOCONSULT\.LOG\s+present=1') { throw "Missing OOCONSULT.LOG presence in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch 'cmd=oo_consult') { throw "Missing oo_consult journal context marker. Serial: $serialPath" }
  }

  if ($Mode -eq 'oo_outcome_smoke') {
    if ($serial -notmatch '(?m)^\[oo_consult_mock\] using mock suggestion\s*$') { throw "Missing /oo_consult_mock marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO auto-apply: reduce_ctx') { throw "Missing reduce_ctx auto-apply marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO consult metric: action=reduce_ctx improved=1 expected=ctx_256 observed=ctx_256') { throw "Missing confirmed next-boot outcome metric. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO feedback: good=1 bad=0 bias=[1-9][0-9]* action_bias=[1-9][0-9]*') { throw "Missing adaptive feedback bias marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO boot feedback: reduce_ctx=selected_matches_confirmed_good\(8\) reduce_seq=selected_differs_from_last_confirmed\(0\) increase_ctx=na\(0\)') { throw "Missing boot-relation feedback marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO trend feedback: reduce_ctx=trend_recent_positive\(4\) reduce_seq=trend_recent_none\(0\) increase_ctx=na\(0\)') { throw "Missing trend feedback marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO saturation feedback: reduce_ctx=saturated_min\(-10\) reduce_seq=ready\(0\) increase_ctx=na\(0\)') { throw "Missing saturation feedback marker. Serial: $serialPath" }
    if ($serial -notmatch 'OK: OO action selection: selected=reduce_seq mode=single_best') { throw "Missing trend+saturation action selection marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_outcome\] OOOUTCOME\.LOG tail:\s*$') { throw "Missing /oo_outcome header. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_outcome\] pending\.action=reduce_ctx\s*$') { throw "Missing pending outcome action marker. Serial: $serialPath" }
    if ($serial -notmatch 'out b=\d+ a=reduce_ctx i=-1 exp=ctx_256 obs=pending_next_boot') { throw "Missing OOOUTCOME pending line. Serial: $serialPath" }
    if ($serial -notmatch 'out b=\d+ a=reduce_ctx i=1 exp=ctx_256 obs=ctx_256') { throw "Missing OOOUTCOME confirmed line. Serial: $serialPath" }
    if ($serial -notmatch 'OOOUTCOME\.LOG\s+present=1') { throw "Missing OOOUTCOME.LOG presence in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch 'pending\.action=reduce_ctx') { throw "Missing pending action in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch 'last\.consult\.decision=multi selected=reduce_seq reason_id=OO_BLOCK_PLAN_BUDGET applied=0 score=\d+ threshold=60 feedback_bias=[1-9][0-9]*') { throw "Missing selected-action consult summary in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch 'last\.consult\.conf_reason_id=OO_CONF_LOG_ONLY plan\.enabled=0 remain=0 hard_stop=0 plan_reason_id=OO_PLAN_ACTIVE') { throw "Missing confidence/plan consult summary in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch 'last\.consult\.boot_relation=selected_differs_from_last_confirmed boot_bias=0') { throw "Missing boot relation consult summary in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch 'last\.consult\.trend=trend_recent_none trend_bias=0 saturation=ready saturation_bias=0') { throw "Missing trend and saturation consult summary in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch 'last\.consult\.operator_summary=positive_but_saturated') { throw "Missing operator summary in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch 'last\.consult\.why=not_applied_plan_budget') { throw "Missing human-readable consult why summary in /oo_status. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_explain\] latest consult:\s*$') { throw "Missing /oo_explain header. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain\] why=not_applied_plan_budget reason_id=OO_BLOCK_PLAN_BUDGET') { throw "Missing /oo_explain why summary. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain\] conf_reason_id=OO_CONF_LOG_ONLY') { throw "Missing verbose /oo_explain confidence reason. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain\] plan\.enabled=0 remain=0 hard_stop=0 plan_reason_id=OO_PLAN_ACTIVE') { throw "Missing verbose /oo_explain plan summary. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain\] boot_relation=selected_differs_from_last_confirmed boot_bias=0') { throw "Missing verbose /oo_explain boot relation summary. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain\] trend=trend_recent_none trend_bias=0 saturation=ready saturation_bias=0') { throw "Missing verbose /oo_explain trend and saturation summary. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain\] operator_summary=positive_but_saturated') { throw "Missing verbose /oo_explain operator summary. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_explain\] boot comparison:\s*$') { throw "Missing /oo_explain boot header. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain_boot\] consult\.selected=reduce_seq reason_id=OO_BLOCK_PLAN_BUDGET') { throw "Missing /oo_explain boot consult summary. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain_boot\] operator_summary=positive_but_saturated') { throw "Missing /oo_explain boot operator summary. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain_boot\] outcome\.action=reduce_ctx improved=1 expected=ctx_256 observed=ctx_256') { throw "Missing /oo_explain boot outcome summary. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain_boot\] relation=selected_differs_from_last_confirmed') { throw "Missing /oo_explain boot relation summary. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain_boot\] recent\.count=1') { throw "Missing /oo_explain boot recent count. Serial: $serialPath" }
    if ($serial -notmatch '\[oo_explain_boot\] recent\[0\]\.action=reduce_ctx improved=1 expected=ctx_256 observed=ctx_256') { throw "Missing /oo_explain boot recent outcome detail. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_log\] latest summary:\s*$') { throw "Missing enriched /oo_log summary header. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_log\] OOCONSULT\.LOG tail:\s*$') { throw "Missing /oo_log tail marker. Serial: $serialPath" }
    if ($serial -notmatch 'cmd=oo_outcome') { throw "Missing oo_outcome journal marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] verified=1\s*$') { throw "Missing reboot verification marker in outcome smoke. Serial: $serialPath" }
    if ($serial -notmatch 'consult b=\d+ .* sel=reduce_seq br=selected_differs_from_last_confirmed bb=0 tr=trend_recent_none tb=0 sr=ready sb=0 os=positive_but_saturated ri=OO_BLOCK_PLAN_BUDGET cri=OO_CONF_LOG_ONLY pe=0 pr=0 ph=0 pri=OO_PLAN_ACTIVE .* fb=[1-9][0-9]*') { throw "Missing persisted selected action, boot relation, trend, saturation, operator summary, reason, confidence, plan, and feedback bias in OOCONSULT.LOG. Serial: $serialPath" }
  }

  if ($Mode -eq 'oo_reboot_smoke') {
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] action=rebooting\s*$') { throw "Missing reboot arm marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] bootnext\.current=\d+\s*$') { throw "Missing bootnext.current marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] bootnext\.armed=1\s*$') { throw "Missing bootnext.armed=1 marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] armed\.present=1\s*$') { throw "Missing reboot verify marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] boot_advanced=1\s*$') { throw "Missing boot_advanced=1 marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] recovery_match=1\s*$') { throw "Missing recovery_match=1 marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] mode_ok=1\s*$') { throw "Missing mode_ok=1 marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] verified=1\s*$') { throw "Missing verified=1 marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^\[oo_reboot_probe\] summary=pass\s*$') { throw "Missing reboot probe pass summary. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^oo\s+(event=)?reboot_probe_arm(\s|$)') { throw "Missing reboot_probe_arm journal marker. Serial: $serialPath" }
    if ($serial -notmatch '(?m)^oo\s+(event=)?reboot_probe_verified(\s|$)') { throw "Missing reboot_probe_verified journal marker. Serial: $serialPath" }
  }

  Write-Host "[Autorun] PASS" -ForegroundColor Green
  exit 0
}
finally {
  Restore-File -path $replCfg -backup $replBak
  Restore-File -path $autorunTxt -backup $autorunBak
}
