#include "gguf_loader.h"
#include "gguf_portable_uefi.h"

static EFI_STATUS map_portable_status(LlmkPortableStatus st) {
    switch (st) {
        case LLMK_PORTABLE_OK:
            return EFI_SUCCESS;
        case LLMK_PORTABLE_INVALID_ARGUMENT:
            return EFI_INVALID_PARAMETER;
        case LLMK_PORTABLE_IO_ERROR:
            return EFI_DEVICE_ERROR;
        case LLMK_PORTABLE_UNSUPPORTED:
        case LLMK_PORTABLE_NOT_IMPLEMENTED:
        default:
            return EFI_UNSUPPORTED;
    }
}

EFI_STATUS gguf_read_summary(EFI_FILE_HANDLE f, GgufSummary *out) {
    if (!f || !out) return EFI_INVALID_PARAMETER;

    // Best-effort init of the portable reader (preserves file position).
    LlmkPortableEfiReader efi_reader;
    EFI_STATUS st = llmk_portable_efi_init_reader(f, &efi_reader);
    if (EFI_ERROR(st)) return st;

    LlmkPortableReader reader = llmk_portable_efi_as_portable_reader(&efi_reader);
    LlmkPortableGgufSummary portable_summary;
    // Manual zero to avoid pulling in libc
    UINT8 *p = (UINT8 *)&portable_summary;
    for (UINTN i = 0; i < (UINTN)sizeof(portable_summary); i++) p[i] = 0;

    LlmkPortableStatus pst = llmk_portable_gguf_read_summary(&reader, &portable_summary);
    st = map_portable_status(pst);
    if (EFI_ERROR(st)) return st;

    // Manual zero out
    p = (UINT8 *)out;
    for (UINTN i = 0; i < (UINTN)sizeof(*out); i++) p[i] = 0;

    out->version = portable_summary.version;
    out->tensor_count = portable_summary.tensor_count;
    out->kv_count = portable_summary.kv_count;
    out->context_length = portable_summary.context_length;
    out->embedding_length = portable_summary.embedding_length;
    out->block_count = portable_summary.block_count;
    out->head_count = portable_summary.head_count;
    out->head_count_kv = portable_summary.head_count_kv;
    out->vocab_size = portable_summary.vocab_size;
    out->file_type = portable_summary.file_type;
    out->header_bytes = portable_summary.header_bytes;

    // Bounded string copies
    for (UINTN i = 0; i < (UINTN)sizeof(out->architecture) - 1 && portable_summary.architecture[i]; i++) {
        out->architecture[i] = portable_summary.architecture[i];
    }
    for (UINTN i = 0; i < (UINTN)sizeof(out->name) - 1 && portable_summary.name[i]; i++) {
        out->name[i] = portable_summary.name[i];
    }
    for (UINTN i = 0; i < (UINTN)sizeof(out->tokenizer_model) - 1 && portable_summary.tokenizer_model[i]; i++) {
        out->tokenizer_model[i] = portable_summary.tokenizer_model[i];
    }

    return EFI_SUCCESS;
}
