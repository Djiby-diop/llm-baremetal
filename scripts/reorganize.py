#!/usr/bin/env python3
"""
Réorganise llm-baremetal/ sans rien supprimer.
Lance depuis n'importe où — le chemin ROOT est hardcodé.
"""
import os, shutil, pathlib

ROOT = pathlib.Path(r"C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal")

def mkdirs(*parts):
    p = ROOT.joinpath(*parts)
    p.mkdir(parents=True, exist_ok=True)
    return p

def move(src_rel, dst_rel):
    src = ROOT / src_rel
    dst = ROOT / dst_rel
    if src.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.exists():
            shutil.move(str(src), str(dst))
            print(f"  [MOVE] {src_rel}  →  {dst_rel}")
        else:
            print(f"  [SKIP] {dst_rel} already exists")
    # no warning if src doesn't exist — expected for many optional files

# ─── Create target folders ───────────────────────────────────────────────────
for d in ["docs","scripts","tests","engine/djiblas","engine/gguf",
          "engine/llama2","engine/ssm","artifacts/efi","artifacts/logs"]:
    mkdirs(d)

# ─── 1. DOCS ─────────────────────────────────────────────────────────────────
print("\n[1/7] docs/")
for f in [
    "PHASE_2_COMPLETE.md","PHASE_2.2_COMPLETE.md","PHASE_2.3_COMPLETE.md",
    "PHASE_2.4_COMPLETE.md","PHASE_3_COMPLETE.md","PHASE_3.1_HARDWARE_DETECTION.md",
    "PHASE_3.2_PERFORMANCE_ANALYSIS.md","PHASE_3_ROADMAP.md","PHASE_4_COMPLETE.md",
    "PHASE_5_COMPLETE.md","QUICKREF_PHASE_2.2.md","QUICKREF_PHASE_2.3.md",
    "QUICKREF_PHASE_2.4.md","RELEASE_CANDIDATE.md","RELEASE_NOTES_rc-2026-03-15.md",
    "NEURAL_PROTECTOR_ROADMAP.md","NEURAL_PROTECTOR_SUMMARY.md","SESSION_SUMMARY.md",
    "OO_SOVEREIGN_HANDOFF_FORMAT.md","OPTIMIZATIONS_GUIDE.md","TEST_USB_GUIDE.md",
    "ORGANISM_STATUS.md","ORGANISM_MANIFEST.md","OO_VISION.md",
    "OO_SPEC.md","OO_GLOBAL_ARCHITECTURE.md","OO_HOST_RUNTIME_V0.md",
    "OO_SHARED_IDENTITY_AND_JOURNAL.md","OO_SOVEREIGN_RUNTIME_INVARIANTS.md",
    "REPERE.md","ROADMAP.md","COMMANDES.md","mine.md",
    "ssm_integration_template.md","bug_report_template.md","new_module_template.md",
]:
    move(f, f"docs/{f}")

# ─── 2. SCRIPTS ──────────────────────────────────────────────────────────────
print("\n[2/7] scripts/")
for f in [
    "build.ps1","run.ps1","run-m19.ps1","run-oo-guard.ps1","run-osg-smoke.ps1",
    "run-qemu-oo-validation.ps1","run-qemu.ps1","run-oo-sim-lab.ps1",
    "run-real-hw-handoff-validation.ps1","run-real-hw-oo-consult-validation.ps1",
    "run-real-hw-oo-reboot-validation.ps1","launch-qemu-now.bat",
    "launch-qemu-simple.ps1","launch-qemu.ps1","preflight-host.ps1",
    "report-prebuild-violations.ps1","collect-real-hw-oo-artifacts.ps1",
    "validate-real-hw-oo-artifacts.ps1","write-real-hw-oo-validation-report.ps1",
    "write-usb-admin.ps1","prepare-real-hw-chat.ps1","prepare-real-hw-handoff.ps1",
    "prepare-real-hw-reboot.ps1","qemu-smoke.ps1","reliability.ps1",
    "validate.ps1","verify-image.ps1","bench-matrix.ps1","debug-boot-failure.ps1",
    "diagnose-boot.ps1","create-bootable-image.ps1","create-bootable-usb.ps1",
    "create-image-simple.ps1","compile-fix.sh","compile-optimized.sh",
    "create-boot-mtools.sh","create-bootable-image.sh","create-bootable-wsl.sh",
    "create-hwsim-image.sh","extract-splash-from-img.sh","run-qemu.sh",
    "test-qemu-simple.sh","test-qemu-wsl.sh",
]:
    move(f, f"scripts/{f}")

