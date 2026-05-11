#!/usr/bin/env python3
"""
Analyze the REPL loop to find the bad function pointer call.
Strategy: scan .text for indirect CALL instructions that load from .data,
and also scan .data for 8-byte values that fall in the .text range.
"""
import struct, sys

ELF_PATH = "/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2_repl.so"

try:
    data = open(ELF_PATH, "rb").read()
except FileNotFoundError:
    print(f"ERROR: ELF not found at {ELF_PATH}")
    sys.exit(1)

# Parse ELF header
e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
e_phnum = struct.unpack_from("<H", data, 0x38)[0]
e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
e_shentsize = struct.unpack_from("<H", data, 0x3A)[0]
e_shnum = struct.unpack_from("<H", data, 0x3C)[0]
e_shstrndx = struct.unpack_from("<H", data, 0x3E)[0]

# Read section headers
sections = {}
shstr_shdr = e_shoff + e_shstrndx * e_shentsize
shstr_off = struct.unpack_from("<Q", data, shstr_shdr + 24)[0]

for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    name_off = struct.unpack_from("<I", data, off)[0]
    sh_type = struct.unpack_from("<I", data, off + 4)[0]
    sh_vma = struct.unpack_from("<Q", data, off + 16)[0]
    sh_file_off = struct.unpack_from("<Q", data, off + 24)[0]
    sh_size = struct.unpack_from("<Q", data, off + 32)[0]
    name = data[shstr_off + name_off:].split(b'\x00')[0].decode('utf-8', errors='replace')
    sections[name] = (sh_vma, sh_file_off, sh_size, sh_type)

text_vma, text_off, text_size, _ = sections.get(".text", (0, 0, 0, 0))
data_vma, data_foff, data_size, _ = sections.get(".data", (0, 0, 0, 0))

print(f".text: VMA=0x{text_vma:x} off=0x{text_off:x} size=0x{text_size:x}")
print(f".data: VMA=0x{data_vma:x} off=0x{data_foff:x} size=0x{data_size:x}")
print()

# CRASH target
CRASH_VMA = 0xCD070
print(f"Crash VMA in ELF: 0x{CRASH_VMA:x}")
print()

# Strategy 1: Scan .data for 8-byte pointers that might be function pointers
# (i.e., values in the .text range 0x16000 .. 0x16000+text_size)
text_end = text_vma + text_size
print(f"=== .data ptrs pointing INTO .text (0x{text_vma:x}..0x{text_end:x}) ===")
data_bytes = data[data_foff:data_foff + min(data_size, 0x1000)]  # first 4KB of .data
for i in range(0, len(data_bytes) - 8, 8):
    val = struct.unpack_from("<Q", data_bytes, i)[0]
    if text_vma <= val < text_end:
        vma = data_vma + i
        print(f"  .data+0x{i:04x} (VMA 0x{vma:x}) -> 0x{val:x} (TEXT)")
    elif data_vma <= val < data_vma + data_size:
        vma = data_vma + i
        print(f"  .data+0x{i:04x} (VMA 0x{vma:x}) -> 0x{val:x} (DATA!)")

print()

# Strategy 2: Find which CALL sites in .text use PC-relative address near .data
# Look for 0xFF followed by 0x15 (CALL [rip+disp]) where displacement lands in .data
print("=== CALL [rip+disp] instructions in .text pointing to .data ===")
text_bytes = data[text_off:text_off + text_size]
count = 0
for i in range(len(text_bytes) - 6):
    if text_bytes[i] == 0xFF and text_bytes[i+1] == 0x15:
        disp = struct.unpack_from("<i", text_bytes, i+2)[0]
        # RIP after CALL is i+6 (relative to .text start)
        rip = text_vma + i + 6
        target_addr = rip + disp  # address IN MEMORY where function pointer is stored
        if data_vma <= target_addr < data_vma + 0x2000:  # first 8KB of .data
            call_vma = text_vma + i
            print(f"  CALL at 0x{call_vma:x}: loads from 0x{target_addr:x} (.data+0x{target_addr-data_vma:x})")
            count += 1
            if count > 50:
                print("  ... (truncated)")
                break
print(f"  Total: {count}")
