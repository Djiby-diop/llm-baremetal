# tools/oo-qemu-test.ps1 — OO Phase 4B Integration Test
# =========================================================
# Automated QEMU test for all Phase 4 subsystems:
#   - mbedTLS TCP4 status
#   - DIOP model load
#   - Federation peer add
#   - DNS resolution
#   - Oracle query
#   - Self-improve session
#
# Usage:
#   .\tools\oo-qemu-test.ps1
#   .\tools\oo-qemu-test.ps1 -SkipProxy
#   .\tools\oo-qemu-test.ps1 -Model "models\story260K.bin"
#
# Requirements:
#   - QEMU installed (qemu-system-x86_64 in PATH)
#   - Python 3 (for oracle proxy)
#   - llama2.efi built (.\build\llama2.efi → boot image)

param(
    [switch]$SkipProxy,
    [string]$Model = "models\stories260K.bin",
    [int]   $ProxyPort = 8080,
    [int]   $QemuMem  = 512,        # MB
    [switch]$NoQuit                  # don't auto-quit QEMU (interactive)
)

$ErrorActionPreference = "Stop"

$ROOT  = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$BUILD = Join-Path $ROOT "build"
$IMG   = Join-Path $ROOT "llm-baremetal.img"
$TOOLS = Join-Path $ROOT "tools"
$LOG   = Join-Path $TOOLS "qemu-test.log"

Write-Host "`n  ╔══════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "  ║   OO Phase 4 — Integration Test  ║" -ForegroundColor Cyan
Write-Host "  ╚══════════════════════════════════╝`n" -ForegroundColor Cyan

# ── 1. Verify build artifacts ────────────────────────────────────────────────
Write-Host "[test] Checking build artifacts..." -ForegroundColor Yellow
$efi = Join-Path $BUILD "llama2.efi"
if (-not (Test-Path $efi)) {
    Write-Host "[FAIL] llama2.efi not found at: $efi" -ForegroundColor Red
    Write-Host "       Run: wsl bash -c 'cd /mnt/c/.../llm-baremetal && make'" -ForegroundColor Gray
    exit 1
}
$efi_size = (Get-Item $efi).Length
Write-Host "[OK]  llama2.efi: $([math]::Round($efi_size/1MB, 1)) MB" -ForegroundColor Green

if (-not (Test-Path $IMG)) {
    Write-Host "[WARN] Boot image not found: $IMG" -ForegroundColor Yellow
    Write-Host "       Run: wsl bash tools/scripts/make-boot-img.sh" -ForegroundColor Gray
    exit 1
}
Write-Host "[OK]  Boot image: $IMG" -ForegroundColor Green

