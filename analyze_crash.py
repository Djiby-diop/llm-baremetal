#!/usr/bin/env python3
"""Find what's pointing to .data+0x70 (VMA 0xCD070) in the ELF binary."""
import struct, sys

ELF = r"C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal\llama2_repl.so"
TARGET = 0xCD070  # VMA of crash point

with open(ELF, 'rb') as f:
    data = f.read()

# Parse ELF header
magic = data[0:4]
assert magic == b'\x7fELF', "Not ELF"
ei_class = data[4]  # 2 = 64-bit
assert ei_class == 2

# ELF64 header fields
e_shoff = struct.unpack_from('<Q', data, 0x28)[0]
e_shentsize = struct.unpack_from('<H', data, 0x3A)[0]
e_shnum = struct.unpack_from('<H', data, 0x3C)[0]
e_shstrndx = struct.unpack_from('<H', data, 0x3E)[0]

# Section headers
sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh = struct.unpack_from('<IIQQQQIIQQ', data, off)
    sections.append({
        'sh_name': sh[0], 'sh_type': sh[1], 'sh_flags': sh[2],
        'sh_addr': sh[3], 'sh_offset': sh[4], 'sh_size': sh[5],
        'sh_link': sh[6], 'sh_info': sh[7], 'sh_addralign': sh[8], 'sh_entsize': sh[9]
    })

# Get string table
shstr_off = sections[e_shstrndx]['sh_offset']
shstr_size = sections[e_shstrndx]['sh_size']
shstr = data[shstr_off:shstr_off + shstr_size]

def get_name(idx):
    end = shstr.index(b'\x00', idx)
    return shstr[idx:end].decode('ascii', errors='replace')

# Find .rela sections and .data section
for s in sections:
    s['name'] = get_name(s['sh_name'])

data_sec = next((s for s in sections if s['name'] == '.data'), None)
print(f".data: addr=0x{data_sec['sh_addr']:x}, size=0x{data_sec['sh_size']:x}")
print(f"TARGET: VMA=0x{TARGET:x}, .data offset=0x{TARGET - data_sec['sh_addr']:x}")

# Print bytes at target
toff = data_sec['sh_offset'] + (TARGET - data_sec['sh_addr'])
bs = data[toff:toff+16]
print(f"Bytes at target: {' '.join(f'{b:02x}' for b in bs)}")
try:
    txt = bs.decode('utf-16-le', errors='replace')
    print(f"As UCS-2: {txt!r}")
except:
    pass

# Search all RELA sections for addend == TARGET
print(f"\nSearching for RELA entries with addend=0x{TARGET:x}...")
for s in sections:
    if s['sh_type'] != 4:  # SHT_RELA = 4
        continue
    if s['sh_entsize'] == 0:
        continue
    n = s['sh_size'] // s['sh_entsize']
    for i in range(n):
        eoff = s['sh_offset'] + i * s['sh_entsize']
        r_offset, r_info, r_addend = struct.unpack_from('<QQq', data, eoff)
        # For R_X86_64_RELATIVE (type=8), addend IS the target VMA
        r_type = r_info & 0xFFFFFFFF
        r_sym = r_info >> 32
        if r_type == 8 and r_addend == TARGET:  # R_X86_64_RELATIVE
            print(f"  RELA {s['name']}: offset=0x{r_offset:x} type=R_X86_64_RELATIVE addend=0x{r_addend:x}")

# Also search for the raw bytes 70 D0 0C 00 00 00 00 00 (little-endian 0xCD070)
print(f"\nSearching for raw VMA value 0x{TARGET:x} in .data section...")
target_bytes = struct.pack('<Q', TARGET)
search_off = data_sec['sh_offset']
search_end = search_off + data_sec['sh_size']
idx = data.find(target_bytes, search_off, search_end)
while idx != -1 and idx < search_end:
    vma = data_sec['sh_addr'] + (idx - search_off)
    print(f"  Found at file offset 0x{idx:x}, VMA 0x{vma:x}")
    idx = data.find(target_bytes, idx + 1, search_end)
