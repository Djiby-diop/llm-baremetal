# Makefile for Llama2 Bare-Metal UEFI (stable REPL build)


ARCH = x86_64
CC = gcc

# GNU-EFI paths differ between distros (e.g. /usr/lib vs /usr/lib/x86_64-linux-gnu).
MULTIARCH ?= $(shell $(CC) -print-multiarch 2>/dev/null)
EFI_LIBDIR_CANDIDATES := /usr/lib /usr/lib/$(MULTIARCH)

EFI_LDS := $(firstword $(wildcard $(addsuffix /elf_$(ARCH)_efi.lds,$(EFI_LIBDIR_CANDIDATES))))
EFI_CRT0 := $(firstword $(wildcard $(addsuffix /crt0-efi-$(ARCH).o,$(EFI_LIBDIR_CANDIDATES))))
EFI_LIBDIR := $(firstword $(foreach d,$(EFI_LIBDIR_CANDIDATES),$(if $(wildcard $(d)/libgnuefi.a),$(d),)))

ifeq ($(strip $(EFI_LDS)),)
$(error Could not find elf_$(ARCH)_efi.lds (install gnu-efi))
endif
ifeq ($(strip $(EFI_CRT0)),)
$(error Could not find crt0-efi-$(ARCH).o (install gnu-efi))
endif
ifeq ($(strip $(EFI_LIBDIR)),)
EFI_LIBDIR := /usr/lib
endif

# Canonical GNU-EFI build flags (known-good for this project)
CFLAGS = -ffreestanding -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
		 -I/usr/include/efi -I/usr/include/efi/$(ARCH) -DEFI_FUNCTION_WRAPPER \
		 -Icore -Iengine/llama2 -Iengine/gguf -Iengine/djiblas -Iengine/ssm \
		 -I../oo-system/shared/oo-proto/include \
		 -Iengine/network/vendor/mbedtls/include \
		 -I. \
		 -O2 -msse2 -DDJIBLAS_DISABLE_CPUID=1 -DUEFI_BUILD=1 \
		 -DOO_MBEDTLS_REAL=1

# mbedTLS unity build flags: same freestanding base but suppress third-party warnings
MBEDTLS_CFLAGS = $(CFLAGS) -w \
	-Iengine/network/vendor/mbedtls/include \
	-Iengine/network/vendor/mbedtls

# Embed a build identifier for /version output (UTC). Override: make BUILD_ID=...
# NOTE: $(shell ...) in a recursively-expanded variable would re-run on each expansion,
# leading to different timestamps per object file. Force a single evaluation per make run.
BUILD_ID ?= $(shell date -u +%Y-%m-%dT%H:%M:%SZ)
BUILD_ID := $(BUILD_ID)
CFLAGS += -DLLMB_BUILD_ID=L\"$(BUILD_ID)\"

LDFLAGS = -nostdlib -znocombreloc -T $(EFI_LDS) \
		  -shared -Bsymbolic -L$(EFI_LIBDIR) $(EFI_CRT0)

LIBS = -lefi -lgnuefi

# ============================================================
# OO Subsystem archives (P1 split build)
#
# Built from worktree: llm-baremetal.worktrees/copilot-worktree-*
#
# P1 SPLIT: liboo-kernel.a now contains efi_phases.c (unity-build
# wrapper that includes llama2_efi_final.c with LLM_SPLIT_EFI_MAIN,
# extracting phase1-6 + llmk_repl_run) and efi_entry.c (new efi_main).
# We now include liboo-kernel.a and drop llama2_repl.o from the link.
# ============================================================
OO_WORKTREE ?= ../llm-baremetal.worktrees/copilot-worktree-2026-03-21T23-04-08
OO_BUILD_DIR = $(OO_WORKTREE)/build/oo
OO_LINK_ARCHIVES = \
	$(OO_BUILD_DIR)/liboo-kernel.a \
	$(OO_BUILD_DIR)/liboo-warden.a \
	$(OO_BUILD_DIR)/liboo-engine.a \
	$(OO_BUILD_DIR)/liboo-modules.a \
	$(OO_BUILD_DIR)/liboo-bus.a \
	$(OO_BUILD_DIR)/librust_guard.a

