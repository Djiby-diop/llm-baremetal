// soma_export.c — SomaMind Phase K: Cortex Training Data Export
//
// Writes soma_journal ring buffer entries as JSONL to soma_train.jsonl.
// Freestanding C11 — no libc, no sprintf. All formatting is manual.

#include "soma_export.h"

// ─── Helpers ────────────────────────────────────────────────────────────────

static int exp_strnlen(const char *s, int max) {
    int n = 0; while (n < max && s[n]) n++; return n;
}

// Write decimal integer to buf, return chars written
static int exp_itoa(int v, char *buf, int buflen) {
    if (buflen < 2) return 0;
    if (v < 0) { buf[0] = '-'; int r = exp_itoa(-v, buf+1, buflen-1); return r > 0 ? r+1 : 0; }
    if (v == 0) { buf[0] = '0'; return 1; }
    char tmp[12]; int n = 0;
    while (v > 0 && n < 11) { tmp[n++] = '0' + (v % 10); v /= 10; }
    for (int i = 0; i < n && i < buflen-1; i++) buf[i] = tmp[n-1-i];
    return n;
}

// Append string to buf at pos, return new pos
static int exp_puts(char *buf, int pos, int cap, const char *s, int len) {
    for (int i = 0; i < len && pos < cap-1; i++) buf[pos++] = s[i];
    return pos;
}

// Escape a string for JSON: replace \ → \\, " → \", control chars → space
static int exp_json_str(char *buf, int pos, int cap, const char *s, int maxsrc) {
    int len = exp_strnlen(s, maxsrc);
    if (pos >= cap-2) return pos;
    buf[pos++] = '"';
    for (int i = 0; i < len && pos < cap-3; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"')       { buf[pos++] = '\\'; buf[pos++] = '"'; }
        else if (c == '\\') { buf[pos++] = '\\'; buf[pos++] = '\\'; }
        else if (c < 32)    { buf[pos++] = ' '; }  // control → space
        else                { buf[pos++] = (char)c; }
    }
    if (pos < cap-1) buf[pos++] = '"';
    return pos;
}

// ─── soma_export_write ──────────────────────────────────────────────────────

SomaExportResult soma_export_write(const SomaMemCtx *mem,
                                   EFI_FILE_PROTOCOL *root,
                                   int append,
                                   int default_domain,
                                   int default_safety) {
    SomaExportResult res = {0, 0, 0, 0};
    if (!mem || !root) { res.error = 1; return res; }

    // Open file (create or append)
    UINT64 open_mode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_EXPORT_FILENAME, open_mode, 0ULL);
    if (EFI_ERROR(st) || !fh) { res.error = 1; return res; }

    if (append) {
        // Seek to end of file
        UINT64 end = (UINT64)-1;
        uefi_call_wrapper(fh->SetPosition, 2, fh, end);
        res.appended = 1;
    }

    // Iterate ring buffer oldest-first
    int count = mem->count;
    if (count > SOMA_MEM_MAX_ENTRIES) count = SOMA_MEM_MAX_ENTRIES;

    for (int i = 0; i < count; i++) {
        int idx = (mem->head - count + i + SOMA_MEM_MAX_ENTRIES * 64) % SOMA_MEM_MAX_ENTRIES;
        const SomaMemEntry *e = &mem->entries[idx];
        if (!e->valid) { res.lines_skipped++; continue; }

        // Build JSONL line manually
        char line[SOMA_EXPORT_MAX_LINE];
        int p = 0;
        int cap = SOMA_EXPORT_MAX_LINE;

        p = exp_puts(line, p, cap, "{", 1);

        // "prompt":
        p = exp_puts(line, p, cap, "\"prompt\":", 9);
        p = exp_json_str(line, p, cap, e->prompt, SOMA_MEM_PROMPT_LEN);
        p = exp_puts(line, p, cap, ",", 1);

        // "response":
        p = exp_puts(line, p, cap, "\"response\":", 11);
        p = exp_json_str(line, p, cap, e->response, SOMA_MEM_RESPONSE_LEN);
        p = exp_puts(line, p, cap, ",", 1);

        // "domain":N,
        p = exp_puts(line, p, cap, "\"domain\":", 9);
        { char nb[8]; int nl = exp_itoa(default_domain, nb, 8);
          p = exp_puts(line, p, cap, nb, nl); }
        p = exp_puts(line, p, cap, ",", 1);

        // "safety":N,
        p = exp_puts(line, p, cap, "\"safety\":", 9);
        { char nb[8]; int nl = exp_itoa(default_safety, nb, 8);
          p = exp_puts(line, p, cap, nb, nl); }
        p = exp_puts(line, p, cap, ",", 1);

        // "session":N,
        p = exp_puts(line, p, cap, "\"session\":", 10);
        { char nb[8]; int nl = exp_itoa(mem->boot_count, nb, 8);
          p = exp_puts(line, p, cap, nb, nl); }
        p = exp_puts(line, p, cap, ",", 1);

        // "turn":N
        p = exp_puts(line, p, cap, "\"turn\":", 7);
        { char nb[8]; int nl = exp_itoa(e->turn, nb, 8);
          p = exp_puts(line, p, cap, nb, nl); }

        p = exp_puts(line, p, cap, "}\n", 2);

        // Write line
        UINTN sz = (UINTN)p;
        st = uefi_call_wrapper(fh->Write, 3, fh, &sz, line);
        if (EFI_ERROR(st)) { res.error = 1; break; }
        res.lines_written++;
    }

    uefi_call_wrapper(fh->Close, 1, fh);
    return res;
}

// ─── soma_export_count ──────────────────────────────────────────────────────

int soma_export_count(EFI_FILE_PROTOCOL *root) {
    if (!root) return -1;

    EFI_FILE_HANDLE fh = NULL;
    EFI_STATUS st = uefi_call_wrapper(root->Open, 5, root, &fh,
                        (CHAR16 *)SOMA_EXPORT_FILENAME,
                        EFI_FILE_MODE_READ, 0ULL);
    if (EFI_ERROR(st) || !fh) return -1;

    // Count newlines by reading in 512-byte chunks
    int newlines = 0;
    char buf[512];
    while (1) {
        UINTN sz = sizeof(buf);
        st = uefi_call_wrapper(fh->Read, 3, fh, &sz, buf);
        if (EFI_ERROR(st) || sz == 0) break;
        for (UINTN i = 0; i < sz; i++)
            if (buf[i] == '\n') newlines++;
    }
    uefi_call_wrapper(fh->Close, 1, fh);
    return newlines;
}
