#include "gguf_portable_uefi.h"

static size_t llmk_portable_efi_read_at(void *user_data, uint64_t offset, void *dst, size_t nbytes) {
    LlmkPortableEfiReader *r = (LlmkPortableEfiReader *)user_data;
    if (!r || !r->f || (!dst && nbytes > 0)) return 0;

    EFI_STATUS st = uefi_call_wrapper(r->f->SetPosition, 2, r->f, (UINT64)offset);
    if (EFI_ERROR(st)) return 0;

    UINTN want = (UINTN)nbytes;
    st = uefi_call_wrapper(r->f->Read, 3, r->f, &want, dst);
    if (EFI_ERROR(st)) return 0;
    return (size_t)want;
}

static uint64_t llmk_portable_efi_size(void *user_data) {
    LlmkPortableEfiReader *r = (LlmkPortableEfiReader *)user_data;
    if (!r) return 0;
    return (uint64_t)r->file_size;
}

EFI_STATUS llmk_portable_efi_init_reader(EFI_FILE_HANDLE f, LlmkPortableEfiReader *out) {
    EFI_STATUS st;
    UINT64 saved_pos = 0;
    UINT64 end_pos = 0;

    if (!f || !out) return EFI_INVALID_PARAMETER;
    out->f = f;
    out->file_size = 0;

    st = uefi_call_wrapper(f->GetPosition, 2, f, &saved_pos);
    if (EFI_ERROR(st)) return st;

    st = uefi_call_wrapper(f->SetPosition, 2, f, (UINT64)-1);
    if (EFI_ERROR(st)) return st;

    st = uefi_call_wrapper(f->GetPosition, 2, f, &end_pos);
    if (EFI_ERROR(st)) return st;

    // restore
    st = uefi_call_wrapper(f->SetPosition, 2, f, saved_pos);
    if (EFI_ERROR(st)) return st;

    out->file_size = end_pos;
    return EFI_SUCCESS;
}

LlmkPortableReader llmk_portable_efi_as_portable_reader(LlmkPortableEfiReader *reader) {
    LlmkPortableReader r;
    r.user_data = reader;
    r.read_at = llmk_portable_efi_read_at;
    r.size = llmk_portable_efi_size;
    return r;
}

