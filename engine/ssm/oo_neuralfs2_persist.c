// oo_neuralfs2_persist.c — Persistence for NeuralFS v2
//
// Freestanding C11 — no libc, no malloc.

#include "oo_neuralfs2_persist.h"

int nfs2_persist_save(const Nfs2Store *s, EFI_FILE_PROTOCOL *root) {
    if (!s || !root) return NFS2_PERSIST_IO_ERR;

    EFI_FILE_HANDLE fh = NULL;
    UINT64 open_mode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)NFS2_PERSIST_FILENAME, open_mode, 0ULL);
    if (EFI_ERROR(st) || !fh) return NFS2_PERSIST_IO_ERR;

    uefi_call_wrapper(fh->SetPosition, 2, fh, (UINT64)0);

    UINTN sz = sizeof(Nfs2Store);
    st = uefi_call_wrapper(fh->Write, 3, fh, &sz, (void*)s);
    uefi_call_wrapper(fh->Close, 1, fh);

    if (EFI_ERROR(st)) return NFS2_PERSIST_IO_ERR;
    return NFS2_PERSIST_OK;
}

int nfs2_persist_load(Nfs2Store *s, EFI_FILE_PROTOCOL *root) {
    if (!s || !root) return NFS2_PERSIST_IO_ERR;

    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)NFS2_PERSIST_FILENAME,
                        EFI_FILE_MODE_READ, 0ULL);
    if (EFI_ERROR(st) || !fh) return NFS2_PERSIST_NOT_FOUND;

    UINTN sz = sizeof(Nfs2Store);
    st = uefi_call_wrapper(fh->Read, 3, fh, &sz, (void*)s);
    uefi_call_wrapper(fh->Close, 1, fh);

    if (EFI_ERROR(st) || sz < sizeof(Nfs2Store))
        return NFS2_PERSIST_CORRUPT;
    
    if (s->magic != NFS2_MAGIC)
        return NFS2_PERSIST_CORRUPT;

    return NFS2_PERSIST_OK;
}
