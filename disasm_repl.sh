#!/bin/bash
ELF=/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2_repl.so

# Disassemble dreamion_tick at 0xc0fd0
echo "=== dreamion_tick @ 0xc0fd0 ==="
objdump -d --start-address=0xc0fd0 --stop-address=0xc0ff0 "$ELF" 2>/dev/null | grep -v "^$"

echo ""
echo "=== dreamion_tick_active @ 0xc0fe0 ==="
objdump -d --start-address=0xc0fe0 --stop-address=0xc1030 "$ELF" 2>/dev/null | grep -v "^$"

echo ""
echo "=== soma_vitals_tick @ 0x99fd0 ==="
objdump -d --start-address=0x99fd0 --stop-address=0x9a100 "$ELF" 2>/dev/null | grep -v "^$"

echo ""
echo "=== oo_organ_bus_tick @ 0x8f2d0 ==="
objdump -d --start-address=0x8f2d0 --stop-address=0x8f400 "$ELF" 2>/dev/null | grep -v "^$"

echo ""
echo "=== oo_voice_loop_tick @ 0x3f200 ==="
objdump -d --start-address=0x3f200 --stop-address=0x3f350 "$ELF" 2>/dev/null | grep -v "^$"
