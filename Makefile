# Makefile for Llama2 Bare-Metal UEFI (stable REPL build)
# Made in Senegal 🇸🇳

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
DJIBION_OBJ = djibion-engine/core/djibion.o
DIOPION_OBJ = diopion-engine/core/diopion.o
DIAGNOSTION_OBJ = diagnostion-engine/core/diagnostion.o
MEMORION_OBJ = memorion-engine/core/memorion.o
ORCHESTRION_OBJ = orchestrion-engine/core/orchestrion.o
CALIBRION_OBJ = calibrion-engine/core/calibrion.o
COMPATIBILION_OBJ = compatibilion-engine/core/compatibilion.o
REPL_OBJS = $(REPL_OBJ) $(DJIBION_OBJ) $(DIOPION_OBJ) $(DIAGNOSTION_OBJ) $(MEMORION_OBJ) $(ORCHESTRION_OBJ) $(CALIBRION_OBJ) $(COMPATIBILION_OBJ) llmk_zones.o llmk_log.o llmk_sentinel.o llmk_oo.o djiblas.o djiblas_avx2.o attention_avx2.o gguf_loader.o gguf_infer.o
REPL_SO  = llama2_repl.so

all: repl

repl: $(TARGET)
	@echo "✅ Build complete: $(TARGET)"
	@ls -lh $(TARGET)


# Rebuild when key headers change (Make doesn't auto-detect includes).
$(REPL_OBJ): $(REPL_SRC) djiblas.h interface.h
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
	rm -f *.o *.so $(TARGET)
	@echo "✅ Clean complete"

rebuild: clean all

test: all
	@echo "Creating bootable image..."
	@./create-boot-mtools.sh

