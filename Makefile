# Makefile — Operating Organism Public Prototype
# Builds: llama2.efi (UEFI x86_64 EFI application)

CC      = gcc
LD      = ld
OBJCOPY = objcopy

EFI_INC = /usr/include/efi
EFI_LIB = /usr/lib

CFLAGS  = -ffreestanding -fno-stack-protector -fpic -fshort-wchar \
          -mno-red-zone -I$(EFI_INC) -I$(EFI_INC)/x86_64 \
          -DEFI_FUNCTION_WRAPPER -O2 -msse2 -DUEFI_BUILD=1

LDFLAGS = -nostdlib -znocombreloc \
          -T $(EFI_LIB)/elf_x86_64_efi.lds \
          -shared -Bsymbolic -L$(EFI_LIB)

SRCS = efi_main.c \
       core/llmk_zones.c

OBJS = $(SRCS:.c=.o)

all: llama2.efi

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

llama2.so: $(OBJS)
	$(LD) $(LDFLAGS) $(EFI_LIB)/crt0-efi-x86_64.o $(OBJS) \
	    -o $@ -lefi -lgnuefi

llama2.efi: llama2.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	    -j .rel -j .rela -j .reloc \
	    --target=efi-app-x86_64 $< $@
	@echo "OK: llama2.efi built"

clean:
	rm -f $(OBJS) llama2.so llama2.efi

.PHONY: all clean
