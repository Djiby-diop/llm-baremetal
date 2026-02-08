#pragma once

#include <efi.h>
#include <efilib.h>
#include <stdint.h>

typedef struct {
    UINT32 version;
    UINT64 tensor_count;
    UINT64 kv_count;

    // Common metadata (best-effort; may be empty/0)
    char architecture[64];
    char name[96];

    // LLaMA-ish keys (best-effort)
    UINT64 context_length;
    UINT64 embedding_length;
    UINT64 block_count;
    UINT64 head_count;
    UINT64 head_count_kv;
    UINT64 vocab_size;

    // tokenizer.ggml.model (string) if present
    char tokenizer_model[64];

    // general.file_type (u32/u64) if present
    UINT64 file_type;

    // For debugging / sanity
    UINT64 header_bytes; // bytes consumed through tensor info table
} GgufSummary;

// Reads GGUF header + KV metadata + tensor info table (best-effort).
// Leaves file position unspecified on return.
EFI_STATUS gguf_read_summary(EFI_FILE_HANDLE f, GgufSummary *out);
