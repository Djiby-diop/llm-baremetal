#pragma once
/*
 * oo-storage — Bare-Metal Storage for OO
 * ========================================
 * No libc. UEFI SimpleFileSystem protocol wrapper.
 *
 * Two tiers:
 *   Tier 1: EFI partition FAT32 via UEFI SimpleFileSystem (always available at boot)
 *   Tier 2: NVMe/AHCI direct MMIO (post-ExitBootServices, optional)
 *
 * OO-specific files on EFI partition:
 *   OO_MODEL.BIN     — Mamba weights (2.8B)
 *   OO_KV.BIN        — KV cache persist
 *   OO_DNA.BIN       — Hardware DNA + session DNA chain
 *   OO_JOURNAL.BIN   — Event journal
 *   OO_METADRV.BIN   — Meta-driver stubs (LLM-generated)
 *   OO_SOMA.BIN      — SomaMind cortex weights
 *   OO_HANDOFF.TXT   — IPC bridge to Batterfyl/Syrin
 *   OO_HANDOFF_RESP.TXT — Response from Syrin
 */

#ifndef OO_STORAGE_H
#define OO_STORAGE_H

#include <stdint.h>

#define OO_STORAGE_MAX_PATH   128
#define OO_STORAGE_MAX_OPEN   8

/* ── File handle ─────────────────────────────────────────────────── */
typedef struct {
    int      valid;
    int      slot;
    uint64_t size;
    uint64_t pos;
    void    *efi_handle;   /* EFI_FILE_PROTOCOL* (opaque) */
    char     path[OO_STORAGE_MAX_PATH];
} OoFile;

/* ── Storage context ─────────────────────────────────────────────── */
typedef struct {
    int   initialized;
    void *fs_proto;        /* EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* */
    void *root_dir;        /* EFI_FILE_PROTOCOL* for root */
    OoFile open_files[OO_STORAGE_MAX_OPEN];
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint32_t opens;
    uint32_t errors;
} OoStorageCtx;

/* ── API ─────────────────────────────────────────────────────────── */

int  oo_storage_init(OoStorageCtx *ctx, void *efi_image_handle,
                      void *efi_system_table);

int  oo_storage_open(OoStorageCtx *ctx, const char *path,
                      int write, OoFile *out);
int  oo_storage_read(OoStorageCtx *ctx, OoFile *f,
                      void *buf, uint64_t len, uint64_t *read_out);
int  oo_storage_write(OoStorageCtx *ctx, OoFile *f,
                       const void *buf, uint64_t len);
int  oo_storage_seek(OoStorageCtx *ctx, OoFile *f, uint64_t pos);
void oo_storage_close(OoStorageCtx *ctx, OoFile *f);
int  oo_storage_delete(OoStorageCtx *ctx, const char *path);

int64_t oo_storage_read_all(OoStorageCtx *ctx, const char *path,
                              void *buf, uint64_t max_len);
int     oo_storage_write_all(OoStorageCtx *ctx, const char *path,
                              const void *buf, uint64_t len);
int     oo_storage_exists(OoStorageCtx *ctx, const char *path);
uint64_t oo_storage_size(OoStorageCtx *ctx, const char *path);

void oo_storage_print(const OoStorageCtx *ctx);

#endif /* OO_STORAGE_H */
