// soma_memory_persist.h — SomaMind Phase P: Session Memory Persistence
//
// Handles saving/loading the rolling session memory (SomaMemCtx)
// to/from a binary file on the FAT32 volume (UEFI EFI Partition).
//
// Freestanding C11 — no libc, no UEFI dependency in this header.

#pragma once

#include "soma_memory.h"
#include <efi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOMA_MEM_PERSIST_MAGIC    0x534D454Du  // "SMEM"
#define SOMA_MEM_PERSIST_VERSION  0x00000001u
#define SOMA_MEM_PERSIST_FILENAME L"OOMEMORY.BIN"

// Status codes
#define SOMA_MEM_PERSIST_OK        0
#define SOMA_MEM_PERSIST_IO_ERR   -1
#define SOMA_MEM_PERSIST_CORRUPT  -2
#define SOMA_MEM_PERSIST_NOT_FOUND -3

// Save current memory state to file. 
// root: open handle to the filesystem root directory.
int soma_memory_save(const SomaMemCtx *ctx, EFI_FILE_PROTOCOL *root);

// Load memory state from file into ctx.
// root: open handle to the filesystem root directory.
int soma_memory_load(SomaMemCtx *ctx, EFI_FILE_PROTOCOL *root);

// Delete the memory file from disk.
int soma_memory_delete(EFI_FILE_PROTOCOL *root);

#ifdef __cplusplus
}
#endif
