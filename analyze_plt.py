#!/usr/bin/env python3
"""Analyze PLT section and GOT entries in the ELF."""
import struct, sys

ELF_PATH = "/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2_repl.so"
data = open(ELF_PATH, "rb").read()

# Parse section table
e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
e_shentsize = struct.unpack_from("<H", data, 0x3A)[0]
e_shnum = struct.unpack_from("<H", data, 0x3C)[0]
e_shstrndx = struct.unpack_from("<H", data, 0x3E)[0]
shstr_off = struct.unpack_from("<Q", data, e_shoff + e_shstrndx * e_shentsize + 24)[0]

sections = {}
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    name_off = struct.unpack_from("<I", data, off)[0]
    sh_type = struct.unpack_from("<I", data, off + 4)[0]
    sh_vma = struct.unpack_from("<Q", data, off + 16)[0]
    sh_foff = struct.unpack_from("<Q", data, off + 24)[0]
    sh_size = struct.unpack_from("<Q", data, off + 32)[0]
    name = data[shstr_off + name_off:].split(b'\x00')[0].decode('utf-8', errors='replace')
    sections[name] = (sh_vma, sh_foff, sh_size, sh_type)

text_vma, text_foff, text_size, _ = sections[".text"]
data_vma, data_foff, data_size, _ = sections[".data"]
text_end = text_vma + text_size

print(f".text: 0x{text_vma:x}..0x{text_end:x}")
print()

# Show all sections for reference
print("All sections:")
for name, (vma, foff, size, stype) in sorted(sections.items(), key=lambda x: x[1][0]):
    if size > 0:
        print(f"  {name:30s} VMA=0x{vma:x} foff=0x{foff:x} size=0x{size:x}")
print()

# Check PLT bytes at ELF VMA 0xcbd30 (oo_scheduler_get_state@plt)
PLT_VMA = 0xcbd30
if PLT_VMA > text_end:
    # PLT is outside .text, find what section contains it
    plt_foff = PLT_VMA - text_vma + text_foff  # estimate
    print(f"PLT at 0x{PLT_VMA:x} is OUTSIDE .text (ends at 0x{text_end:x})")
    print(f"PLT bytes (raw ELF, offset 0x{plt_foff:x}):")
    b = data[plt_foff:plt_foff+32]
    print("  " + " ".join(f"{x:02x}" for x in b))
    
# BOT_GET_THREAT_LEVEL@plt and oo_scheduler_get_state@plt - check GOT entries  
print()
print("PLT stubs at ELF offsets:")
for plt_vma in [0xcbd30, 0xcbce0]:
    foff = plt_vma - text_vma + text_foff
    b = data[foff:foff+16]
    print(f"  PLT 0x{plt_vma:x}: {' '.join(f'{x:02x}' for x in b)}")
    # Decode: FF 25 XX XX XX XX = JMP [rip+disp]
    if b[0] == 0xFF and b[1] == 0x25:
        disp = struct.unpack_from("<i", b, 2)[0]
        rip_at_next = plt_vma + 6
        got_addr = rip_at_next + disp
        got_foff = got_addr - data_vma + data_foff
        if 0 <= got_foff < len(data):
            got_val = struct.unpack_from("<Q", data, got_foff)[0]
            print(f"    -> JMP [0x{got_addr:x}]  GOT entry = 0x{got_val:x}")
            in_text = text_vma <= got_val < text_end
            in_data = data_vma <= got_val < data_vma + data_size
            print(f"    -> GOT points to: {'TEXT' if in_text else 'DATA' if in_data else 'UNKNOWN'}")
        else:
            print(f"    -> GOT addr 0x{got_addr:x} out of range")
    else:
        print(f"    -> NOT FF 25 pattern!")