# P1 split build: no god-file object (efi_phases.c inside liboo-kernel.a)
TARGET = llama2.efi
REPL_SRC = engine/llama2/llama2_efi_final.c
REPL_OBJ = llama2_repl.o

# Phase 5 (Zig): metabolism profile selection
METABION_PROFILE ?= balanced
ZIG ?= zig
METABION_PROFILE_HDR = metabion_profile.h
METABION_PROFILE_DEFAULT = metabion_profile_default.h

# Engine cores NOT in any OO archive (missing → undefined symbols → runtime crash).
# Engines IN archives (OO_ENGINE_SRCS or OO_MODULES_SRCS) are excluded to avoid duplicates:
#   IN OO_ENGINE_SRCS:  evolvion, ghost, dreamion, morphion
#   IN OO_MODULES_SRCS: neuralfs, collectivion, cellion
SOMA_OBJS = engine/ssm/soma_router.o engine/ssm/soma_dna.o engine/ssm/soma_dual.o \
	engine/ssm/soma_smb.o engine/ssm/soma_dream.o engine/ssm/soma_meta.o \
	engine/ssm/soma_swarm.o engine/ssm/soma_reflex.o engine/ssm/soma_logic.o \
	engine/ssm/soma_memory.o engine/ssm/soma_journal.o engine/ssm/soma_cortex.o \
	engine/ssm/soma_export.o engine/ssm/soma_warden.o engine/ssm/soma_session.o \
	engine/ssm/soma_dna_persist.o engine/ssm/soma_spec.o \
	engine/ssm/soma_swarm_net.o \
	engine/ssm/oo_swarm_node.o engine/ssm/oo_swarm_sync.o \
	engine/ssm/core/soma_mind.o

REPL_OBJS = llmk_zones.o llmk_log.o llmk_sentinel.o llmk_oo.o llmk_oo_infer.o \
	llmk_stubs.o \
	djiblas.o djiblas_avx2.o attention_avx2.o gguf_loader.o gguf_infer.o \
	ssm_infer.o mamba_block.o mamba_weights.o bpe_tokenizer.o \
	oosi_loader.o oosi_infer.o oosi_v3_loader.o oosi_v3_infer.o \
	$(SOMA_OBJS) \
	engine/network/oo_mbedtls_port.o \
	engine/voice/oo_voice_router.o \
	engine/voice/oo_voice_context.o \
	engine/voice/oo_persona.o \
	engine/voice/oo_wakeword.o \
	engine/voice/oo_tts_phoneme.o \
	engine/voice/oo_voice_desktop_bridge.o \
	engine/voice/oo_voice_nlp.o \
	engine/voice/oo_voice_state_writer.o \
	engine/voice/oo_voice_loop.o \
	engine/drivers/oo_rtc.o \
	engine/drivers/oo_audio_hda.o \
	engine/drivers/oo_usb_msc.o \
	engine/drivers/oo_acpi.o \
	engine/drivers/oo_ioapic.o \
	engine/drivers/oo_edid.o \
	oo-modules/djibion-engine/core/djibion.o \
	oo-modules/diopion-engine/core/diopion.o \
	oo-modules/diagnostion-engine/core/diagnostion.o \
	oo-modules/memorion-engine/core/memorion.o \
	oo-modules/orchestrion-engine/core/orchestrion.o \
	oo-modules/calibrion-engine/core/calibrion.o \
	oo-modules/compatibilion-engine/core/compatibilion.o \
	oo-modules/synaption-engine/core/synaption.o \
	oo-modules/conscience-engine/core/conscience.o \
	oo-modules/immunion-engine/core/immunion.o \
	oo-modules/symbion-engine/core/symbion.o \
	oo-modules/metabion-engine/core/metabion.o \
	oo-modules/pheromion-engine/core/pheromion.o \
	oo-modules/evolvion-engine/core/evolvion.o \
	oo-modules/evolvion-engine/core/oo_driver_probe.o \
	oo-modules/ghost-engine/core/oo_net_packet.o
