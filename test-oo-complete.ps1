[CmdletBinding(PositionalBinding = $false)]
param(
  [ValidateSet('debug','release')][string]$OsgProfile = 'release',
  [switch]$SkipOsgSmoke,
  [switch]$SkipLlmkSmoke,
  [switch]$SkipImage,
  [switch]$SkipPrebuild,
  [int]$TimeoutSec = 180
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Info([string]$msg) { Write-Host $msg -ForegroundColor Cyan }
function Ok([string]$msg) { Write-Host $msg -ForegroundColor Green }
function Warn([string]$msg) { Write-Host $msg -ForegroundColor Yellow }

function Assert([bool]$cond, [string]$msg) {
  if (-not $cond) { throw $msg }
}

function ConvertTo-WslPath([string]$winPath) {
  $norm = ($winPath -replace '\\','/')
  if ($norm -match '^([A-Za-z]):/(.*)$') {
    $drive = $Matches[1].ToLowerInvariant()
    $rest = $Matches[2]
    return "/mnt/$drive/$rest"
  }
  throw "Failed to convert path to WSL path: $winPath"
}

function Test-OoPolicyBin([string]$path, [bool]$expectAllowByDefault, [string]$expectFirstAllow) {
  Assert (Test-Path -LiteralPath $path) "Missing OOPOLICY.BIN at: $path"

  $bytes = [System.IO.File]::ReadAllBytes($path)
  Assert ($bytes.Length -eq 4112) "OOPOLICY.BIN size mismatch: expected 4112, got $($bytes.Length)"

  $magic = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
  Assert ($magic -eq 'OOPL') "OOPOLICY.BIN magic mismatch: expected OOPL, got '$magic'"

  $ver = [int]$bytes[4]
  Assert ($ver -eq 1) "OOPOLICY.BIN version mismatch: expected 1, got $ver"

  $mode = [int]$bytes[5]
  $allowCount = [int]$bytes[6]
  $denyCount = [int]$bytes[7]

  $modeExpect = if ($expectAllowByDefault) { 1 } else { 0 }
  Assert ($mode -eq $modeExpect) "OOPOLICY.BIN mode mismatch: expected $modeExpect, got $mode"
  Assert ($allowCount -ge 1) "OOPOLICY.BIN expected at least 1 allow rule, got $allowCount"
  Assert ($denyCount -ge 0) "OOPOLICY.BIN denyCount invalid: $denyCount"

  # First allow rule slot (NUL-terminated, 64 bytes)
  $allow0 = [System.Text.Encoding]::ASCII.GetString($bytes, 16, 64)
  $allow0 = ($allow0 -split "\x00")[0]
  Assert ($allow0 -eq $expectFirstAllow) "First allow rule mismatch: expected '$expectFirstAllow', got '$allow0'"
}

Info "=== OO Complete Test Suite (host + image + optional QEMU) ==="

# 1) Host preflight (best-effort if skipping QEMU smoke)
$preflight = Join-Path $PSScriptRoot 'preflight-host.ps1'
if (Test-Path -LiteralPath $preflight) {
  Info "[1/5] Host preflight"
  & $preflight -IncludeOsg
  if ($LASTEXITCODE -ne 0) {
    if ($SkipOsgSmoke -and $SkipLlmkSmoke) {
      Warn "Preflight not ready, but -SkipOsgSmoke and -SkipLlmkSmoke are set; continuing."
    } else {
      throw "Preflight failed ($LASTEXITCODE). Fix host deps or re-run with -SkipOsgSmoke/-SkipLlmkSmoke."
    }
  }
} else {
  Warn "[1/5] Host preflight: missing preflight-host.ps1 (skipping)"
}

# 2) OS-G host checks (kernel tooling + verifier)
$osgRoot = Join-Path $PSScriptRoot 'OS-G (Operating System Genesis)'
Assert (Test-Path -LiteralPath $osgRoot) "OS-G folder not found: $osgRoot"

$cargo = Get-Command cargo -ErrorAction SilentlyContinue
Assert ([bool]$cargo) "cargo not found (install Rust toolchain)"

Info "[2/5] OS-G host checks: cargo test + dplus_check"
Push-Location -LiteralPath $osgRoot
try {
  & cargo test --features std
  if ($LASTEXITCODE -ne 0) { throw "OS-G cargo test failed ($LASTEXITCODE)" }

  $smokePolicy = Join-Path $osgRoot 'qemu-fs\policy.dplus'
  Assert (Test-Path -LiteralPath $smokePolicy) "OS-G smoke policy not found: $smokePolicy"

  & cargo run --quiet --features std --bin dplus_check -- $smokePolicy
  if ($LASTEXITCODE -ne 0) { throw "dplus_check failed ($LASTEXITCODE)" }
}
finally {
  Pop-Location
}

# 3) Compile firmware OO policy (strict D+ LAW/PROOF) -> OOPOLICY.BIN
$firmwarePolicy = Join-Path $PSScriptRoot 'policy.dplus'
Assert (Test-Path -LiteralPath $firmwarePolicy) "Firmware policy not found: $firmwarePolicy"

$tmpOut = Join-Path $env:TEMP ("OOPOLICY-" + [Guid]::NewGuid().ToString('N') + ".BIN")

Info "[3/5] Compile policy.dplus -> OOPOLICY.BIN (strict)"
Push-Location -LiteralPath $osgRoot
try {
  & cargo run --quiet --features std --bin dplus_compile_oo -- $firmwarePolicy $tmpOut
  if ($LASTEXITCODE -ne 0) { throw "dplus_compile_oo failed ($LASTEXITCODE)" }
}
finally {
  Pop-Location
}

Test-OoPolicyBin -path $tmpOut -expectAllowByDefault:$true -expectFirstAllow '/oo*'
Copy-Item -Force $tmpOut (Join-Path $PSScriptRoot 'OOPOLICY.BIN')
Ok "OK: Built OOPOLICY.BIN (and wrote to repo root)"

# 4) Negative test: missing @@PROOF should fail
$badPolicy = Join-Path $env:TEMP ("policy-bad-" + [Guid]::NewGuid().ToString('N') + ".dplus")
$badOut = Join-Path $env:TEMP ("OOPOLICY-bad-" + [Guid]::NewGuid().ToString('N') + ".BIN")
@'
@@LAW
mode=deny_by_default
allow /oo_list
'@ | Set-Content -LiteralPath $badPolicy -Encoding UTF8

Info "[4/5] Negative compile: policy missing @@PROOF (expect failure)"
$exitCode = 0
Push-Location -LiteralPath $osgRoot
try {
  & cargo run --quiet --features std --bin dplus_compile_oo -- $badPolicy $badOut
  $exitCode = $LASTEXITCODE
}
finally {
  Pop-Location
}
Assert ($exitCode -ne 0) "Expected dplus_compile_oo to fail for missing @@PROOF, but it succeeded"
Ok "OK: Negative test failed as expected (exit=$exitCode)"

# 5) Build no-model image and verify policy artifacts are embedded
if (-not $SkipImage) {
  $build = Join-Path $PSScriptRoot 'build.ps1'
  Assert (Test-Path -LiteralPath $build) "build.ps1 not found: $build"

  Info "[5/5] Build no-model image (policy.dplus + OOPOLICY.BIN embedded)"
  $buildParams = @{ Target = 'repl'; NoModel = $true }
  if ($SkipPrebuild) { $buildParams.SkipPrebuild = $true }
  & $build @buildParams
  if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed ($LASTEXITCODE)" }

  $img = Get-ChildItem -Path $PSScriptRoot -Filter 'llm-baremetal-boot*.img' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  Assert ($null -ne $img) "Boot image not found after build"

  $wslImg = ConvertTo-WslPath $img.FullName

  # NOTE: Passing bash one-liners to `wsl bash -lc` from PowerShell can mangle `$var` expansions.
  # Use a temporary script file instead (LF + UTF-8 without BOM) for robust execution.
  $verifyScriptWin = Join-Path $env:TEMP ("oo-verify-image-" + [Guid]::NewGuid().ToString('N') + ".sh")
  $verifyScriptWsl = ConvertTo-WslPath $verifyScriptWin
  $encNoBom = New-Object System.Text.UTF8Encoding($false)

  $script = @'
set -euo pipefail

img_src="__IMG__"
echo "[verify] Copying image into WSL temp (avoid /mnt/c hangs)..."
img="$(mktemp --suffix=.img)"
cp -f "$img_src" "$img"
off=$((1024*1024))

tmp=$(mktemp)
echo "mtools_skip_check=1" > "$tmp"
printf 'drive z: file="%s" offset=%s\n' "$img" "$off" >> "$tmp"

export MTOOLSRC="$tmp"

echo "[verify] FAT root contains policy artifacts..."

fat_list="$(timeout 20s mdir -b z:/ | tr -d "\r")"
echo "$fat_list" | grep -F -i "OOPOLICY.BIN" >/dev/null
echo "$fat_list" | grep -F -i "OOPOLICY.CRC" >/dev/null
echo "$fat_list" | grep -F -i "policy.dplus" >/dev/null

# Verify notary sidecar matches embedded OOPOLICY.BIN.
tmpdir="$(mktemp -d)"
tmp_bin="$tmpdir/OOPOLICY.BIN"
tmp_crc="$tmpdir/OOPOLICY.CRC"
timeout 20s mcopy -o z:/OOPOLICY.BIN "$tmp_bin" >/dev/null 2>&1
timeout 20s mcopy -o z:/OOPOLICY.CRC "$tmp_crc" >/dev/null 2>&1

echo "[verify] Checking OOPOLICY.CRC matches OOPOLICY.BIN..."

export OO_TMP_BIN="$tmp_bin"
export OO_TMP_CRC="$tmp_crc"
python3 -c "import os, pathlib, re, sys, zlib; bp=pathlib.Path(os.environ['OO_TMP_BIN']); cp=pathlib.Path(os.environ['OO_TMP_CRC']); data=bp.read_bytes(); actual=zlib.crc32(data) & 0xFFFFFFFF; txt=cp.read_text(encoding='utf-8', errors='replace'); m=re.search(r'0x([0-9a-fA-F]{8})', txt);\
    (print('ERROR: OOPOLICY.CRC missing crc32=0x........') or sys.exit(2)) if not m else None; expected=int(m.group(1),16);\
    (print(f'ERROR: OOPOLICY.CRC mismatch: expected=0x{expected:08x} actual=0x{actual:08x}') or sys.exit(3)) if expected!=actual else None"

echo "[verify] OK"

rm -rf "$tmpdir"

rm -f "$tmp"
rm -f "$img"
'@
  $script = $script.Replace('__IMG__', $wslImg)
  $script = $script.Replace("`r`n", "`n").Replace("`r", "")

  try {
    [System.IO.File]::WriteAllText($verifyScriptWin, $script, $encNoBom)
    Info "Verifying image contents + OOPOLICY.CRC (WSL/mtools)..."
    & wsl bash $verifyScriptWsl
    if ($LASTEXITCODE -ne 0) { throw "WSL mtools verify failed ($LASTEXITCODE)" }
  }
  finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $verifyScriptWin
  }

  Ok "OK: Image contains OOPOLICY.BIN + policy.dplus"
} else {
  Warn "[5/5] Build image skipped (-SkipImage)"
}

