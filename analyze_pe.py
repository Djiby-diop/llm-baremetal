#!/usr/bin/env python3
"""Parse the PE binary to check what sections are mapped and if PLT is included."""
import struct, sys

PE_PATH = "/mnt/c/Users/djibi/OneDrive/Bureau/baremetal/llm-baremetal/llama2.efi"
data = open(PE_PATH, "rb").read()

# DOS header
if data[:2] != b'MZ':
    print("Not a PE file")
    sys.exit(1)

pe_off = struct.unpack_from("<I", data, 0x3C)[0]
if data[pe_off:pe_off+4] != b'PE\x00\x00':
    print("PE signature not found")
    sys.exit(1)

# COFF header
machine = struct.unpack_from("<H", data, pe_off+4)[0]
num_sections = struct.unpack_from("<H", data, pe_off+6)[0]
opt_hdr_size = struct.unpack_from("<H", data, pe_off+20)[0]

print(f"Machine: 0x{machine:x}, Sections: {num_sections}, OPT header size: {opt_hdr_size}")

# Optional header
opt_off = pe_off + 24
magic = struct.unpack_from("<H", data, opt_off)[0]
print(f"PE magic: 0x{magic:x} ({'PE32+' if magic==0x20b else 'PE32'})")

entry_rva = struct.unpack_from("<I", data, opt_off+16)[0]
image_base = struct.unpack_from("<Q", data, opt_off+24)[0]
section_alignment = struct.unpack_from("<I", data, opt_off+32)[0]
print(f"ImageBase: 0x{image_base:x}, EntryRVA: 0x{entry_rva:x}, SectAlign: 0x{section_alignment:x}")
print()

# Sections
sections_off = opt_off + opt_hdr_size
print(f"{'Name':12} {'VMA':>12} {'RawOff':>10} {'RawSz':>10} {'VirtSz':>10}")
print("-" * 65)
for i in range(num_sections):
    off = sections_off + i * 40
    name = data[off:off+8].split(b'\x00')[0].decode('utf-8', errors='replace').rstrip('\x00')
    virtual_size = struct.unpack_from("<I", data, off+8)[0]
    virtual_addr = struct.unpack_from("<I", data, off+12)[0]
    raw_size = struct.unpack_from("<I", data, off+16)[0]
    raw_off = struct.unpack_from("<I", data, off+20)[0]
    print(f"{name:12} 0x{virtual_addr:010x} 0x{raw_off:08x} 0x{raw_size:08x} 0x{virtual_size:08x}")

print()
# Check if PLT (ELF VMA 0xcbc90) is covered by any PE section
PLT_ELF_VMA = 0xcbc90
print(f"PLT ELF VMA: 0x{PLT_ELF_VMA:x}")
print("Checking which PE section covers PLT range...")
for i in range(num_sections):
    off = sections_off + i * 40
    name = data[off:off+8].split(b'\x00')[0].decode('utf-8', errors='replace')
    virt_sz = struct.unpack_from("<I", data, off+8)[0]
    virt_addr = struct.unpack_from("<I", data, off+12)[0]
    raw_sz = struct.unpack_from("<I", data, off+16)[0]
    raw_off = struct.unpack_from("<I", data, off+20)[0]
    if virt_addr <= PLT_ELF_VMA < virt_addr + max(virt_sz, raw_sz):
        print(f"  FOUND: section '{name.strip()}' covers 0x{PLT_ELF_VMA:x}")
        # Read the PLT bytes at runtime
        plt_raw_off = raw_off + (PLT_ELF_VMA - virt_addr)
        plt_bytes = data[plt_raw_off:plt_raw_off+32]
        print(f"  PLT bytes in PE: {' '.join(f'{b:02x}' for b in plt_bytes)}")
