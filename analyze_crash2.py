#!/usr/bin/env python3
"""More detailed analysis: what's around .data+0x70 and what could point there at runtime."""
import struct, sys

ELF = r"C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal\llama2_repl.so"
TARGET = 0xCD070

with open(ELF, 'rb') as f:
    data = f.read()

# Parse minimal ELF
e_shoff = struct.unpack_from('<Q', data, 0x28)[0]
e_shentsize = struct.unpack_from('<H', data, 0x3A)[0]
e_shnum = struct.unpack_from('<H', data, 0x3C)[0]
e_shstrndx = struct.unpack_from('<H', data, 0x3E)[0]

sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh = struct.unpack_from('<IIQQQQIIQQ', data, off)
    sections.append({'sh_name':sh[0],'sh_type':sh[1],'sh_addr':sh[3],'sh_offset':sh[4],'sh_size':sh[5],'sh_entsize':sh[9],'sh_link':sh[6],'sh_info':sh[7]})

shstr_sec = sections[e_shstrndx]
shstr = data[shstr_sec['sh_offset']:shstr_sec['sh_offset']+shstr_sec['sh_size']]
for s in sections:
    end = shstr.index(b'\x00', s['sh_name'])
    s['name'] = shstr[s['sh_name']:end].decode('ascii','replace')

data_sec = next(s for s in sections if s['name'] == '.data')
text_sec = next(s for s in sections if s['name'] == '.text')

# Dump bytes around target
print("=== Bytes around .data+0x60 to +0x90 ===")
off = data_sec['sh_offset'] + 0x60
for i in range(0, 0x40, 16):
    bs = data[off+i:off+i+16]
    hex_str = ' '.join(f'{b:02x}' for b in bs)
    # Try to interpret as UCS-2
    try:
        txt = ''.join(chr(b) if 32<=b<127 else '.' for b in bs[0::2])
    except:
        txt = '?'
    print(f"  0x{TARGET-0x10+i:06x}: {hex_str}  [{txt}]")

print()

# Look at ALL RELA sections - find any entry where addend is close to TARGET
print("=== RELA entries with addend in range [0xCC000, 0xCE000] ===")
for s in sections:
    if s['sh_type'] != 4 or s['sh_entsize'] == 0:
        continue
    n = s['sh_size'] // s['sh_entsize']
    for i in range(n):
        eoff = s['sh_offset'] + i * s['sh_entsize']
        r_offset, r_info, r_addend = struct.unpack_from('<QQq', data, eoff)
        r_type = r_info & 0xFFFFFFFF
        r_sym = r_info >> 32
        if 0xCC000 <= r_addend <= 0xCE000:
            print(f"  [{s['name']}] offset=0x{r_offset:x} type={r_type} sym={r_sym} addend=0x{r_addend:x}")

print()
print("=== RELA R_X86_64_RELATIVE entries near target (0xCD000-0xCE000) ===")
rela_sec = next((s for s in sections if s['name'] == '.rela.dyn'), None)
if rela_sec:
    n = rela_sec['sh_size'] // rela_sec['sh_entsize']
    for i in range(n):
        eoff = rela_sec['sh_offset'] + i * rela_sec['sh_entsize']
        r_offset, r_info, r_addend = struct.unpack_from('<QQq', data, eoff)
        r_type = r_info & 0xFFFFFFFF
        if r_type == 8 and 0xCD000 <= r_addend <= 0xCE000:
            print(f"  offset=0x{r_offset:x} -> VMA 0x{r_addend:x}")
    print(f"  (searched {n} entries)")

# Find what's at .data+0 to .data+0x200 as potential string/pointer table
print()
print("=== First 512 bytes of .data (pointer table candidates) ===")
off = data_sec['sh_offset']
for i in range(0, 256, 8):  # 64-bit pointers
    val = struct.unpack_from('<Q', data, off+i)[0]
    if val != 0:
        # Is it in .text or .data range?
        if text_sec['sh_addr'] <= val <= text_sec['sh_addr'] + text_sec['sh_size']:
            region = '.text'
        elif data_sec['sh_addr'] <= val <= data_sec['sh_addr'] + data_sec['sh_size']:
            region = '.data'
        else:
            region = 'other'
        if region != 'other':
            print(f"  .data+0x{i:04x}: 0x{val:016x} [{region}]")