# Optional: llm-baremetal UEFI/QEMU autorun smoke (exercises llama2.efi built from llama2_efi_final.c)
if (-not $SkipLlmkSmoke) {
  $autorun = Join-Path $PSScriptRoot 'test-qemu-autorun.ps1'
  $handoff = Join-Path $PSScriptRoot 'test-qemu-handoff.ps1'
  Assert (Test-Path -LiteralPath $autorun) "test-qemu-autorun.ps1 not found: $autorun"
  Assert (Test-Path -LiteralPath $handoff) "test-qemu-handoff.ps1 not found: $handoff"

  Info "Running llm-baremetal UEFI/QEMU autorun smoke (no-model)..."
  & $autorun -Mode oo_smoke -Accel tcg -MemMB 1024 -TimeoutSec $TimeoutSec
  if ($LASTEXITCODE -ne 0) { throw "llm-baremetal autorun smoke failed ($LASTEXITCODE)" }
  Ok "OK: llm-baremetal autorun smoke PASS"

  Info "Running llm-baremetal host->sovereign handoff smoke..."
  & $handoff -Accel tcg -MemMB 1024 -TimeoutSec $TimeoutSec
  if ($LASTEXITCODE -ne 0) { throw "llm-baremetal handoff smoke failed ($LASTEXITCODE)" }
  Ok "OK: llm-baremetal handoff smoke PASS"
} else {
  Warn "llm-baremetal autorun smoke skipped (-SkipLlmkSmoke)"
}

# Optional: OS-G UEFI/QEMU smoke
if (-not $SkipOsgSmoke) {
  $smoke = Join-Path $PSScriptRoot 'run-osg-smoke.ps1'
  Assert (Test-Path -LiteralPath $smoke) "run-osg-smoke.ps1 not found: $smoke"

  Info "Running OS-G UEFI/QEMU smoke..."
  & $smoke -Profile $OsgProfile -TimeoutSec $TimeoutSec
  if ($LASTEXITCODE -ne 0) { throw "OS-G smoke failed ($LASTEXITCODE)" }
  Ok "OK: OS-G smoke PASS"
} else {
  Warn "OS-G smoke skipped (-SkipOsgSmoke)"
}

Ok "OO complete tests: OK"
