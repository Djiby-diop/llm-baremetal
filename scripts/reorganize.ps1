#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Réorganise llm-baremetal/ en structure propre.
    NE SUPPRIME RIEN — déplace seulement.
    
.DESCRIPTION
    Crée les dossiers cibles et déplace les fichiers.
    Après exécution, vérifier avec git status avant de commiter.
    
.USAGE
    cd C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal
    .\scripts\reorganize.ps1 -WhatIf   # Voir ce qui sera déplacé
    .\scripts\reorganize.ps1            # Exécuter
#>

[CmdletBinding(SupportsShouldProcess)]
param()

$ROOT = Split-Path -Parent $PSCommandPath | Split-Path -Parent
Write-Host "Root: $ROOT" -ForegroundColor Cyan

# ── Créer les dossiers cibles ─────────────────────────────────────────────
$dirs = @(
    "docs",
    "scripts",
    "tests",
    "engine",
    "engine\djiblas",
    "engine\gguf",
    "engine\llama2",
    "engine\ssm",
    "models",
    "artifacts\efi",
    "artifacts\images",
    "artifacts\logs"
)
foreach ($d in $dirs) {
    $path = Join-Path $ROOT $d
    if (-not (Test-Path $path)) {
        if ($PSCmdlet.ShouldProcess($path, "Create directory")) {
            New-Item -ItemType Directory -Path $path -Force | Out-Null
            Write-Host "  [MKDIR] $d" -ForegroundColor Green
        }
    }
}

# ── Fonction de déplacement sécurisé ─────────────────────────────────────
function Move-Safe {
    param([string]$src, [string]$dst)
    if (Test-Path $src) {
        $dstDir = Split-Path $dst -Parent
        if (-not (Test-Path $dstDir)) {
            New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
        }
        if ($PSCmdlet.ShouldProcess("$src -> $dst", "Move file")) {
            Move-Item -Path $src -Destination $dst -Force
            Write-Host "  [MOVE] $(Split-Path $src -Leaf) -> $dst" -ForegroundColor Yellow
        }
    }
}

# ── 1. DOCS — tous les markdown de phases/roadmap/status ─────────────────
Write-Host "`n[1/6] Moving docs..." -ForegroundColor Cyan
$docFiles = @(
    "PHASE_2_COMPLETE.md", "PHASE_2.2_COMPLETE.md", "PHASE_2.3_COMPLETE.md",
    "PHASE_2.4_COMPLETE.md", "PHASE_3_COMPLETE.md", "PHASE_3.1_HARDWARE_DETECTION.md",
    "PHASE_3.2_PERFORMANCE_ANALYSIS.md", "PHASE_3_ROADMAP.md", "PHASE_4_COMPLETE.md",
    "PHASE_5_COMPLETE.md", "QUICKREF_PHASE_2.2.md", "QUICKREF_PHASE_2.3.md",
    "QUICKREF_PHASE_2.4.md", "RELEASE_CANDIDATE.md", "RELEASE_NOTES_rc-2026-03-15.md",
    "NEURAL_PROTECTOR_ROADMAP.md", "NEURAL_PROTECTOR_SUMMARY.md",
    "SESSION_SUMMARY.md", "STATUS_UPDATE_EN.txt", "EXECUTION_LOG.md",
    "ORGANISM_STATUS.md", "ORGANISM_MANIFEST.md", "OO_VISION.md",
    "OO_SOVEREIGN_HANDOFF_FORMAT.md", "COMMANDES.md", "SECURITY.md",
    "OPTIMIZATIONS_GUIDE.md", "TEST_USB_GUIDE.md", "OOHANDOFF.TXT",
    "mine.md"
)
foreach ($f in $docFiles) {
    Move-Safe (Join-Path $ROOT $f) (Join-Path $ROOT "docs\$f")
}

# ── 2. SCRIPTS — tous les .ps1 et .sh helpers ────────────────────────────
Write-Host "`n[2/6] Moving scripts..." -ForegroundColor Cyan
$scriptFiles = @(
    "build.ps1", "run.ps1", "run-m19.ps1", "run-oo-guard.ps1",
    "run-osg-smoke.ps1", "run-qemu-oo-validation.ps1",
    "run-qemu.ps1", "run-oo-sim-lab.ps1", "run-real-hw-handoff-validation.ps1",
    "run-real-hw-oo-consult-validation.ps1", "run-real-hw-oo-reboot-validation.ps1",
    "launch-qemu-now.bat", "launch-qemu-simple.ps1", "launch-qemu.ps1",
    "preflight-host.ps1", "report-prebuild-violations.ps1",
    "collect-real-hw-oo-artifacts.ps1", "validate-real-hw-oo-artifacts.ps1",
    "write-real-hw-oo-validation-report.ps1", "write-usb-admin.ps1",
    "prepare-real-hw-chat.ps1", "prepare-real-hw-handoff.ps1", "prepare-real-hw-reboot.ps1",
    "qemu-smoke.ps1", "reliability.ps1", "validate.ps1", "verify-image.ps1",
    "bench-matrix.ps1", "debug-boot-failure.ps1", "diagnose-boot.ps1",
    "preflight-host.ps1", "create-bootable-image.ps1", "create-bootable-usb.ps1",
    "create-image-simple.ps1", "compile-fix.sh", "compile-optimized.sh",
    "create-boot-mtools.sh", "create-bootable-image.sh", "create-bootable-wsl.sh",
    "create-hwsim-image.sh", "extract-splash-from-img.sh", "run-qemu.sh",
    "test-qemu-simple.sh", "test-qemu-wsl.sh", "preflight-host.ps1"
)
foreach ($f in $scriptFiles) {
    Move-Safe (Join-Path $ROOT $f) (Join-Path $ROOT "scripts\$f")
}

