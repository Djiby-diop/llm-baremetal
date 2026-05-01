#!/bin/bash
cd /mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal
gcc -fsyntax-only -ffreestanding -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
    -I/usr/include/efi -I/usr/include/efi/x86_64 -DEFI_FUNCTION_WRAPPER \
    -Icore -Iengine/llama2 -Iengine/gguf -Iengine/djiblas -Iengine/ssm -Ioo-modules \
    -O2 -msse2 -DDJIBLAS_DISABLE_CPUID=1 -DLLMB_BUILD_ID='L"test"' \
    engine/llama2/llama2_efi_final.c 2>&1 | grep -i error | head -30
echo "EXIT=$?"