REPL_SO  = llama2_repl.so

all: repl

.PHONY: all repl clean rebuild genome test oo-subsystems

oo-subsystems:
	@if test -f $(OO_BUILD_DIR)/liboo-kernel.a; then \
	    echo "OK: oo-subsystems: using cached archives"; \
	else \
	    $(MAKE) -C $(OO_WORKTREE) -f tools/build/Makefile.oo-build OO_ROOT=. SRC_ROOT=../../llm-baremetal all; \
	fi

repl: $(TARGET)
	@echo "OK: Build complete: $(TARGET)"
	@ls -lh $(TARGET)


# Phase 5: generate metabion_profile.h (best-effort).
# If Zig (or the generator) is missing, use the default header.
$(METABION_PROFILE_HDR): $(METABION_PROFILE_DEFAULT)
	@tmp="$(METABION_PROFILE_HDR).tmp"; \
	if command -v $(ZIG) >/dev/null 2>&1 && [ -f tools/metabion_profile_gen.zig ]; then \
		if $(ZIG) run tools/metabion_profile_gen.zig -- $(METABION_PROFILE) > "$$tmp"; then \
			mv "$$tmp" $(METABION_PROFILE_HDR); \
			echo "OK: generated $(METABION_PROFILE_HDR) (profile=$(METABION_PROFILE))"; \
		else \
			rm -f "$$tmp"; \
			cp $(METABION_PROFILE_DEFAULT) $(METABION_PROFILE_HDR); \
			echo "OK: using $(METABION_PROFILE_HDR) fallback (zig/gen failed)"; \
		fi; \
	else \
		cp $(METABION_PROFILE_DEFAULT) $(METABION_PROFILE_HDR); \
		echo "OK: using $(METABION_PROFILE_HDR) fallback (zig/gen missing)"; \
	fi

# Rebuild when key headers change (Make doesn't auto-detect includes).
$(REPL_OBJ): $(REPL_SRC) engine/djiblas/djiblas.h engine/ssm/interface.h $(METABION_PROFILE_HDR)
	$(CC) $(CFLAGS) -c $(REPL_SRC) -o $(REPL_OBJ)

llmk_zones.o: core/llmk_zones.c core/llmk_zones.h core/llmk_log.h
	$(CC) $(CFLAGS) -c core/llmk_zones.c -o llmk_zones.o

llmk_log.o: core/llmk_log.c core/llmk_log.h core/llmk_zones.h
	$(CC) $(CFLAGS) -c core/llmk_log.c -o llmk_log.o

llmk_sentinel.o: core/llmk_sentinel.c core/llmk_sentinel.h core/llmk_zones.h core/llmk_log.h
	$(CC) $(CFLAGS) -c core/llmk_sentinel.c -o llmk_sentinel.o

llmk_oo.o: core/llmk_oo.c core/llmk_oo.h core/llmk_oo_infer.h
	$(CC) $(CFLAGS) -c core/llmk_oo.c -o llmk_oo.o

llmk_oo_infer.o: core/llmk_oo_infer.c core/llmk_oo_infer.h engine/ssm/bpe_tokenizer.h engine/ssm/oosi_infer.h
	$(CC) $(CFLAGS) -c core/llmk_oo_infer.c -o llmk_oo_infer.o

gguf_loader.o: engine/gguf/gguf_loader.c engine/gguf/gguf_loader.h
	$(CC) $(CFLAGS) -c engine/gguf/gguf_loader.c -o gguf_loader.o

gguf_infer.o: engine/gguf/gguf_infer.c engine/gguf/gguf_infer.h
	$(CC) $(CFLAGS) -c engine/gguf/gguf_infer.c -o gguf_infer.o

