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
		 -O2 -msse2

# Embed a build identifier for /version output (UTC). Override: make BUILD_ID=...
# NOTE: $(shell ...) in a recursively-expanded variable would re-run on each expansion,
# leading to different timestamps per object file. Force a single evaluation per make run.
BUILD_ID ?= $(shell date -u +%Y-%m-%dT%H:%M:%SZ)
BUILD_ID := $(BUILD_ID)
CFLAGS += -DLLMB_BUILD_ID=L\"$(BUILD_ID)\"

LDFLAGS = -nostdlib -znocombreloc -T $(EFI_LDS) \
		  -shared -Bsymbolic -L$(EFI_LIBDIR) $(EFI_CRT0)

LIBS = -lefi -lgnuefi

# Stable build: chat REPL (single-file + kernel primitives)
TARGET = llama2.efi
REPL_SRC = llama2_efi_final.c
REPL_OBJ = llama2_repl.o

# Phase 5 (Zig): metabolism profile selection
METABION_PROFILE ?= balanced
ZIG ?= zig
METABION_PROFILE_HDR = metabion_profile.h
METABION_PROFILE_DEFAULT = metabion_profile_default.h

DJIBION_OBJ = djibion-engine/core/djibion.o
DIOPION_OBJ = diopion-engine/core/diopion.o
DIAGNOSTION_OBJ = diagnostion-engine/core/diagnostion.o
MEMORION_OBJ = memorion-engine/core/memorion.o
ORCHESTRION_OBJ = orchestrion-engine/core/orchestrion.o
CALIBRION_OBJ = calibrion-engine/core/calibrion.o
COMPATIBILION_OBJ = compatibilion-engine/core/compatibilion.o
EVOLVION_OBJ = evolvion-engine/core/evolvion.o
SYNAPTION_OBJ = synaption-engine/core/synaption.o
CONSCIENCE_OBJ = conscience-engine/core/conscience.o
NEURALFS_OBJ = neuralfs-engine/core/neuralfs.o
GHOST_OBJ = ghost-engine/core/ghost.o
IMMUNION_OBJ = immunion-engine/core/immunion.o
DREAMION_OBJ = dreamion-engine/core/dreamion.o
SYMBION_OBJ = symbion-engine/core/symbion.o
COLLECTIVION_OBJ = collectivion-engine/core/collectivion.o
METABION_OBJ = metabion-engine/core/metabion.o
CELLION_OBJ = cellion-engine/core/cellion.o
MORPHION_OBJ = morphion-engine/core/morphion.o
PHEROMION_OBJ = pheromion-engine/core/pheromion.o
REPL_OBJS = $(REPL_OBJ) $(DJIBION_OBJ) $(DIOPION_OBJ) $(DIAGNOSTION_OBJ) $(MEMORION_OBJ) $(ORCHESTRION_OBJ) $(CALIBRION_OBJ) $(COMPATIBILION_OBJ) $(EVOLVION_OBJ) $(SYNAPTION_OBJ) $(CONSCIENCE_OBJ) $(NEURALFS_OBJ) $(GHOST_OBJ) $(IMMUNION_OBJ) $(DREAMION_OBJ) $(SYMBION_OBJ) $(COLLECTIVION_OBJ) $(METABION_OBJ) $(CELLION_OBJ) $(MORPHION_OBJ) $(PHEROMION_OBJ) llmk_zones.o llmk_log.o llmk_sentinel.o llmk_oo.o djiblas.o djiblas_avx2.o attention_avx2.o gguf_loader.o gguf_infer.o
REPL_SO  = llama2_repl.so

all: repl

.PHONY: all repl clean rebuild genome test

repl: $(TARGET)
	@echo "OK: Build complete: $(TARGET)"
	@ls -lh $(TARGET)


