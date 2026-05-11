#!/bin/bash
set -e
ELF=/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2_repl.so

echo "=== Sections ==="
objdump -h "$ELF" | grep -E "Idx|\.text|\.data|\.bss|\.rodata|\.reloc" 

echo ""
echo "=== Symbols near VMA 0xCD000-0xCE000 ==="
# objdump -t outputs: addr flags type size name
nm "$ELF" 2>/dev/null | awk '{
    v=strtonum("0x"$1)
    if(v >= 0xCD000 && v <= 0xCE000) printf "0x%06x  %s  %s\n", v, $2, $3
}' | sort

echo ""
echo "=== String symbols in .data ==="
nm "$ELF" 2>/dev/null | grep " D " | awk '{v=strtonum("0x"$1); if(v>=0xCD000 && v<=0xCE000) print}' | sort | head -20

echo "done"