oo-modules/djibion-engine/core/djibion.o: oo-modules/djibion-engine/core/djibion.c oo-modules/djibion-engine/core/djibion.h
	$(CC) $(CFLAGS) -c oo-modules/djibion-engine/core/djibion.c -o oo-modules/djibion-engine/core/djibion.o

oo-modules/diopion-engine/core/diopion.o: oo-modules/diopion-engine/core/diopion.c oo-modules/diopion-engine/core/diopion.h
	$(CC) $(CFLAGS) -c oo-modules/diopion-engine/core/diopion.c -o oo-modules/diopion-engine/core/diopion.o

oo-modules/diagnostion-engine/core/diagnostion.o: oo-modules/diagnostion-engine/core/diagnostion.c oo-modules/diagnostion-engine/core/diagnostion.h
	$(CC) $(CFLAGS) -c oo-modules/diagnostion-engine/core/diagnostion.c -o oo-modules/diagnostion-engine/core/diagnostion.o

oo-modules/memorion-engine/core/memorion.o: oo-modules/memorion-engine/core/memorion.c oo-modules/memorion-engine/core/memorion.h
	$(CC) $(CFLAGS) -c oo-modules/memorion-engine/core/memorion.c -o oo-modules/memorion-engine/core/memorion.o

oo-modules/orchestrion-engine/core/orchestrion.o: oo-modules/orchestrion-engine/core/orchestrion.c oo-modules/orchestrion-engine/core/orchestrion.h
	$(CC) $(CFLAGS) -c oo-modules/orchestrion-engine/core/orchestrion.c -o oo-modules/orchestrion-engine/core/orchestrion.o

oo-modules/calibrion-engine/core/calibrion.o: oo-modules/calibrion-engine/core/calibrion.c oo-modules/calibrion-engine/core/calibrion.h
	$(CC) $(CFLAGS) -c oo-modules/calibrion-engine/core/calibrion.c -o oo-modules/calibrion-engine/core/calibrion.o

oo-modules/compatibilion-engine/core/compatibilion.o: oo-modules/compatibilion-engine/core/compatibilion.c oo-modules/compatibilion-engine/core/compatibilion.h
	$(CC) $(CFLAGS) -c oo-modules/compatibilion-engine/core/compatibilion.c -o oo-modules/compatibilion-engine/core/compatibilion.o

oo-modules/evolvion-engine/core/evolvion.o: oo-modules/evolvion-engine/core/evolvion.c oo-modules/evolvion-engine/core/evolvion.h
	$(CC) $(CFLAGS) -c oo-modules/evolvion-engine/core/evolvion.c -o oo-modules/evolvion-engine/core/evolvion.o

oo-modules/synaption-engine/core/synaption.o: oo-modules/synaption-engine/core/synaption.c oo-modules/synaption-engine/core/synaption.h
	$(CC) $(CFLAGS) -c oo-modules/synaption-engine/core/synaption.c -o oo-modules/synaption-engine/core/synaption.o

oo-modules/conscience-engine/core/conscience.o: oo-modules/conscience-engine/core/conscience.c oo-modules/conscience-engine/core/conscience.h
	$(CC) $(CFLAGS) -c oo-modules/conscience-engine/core/conscience.c -o oo-modules/conscience-engine/core/conscience.o

oo-modules/neuralfs-engine/core/neuralfs.o: oo-modules/neuralfs-engine/core/neuralfs.c oo-modules/neuralfs-engine/core/neuralfs.h
	$(CC) $(CFLAGS) -c oo-modules/neuralfs-engine/core/neuralfs.c -o oo-modules/neuralfs-engine/core/neuralfs.o

oo-modules/ghost-engine/core/ghost.o: oo-modules/ghost-engine/core/ghost.c oo-modules/ghost-engine/core/ghost.h
	$(CC) $(CFLAGS) -c oo-modules/ghost-engine/core/ghost.c -o oo-modules/ghost-engine/core/ghost.o

