/* efi_compat.h — Minimal EFI type stubs for non-UEFI (host) builds
 *
 * When compiling soma_*.c on a host (Linux/Windows) without EDK2 headers,
 * include this header instead of <efi.h> / <efilib.h> to get just the
 * primitive typedefs needed by soma APIs.
 *
 * In a real UEFI build the EDK2 toolchain defines UEFI_BUILD and the
 * real <efi.h> / <efilib.h> are used instead.
 */
#ifndef EFI_COMPAT_H
#define EFI_COMPAT_H

#ifndef UEFI_BUILD

#include <stdint.h>
#include <stddef.h>

/* ── Basic EFI integer types ─────────────────────────────────────────────── */
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int8_t    INT8;
typedef int16_t   INT16;
typedef int32_t   INT32;
typedef int64_t   INT64;
typedef size_t    UINTN;
typedef ptrdiff_t INTN;
typedef int       BOOLEAN;
typedef uint64_t  EFI_STATUS;
typedef void *    EFI_HANDLE;
typedef uint16_t  CHAR16;

/* ── Minimal EFI_FILE_PROTOCOL stub ─────────────────────────────────────── */
#ifndef EFI_FILE_PROTOCOL_STUB
#define EFI_FILE_PROTOCOL_STUB

typedef struct _EFI_FILE_PROTOCOL {
    UINT64 Revision;
    void  *Open;
    void  *Close;
    void  *Delete;
    void  *Read;
    void  *Write;
    void  *GetPosition;
    void  *SetPosition;
    void  *GetInfo;
    void  *SetInfo;
    void  *Flush;
} EFI_FILE_PROTOCOL;

#define EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL  /* satisfy #ifndef EFI_FILE_PROTOCOL guards */

#endif /* EFI_FILE_PROTOCOL_STUB */

/* ── Extended EFI types (commonly used in OO kernel headers) ─────────────── */
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;
typedef UINT64 EFI_LBA;

/* Minimal EFI_BOOT_SERVICES stub — only fields used in OO headers */
typedef struct _EFI_BOOT_SERVICES {
    UINT64 Hdr;
    void  *RaiseTpl;
    void  *RestoreTpl;
    void  *AllocatePages;
    void  *FreePages;
    void  *GetMemoryMap;
    void  *AllocatePool;
    void  *FreePool;
    void  *Stall;
    void  *SetWatchdogTimer;
} EFI_BOOT_SERVICES;

/* EFI_FILE_HANDLE is an alias for EFI_FILE_PROTOCOL* */
typedef EFI_FILE_PROTOCOL * EFI_FILE_HANDLE;

/* File open mode flags */
#ifndef EFI_FILE_MODE_READ
#define EFI_FILE_MODE_READ    0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE   0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE  0x8000000000000000ULL
#endif
#ifndef EFI_FILE_HIDDEN
#define EFI_FILE_HIDDEN       0x0000000000000001ULL
#define EFI_FILE_SYSTEM       0x0000000000000002ULL
#define EFI_FILE_RESERVED     0x0000000000000004ULL
#define EFI_FILE_DIRECTORY    0x0000000000000010ULL
#define EFI_FILE_ARCHIVE      0x0000000000000020ULL
#endif

/* EFI_ERROR macro */
#ifndef EFI_ERROR
#define EFI_ERROR(s) ((UINT64)(s) & 0x8000000000000000ULL)
#endif
#ifndef EFI_SUCCESS
#define EFI_SUCCESS 0ULL
#endif
#ifndef EFI_NOT_FOUND
#define EFI_NOT_FOUND 0x8000000000000000ULL | 14ULL
#endif

/* ── Wide string helpers (stubs) ─────────────────────────────────────────── */
#ifndef Print
#define Print(fmt, ...) ((void)0)
#endif
#ifndef AllocatePool
#define AllocatePool(sz) ((void*)0)
#endif
#ifndef FreePool
#define FreePool(p)     ((void)(p))
#endif

#endif /* !UEFI_BUILD */
#endif /* EFI_COMPAT_H */
