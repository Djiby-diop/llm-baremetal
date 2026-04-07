// soma_journal.h — SomaMind Phase I: Persistent Session Journal
//
// Persists soma_memory ring buffer to a binary file on the EFI partition
// (soma_journal.bin). Loaded at boot, saved periodically and on demand.
//
// This lets the kernel remember past interactions across reboots — the
// minimal foundation for continuous learning and oo-model training data.
//
// File format (little-endian):
//   [SomaJournalHeader  32 bytes]
//   [SomaJournalEntry × entry_count]  each 216 bytes
//
// Freestanding C11 — only depends on soma_memory.h and EFI headers.

#pragma once

#include "soma_memory.h"

// EFI forward declaration — included by the god file before this header
#ifndef EFI_FILE_PROTOCOL
#include <efi.h>
#include <efilib.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================
#define SOMA_JOURNAL_MAGIC          0x534A524Eu  // "SJRN"
#define SOMA_JOURNAL_VERSION        1u
#define SOMA_JOURNAL_MAX_ENTRIES    64           // Keep last 64 turns on disk
#define SOMA_JOURNAL_AUTOSAVE_EVERY 4            // Auto-save every N new turns
#define SOMA_JOURNAL_FILENAME       L"soma_journal.bin"

// ============================================================
// Disk header (32 bytes, packed)
// ============================================================
typedef struct __attribute__((packed)) {
    unsigned int  magic;          // SOMA_JOURNAL_MAGIC
    unsigned int  version;        // SOMA_JOURNAL_VERSION
    unsigned int  entry_count;    // Valid entries in this file
    unsigned int  total_turns;    // Cumulative turns ever recorded (all sessions)
    unsigned int  boot_count;     // Sessions so far (including this one)
    unsigned int  reserved[3];    // Pad to 32 bytes
} SomaJournalHeader;

// ============================================================
// Disk entry (216 bytes, packed)
// Matches SomaMemEntry fields + session tag + flags
// ============================================================
typedef struct __attribute__((packed)) {
    char         prompt[SOMA_MEM_PROMPT_LEN];       // 80 bytes
    char         response[SOMA_MEM_RESPONSE_LEN];   // 120 bytes
    int          turn;                               // Turn index within session
    unsigned int session;                            // boot_count when recorded
    unsigned int prompt_hash;                        // djb2 hash
    unsigned int flags;                              // 0x1 = valid
} SomaJournalEntry;

// ============================================================
// Stats (populated by soma_journal_load/save)
// ============================================================
typedef struct {
    int loaded;           // Entries loaded into ring buffer
    int saved;            // Entries written to disk
    int total_turns;      // From header: cumulative turns
    int boot_count;       // From header: session count
    int error;            // 1 = last operation failed
} SomaJournalStats;

// ============================================================
// API
// ============================================================

// Load soma_journal.bin → populate soma_memory via soma_memory_record().
// Sets ctx->boot_count from journal (adds 1 for current session).
// Returns number of entries loaded, -1 on file-not-found, -2 on error.
int soma_journal_load(SomaMemCtx *mem, EFI_FILE_PROTOCOL *root,
                      unsigned int *total_turns_out);

// Save current ring buffer → soma_journal.bin (overwrite).
// Returns number of entries written, -1 on error.
int soma_journal_save(const SomaMemCtx *mem, EFI_FILE_PROTOCOL *root,
                      unsigned int total_turns_ever);

// Delete soma_journal.bin from EFI partition.
// Returns 0 on success, -1 on error.
int soma_journal_clear(EFI_FILE_PROTOCOL *root);

// Populate a stats struct from a journal file (header only, no mem changes).
int soma_journal_read_stats(EFI_FILE_PROTOCOL *root, SomaJournalStats *out);

#ifdef __cplusplus
}
#endif