oo-modules/immunion-engine/core/immunion.o: oo-modules/immunion-engine/core/immunion.c oo-modules/immunion-engine/core/immunion.h
	$(CC) $(CFLAGS) -c oo-modules/immunion-engine/core/immunion.c -o oo-modules/immunion-engine/core/immunion.o

oo-modules/dreamion-engine/core/dreamion.o: oo-modules/dreamion-engine/core/dreamion.c oo-modules/dreamion-engine/core/dreamion.h
	$(CC) $(CFLAGS) -c oo-modules/dreamion-engine/core/dreamion.c -o oo-modules/dreamion-engine/core/dreamion.o

oo-modules/symbion-engine/core/symbion.o: oo-modules/symbion-engine/core/symbion.c oo-modules/symbion-engine/core/symbion.h
	$(CC) $(CFLAGS) -c oo-modules/symbion-engine/core/symbion.c -o oo-modules/symbion-engine/core/symbion.o

oo-modules/collectivion-engine/core/collectivion.o: oo-modules/collectivion-engine/core/collectivion.c oo-modules/collectivion-engine/core/collectivion.h
	$(CC) $(CFLAGS) -c oo-modules/collectivion-engine/core/collectivion.c -o oo-modules/collectivion-engine/core/collectivion.o

oo-modules/metabion-engine/core/metabion.o: oo-modules/metabion-engine/core/metabion.c oo-modules/metabion-engine/core/metabion.h
	$(CC) $(CFLAGS) -c oo-modules/metabion-engine/core/metabion.c -o oo-modules/metabion-engine/core/metabion.o

oo-modules/cellion-engine/core/cellion.o: oo-modules/cellion-engine/core/cellion.c oo-modules/cellion-engine/core/cellion.h
	$(CC) $(CFLAGS) -c oo-modules/cellion-engine/core/cellion.c -o oo-modules/cellion-engine/core/cellion.o

oo-modules/morphion-engine/core/morphion.o: oo-modules/morphion-engine/core/morphion.c oo-modules/morphion-engine/core/morphion.h
	$(CC) $(CFLAGS) -c oo-modules/morphion-engine/core/morphion.c -o oo-modules/morphion-engine/core/morphion.o

oo-modules/pheromion-engine/core/pheromion.o: oo-modules/pheromion-engine/core/pheromion.c oo-modules/pheromion-engine/core/pheromion.h
	$(CC) $(CFLAGS) -c oo-modules/pheromion-engine/core/pheromion.c -o oo-modules/pheromion-engine/core/pheromion.o

oo-modules/evolvion-engine/core/oo_driver_probe.o: oo-modules/evolvion-engine/core/oo_driver_probe.c oo-modules/evolvion-engine/core/oo_driver_probe.h
	$(CC) $(CFLAGS) -c oo-modules/evolvion-engine/core/oo_driver_probe.c -o oo-modules/evolvion-engine/core/oo_driver_probe.o

oo-modules/ghost-engine/core/oo_net_packet.o: oo-modules/ghost-engine/core/oo_net_packet.c oo-modules/ghost-engine/core/oo_net_packet.h
	$(CC) $(CFLAGS) -c oo-modules/ghost-engine/core/oo_net_packet.c -o oo-modules/ghost-engine/core/oo_net_packet.o

$(REPL_SO): $(REPL_OBJS) $(MBEDTLS_LIB) | oo-subsystems
	ld $(LDFLAGS) --allow-multiple-definition $(REPL_OBJS) \
		--start-group $(OO_LINK_ARCHIVES) $(MBEDTLS_LIB) --end-group \
		-o $(REPL_SO) $(LIBS)

$(TARGET): $(REPL_SO)
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
			-j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) $(REPL_SO) $(TARGET)

djiblas.o: engine/djiblas/djiblas.c engine/djiblas/djiblas.h
	$(CC) $(CFLAGS) -c engine/djiblas/djiblas.c -o djiblas.o

