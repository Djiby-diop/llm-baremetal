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

/* ── Scheduler / bot ─────────────────────────────────────────────────
 * oo_scheduler_get_state: 0 = HOMEOSTASIS_NORMAL
 * bot_get_threat_level:   0 = no threat detected
 */
int     oo_scheduler_get_state(void)  { return 0; }
uint8_t bot_get_threat_level(void)    { return 0; }

/* ── EBS phase hook ──────────────────────────────────────────────────── */
void efi_phase_exit_boot_services(void) {}

/* ── Evolution engine ────────────────────────────────────────────────── */
void evolution_init(void) {}
int  evolution_apply_mutation(const void *lora_weights, uint32_t size) {
    (void)lora_weights; (void)size; return 0;
}
void evolution_evaluate_fitness(uint32_t pattern_id, uint8_t success_rate) {
    (void)pattern_id; (void)success_rate;
}

/* ── GGUF K-quant dequantization stubs ──────────────────────────────── */
void oo_dequant_q4_k(const void *blocks, uint64_t n_blocks, float *out) {
    (void)blocks; (void)n_blocks; (void)out;
}
void oo_dequant_q5_k(const void *blocks, uint64_t n_blocks, float *out) {
    (void)blocks; (void)n_blocks; (void)out;
}
void oo_dequant_q6_k(const void *blocks, uint64_t n_blocks, float *out) {
    (void)blocks; (void)n_blocks; (void)out;
}

/* ── IDT gate stub ───────────────────────────────────────────────────── */
void oo_idt_set_gate(int vec, uint64_t handler, uint8_t type_attr) {
    (void)vec; (void)handler; (void)type_attr;
}

/* ── LLM inference stubs ─────────────────────────────────────────────── */
int oo_llm_infer_classify(void *ctx, const char *prompt, int plen,
                          char *out_token, int out_cap, float *confidence) {
    (void)ctx; (void)prompt; (void)plen;
    if (out_token && out_cap) out_token[0] = '\0';
    if (confidence) *confidence = 0.0f;
    return -1;
}
int oo_llm_infer_generate(void *ctx, const char *prompt, int plen,
                          char *out_text, int out_cap,
                          int max_tokens, float temperature) {
    (void)ctx; (void)prompt; (void)plen;
    (void)max_tokens; (void)temperature;
    if (out_text && out_cap) out_text[0] = '\0';
    return -1;
}

/* ── Network boot stubs ──────────────────────────────────────────────── */
unsigned long oo_netboot_http_get(void *ctx, const void *url, void *buf,
                                  unsigned long max_len, unsigned long *out_len) {
    (void)ctx; (void)url; (void)buf; (void)max_len;
    if (out_len) *out_len = 0;
    return 3UL; /* EFI_UNSUPPORTED */
}
unsigned long oo_netboot_http_post_json(void *ctx, const void *url,
                                        const void *json, void *resp,
                                        unsigned long resp_max) {
    (void)ctx; (void)url; (void)json;
    if (resp && resp_max) ((char *)resp)[0] = '\0';
    return 3UL; /* EFI_UNSUPPORTED */
}

/* ── NVMe stubs ──────────────────────────────────────────────────────── */
int oo_nvme_read_lba(void *ctx, uint64_t lba, uint32_t cnt, void *buf) {
    (void)ctx; (void)lba; (void)cnt; (void)buf; return -1;
}
int oo_nvme_write_lba(void *ctx, uint64_t lba, uint32_t cnt, const void *buf) {
    (void)ctx; (void)lba; (void)cnt; (void)buf; return -1;
}

/* ── Storage stubs ───────────────────────────────────────────────────── */
int oo_pressure_sample(void) { return 0; }
int oo_storage_exists(const char *path) { (void)path; return 0; }
int oo_storage_read_all(const char *path, void *buf, uint64_t cap,
                        uint64_t *out) {
    (void)path; (void)buf; (void)cap;
    if (out) *out = 0;
    return -1;
}
int oo_storage_write_all(const char *path, const void *buf, uint64_t len) {
    (void)path; (void)buf; (void)len; return -1;
}

/* ── REPL / task stubs ───────────────────────────────────────────────── */
void repl_register_cmd(const char *name, void *fn, const char *help) {
    (void)name; (void)fn; (void)help;
}
void oo_repl_native_run(void *ctx, const char *cmd) { (void)ctx; (void)cmd; }
void _task_wrapper(void) {}

/* ── Reflex module ───────────────────────────────────────────────────── */
void reflex_init(void)            {}
void reflex_on_thermal_burn(void) {}

/* ── Misc ────────────────────────────────────────────────────────────── */
void trigger_diop_sleep_learning(void) {}
void __stack_chk_fail(void) { for (;;) {} }

