#!/bin/bash
# Find all indirect CALL instructions in .text that could reach .data at runtime
ELF=/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2_repl.so

echo "=== REPL-related symbols in .text ==="
nm "$ELF" 2>/dev/null | grep -E "soma_repl|dreamion_tick|organ_bus_tick|voice_loop|vitals_tick|swarm_node|kbd_get|autorun" | sort -k3

echo ""
echo "=== All indirect CALL instructions in .text (CALL *reg or CALL *mem) ==="
objdump -d "$ELF" 2>/dev/null | grep -n "call\s*\*" | head -80