djiblas_avx2.o: engine/djiblas/djiblas_avx2.c engine/djiblas/djiblas.h
	$(CC) $(CFLAGS) -mavx2 -mfma -mno-vzeroupper -c engine/djiblas/djiblas_avx2.c -o djiblas_avx2.o

attention_avx2.o: engine/ssm/attention_avx2.c
	$(CC) $(CFLAGS) -mavx2 -mfma -mno-vzeroupper -c engine/ssm/attention_avx2.c -o attention_avx2.o

ssm_infer.o: engine/ssm/ssm_infer.c engine/ssm/ssm_infer.h engine/ssm/ssm_types.h
	$(CC) $(CFLAGS) -c engine/ssm/ssm_infer.c -o ssm_infer.o

mamba_block.o: engine/ssm/mamba_block.c engine/ssm/mamba_block.h engine/ssm/ssm_types.h
	$(CC) $(CFLAGS) -c engine/ssm/mamba_block.c -o mamba_block.o

mamba_weights.o: engine/ssm/mamba_weights.c engine/ssm/mamba_weights.h engine/ssm/ssm_types.h
	$(CC) $(CFLAGS) -c engine/ssm/mamba_weights.c -o mamba_weights.o

bpe_tokenizer.o: engine/ssm/bpe_tokenizer.c engine/ssm/bpe_tokenizer.h
	$(CC) $(CFLAGS) -c engine/ssm/bpe_tokenizer.c -o bpe_tokenizer.o

oosi_loader.o: engine/ssm/oosi_loader.c engine/ssm/oosi_loader.h engine/ssm/ssm_types.h
	$(CC) $(CFLAGS) -c engine/ssm/oosi_loader.c -o oosi_loader.o

oosi_infer.o: engine/ssm/oosi_infer.c engine/ssm/oosi_infer.h engine/ssm/oosi_loader.h engine/ssm/mamba_weights.h engine/ssm/ssm_types.h
	$(CC) $(CFLAGS) -c engine/ssm/oosi_infer.c -o oosi_infer.o

oosi_v3_loader.o: engine/ssm/oosi_v3_loader.c engine/ssm/oosi_v3_loader.h engine/ssm/ssm_types.h
	$(CC) $(CFLAGS) -c engine/ssm/oosi_v3_loader.c -o oosi_v3_loader.o

oosi_v3_infer.o: engine/ssm/oosi_v3_infer.c engine/ssm/oosi_v3_infer.h engine/ssm/oosi_v3_loader.h
	$(CC) $(CFLAGS) -c engine/ssm/oosi_v3_infer.c -o oosi_v3_infer.o

# SomaMind modules (Phases A-G)
engine/ssm/soma_router.o: engine/ssm/soma_router.c engine/ssm/soma_router.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_router.c -o engine/ssm/soma_router.o

engine/ssm/soma_dna.o: engine/ssm/soma_dna.c engine/ssm/soma_dna.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_dna.c -o engine/ssm/soma_dna.o

engine/ssm/soma_dual.o: engine/ssm/soma_dual.c engine/ssm/soma_dual.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_dual.c -o engine/ssm/soma_dual.o

engine/ssm/soma_smb.o: engine/ssm/soma_smb.c engine/ssm/soma_smb.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_smb.c -o engine/ssm/soma_smb.o

engine/ssm/soma_dream.o: engine/ssm/soma_dream.c engine/ssm/soma_dream.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_dream.c -o engine/ssm/soma_dream.o

engine/ssm/soma_meta.o: engine/ssm/soma_meta.c engine/ssm/soma_meta.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_meta.c -o engine/ssm/soma_meta.o

engine/ssm/soma_swarm.o: engine/ssm/soma_swarm.c engine/ssm/soma_swarm.h engine/ssm/soma_dna.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_swarm.c -o engine/ssm/soma_swarm.o

engine/ssm/soma_reflex.o: engine/ssm/soma_reflex.c engine/ssm/soma_reflex.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_reflex.c -o engine/ssm/soma_reflex.o