# ── 3. TESTS — fichiers de test ───────────────────────────────────────────
Write-Host "`n[3/6] Moving tests..." -ForegroundColor Cyan
$testFiles = @(
    "test-qemu.ps1", "test-qemu-autorun.ps1", "test-qemu-graphique.ps1",
    "test-qemu-handoff.ps1", "test-qemu-manual-smoke.ps1",
    "test-oo-complete.ps1", "test-wsl.ps1",
    "test_ffi_integration.c", "test_minimal.c", "test_prebuild.c",
    "test-qemu.sh", "test-qemu-simple.sh", "test-qemu-wsl.sh"
)
foreach ($f in $testFiles) {
    Move-Safe (Join-Path $ROOT $f) (Join-Path $ROOT "tests\$f")
}

# ── 4. ENGINE — code moteur LLM ───────────────────────────────────────────
Write-Host "`n[4/6] Moving engine files..." -ForegroundColor Cyan

# djiblas
@("djiblas.c", "djiblas.h", "djiblas.o",
  "djiblas_avx2.c", "djiblas_avx2.o", "djibmark.h") | ForEach-Object {
    Move-Safe (Join-Path $ROOT $_) (Join-Path $ROOT "engine\djiblas\$_")
}

# gguf
@("gguf_infer.c", "gguf_infer.h", "gguf_infer.o",
  "gguf_loader.c", "gguf_loader.h", "gguf_loader.o") | ForEach-Object {
    Move-Safe (Join-Path $ROOT $_) (Join-Path $ROOT "engine\gguf\$_")
}

# llama2 versions
1..10 | ForEach-Object { Move-Safe (Join-Path $ROOT "llama2_step$_.c") (Join-Path $ROOT "engine\llama2\llama2_step$_.c") }
@("llama2_repl_v1.c","llama2_repl_v2.c","llama2_repl_v3.c",
  "llama2_fixed.c","llama2_simple.c","llama2_efi_final.c",
  "llama2_repl.o","llama2_repl.so") | ForEach-Object {
    Move-Safe (Join-Path $ROOT $_) (Join-Path $ROOT "engine\llama2\$_")
}

# ssm / attention
@("attention_avx2.c","attention_avx2.o","safe_avx2.h",
  "matmul_advanced.h","matmul_optimized.h","memcmp_optimized.h",
  "heap_allocator.h","tinyblas.h","interface.h") | ForEach-Object {
    Move-Safe (Join-Path $ROOT $_) (Join-Path $ROOT "engine\ssm\$_")
}

# ── 5. ARTIFACTS — .efi et logs de debug ─────────────────────────────────
Write-Host "`n[5/6] Moving artifacts..." -ForegroundColor Cyan
@("_BOOTX64_from_bootimg.efi","_BOOTX64_from_image.efi",
  "_BOOTX64_from_image_after_build.efi","_BOOTX64_from_image_after_fix.efi",
  "llama2.efi","llama2.efi.backup","KERNEL.EFI") | ForEach-Object {
    Move-Safe (Join-Path $ROOT $_) (Join-Path $ROOT "artifacts\efi\$_")
}
@("qemu-crash.log","qemu-serial.log","test-serial.txt","test-serial-violations.txt",
  "_tmp_model_check.txt","test_tokenizer.txt","llmk-autorun-bench.txt",
  "llmk-autorun-handoff-smoke.txt","llmk-autorun-oo-consult-smoke.txt",
  "llmk-autorun-oo-outcome-smoke.txt","llmk-autorun-oo-reboot-smoke.txt",
  "llmk-autorun-oo-smoke.txt","llmk-autorun-real-hw-handoff-smoke.txt",
  "llmk-autorun-real-hw-model-chat-smoke.txt","llmk-autorun-real-hw-oo-consult-smoke.txt",
  "llmk-autorun-real-hw-oo-reboot-smoke.txt","llmk-autorun-real-hw-oo-smoke.txt",
  "llmk-autorun.txt","STATUS_UPDATE_EN.txt","OOHANDOFF.TXT") | ForEach-Object {
    Move-Safe (Join-Path $ROOT $_) (Join-Path $ROOT "artifacts\logs\$_")
}

# ── 6. MODELS — modèles locaux (non git) ─────────────────────────────────
Write-Host "`n[6/6] Moving models..." -ForegroundColor Cyan
@("stories110M.bin","stories15M.q8_0.gguf",
  "tinyllama-1.1b-chat-v1.0.Q2_K.gguf",
  "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf",
  "tinyllama-1.1b-chat-v1.0.Q8_0.gguf",
  "tokenizer-new.bin") | ForEach-Object {
    Move-Safe (Join-Path $ROOT $_) (Join-Path $ROOT "models\$_")
}

# ── Résumé ────────────────────────────────────────────────────────────────
Write-Host "`n╔══════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║  Réorganisation terminée             ║" -ForegroundColor Green
Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "Nouvelle structure :" -ForegroundColor Cyan
Write-Host "  llm-baremetal/"
Write-Host "  ├── core/          ← kernel UEFI (inchangé)"
Write-Host "  ├── docs/          ← toute la doc déplacée ici"
Write-Host "  ├── scripts/       ← tous les .ps1/.sh"
Write-Host "  ├── tests/         ← fichiers de test"
Write-Host "  ├── engine/        ← djiblas, gguf, llama2, ssm"
Write-Host "  ├── models/        ← .bin .gguf (ignorés par git)"
Write-Host "  ├── artifacts/     ← .efi, images, logs"
Write-Host "  └── oo-hardware-sim/ (worktree) ← propre ✅"
Write-Host ""
Write-Host "Prochaine étape : git status pour voir les changements" -ForegroundColor Yellow
