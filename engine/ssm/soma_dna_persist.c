// soma_dna_persist.c — SomaMind Phase O: DNA Persistence
//
// Freestanding C11 — no libc, no malloc.

#include "soma_dna_persist.h"

// ─── CRC32 (nibble-based, no lookup table, ~30 bytes of code) ───────────────

uint32_t soma_dna_crc32(const void *buf, int len) {
    const unsigned char *p = (const unsigned char *)buf;
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++) {
        crc ^= (uint32_t)p[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;  // CRC32/ISO-HDLC polynomial
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// ─── File buffer layout ─────────────────────────────────────────────────────
//
//  [0..3]   magic   (u32 LE)
//  [4..7]   version (u32 LE)
//  [8..135] SomaDNA (128 bytes packed)
//  [136..139] crc32 over [0..135] (u32 LE)

static void write_u32_le(unsigned char *buf, int pos, uint32_t v) {
    buf[pos+0] = (unsigned char)(v & 0xFF);
    buf[pos+1] = (unsigned char)((v >> 8)  & 0xFF);
    buf[pos+2] = (unsigned char)((v >> 16) & 0xFF);
    buf[pos+3] = (unsigned char)((v >> 24) & 0xFF);
}

static uint32_t read_u32_le(const unsigned char *buf, int pos) {
    return ((uint32_t)buf[pos+0])
         | ((uint32_t)buf[pos+1] << 8)
         | ((uint32_t)buf[pos+2] << 16)
         | ((uint32_t)buf[pos+3] << 24);
}

// ─── soma_dna_save ───────────────────────────────────────────────────────────

int soma_dna_save(const SomaDNA *dna, EFI_FILE_PROTOCOL *root) {
    if (!dna || !root) return SOMA_DNA_PERSIST_IO_ERR;

    unsigned char buf[SOMA_DNA_PERSIST_SIZE];

    // Header
    write_u32_le(buf, 0, SOMA_DNA_PERSIST_MAGIC);
    write_u32_le(buf, 4, SOMA_DNA_PERSIST_VERSION);

    // Copy DNA struct (128 bytes)
    const unsigned char *src = (const unsigned char *)dna;
    for (int i = 0; i < 128; i++)
        buf[8 + i] = src[i];

    // CRC32 over bytes [0..135]
    uint32_t crc = soma_dna_crc32(buf, 136);
    write_u32_le(buf, 136, crc);

    // Open / create file
    EFI_FILE_HANDLE fh = NULL;
    UINT64 open_mode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_DNA_PERSIST_FILENAME, open_mode, 0ULL);
    if (EFI_ERROR(st) || !fh) return SOMA_DNA_PERSIST_IO_ERR;

    // Write from position 0
    uefi_call_wrapper(fh->SetPosition, 2, fh, (UINT64)0);

    UINTN sz = SOMA_DNA_PERSIST_SIZE;
    st = uefi_call_wrapper(fh->Write, 3, fh, &sz, buf);
    uefi_call_wrapper(fh->Close, 1, fh);

    if (EFI_ERROR(st)) return SOMA_DNA_PERSIST_IO_ERR;
    return SOMA_DNA_PERSIST_OK;
}

// ─── soma_dna_load ───────────────────────────────────────────────────────────

int soma_dna_load(SomaDNA *dna, EFI_FILE_PROTOCOL *root) {
    if (!dna || !root) return SOMA_DNA_PERSIST_IO_ERR;

    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_DNA_PERSIST_FILENAME,
                        EFI_FILE_MODE_READ, 0ULL);
    if (EFI_ERROR(st) || !fh) return SOMA_DNA_PERSIST_NOT_FOUND;

    unsigned char buf[SOMA_DNA_PERSIST_SIZE];
    UINTN sz = SOMA_DNA_PERSIST_SIZE;
    st = uefi_call_wrapper(fh->Read, 3, fh, &sz, buf);
    uefi_call_wrapper(fh->Close, 1, fh);

    if (EFI_ERROR(st) || sz < SOMA_DNA_PERSIST_SIZE)
        return SOMA_DNA_PERSIST_CORRUPT;

    // Validate magic
    uint32_t magic = read_u32_le(buf, 0);
    if (magic != SOMA_DNA_PERSIST_MAGIC) return SOMA_DNA_PERSIST_CORRUPT;

    // Validate version
    uint32_t ver = read_u32_le(buf, 4);
    if (ver != SOMA_DNA_PERSIST_VERSION) return SOMA_DNA_PERSIST_CORRUPT;

    // Validate CRC32
    uint32_t stored_crc = read_u32_le(buf, 136);
    uint32_t computed_crc = soma_dna_crc32(buf, 136);
    if (stored_crc != computed_crc) return SOMA_DNA_PERSIST_CORRUPT;

    // Copy DNA struct
    unsigned char *dst = (unsigned char *)dna;
    for (int i = 0; i < 128; i++)
        dst[i] = buf[8 + i];

    // Validate DNA content
    if (!soma_dna_validate(dna)) {
        // Restore safe defaults if content is internally invalid
        soma_dna_init_default(dna);
        return SOMA_DNA_PERSIST_CORRUPT;
    }

    return SOMA_DNA_PERSIST_OK;
}

// ─── soma_dna_delete ─────────────────────────────────────────────────────────

int soma_dna_delete(EFI_FILE_PROTOCOL *root) {
    if (!root) return -1;

    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_DNA_PERSIST_FILENAME,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0ULL);
    if (EFI_ERROR(st) || !fh) return 0;  // not found

    st = uefi_call_wrapper(fh->Delete, 1, fh);
    // Note: Delete closes the handle automatically on success.
    // On failure, we still need to close.
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(fh->Close, 1, fh);
        return -1;
    }
    return 1;
}