engine/ssm/soma_logic.o: engine/ssm/soma_logic.c engine/ssm/soma_logic.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_logic.c -o engine/ssm/soma_logic.o

engine/ssm/soma_memory.o: engine/ssm/soma_memory.c engine/ssm/soma_memory.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_memory.c -o engine/ssm/soma_memory.o

engine/ssm/soma_journal.o: engine/ssm/soma_journal.c engine/ssm/soma_journal.h engine/ssm/soma_memory.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_journal.c -o engine/ssm/soma_journal.o

engine/ssm/soma_cortex.o: engine/ssm/soma_cortex.c engine/ssm/soma_cortex.h engine/ssm/oosi_v3_loader.h engine/ssm/oosi_v3_infer.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_cortex.c -o engine/ssm/soma_cortex.o

engine/ssm/soma_export.o: engine/ssm/soma_export.c engine/ssm/soma_export.h engine/ssm/soma_memory.h engine/ssm/soma_journal.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_export.c -o engine/ssm/soma_export.o

engine/ssm/soma_warden.o: engine/ssm/soma_warden.c engine/ssm/soma_warden.h engine/ssm/soma_router.h core/llmk_sentinel.h core/llmk_zones.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_warden.c -o engine/ssm/soma_warden.o

engine/ssm/soma_session.o: engine/ssm/soma_session.c engine/ssm/soma_session.h engine/ssm/soma_dna.h engine/ssm/soma_router.h engine/ssm/soma_warden.h engine/ssm/soma_cortex.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_session.c -o engine/ssm/soma_session.o

engine/ssm/soma_dna_persist.o: engine/ssm/soma_dna_persist.c engine/ssm/soma_dna_persist.h engine/ssm/soma_dna.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_dna_persist.c -o engine/ssm/soma_dna_persist.o

engine/ssm/soma_spec.o: engine/ssm/soma_spec.c engine/ssm/soma_spec.h engine/ssm/ssm_types.h engine/ssm/soma_dna.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_spec.c -o engine/ssm/soma_spec.o

engine/ssm/soma_swarm_net.o: engine/ssm/soma_swarm_net.c engine/ssm/soma_swarm_net.h engine/ssm/soma_dna.h engine/ssm/oosi_v3_infer.h
	$(CC) $(CFLAGS) -c engine/ssm/soma_swarm_net.c -o engine/ssm/soma_swarm_net.o

engine/ssm/oo_swarm_node.o: engine/ssm/oo_swarm_node.c engine/ssm/oo_swarm_node.h
	$(CC) $(CFLAGS) -c engine/ssm/oo_swarm_node.c -o engine/ssm/oo_swarm_node.o

engine/ssm/oo_swarm_sync.o: engine/ssm/oo_swarm_sync.c engine/ssm/oo_swarm_sync.h
	$(CC) $(CFLAGS) -c engine/ssm/oo_swarm_sync.c -o engine/ssm/oo_swarm_sync.o

engine/ssm/core/soma_mind.o: engine/ssm/core/soma_mind.c engine/ssm/core/soma_mind.h
	$(CC) $(CFLAGS) -Iengine/ssm/core -c engine/ssm/core/soma_mind.c -o engine/ssm/core/soma_mind.o

llmk_stubs.o: core/llmk_stubs.c
	$(CC) $(CFLAGS) -c core/llmk_stubs.c -o llmk_stubs.o

