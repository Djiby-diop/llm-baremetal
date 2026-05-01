#pragma once

#include <efi.h>
#include <efilib.h>
#include <stdint.h>

#include "gguf_portable.h"

typedef struct {
    EFI_FILE_HANDLE f;
    UINT64 file_size;
} LlmkPortableEfiReader;

// Initializes a portable reader backed by an EFI_FILE_HANDLE.
// Note: this function restores the file position to its original value.
EFI_STATUS llmk_portable_efi_init_reader(EFI_FILE_HANDLE f, LlmkPortableEfiReader *out);

// Populates an LlmkPortableReader that can be passed into llmk_portable_gguf_* functions.
LlmkPortableReader llmk_portable_efi_as_portable_reader(LlmkPortableEfiReader *reader);

