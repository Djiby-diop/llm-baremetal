// soma_journal.c — SomaMind Phase I: Persistent Session Journal
//
// EFI file I/O: reads/writes soma_journal.bin on the boot partition.
// Uses the EFI SimpleFileSystem root handle (g_root) passed in by caller.
//
// Freestanding C11 — no libc. All loops/copies manual.

#include "soma_journal.h"

// ─── Helpers ────────────────────────────────────────────────────────────────

static void jrnl_memset(void *dst, int val, int n) {
    unsigned char *p = (unsigned char *)dst;
    for (int i = 0; i < n; i++) p[i] = (unsigned char)val;
}

static void jrnl_memcpy(void *dst, const void *src, int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static int jrnl_strnlen(const char *s, int max) {
    int n = 0;
    while (n < max && s[n]) n++;
    return n;
}

// Copy at most (dstlen-1) chars, always NUL-terminate
static void jrnl_strlcpy(char *dst, const char *src, int dstlen) {
    int i = 0;
    while (i < dstlen - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// ─── soma_journal_save ──────────────────────────────────────────────────────

int soma_journal_save(const SomaMemCtx *mem, EFI_FILE_PROTOCOL *root,
                      unsigned int total_turns_ever) {
    if (!mem || !root) return -1;

    // Open (or create) soma_journal.bin for write
    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_JOURNAL_FILENAME,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                        0ULL);
    if (EFI_ERROR(st) || !fh) return -1;

    // Collect valid entries from ring buffer (oldest-first order)
    SomaJournalEntry disk_entries[SOMA_JOURNAL_MAX_ENTRIES];
    jrnl_memset(disk_entries, 0, sizeof(disk_entries));
    int n = 0;

    // Ring buffer: head points to next-write slot.
    // Oldest entry is at (head - count + MAX) % MAX, newest at (head-1+MAX)%MAX
    int count = mem->count;
    if (count > SOMA_MEM_MAX_ENTRIES) count = SOMA_MEM_MAX_ENTRIES;
    int max_save = (count < SOMA_JOURNAL_MAX_ENTRIES) ? count : SOMA_JOURNAL_MAX_ENTRIES;

    for (int i = 0; i < max_save; i++) {
        // oldest first: index from tail
        int idx = (mem->head - count + i + SOMA_MEM_MAX_ENTRIES * 64) % SOMA_MEM_MAX_ENTRIES;
        const SomaMemEntry *e = &mem->entries[idx];
        if (!e->valid) continue;

        SomaJournalEntry *de = &disk_entries[n];
        jrnl_strlcpy(de->prompt,   e->prompt,   SOMA_MEM_PROMPT_LEN);
        jrnl_strlcpy(de->response, e->response, SOMA_MEM_RESPONSE_LEN);
        de->turn        = e->turn;
        de->session     = (unsigned int)mem->boot_count;
        de->prompt_hash = e->prompt_hash;
        de->flags       = 0x1u;
        n++;
    }

    // Write header
    SomaJournalHeader hdr;
    jrnl_memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = SOMA_JOURNAL_MAGIC;
    hdr.version     = SOMA_JOURNAL_VERSION;
    hdr.entry_count = (unsigned int)n;
    hdr.total_turns = total_turns_ever;
    hdr.boot_count  = (unsigned int)mem->boot_count;

    UINTN sz = sizeof(SomaJournalHeader);
    st = uefi_call_wrapper(fh->Write, 3, fh, &sz, &hdr);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(fh->Close, 1, fh);
        return -1;
    }

    // Write entries
    if (n > 0) {
        sz = (UINTN)(n * (int)sizeof(SomaJournalEntry));
        st = uefi_call_wrapper(fh->Write, 3, fh, &sz, disk_entries);
        if (EFI_ERROR(st)) {
            uefi_call_wrapper(fh->Close, 1, fh);
            return -1;
        }
    }

    uefi_call_wrapper(fh->Close, 1, fh);
    return n;
}

// ─── soma_journal_load ──────────────────────────────────────────────────────

int soma_journal_load(SomaMemCtx *mem, EFI_FILE_PROTOCOL *root,
                      unsigned int *total_turns_out) {
    if (!mem || !root) return -2;

    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_JOURNAL_FILENAME,
                        EFI_FILE_MODE_READ, 0ULL);
    if (EFI_ERROR(st) || !fh) return -1;  // file not found

    // Read header
    SomaJournalHeader hdr;
    jrnl_memset(&hdr, 0, sizeof(hdr));
    UINTN sz = sizeof(SomaJournalHeader);
    st = uefi_call_wrapper(fh->Read, 3, fh, &sz, &hdr);
    if (EFI_ERROR(st) || sz != sizeof(SomaJournalHeader)) {
        uefi_call_wrapper(fh->Close, 1, fh);
        return -2;
    }

    // Validate
    if (hdr.magic != SOMA_JOURNAL_MAGIC || hdr.version != SOMA_JOURNAL_VERSION) {
        uefi_call_wrapper(fh->Close, 1, fh);
        return -2;
    }

    if (total_turns_out) *total_turns_out = hdr.total_turns;

    // Restore boot_count from journal (will be incremented by soma_memory_init)
    // We set it here so init sees prior sessions
    if (hdr.boot_count > 0)
        mem->boot_count = (int)hdr.boot_count;

    // Read entries
    int count = (int)hdr.entry_count;
    if (count > SOMA_JOURNAL_MAX_ENTRIES) count = SOMA_JOURNAL_MAX_ENTRIES;

    int loaded = 0;
    for (int i = 0; i < count; i++) {
        SomaJournalEntry de;
        jrnl_memset(&de, 0, sizeof(de));
        sz = sizeof(SomaJournalEntry);
        st = uefi_call_wrapper(fh->Read, 3, fh, &sz, &de);
        if (EFI_ERROR(st) || sz != sizeof(SomaJournalEntry)) break;
        if (!(de.flags & 0x1u)) continue;

        // Re-inject into ring buffer via soma_memory_record
        // Use turn = -(i+1) as sentinel for "historical" (not this session)
        soma_memory_record(mem, de.prompt, de.response);
        // Override turn to preserve original (record sets total_turns, patch it)
        if (mem->count > 0) {
            int prev_slot = (mem->head - 1 + SOMA_MEM_MAX_ENTRIES) % SOMA_MEM_MAX_ENTRIES;
            mem->entries[prev_slot].turn = de.turn;
        }
        loaded++;
    }

    uefi_call_wrapper(fh->Close, 1, fh);
    return loaded;
}

