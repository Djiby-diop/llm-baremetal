// soma_export.h — SomaMind Phase K: Cortex Training Data Export
//
// Exports soma_journal entries as JSONL training data for oo-model.
// Written to soma_train.jsonl on the EFI boot partition.
//
// Format (one JSON object per line, UTF-8):
//   {"prompt":"...","response":"...","domain":N,"safety":N,"session":N,"turn":N}
//
// The file can be:
//   1. Read directly via USB by the oo-model training pipeline
//   2. Used to fine-tune the cortex 15M model on real kernel interactions
//   3. Accumulated across many sessions for continual learning
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include "soma_memory.h"
#include "soma_journal.h"

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
#define SOMA_EXPORT_FILENAME   L"soma_train.jsonl"
#define SOMA_EXPORT_MAX_LINE   384   // Max bytes per JSONL line

// ============================================================
// Export result
// ============================================================
typedef struct {
    int lines_written;    // Records exported this call
    int lines_skipped;    // Invalid entries skipped
    int appended;         // 1 = appended to existing file, 0 = created new
    int error;            // 1 = write error
} SomaExportResult;

// ============================================================
// API
// ============================================================

// Export current soma_memory ring buffer to soma_train.jsonl.
// If append=1, appends to existing file; if 0, overwrites.
// domain and safety_score per entry come from cortex if available,
// otherwise defaults (domain=0, safety=100).
SomaExportResult soma_export_write(const SomaMemCtx *mem,
                                   EFI_FILE_PROTOCOL *root,
                                   int append,
                                   int default_domain,
                                   int default_safety);

// Read the current line count from soma_train.jsonl (header scan).
// Returns number of newlines, -1 if file not found.
int soma_export_count(EFI_FILE_PROTOCOL *root);

#ifdef __cplusplus
}
#endif