/* ── Render / sense / united-bus stubs ──────────────────────────────── */
void render_dna_helix(uint32_t tick, int x, int y, int h) {
    (void)tick; (void)x; (void)y; (void)h;
}
void sense_init(void) {}
void sense_transduce_keystroke(uint16_t scancode, uint16_t unicode) {
    (void)scancode; (void)unicode;
}
void united_bus_init(void) {}
void united_bus_absorb(uint32_t channel, const void *data, uint32_t len) {
    (void)channel; (void)data; (void)len;
}
void united_bus_pump(void) {}

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

/* ── Phase 5B: MMU stubs ────────────────────────────────────────────────── */
/* Real implementations in engine/kernel/oo_mmu.c; stubs for no-model path  */
unsigned long oo_mmu_init(void *ctx)                        { (void)ctx; return 0; }
unsigned long oo_mmu_build(void *ctx, const void *bs)       { (void)ctx; (void)bs; return 0; }
unsigned long oo_mmu_map(void *ctx, uint64_t v, uint64_t p,
                         uint64_t sz, uint64_t fl)          { (void)ctx;(void)v;(void)p;(void)sz;(void)fl; return 0; }
unsigned long oo_mmu_map_huge(void *ctx, uint64_t v,
                               uint64_t p, uint64_t fl)     { (void)ctx;(void)v;(void)p;(void)fl; return 0; }
unsigned long oo_mmu_identity_map(void *ctx, uint64_t sz)   { (void)ctx;(void)sz; return 0; }
unsigned long oo_mmu_map_fb(void *ctx, uint64_t phys,
                             uint64_t sz)                   { (void)ctx;(void)phys;(void)sz; return 0; }
void          oo_mmu_activate(void *ctx)                    { (void)ctx; }
uint64_t      oo_mmu_v2p(const void *ctx, uint64_t v)      { (void)ctx; return v; }
void          oo_mmu_print(const void *ctx)                 { (void)ctx; }
int           oo_mmu_repl_cmd(void *ctx, const void *bs,
                               const char *cmd)             { (void)ctx;(void)bs;(void)cmd; return 0; }

/* ── Phase 5E: Scheduler stubs ──────────────────────────────────────────── */
unsigned long oo_sched_init(void *s, uint32_t qms)          { (void)s;(void)qms; return 0; }
int           oo_sched_spawn(void *s, void *fn, void *arg,
                              const char *name)             { (void)s;(void)fn;(void)arg;(void)name; return -1; }
void          oo_yield(void)                                {}
void          oo_sched_tick(void)                           {}
void          oo_sched_run(void *s)                         { (void)s; }
void          oo_sched_print(const void *s)                 { (void)s; }
int           oo_sched_repl_cmd(void *s, const char *cmd)   { (void)s;(void)cmd; return 0; }

/* ── Phase 5H: GPU stubs ────────────────────────────────────────────────── */
unsigned long oo_gpu_init(void *g, void *st)                { (void)g;(void)st; return 0; }
void          oo_gpu_clear(void *g, uint32_t c)             { (void)g;(void)c; }
void          oo_gpu_put_pixel(void *g,int x,int y,uint32_t c){(void)g;(void)x;(void)y;(void)c;}
void          oo_gpu_fill_rect(void *g,int x,int y,int w,
                                int h,uint32_t c)           {(void)g;(void)x;(void)y;(void)w;(void)h;(void)c;}
void          oo_gpu_draw_hline(void *g,int x,int y,int l,
                                 uint32_t c)                {(void)g;(void)x;(void)y;(void)l;(void)c;}
void          oo_gpu_draw_vline(void *g,int x,int y,int l,
                                 uint32_t c)                {(void)g;(void)x;(void)y;(void)l;(void)c;}
void          oo_gpu_flip(void *g)                          { (void)g; }
void          oo_gpu_print(const void *g)                   { (void)g; }
int           oo_gpu_repl_cmd(void *g, const char *cmd)     { (void)g;(void)cmd; return 0; }

/* ── Phase 5G: Self-coding stubs ────────────────────────────────────────── */
unsigned long oo_coding_init(void *ctx)                     { (void)ctx; return 0; }
int           oo_coding_generate(void *ctx, const char *p,
                                  const char *f)            { (void)ctx;(void)p;(void)f; return 0; }
int           oo_coding_approve(void *ctx, const char *id)  { (void)ctx;(void)id; return 0; }
int           oo_coding_reject(void *ctx, const char *id)   { (void)ctx;(void)id; return 0; }
int           oo_coding_apply(void *ctx, const char *id)    { (void)ctx;(void)id; return 0; }
void          oo_coding_list_patches(const void *ctx)       { (void)ctx; }
void          oo_coding_print(const void *ctx)              { (void)ctx; }
int           oo_coding_repl_cmd(void *ctx, const char *cmd){ (void)ctx;(void)cmd; return 0; }

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