# Phase 5: generate metabion_profile.h (best-effort). If Zig is missing, use the default header.
$(METABION_PROFILE_HDR): $(METABION_PROFILE_DEFAULT) tools/metabion_profile_gen.zig
	@if command -v $(ZIG) >/dev/null 2>&1; then \
		$(ZIG) run tools/metabion_profile_gen.zig -- $(METABION_PROFILE) > $(METABION_PROFILE_HDR); \
		echo "OK: generated $(METABION_PROFILE_HDR) (profile=$(METABION_PROFILE))"; \
	else \
		cp $(METABION_PROFILE_DEFAULT) $(METABION_PROFILE_HDR); \
		echo "OK: using $(METABION_PROFILE_HDR) fallback (zig not found)"; \
	fi

# Rebuild when key headers change (Make doesn't auto-detect includes).
$(REPL_OBJ): $(REPL_SRC) djiblas.h interface.h $(METABION_PROFILE_HDR)
	$(CC) $(CFLAGS) -c $(REPL_SRC) -o $(REPL_OBJ)

llmk_zones.o: llmk_zones.c llmk_zones.h
	$(CC) $(CFLAGS) -c llmk_zones.c -o llmk_zones.o

llmk_log.o: llmk_log.c llmk_log.h llmk_zones.h
	$(CC) $(CFLAGS) -c llmk_log.c -o llmk_log.o

llmk_sentinel.o: llmk_sentinel.c llmk_sentinel.h llmk_zones.h llmk_log.h
	$(CC) $(CFLAGS) -c llmk_sentinel.c -o llmk_sentinel.o

llmk_oo.o: llmk_oo.c llmk_oo.h
	$(CC) $(CFLAGS) -c llmk_oo.c -o llmk_oo.o

gguf_loader.o: gguf_loader.c gguf_loader.h
	$(CC) $(CFLAGS) -c gguf_loader.c -o gguf_loader.o

gguf_infer.o: gguf_infer.c gguf_infer.h
	$(CC) $(CFLAGS) -c gguf_infer.c -o gguf_infer.o

djibion-engine/core/djibion.o: djibion-engine/core/djibion.c djibion-engine/core/djibion.h
	$(CC) $(CFLAGS) -c djibion-engine/core/djibion.c -o djibion-engine/core/djibion.o

diopion-engine/core/diopion.o: diopion-engine/core/diopion.c diopion-engine/core/diopion.h
	$(CC) $(CFLAGS) -c diopion-engine/core/diopion.c -o diopion-engine/core/diopion.o

diagnostion-engine/core/diagnostion.o: diagnostion-engine/core/diagnostion.c diagnostion-engine/core/diagnostion.h
	$(CC) $(CFLAGS) -c diagnostion-engine/core/diagnostion.c -o diagnostion-engine/core/diagnostion.o

memorion-engine/core/memorion.o: memorion-engine/core/memorion.c memorion-engine/core/memorion.h
	$(CC) $(CFLAGS) -c memorion-engine/core/memorion.c -o memorion-engine/core/memorion.o

orchestrion-engine/core/orchestrion.o: orchestrion-engine/core/orchestrion.c orchestrion-engine/core/orchestrion.h
	$(CC) $(CFLAGS) -c orchestrion-engine/core/orchestrion.c -o orchestrion-engine/core/orchestrion.o

calibrion-engine/core/calibrion.o: calibrion-engine/core/calibrion.c calibrion-engine/core/calibrion.h
	$(CC) $(CFLAGS) -c calibrion-engine/core/calibrion.c -o calibrion-engine/core/calibrion.o

compatibilion-engine/core/compatibilion.o: compatibilion-engine/core/compatibilion.c compatibilion-engine/core/compatibilion.h
	$(CC) $(CFLAGS) -c compatibilion-engine/core/compatibilion.c -o compatibilion-engine/core/compatibilion.o

evolvion-engine/core/evolvion.o: evolvion-engine/core/evolvion.c evolvion-engine/core/evolvion.h
	$(CC) $(CFLAGS) -c evolvion-engine/core/evolvion.c -o evolvion-engine/core/evolvion.o

