/*
 * llmk_stubs.c — Weak stub implementations for undefined symbols.
 *
 * These are minimal no-op / safe-default implementations for functions that
 * are declared and called but not yet fully implemented in a separate
 * compilation unit. Weak linkage means real implementations override these
 * automatically when they are added to the build.
 *
 * Freestanding C11. No libc dependency (stdint.h / stdarg.h only).
 */

#include <stdint.h>
#include <stdarg.h>

/* ── uart stubs (used by oo_swarm_node.c / oo_swarm_sync.c / orchestrion_ci.c) */

void oo_uart_print_str(const char *s)    { (void)s; }
void oo_uart_print_hex32(unsigned int v) { (void)v; }
void soma_uart_emit(const char *s)       { (void)s; }

/* ── soma dream pulse stub ──────────────────────────────────────────────
 * SomaDreamSummary is { int result; int ticks_taken; float final_pressure;
 *                       char fail_reason[64]; } — we return a zeroed struct.
 * sizeof matches regardless of include path thanks to the layout being trivial.
 */
typedef struct {
    int   result;
    int   ticks_taken;
    float final_pressure;
    char  fail_reason[64];
} StubSomaDreamSummary;

StubSomaDreamSummary soma_dream_pulse(void *m, void *obj)
{
    (void)m; (void)obj;
    StubSomaDreamSummary s;
    for (int i = 0; i < (int)sizeof(s); i++) ((unsigned char *)&s)[i] = 0;
    return s;
}

/* ── collectivion ───────────────────────────────────────────────────── */

void collectivion_broadcast(void *e, const void *data, uint32_t len)
{
    (void)e; (void)data; (void)len;
}

/* ── D+ policy helpers ──────────────────────────────────────────────── */

const char *dplus_mode_name(int mode)
{
    (void)mode;
    return "UNKNOWN";
}

/* ── soma warden D+ interface ───────────────────────────────────────── */

int soma_warden_dplus_status_str(const void *w, char *buf, int buflen)
{
    (void)w;
    if (buf && buflen > 0) buf[0] = '\0';
    return 0;
}

void soma_warden_dplus_reset(void *w, void *router)
{
    (void)w; (void)router;
}

/* ── affective engine stubs ─────────────────────────────────────────── */

void limbion_format_context(void *e, char *buf, int sz)
{
    (void)e;
    if (buf && sz > 0) buf[0] = '\0';
}

void trophion_format_context(void *e, char *buf, int sz)
{
    (void)e;
    if (buf && sz > 0) buf[0] = '\0';
}

int mirrorion_flush_jsonl(void *e, char *buf, int sz)
{
    (void)e;
    if (buf && sz > 0) buf[0] = '\0';
    return 0;
}

/* ── OIT LoRA ───────────────────────────────────────────────────────── */

void oit_lora_apply_global(float *vec, int dim)
{
    (void)vec; (void)dim;
}

/* ── OO message bus ─────────────────────────────────────────────────── */

void oo_msg_init(void *bus) { (void)bus; }

/* ── ASCII / string utilities ───────────────────────────────────────── */

/*
 * llmk_ascii_from_i32 — write decimal int into buffer, no libc.
 * Returns number of characters written (not including NUL).
 */
int llmk_ascii_from_i32(char *buf, int bufsz, int val)
{
    if (!buf || bufsz <= 0) return 0;
    char tmp[12];
    int  neg = 0;
    unsigned u;
    if (val < 0) { neg = 1; u = (unsigned)(-(val + 1)) + 1u; }
    else          { u = (unsigned)val; }
    int i = 0;
    do { tmp[i++] = (char)('0' + (int)(u % 10u)); u /= 10u; } while (u && i < 11);
    if (neg && i < 11) tmp[i++] = '-';
    int len = (i < bufsz - 1) ? i : bufsz - 1;
    for (int j = 0; j < len; j++) buf[j] = tmp[len - 1 - j];
    buf[len] = '\0';
    return len;
}

/*
 * llmk_ascii_strstr — freestanding strstr over plain ASCII.
 */
char *llmk_ascii_strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return (char *)0;
}

/* ── Tick counter ───────────────────────────────────────────────────── */

uint64_t llmk_get_ticks(void) { return 0; }

/* ── NFS save trigger ───────────────────────────────────────────────── */

void llmk_oo_trigger_nfs_save(void) {}

/* ── Portable GGUF reader stubs ─────────────────────────────────────── */

void llmk_portable_efi_init_reader(void *reader, void *buf, uint64_t len)
{
    (void)reader; (void)buf; (void)len;
}

void *llmk_portable_efi_as_portable_reader(void *efi_reader)
{
    (void)efi_reader;
    return (void *)0;
}

int llmk_portable_gguf_read_summary(void *reader, void *summary)
{
    (void)reader; (void)summary;
    return -1;
}

/* ── Minimal snprintf ────────────────────────────────────────────────
 *
 * my_snprintf(buf, n, fmt, ...) — supports %d, %u, %s, %%, nothing else.
 * Returns number of characters that would have been written (like snprintf).
 */
static int _stub_puts(char *dst, int rem, const char *s)
{
    int n = 0;
    while (*s && rem > 0) { *dst++ = *s++; n++; rem--; }
    return n;
}

static int _stub_puti(char *dst, int rem, long val, int is_unsigned)
{
    char tmp[22];
    unsigned long u;
    int neg = 0, i = 0;
    if (!is_unsigned && val < 0) { neg = 1; u = (unsigned long)(-(val + 1)) + 1ul; }
    else { u = (unsigned long)val; }
    do { tmp[i++] = (char)('0' + (int)(u % 10ul)); u /= 10ul; } while (u);
    if (neg) tmp[i++] = '-';
    int len = i, n = 0;
    while (len-- > 0 && rem > 0) { *dst++ = tmp[len]; n++; rem--; }
    return n;
}

int my_snprintf(char *buf, uint64_t n, const char *fmt, ...)
{
    if (!buf || n == 0) return 0;
    va_list ap;
    va_start(ap, fmt);
    int rem = (int)n - 1, total = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (rem > 0) { buf[total] = *fmt; rem--; }
            total++;
            continue;
        }
        fmt++;
        if (!*fmt) break;
        switch (*fmt) {
        case 'd': { int v = va_arg(ap, int); int w = _stub_puti(buf + total, rem, (long)v, 0); total += w; rem -= w; break; }
        case 'u': { unsigned v = va_arg(ap, unsigned); int w = _stub_puti(buf + total, rem, (long)(unsigned long)v, 1); total += w; rem -= w; break; }
        case 's': { const char *v = va_arg(ap, const char *); if (!v) v = "(null)"; int w = _stub_puts(buf + total, rem, v); total += w; rem -= w; break; }
        case '%': if (rem > 0) { buf[total] = '%'; rem--; } total++; break;
        default:  (void)va_arg(ap, int); break;
        }
    }
    buf[total < (int)n ? total : (int)n - 1] = '\0';
    va_end(ap);
    return total;
}