# ─── 3. TESTS ────────────────────────────────────────────────────────────────
print("\n[3/7] tests/")
for f in [
    "test-qemu.ps1","test-qemu-autorun.ps1","test-qemu-graphique.ps1",
    "test-qemu-handoff.ps1","test-qemu-manual-smoke.ps1",
    "test-oo-complete.ps1","test-wsl.ps1","test-qemu.sh",
    "test_ffi_integration.c","test_minimal.c","test_prebuild.c",
]:
    move(f, f"tests/{f}")

# ─── 4. ENGINE ───────────────────────────────────────────────────────────────
print("\n[4/7] engine/")
# djiblas
for f in ["djiblas.c","djiblas.h","djiblas.o","djiblas_avx2.c","djiblas_avx2.o","djibmark.h"]:
    move(f, f"engine/djiblas/{f}")
# gguf
for f in ["gguf_infer.c","gguf_infer.h","gguf_infer.o","gguf_loader.c","gguf_loader.h","gguf_loader.o"]:
    move(f, f"engine/gguf/{f}")
# llama2
for i in range(1, 11):
    move(f"llama2_step{i}.c", f"engine/llama2/llama2_step{i}.c")
for f in ["llama2_repl_v1.c","llama2_repl_v2.c","llama2_repl_v3.c",
          "llama2_fixed.c","llama2_simple.c","llama2_efi_final.c",
          "llama2_repl.o","llama2_repl.so"]:
    move(f, f"engine/llama2/{f}")
# ssm / headers divers
for f in ["attention_avx2.c","attention_avx2.o","safe_avx2.h",
          "matmul_advanced.h","matmul_optimized.h","memcmp_optimized.h",
          "heap_allocator.h","tinyblas.h"]:
    move(f, f"engine/ssm/{f}")

# ─── 5. ARTIFACTS EFI ────────────────────────────────────────────────────────
print("\n[5/7] artifacts/efi/")
for f in ["_BOOTX64_from_bootimg.efi","_BOOTX64_from_image.efi",
          "_BOOTX64_from_image_after_build.efi","_BOOTX64_from_image_after_fix.efi",
          "llama2.efi","llama2.efi.backup","KERNEL.EFI",
          "llm-baremetal-boot-nomodel-x86_64.img.xz",
          "llm-baremetal-boot-stories110m-x86_64.img.xz",
          "llm-baremetal-boot.img"]:
    move(f, f"artifacts/efi/{f}")

# ─── 6. ARTIFACTS LOGS ───────────────────────────────────────────────────────
print("\n[6/7] artifacts/logs/")
for f in ["qemu-crash.log","qemu-serial.log","test-serial.txt","test-serial-violations.txt",
          "_tmp_model_check.txt","test_tokenizer.txt","OOHANDOFF.TXT",
          "llmk-autorun-bench.txt","llmk-autorun-handoff-smoke.txt",
          "llmk-autorun.txt","STATUS_UPDATE_EN.txt"]:
    move(f, f"artifacts/logs/{f}")
# llmk-autorun smoke files: keep them accessible (also ignored by .gitignore)
for f in ["llmk-autorun-oo-consult-smoke.txt","llmk-autorun-oo-outcome-smoke.txt",
          "llmk-autorun-oo-reboot-smoke.txt","llmk-autorun-oo-smoke.txt",
          "llmk-autorun-real-hw-handoff-smoke.txt","llmk-autorun-real-hw-model-chat-smoke.txt",
          "llmk-autorun-real-hw-oo-consult-smoke.txt","llmk-autorun-real-hw-oo-reboot-smoke.txt",
          "llmk-autorun-real-hw-oo-smoke.txt","llmk-autorun-handoff-smoke.txt"]:
    move(f, f"artifacts/logs/{f}")

# ─── 7. MODELS ───────────────────────────────────────────────────────────────
print("\n[7/7] models/ (déjà ignorés par git)")
for f in ["stories110M.bin","stories15M.q8_0.gguf",
          "tinyllama-1.1b-chat-v1.0.Q2_K.gguf",
          "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf",
          "tinyllama-1.1b-chat-v1.0.Q8_0.gguf",
          "tokenizer-new.bin"]:
    move(f, f"models/{f}")

# ─── Résumé ───────────────────────────────────────────────────────────────────
print("\n" + "═"*50)
print("  Réorganisation terminée ✅")
print("═"*50)
print("\nStructure maintenant :")
for name in sorted(os.listdir(ROOT)):
    p = ROOT / name
    if p.is_dir() and not name.startswith('.'):
        count = sum(1 for _ in p.rglob('*') if _.is_file())
        print(f"  {name}/  ({count} fichiers)")
    elif p.is_file() and not name.startswith('.'):
        print(f"  {name}")
print("\nProchaine étape: git status dans llm-baremetal/")
