// oo_neuralfs2_persist.h — Persistence for NeuralFS v2
//
// Handles saving/loading the Nfs2Store (~29KB) to/from FAT32.
// Freestanding C11 — no libc.

#pragma once

#include "oo_neuralfs2.h"
#include <efi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFS2_PERSIST_FILENAME L"OONFS2.BIN"

// Status codes
#define NFS2_PERSIST_OK        0
#define NFS2_PERSIST_IO_ERR   -1
#define NFS2_PERSIST_CORRUPT  -2
#define NFS2_PERSIST_NOT_FOUND -3

int nfs2_persist_save(const Nfs2Store *s, EFI_FILE_PROTOCOL *root);
int nfs2_persist_load(Nfs2Store *s, EFI_FILE_PROTOCOL *root);

#ifdef __cplusplus
}
#endif