synaption-engine/core/synaption.o: synaption-engine/core/synaption.c synaption-engine/core/synaption.h
	$(CC) $(CFLAGS) -c synaption-engine/core/synaption.c -o synaption-engine/core/synaption.o

conscience-engine/core/conscience.o: conscience-engine/core/conscience.c conscience-engine/core/conscience.h
	$(CC) $(CFLAGS) -c conscience-engine/core/conscience.c -o conscience-engine/core/conscience.o

neuralfs-engine/core/neuralfs.o: neuralfs-engine/core/neuralfs.c neuralfs-engine/core/neuralfs.h
	$(CC) $(CFLAGS) -c neuralfs-engine/core/neuralfs.c -o neuralfs-engine/core/neuralfs.o

ghost-engine/core/ghost.o: ghost-engine/core/ghost.c ghost-engine/core/ghost.h
	$(CC) $(CFLAGS) -c ghost-engine/core/ghost.c -o ghost-engine/core/ghost.o

immunion-engine/core/immunion.o: immunion-engine/core/immunion.c immunion-engine/core/immunion.h
	$(CC) $(CFLAGS) -c immunion-engine/core/immunion.c -o immunion-engine/core/immunion.o

dreamion-engine/core/dreamion.o: dreamion-engine/core/dreamion.c dreamion-engine/core/dreamion.h
	$(CC) $(CFLAGS) -c dreamion-engine/core/dreamion.c -o dreamion-engine/core/dreamion.o

symbion-engine/core/symbion.o: symbion-engine/core/symbion.c symbion-engine/core/symbion.h
	$(CC) $(CFLAGS) -c symbion-engine/core/symbion.c -o symbion-engine/core/symbion.o

collectivion-engine/core/collectivion.o: collectivion-engine/core/collectivion.c collectivion-engine/core/collectivion.h
	$(CC) $(CFLAGS) -c collectivion-engine/core/collectivion.c -o collectivion-engine/core/collectivion.o

metabion-engine/core/metabion.o: metabion-engine/core/metabion.c metabion-engine/core/metabion.h
	$(CC) $(CFLAGS) -c metabion-engine/core/metabion.c -o metabion-engine/core/metabion.o

cellion-engine/core/cellion.o: cellion-engine/core/cellion.c cellion-engine/core/cellion.h
	$(CC) $(CFLAGS) -c cellion-engine/core/cellion.c -o cellion-engine/core/cellion.o

morphion-engine/core/morphion.o: morphion-engine/core/morphion.c morphion-engine/core/morphion.h
	$(CC) $(CFLAGS) -c morphion-engine/core/morphion.c -o morphion-engine/core/morphion.o

pheromion-engine/core/pheromion.o: pheromion-engine/core/pheromion.c pheromion-engine/core/pheromion.h
	$(CC) $(CFLAGS) -c pheromion-engine/core/pheromion.c -o pheromion-engine/core/pheromion.o

$(REPL_SO): $(REPL_OBJS)
	ld $(LDFLAGS) $(REPL_OBJS) -o $(REPL_SO) $(LIBS)

$(TARGET): $(REPL_SO)
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
			-j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) $(REPL_SO) $(TARGET)

djiblas.o: djiblas.c djiblas.h
	$(CC) $(CFLAGS) -c djiblas.c -o djiblas.o

djiblas_avx2.o: djiblas_avx2.c djiblas.h
	$(CC) $(CFLAGS) -mavx2 -mfma -c djiblas_avx2.c -o djiblas_avx2.o

attention_avx2.o: attention_avx2.c
	$(CC) $(CFLAGS) -mavx2 -mfma -c attention_avx2.c -o attention_avx2.o

clean:
	rm -f $(REPL_OBJS) $(REPL_SO) $(TARGET) $(METABION_PROFILE_HDR)
	@echo "OK: Clean complete"

rebuild: clean all

genome:
	@python3 tools/oo_genome.py 2>/dev/null || python tools/oo_genome.py 2>/dev/null || true

test: all
	@echo "Creating bootable image..."
	@./create-boot-mtools.sh