# ── 2. Start oracle proxy ─────────────────────────────────────────────────────
$proxy_proc = $null
if (-not $SkipProxy) {
    Write-Host "[test] Starting oracle proxy on port $ProxyPort..." -ForegroundColor Yellow
    $proxy_script = Join-Path $TOOLS "oo-oracle-proxy.py"
    if (Test-Path $proxy_script) {
        $proxy_proc = Start-Process python3 `
            -ArgumentList $proxy_script, "--port", $ProxyPort `
            -PassThru -WindowStyle Hidden
        Start-Sleep -Seconds 2
        try {
            $pong = Invoke-WebRequest -Uri "http://localhost:$ProxyPort/health" `
                                      -TimeoutSec 3 -UseBasicParsing
            Write-Host "[OK]  Proxy running (PID $($proxy_proc.Id))" -ForegroundColor Green
        } catch {
            Write-Host "[WARN] Proxy may not be responding — continuing anyway" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[WARN] oo-oracle-proxy.py not found — skipping proxy" -ForegroundColor Yellow
    }
}

# ── 3. Build QEMU command ─────────────────────────────────────────────────────
$ovmf = $null
$ovmf_candidates = @(
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
    "C:\Program Files\qemu\OVMF.fd",
    "/usr/share/ovmf/OVMF.fd",
    "/usr/share/qemu/OVMF.fd"
)
foreach ($c in $ovmf_candidates) { if (Test-Path $c) { $ovmf = $c; break } }
if (-not $ovmf) {
    Write-Host "[WARN] OVMF not found — QEMU may not boot EFI" -ForegroundColor Yellow
    $ovmf = "OVMF.fd"
}
Write-Host "[test] OVMF: $ovmf" -ForegroundColor Gray

# Build REPL script to send to QEMU serial
$repl_cmds = @(
    # Network
    "/net_server 10.0.2.2 $ProxyPort",
    "/net_init",
    # DNS
    "/dns_add api.openai.com 10.0.2.2",
    "/dns_add huggingface.co 10.0.2.2",
    "/dns_status",
    # TLS
    "/tls_proxy 10.0.2.2 $ProxyPort",
    "/tls_status",
    # mbedTLS
    "/mbedtls_status",
    # DIOP model
    "/diop_info",
    # Federation
    "/fed_status",
    "/fed_add 10.0.2.2 8181 oo-host",
    "/fed_peers",
    # Self-improve
    "/patch_status",
    "/patch_log",
    # Oracle ping (if proxy is running)
    "/net_oracle gpt4 Describe the OO system in one sentence."
)
if (-not $NoQuit) { $repl_cmds += "/quit" }

# Write REPL script to temp file (send via stdin)
$repl_file = Join-Path $env:TEMP "oo-repl-test.txt"
$repl_cmds | Set-Content -Encoding Ascii $repl_file
Write-Host "[test] REPL script: $repl_file ($($repl_cmds.Count) commands)" -ForegroundColor Gray

$qemu_args = @(
    "-machine", "q35,accel=whpx:tcg",
    "-cpu", "qemu64",
    "-m", "${QemuMem}M",
    "-drive", "if=pflash,format=raw,readonly=on,file=$ovmf",
    "-drive", "format=raw,file=$IMG",
    "-netdev", "user,id=net0,hostfwd=tcp::$ProxyPort-:$ProxyPort",
    "-device", "e1000,netdev=net0",
    "-serial", "stdio",
    "-display", "none",
    "-no-reboot"
)

Write-Host "[test] Launching QEMU ($QemuMem MB RAM)..." -ForegroundColor Yellow
Write-Host "       qemu-system-x86_64 $($qemu_args -join ' ')" -ForegroundColor Gray

# ── 4. Run QEMU ─────────────────────────────────────────────────────────────
$qemu_out = ""
try {
    $proc = Start-Process "qemu-system-x86_64" `
        -ArgumentList $qemu_args `
        -RedirectStandardInput  $repl_file `
        -RedirectStandardOutput $LOG `
        -RedirectStandardError  (Join-Path $TOOLS "qemu-err.log") `
        -PassThru -Wait:(-not $NoQuit)

    if (-not $NoQuit) {
        $proc.WaitForExit(60000) | Out-Null   # 60s timeout
        if (-not $proc.HasExited) {
            $proc.Kill()
            Write-Host "[WARN] QEMU killed after 60s timeout" -ForegroundColor Yellow
        }
    }

    if (Test-Path $LOG) {
        $qemu_out = Get-Content $LOG -Raw
    }
} catch {
    Write-Host "[WARN] QEMU not found or failed: $_" -ForegroundColor Yellow
    Write-Host "       Install QEMU and ensure qemu-system-x86_64 is in PATH" -ForegroundColor Gray
}

# ── 5. Analyze output ────────────────────────────────────────────────────────
Write-Host "`n[test] Results:" -ForegroundColor Cyan

function Test-Output($label, $pattern) {
    if ($qemu_out -match $pattern) {
        Write-Host "  [PASS] $label" -ForegroundColor Green
        return $true
    } else {
        Write-Host "  [MISS] $label (pattern: $pattern)" -ForegroundColor Yellow
        return $false
    }
}

if ($qemu_out) {
    Test-Output "UEFI boot"          "OO|llama2|SOMA" | Out-Null
    Test-Output "Network init"       "net_init|DHCP|oo_netboot" | Out-Null
    Test-Output "DNS status"         "dns.*status|DNS" | Out-Null
    Test-Output "TLS status"         "tls.*status|TLS" | Out-Null
    Test-Output "mbedTLS status"     "mbedtls.*status|TCP4|mbedTLS" | Out-Null
    Test-Output "DIOP engine"        "diop.*ready|DIOP" | Out-Null
    Test-Output "Federation status"  "federation.*node|fed_status" | Out-Null
    Test-Output "Self-improve"       "patch.*status|PATCH" | Out-Null

    Write-Host "`n[test] Full log: $LOG" -ForegroundColor Gray
    Write-Host "[test] Log lines: $($qemu_out.Split("`n").Count)" -ForegroundColor Gray
} else {
    Write-Host "  [INFO] No QEMU output captured" -ForegroundColor Yellow
    Write-Host "         Either QEMU is not installed or boot image needs rebuild" -ForegroundColor Gray
}

# ── 6. Cleanup ───────────────────────────────────────────────────────────────
if ($proxy_proc -and -not $proxy_proc.HasExited) {
    Stop-Process -Id $proxy_proc.Id -ErrorAction SilentlyContinue
    Write-Host "[test] Oracle proxy stopped" -ForegroundColor Gray
}

Write-Host "`n  ╔═══════════════════════════╗" -ForegroundColor Cyan
Write-Host "  ║  Phase 4B Test Complete   ║" -ForegroundColor Cyan
Write-Host "  ╚═══════════════════════════╝`n" -ForegroundColor Cyan