// ─── soma_journal_clear ─────────────────────────────────────────────────────

int soma_journal_clear(EFI_FILE_PROTOCOL *root) {
    if (!root) return -1;

    // Open + truncate to zero by overwriting with empty header
    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_JOURNAL_FILENAME,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                        0ULL);
    if (EFI_ERROR(st) || !fh) return -1;

    // Write zeroed header (entry_count=0 signals empty)
    SomaJournalHeader hdr;
    jrnl_memset(&hdr, 0, sizeof(hdr));
    hdr.magic   = SOMA_JOURNAL_MAGIC;
    hdr.version = SOMA_JOURNAL_VERSION;
    UINTN sz = sizeof(SomaJournalHeader);
    uefi_call_wrapper(fh->Write, 3, fh, &sz, &hdr);
    // Truncate to header size
    uefi_call_wrapper(fh->SetPosition, 2, fh, (UINT64)sz);
    uefi_call_wrapper(fh->Close, 1, fh);
    return 0;
}

// ─── soma_journal_read_stats ────────────────────────────────────────────────

int soma_journal_read_stats(EFI_FILE_PROTOCOL *root, SomaJournalStats *out) {
    if (!root || !out) return -1;
    jrnl_memset(out, 0, sizeof(*out));

    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_JOURNAL_FILENAME,
                        EFI_FILE_MODE_READ, 0ULL);
    if (EFI_ERROR(st) || !fh) { out->error = 1; return -1; }

    SomaJournalHeader hdr;
    jrnl_memset(&hdr, 0, sizeof(hdr));
    UINTN sz = sizeof(SomaJournalHeader);
    st = uefi_call_wrapper(fh->Read, 3, fh, &sz, &hdr);
    uefi_call_wrapper(fh->Close, 1, fh);

    if (EFI_ERROR(st) || hdr.magic != SOMA_JOURNAL_MAGIC) {
        out->error = 1;
        return -1;
    }

    out->loaded      = (int)hdr.entry_count;  // approx
    out->total_turns = (int)hdr.total_turns;
    out->boot_count  = (int)hdr.boot_count;
    return 0;
}
