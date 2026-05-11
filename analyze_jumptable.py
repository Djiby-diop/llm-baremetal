#!/usr/bin/env python3
"""
Analyze the voice loop jump table and check for corruption.
The jump table is at VMA 0x10e3b0 (_emotion_colors+0x30).
Assembly: lea 0xcf176(%rip),%rcx at IP 0x3f23a -> target = 0x3f23a+6 + 0xcf176 = 0x10e3b0
"""
import struct, sys

ELF_PATH = "/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2_repl.so"
data = open(ELF_PATH, "rb").read()

# Parse ELF section table to get .data offset
e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
e_shentsize = struct.unpack_from("<H", data, 0x3A)[0]
e_shnum = struct.unpack_from("<H", data, 0x3C)[0]
e_shstrndx = struct.unpack_from("<H", data, 0x3E)[0]

sections = {}
shstr_shdr = e_shoff + e_shstrndx * e_shentsize
shstr_off = struct.unpack_from("<Q", data, shstr_shdr + 24)[0]
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    name_off = struct.unpack_from("<I", data, off)[0]
    sh_vma = struct.unpack_from("<Q", data, off + 16)[0]
    sh_file_off = struct.unpack_from("<Q", data, off + 24)[0]
    sh_size = struct.unpack_from("<Q", data, off + 32)[0]
    name = data[shstr_off + name_off:].split(b'\x00')[0].decode('utf-8', errors='replace')
    sections[name] = (sh_vma, sh_file_off, sh_size)

data_vma, data_foff, data_size = sections[".data"]
text_vma, text_foff, text_size = sections[".text"]
text_end = text_vma + text_size

print(f".text: VMA=0x{text_vma:x}..0x{text_end:x}")
print(f".data: VMA=0x{data_vma:x} foff=0x{data_foff:x}")
print()

# Jump table location: VMA 0x10e3b0
JUMP_TABLE_VMA = 0x10e3b0
JUMP_TABLE_FOFF = data_foff + (JUMP_TABLE_VMA - data_vma)

print(f"Jump table @ VMA 0x{JUMP_TABLE_VMA:x} (file offset 0x{JUMP_TABLE_FOFF:x})")
print(f"Reading 6 entries (s_state 0..5):")
print()

for i in range(6):
    entry_foff = JUMP_TABLE_FOFF + i * 4
    rel_offset = struct.unpack_from("<i", data, entry_foff)[0]  # signed 32-bit
    target = JUMP_TABLE_VMA + rel_offset
    in_text = text_vma <= target < text_end
    in_data = data_vma <= target < data_vma + data_size
    region = "TEXT" if in_text else ("DATA!" if in_data else "UNKNOWN")
    print(f"  s_state={i}: rel={rel_offset:+d} (0x{rel_offset & 0xFFFFFFFF:08x}) -> target=0x{target:x} [{region}]")
    if region == "DATA!":
        print(f"  *** CORRUPTION: jump to .data+0x{target-data_vma:x} ***")

print()
# Also check if any entry matches crash address 0xcd070
CRASH_VMA = 0xcd070
for i in range(6):
    entry_foff = JUMP_TABLE_FOFF + i * 4
    rel_offset = struct.unpack_from("<i", data, entry_foff)[0]
    target = JUMP_TABLE_VMA + rel_offset
    if target == CRASH_VMA:
        print(f"*** FOUND CRASH: s_state={i} jumps to 0x{CRASH_VMA:x} ***")

# Also dump first 64 bytes of .data for reference
print()
print("First 128 bytes of .data (file bytes):")
for i in range(0, 128, 16):
    b = data[data_foff + i:data_foff + i + 16]
    hex_str = " ".join(f"{x:02x}" for x in b)
    asc_str = "".join(chr(x) if 32 <= x < 127 else "." for x in b)
    print(f"  .data+0x{i:03x}: {hex_str}  {asc_str}")
