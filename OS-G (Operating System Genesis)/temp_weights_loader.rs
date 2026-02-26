
fn try_read_weights_via_blockio_fat(
    con_out: *mut SimpleTextOutputProtocol,
    bs: &EfiBootServices,
    loaded_image: &EfiLoadedImageProtocol,
    out_ptr: *mut u8,
    out_len: usize,
) -> Option<usize> {
    // Acquire BlockIO from the device handle.
    let mut bio_ptr: *mut c_void = core::ptr::null_mut();
    let st = (bs.handle_protocol)(
        loaded_image.device_handle,
        &EFI_BLOCK_IO_PROTOCOL_GUID as *const EfiGuid,
        &mut bio_ptr,
    );
    if st != EFI_SUCCESS || bio_ptr.is_null() {
        uefi_print_status(con_out, "SOMA: BlockIO(handle_protocol)", st);
        return None;
    }
    let bio = bio_ptr as *mut EfiBlockIoProtocol;
    let media = unsafe { (*bio).media };
    if media.is_null() {
        return None;
    }
    let block_size = unsafe { (*media).block_size as usize };
    if block_size > 4096 {
        return None;
    }

    let mut sector = [0u8; 4096];
    let status0 = read_block(bio, 0, sector.as_mut_ptr(), sector.len());
    if status0 != EFI_SUCCESS {
        uefi_print_status(con_out, "SOMA: BlockIO(read LBA0)", status0);
        return None;
    }

    let parse_bpb = |vbr: &[u8]| -> Option<(u64, FatBpb)> {
        if vbr.len() < 90 {
            return None;
        }
        if vbr[510] != 0x55 || vbr[511] != 0xAA {
            return None;
        }
        let bytes_per_sector = u16::from_le_bytes([vbr[11], vbr[12]]) as u32;
        if bytes_per_sector != 512 && bytes_per_sector != 1024 && bytes_per_sector != 2048 && bytes_per_sector != 4096 {
            return None;
        }
        let sectors_per_cluster = vbr[13] as u32;
        if sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0 {
            return None;
        }
        let reserved = u16::from_le_bytes([vbr[14], vbr[15]]) as u32;
        let fats = vbr[16] as u32;
        let root_entries = u16::from_le_bytes([vbr[17], vbr[18]]) as u32;
        let total16 = u16::from_le_bytes([vbr[19], vbr[20]]) as u32;
        let fat16 = u16::from_le_bytes([vbr[22], vbr[23]]) as u32;
        let total32 = u32::from_le_bytes([vbr[32], vbr[33], vbr[34], vbr[35]]);
        let fat32 = u32::from_le_bytes([vbr[36], vbr[37], vbr[38], vbr[39]]);
        let root_cluster = u32::from_le_bytes([vbr[44], vbr[45], vbr[46], vbr[47]]);

        let fat_size = if fat16 != 0 { fat16 } else { fat32 };
        let total_sectors = if total16 != 0 { total16 } else { total32 };
        if fat_size == 0 || total_sectors == 0 {
            return None;
        }
        let root_dir_sectors = ((root_entries * 32) + (bytes_per_sector - 1)) / bytes_per_sector;
        let first_data_sector = reserved + fats * fat_size + root_dir_sectors;
        if total_sectors <= first_data_sector {
            return None;
        }
        let data_sectors = total_sectors - first_data_sector;
        let clusters = data_sectors / sectors_per_cluster;
        let fat_type = if clusters < 4085 {
            FatType::Fat12
        } else if clusters < 65525 {
            FatType::Fat16
        } else {
            FatType::Fat32
        };

        Some((0, FatBpb {
            bytes_per_sector,
            sectors_per_cluster,
            reserved_sectors: reserved,
            fats,
            fat_size_sectors: fat_size,
            first_data_sector,
            root_dir_sectors,
            fat_type,
            root_cluster,
        }))
    };

    // Try superfloppy VBR at LBA0 first.
    let mut part_lba: u64 = 0;
    let bpb: FatBpb;
    if let Some((_ignored, bb)) = parse_bpb(&sector[..block_size]) {
        bpb = bb;
    } else {
        // MBR: first partition entry at 0x1BE.
        if sector[510] != 0x55 || sector[511] != 0xAA {
            return None;
        }
        let p0 = 0x1BE;
        let lba_start = u32::from_le_bytes([sector[p0 + 8], sector[p0 + 9], sector[p0 + 10], sector[p0 + 11]]) as u64;
        if lba_start == 0 {
            return None;
        }
        part_lba = lba_start;
        let status_vbr = read_block(bio, part_lba, sector.as_mut_ptr(), sector.len());
        if status_vbr != EFI_SUCCESS {
            return None;
        }
        let Some((_ignored, bb)) = parse_bpb(&sector[..block_size]) else {
            return None;
        };
        bpb = bb;
    }

    if matches!(bpb.fat_type, FatType::Fat12) {
        return None;
    }

    // Read FAT into a scratch sector on demand.
    let fat_start_lba = part_lba + bpb.reserved_sectors as u64;
    let root_dir_lba = part_lba + (bpb.reserved_sectors + bpb.fats * bpb.fat_size_sectors) as u64;
    let data_start_lba = part_lba + bpb.first_data_sector as u64;

    let mut lfn_name = [0u16; 260];
    let mut lfn_valid = false;

    let mut check_entry = |entry: &[u8]| -> Option<(u32, u32)> {
        if entry.len() < 32 {
            return None;
        }
        let first = entry[0];
        if first == 0x00 {
            return None;
        }
        if first == 0xE5 {
            lfn_valid = false;
            return Some((0, 0));
        }
        let attr = entry[11];
        if attr == 0x0F {
            // LFN entry
            let seq = (entry[0] & 0x1F) as usize;
            if seq == 0 {
                return Some((0, 0));
            }
            if (entry[0] & 0x40) != 0 {
                // start
                for c in &mut lfn_name {
                    *c = 0;
                }
                lfn_valid = true;
            }
            if !lfn_valid {
                return Some((0, 0));
            }
            let base = (seq - 1) * 13;
            let mut put = |i: usize, lo: u8, hi: u8| {
                let idx = base + i;
                if idx < lfn_name.len() {
                    lfn_name[idx] = u16::from_le_bytes([lo, hi]);
                }
            };
            // name1 (5)
            put(0, entry[1], entry[2]);
            put(1, entry[3], entry[4]);
            put(2, entry[5], entry[6]);
            put(3, entry[7], entry[8]);
            put(4, entry[9], entry[10]);
            // name2 (6)
            put(5, entry[14], entry[15]);
            put(6, entry[16], entry[17]);
            put(7, entry[18], entry[19]);
            put(8, entry[20], entry[21]);
            put(9, entry[22], entry[23]);
            put(10, entry[24], entry[25]);
            // name3 (2)
            put(11, entry[28], entry[29]);
            put(12, entry[30], entry[31]);
            return Some((0, 0));
        }

        // Normal entry.
        let mut name_match = false;
        if lfn_valid {
            // Convert UCS-2 to ASCII for comparison.
            let mut tmp = [0u8; 64];
            let mut n = 0usize;
            for &ch in &lfn_name {
                if ch == 0x0000 || ch == 0xFFFF {
                    break;
                }
                if ch <= 0x7F && n < tmp.len() {
                    tmp[n] = ch as u8;
                    n += 1;
                } else {
                    n = 0;
                    break;
                }
            }
            if n > 0 {
                let s = core::str::from_utf8(&tmp[..n]).ok()?;
                if eq_ascii_ignore_case(s, "weights.bin") {
                    name_match = true;
                }
            }
        }
        lfn_valid = false;

        if !name_match {
            // Fallback 8.3 match: WEIGHTS.*
            let n0 = &entry[0..8];
            let e0 = &entry[8..11];
            if &n0[0..7] == b"WEIGHTS" {
                // accept any extension or check e0 == "BIN"
                let _ = e0;
                name_match = true;
            }
        }

        if !name_match {
            return Some((0, 0));
        }

        let hi = u16::from_le_bytes([entry[20], entry[21]]) as u32;
        let lo = u16::from_le_bytes([entry[26], entry[27]]) as u32;
        let first_cluster = if matches!(bpb.fat_type, FatType::Fat32) {
            (hi << 16) | lo
        } else {
            lo
        };
        let file_size = u32::from_le_bytes([entry[28], entry[29], entry[30], entry[31]]);
        Some((first_cluster, file_size))
    };

    let (mut first_cluster, file_size) = if matches!(bpb.fat_type, FatType::Fat16) {
        // FAT16 root dir is fixed region.
        let mut found: Option<(u32, u32)> = None;
        for s in 0..bpb.root_dir_sectors {
            let lba = root_dir_lba + s as u64;
            let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
            if st != EFI_SUCCESS {
                return None;
            }
            let mut off = 0usize;
            while off + 32 <= block_size {
                let e = &sector[off..off + 32];
                if e[0] == 0x00 {
                    break;
                }
                if let Some((cl, sz)) = check_entry(e) {
                    if cl != 0 && sz != 0 {
                        found = Some((cl, sz));
                        break;
                    }
                }
                off += 32;
            }
            if found.is_some() {
                break;
            }
        }
        found?
    } else {
        // FAT32: root dir is a cluster chain.
        let mut dir_cluster = bpb.root_cluster;
        let mut found: Option<(u32, u32)> = None;
        for _ in 0..1024 {
            if dir_cluster < 2 {
                break;
            }
            let first_sector = data_start_lba + ((dir_cluster as u64 - 2) * bpb.sectors_per_cluster as u64);
            for sc in 0..bpb.sectors_per_cluster {
                let lba = first_sector + sc as u64;
                let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
                if st != EFI_SUCCESS {
                    return None;
                }
                let mut off = 0usize;
                while off + 32 <= block_size {
                    let e = &sector[off..off + 32];
                    if e[0] == 0x00 {
                        break;
                    }
                    if let Some((cl, sz)) = check_entry(e) {
                        if cl != 0 && sz != 0 {
                            found = Some((cl, sz));
                            break;
                        }
                    }
                    off += 32;
                }
                if found.is_some() {
                    break;
                }
            }
            if found.is_some() {
                break;
            }
            // next cluster in dir
            dir_cluster = fat_next_cluster(con_out, bio, &bpb, fat_start_lba, block_size, dir_cluster)?;
            if is_eoc(&bpb, dir_cluster) {
                break;
            }
        }
        found?
    };

    if first_cluster < 2 {
        return None;
    }
    
    uefi_print(con_out, "SOMA: found weights cluster=");
    uefi_print_u64_dec(con_out, first_cluster as u64);
    uefi_print(con_out, " size=");
    uefi_print_u64_dec(con_out, file_size as u64);
    uefi_print(con_out, "\n");

    let mut remaining = core::cmp::min(file_size as usize, out_len);
    let mut written = 0usize;
    while remaining > 0 {
        if is_eoc(&bpb, first_cluster) {
            break;
        }
        let first_sector = data_start_lba + ((first_cluster as u64 - 2) * bpb.sectors_per_cluster as u64);
        for sc in 0..bpb.sectors_per_cluster {
            if remaining == 0 {
                break;
            }
            let lba = first_sector + sc as u64;
            let st = read_block(bio, lba, sector.as_mut_ptr(), sector.len());
            if st != EFI_SUCCESS {
                return None;
            }
            let take = core::cmp::min(remaining, block_size);
            unsafe {
                core::ptr::copy_nonoverlapping(sector.as_ptr(), out_ptr.add(written), take);
            }
            written += take;
            remaining -= take;
        }
        let next = fat_next_cluster(con_out, bio, &bpb, fat_start_lba, block_size, first_cluster)?;
        first_cluster = next;
    }

    Some(written)
}