# ── Phase 9A: mbedTLS 2.28 bare-metal TLS static archive ───────────────────
# Compile each mbedTLS source as a separate object to avoid static symbol
# conflicts that occur with unity builds. Link as libmbedtls.a.
MBEDTLS_DIR = engine/network/vendor/mbedtls
MBEDTLS_SRCS = \
	$(MBEDTLS_DIR)/platform.c \
	$(MBEDTLS_DIR)/platform_util.c \
	$(MBEDTLS_DIR)/error.c \
	$(MBEDTLS_DIR)/version.c \
	$(MBEDTLS_DIR)/version_features.c \
	$(MBEDTLS_DIR)/md.c \
	$(MBEDTLS_DIR)/md5.c \
	$(MBEDTLS_DIR)/sha1.c \
	$(MBEDTLS_DIR)/sha256.c \
	$(MBEDTLS_DIR)/aes.c \
	$(MBEDTLS_DIR)/aesni.c \
	$(MBEDTLS_DIR)/gcm.c \
	$(MBEDTLS_DIR)/ccm.c \
	$(MBEDTLS_DIR)/cipher.c \
	$(MBEDTLS_DIR)/cipher_wrap.c \
	$(MBEDTLS_DIR)/entropy.c \
	$(MBEDTLS_DIR)/ctr_drbg.c \
	$(MBEDTLS_DIR)/hmac_drbg.c \
	$(MBEDTLS_DIR)/bignum.c \
	$(MBEDTLS_DIR)/rsa.c \
	$(MBEDTLS_DIR)/ecp.c \
	$(MBEDTLS_DIR)/ecp_curves.c \
	$(MBEDTLS_DIR)/ecdh.c \
	$(MBEDTLS_DIR)/ecdsa.c \
	$(MBEDTLS_DIR)/pk.c \
	$(MBEDTLS_DIR)/pk_wrap.c \
	$(MBEDTLS_DIR)/pkparse.c \
	$(MBEDTLS_DIR)/pkwrite.c \
	$(MBEDTLS_DIR)/asn1parse.c \
	$(MBEDTLS_DIR)/asn1write.c \
	$(MBEDTLS_DIR)/oid.c \
	$(MBEDTLS_DIR)/base64.c \
	$(MBEDTLS_DIR)/pem.c \
	$(MBEDTLS_DIR)/x509.c \
	$(MBEDTLS_DIR)/x509_crt.c \
	$(MBEDTLS_DIR)/ssl_tls.c \
	$(MBEDTLS_DIR)/ssl_msg.c \
	$(MBEDTLS_DIR)/ssl_ciphersuites.c \
	$(MBEDTLS_DIR)/ssl_cli.c

MBEDTLS_OBJS = $(MBEDTLS_SRCS:.c=.o)
MBEDTLS_LIB  = engine/network/vendor/mbedtls/libmbedtls.a

# Pattern rule: compile each mbedTLS .c with -w to suppress third-party warnings
$(MBEDTLS_DIR)/%.o: $(MBEDTLS_DIR)/%.c
	$(CC) $(MBEDTLS_CFLAGS) -c $< -o $@

$(MBEDTLS_LIB): $(MBEDTLS_OBJS)
	ar rcs $@ $(MBEDTLS_OBJS)
	@echo "OK: libmbedtls.a built ($(words $(MBEDTLS_OBJS)) objects)"

# Port layer: our malloc pool, RDRAND entropy, TCP4 bio, TLS handshake wiring.
engine/network/oo_mbedtls_port.o: engine/network/oo_mbedtls_port.c \
		engine/network/oo_mbedtls_port.h engine/network/oo_mbedtls.h
	$(CC) $(CFLAGS) -c engine/network/oo_mbedtls_port.c \
		-o engine/network/oo_mbedtls_port.o

clean:
	rm -f $(REPL_OBJS) $(REPL_SO) $(TARGET) $(METABION_PROFILE_HDR)
	rm -f oosi_loader.o oosi_infer.o oosi_v3_loader.o oosi_v3_infer.o llmk_oo_infer.o
	rm -f engine/ssm/core/soma_mind.o
	rm -rf $(OO_BUILD_DIR)
	@echo "OK: Clean complete"

rebuild: clean all

genome:
	@python3 tools/oo_genome.py 2>/dev/null || python tools/oo_genome.py 2>/dev/null || true

test: all
	@echo "Creating bootable image..."
	@./create-boot-mtools.sh

