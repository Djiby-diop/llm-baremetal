#!/bin/bash
ELF="/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2_repl.so"
echo "=== Symbols near .data+0x70 (VMA 0xCD000-0xCE000) ==="
objdump -t "$ELF" | awk '{ 
    for(i=1;i<=NF;i++) { 
        v=strtonum("0x" $i)
        if(v >= 0xcd000 && v <= 0xce000) { print; break }
    }
}' | sort | head -30

echo ""
echo "=== What calls address in .data range (indirect calls in .text) ==="
# Look for CALL with offset into .data section
objdump -d "$ELF" 2>/dev/null | grep -A2 "call.*\*" | head -30

echo ""
echo "=== Sections ==="
objdump -h "$ELF" 2>/dev/null | head -20
