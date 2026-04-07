// soma_dna_persist.h — SomaMind Phase O: DNA Persistence across reboots
//
// Saves/loads SomaDNA to soma_dna.bin on the EFI boot partition.
// Without persistence, all evolution (Phase N) is lost on every reboot.
//
// File format (64 bytes total):
//   Bytes 0-3   : magic  = 0x534F4D44 "SOMD"
//   Bytes 4-7   : version = 1
//   Bytes 8-135 : SomaDNA struct (128 bytes, packed)
//   Bytes 136-139: crc32 over bytes 0-135
//
// Auto-load at SomaMind init; auto-save after /dna_evolve_session.
// Freestanding C11 — no libc, no malloc.

#pragma once

#include "soma_dna.h"

#ifndef EFI_FILE_PROTOCOL
#include <efi.h>
#include <efilib.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// File format constants
// ============================================================
#define SOMA_DNA_PERSIST_MAGIC    0x534F4D44u   // "SOMD"
#define SOMA_DNA_PERSIST_VERSION  1u
#define SOMA_DNA_PERSIST_FILENAME L"soma_dna.bin"

// Total file size: 4 (magic) + 4 (version) + 128 (DNA) + 4 (crc32) = 140 bytes
#define SOMA_DNA_PERSIST_SIZE     140

// ============================================================
// Result codes
// ============================================================
#define SOMA_DNA_PERSIST_OK        0
#define SOMA_DNA_PERSIST_NOT_FOUND 1
#define SOMA_DNA_PERSIST_CORRUPT   2
#define SOMA_DNA_PERSIST_IO_ERR    3

// ============================================================
// API
// ============================================================

// Save DNA to soma_dna.bin (overwrites existing file).
// Returns SOMA_DNA_PERSIST_OK on success.
int soma_dna_save(const SomaDNA *dna, EFI_FILE_PROTOCOL *root);

// Load DNA from soma_dna.bin.
// Validates magic, version, and CRC32. If corrupt → returns CORRUPT.
// If not found → returns NOT_FOUND (dna unchanged).
// On success, writes loaded DNA into *dna and returns OK.
int soma_dna_load(SomaDNA *dna, EFI_FILE_PROTOCOL *root);

// Delete soma_dna.bin (useful for /dna_reset — start from defaults).
// Returns 1 if deleted, 0 if not found, -1 on error.
int soma_dna_delete(EFI_FILE_PROTOCOL *root);

// CRC32 over a byte buffer (freestanding, no lookup table needed).
uint32_t soma_dna_crc32(const void *buf, int len);

#ifdef __cplusplus
}
#endif
