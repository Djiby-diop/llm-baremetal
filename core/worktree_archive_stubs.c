#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <stdint.h>

int ShowCyberpunkSplash(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    (void)ImageHandle;
    (void)SystemTable;
    return 0;
}

void dreamion_init(void *engine) { (void)engine; }
void dreamion_set_mode(void *engine, int mode) { (void)engine; (void)mode; }
void dreamion_tick(void *engine, uint32_t idle_cycles) { (void)engine; (void)idle_cycles; }
void dreamion_tick_active(void *engine, uint32_t active_cycles) { (void)engine; (void)active_cycles; }
int dreamion_has_dna_mutation(const void *engine) { (void)engine; return 0; }
int dreamion_pop_dna_mutation(void *engine, float *bias_delta, float *temp_delta) {
    (void)engine;
    if (bias_delta) *bias_delta = 0.0f;
    if (temp_delta) *temp_delta = 0.0f;
    return 0;
}
void dreamion_record_inference(void *engine, const char *prompt, const char *response, uint32_t tokens, uint32_t score) {
    (void)engine; (void)prompt; (void)response; (void)tokens; (void)score;
}
int dreamion_step(void *engine) { (void)engine; return 0; }
uint32_t dreamion_flush_jsonl(void *engine, void *root) { (void)engine; (void)root; return 0; }
const char *dreamion_mode_name_ascii(int mode) { (void)mode; return "off"; }

void ghost_init(void *engine) { (void)engine; }
void ghost_set_mode(void *engine, int mode) { (void)engine; (void)mode; }
const char *ghost_mode_name_ascii(int mode) { (void)mode; return "off"; }

void neuralfs_init(void *engine) { (void)engine; }
void neuralfs_set_mode(void *engine, int mode) { (void)engine; (void)mode; }
uint32_t neuralfs_query(void *engine, const char *query, void *out, uint32_t max_out) {
    (void)engine; (void)query; (void)out; (void)max_out; return 0;
}
const char *neuralfs_mode_name_ascii(int mode) { (void)mode; return "off"; }

void morphion_init(void *engine) { (void)engine; }
void morphion_set_mode(void *engine, int mode) { (void)engine; (void)mode; }
void morphion_probe(void *engine) { (void)engine; }
const char *morphion_mode_name_ascii(int mode) { (void)mode; return "off"; }

void collectivion_init(void *engine) { (void)engine; }
void collectivion_set_mode(void *engine, int mode) { (void)engine; (void)mode; }
const char *collectivion_mode_name_ascii(int mode) { (void)mode; return "off"; }
int collectivion_broadcast_thought(void *engine, const char *text, uint32_t flags) {
    (void)engine; (void)text; (void)flags; return 0;
}

void cellion_init(void *engine) { (void)engine; }
int cellion_perceive(void *engine, const void *bytes, uint32_t len) {
    (void)engine; (void)bytes; (void)len; return 0;
}
int cellion_wasm_find_custom_section(const void *wasm, uint32_t wasm_len, const char *name, const uint8_t **section, uint32_t *section_len) {
    (void)wasm; (void)wasm_len; (void)name;
    if (section) *section = NULL;
    if (section_len) *section_len = 0;
    return 0;
}

void oo_bus_init(void) {}
void oo_bus_post(uint32_t channel, const void *payload, uint32_t len) { (void)channel; (void)payload; (void)len; }
void oo_module_table_tick_all(void *boot_ctx, uint32_t step) { (void)boot_ctx; (void)step; }
void oo_pressure_apply(void *signal) { (void)signal; }
int oo_repl_try(const char *line) { (void)line; return 0; }
void repl_register_builtin_cmds(void) {}
void *repl_find_cmd(const char *name) { (void)name; return NULL; }
void repl_ctx_init_defaults(void *ctx) { (void)ctx; }
void repl_ctx_bind_globals(void *ctx) { (void)ctx; }
void repl_ctx_flush_globals(void *ctx) { (void)ctx; }

EFI_STATUS oo_netboot_http_get(void *ctx, const CHAR8 *url, void *buf, UINTN max_len, UINTN *out_len) {
    (void)ctx; (void)url; (void)buf; (void)max_len;
    if (out_len) *out_len = 0;
    return EFI_UNSUPPORTED;
}
EFI_STATUS oo_netboot_http_post_json(void *ctx, const CHAR8 *url, const CHAR8 *json, CHAR8 *resp, UINTN resp_max) {
    (void)ctx; (void)url; (void)json;
    if (resp && resp_max) resp[0] = 0;
    return EFI_UNSUPPORTED;
}
int oo_llm_infer_classify(const char *text) { (void)text; return 0; }
int oo_llm_infer_generate(const char *prompt, char *out, UINTN out_cap) {
    (void)prompt;
    if (out && out_cap) out[0] = 0;
    return 0;
}
void render_dna_helix(uint32_t tick, int x, int y, int h) { (void)tick; (void)x; (void)y; (void)h; }

UINTN strlen(const char *s) {
    UINTN n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}
char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    if (!dst || !src) return dst;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

/* ── remaining undefined symbols ────────────────────────────────────────
 * These were undefined before the scheduler/bot fix. Adding stubs here
 * ensures no PLT-zero-execution crash if any of these are reached at runtime.
 */
void repl_register_cmd(const char *name, void *fn, const char *help) {
    (void)name; (void)fn; (void)help;
}
void __stack_chk_fail(void) { for (;;) {} }   /* halt on stack corruption */
void _task_wrapper(void) {}
int  oo_nvme_read_lba(void *ctx, UINT64 lba, UINT32 cnt, void *buf) {
    (void)ctx; (void)lba; (void)cnt; (void)buf; return -1;
}
int  oo_nvme_write_lba(void *ctx, UINT64 lba, UINT32 cnt, const void *buf) {
    (void)ctx; (void)lba; (void)cnt; (void)buf; return -1;
}
int  oo_pressure_sample(void) { return 0; }
int  oo_storage_exists(const char *path) { (void)path; return 0; }
int  oo_storage_read_all(const char *path, void *buf, UINTN cap, UINTN *out) {
    (void)path; (void)buf; (void)cap; if (out) *out = 0; return -1;
}
int  oo_storage_write_all(const char *path, const void *buf, UINTN len) {
    (void)path; (void)buf; (void)len; return -1;
}
void trigger_diop_sleep_learning(void) {}

/* ── kernel-baremetal stubs ──────────────────────────────────────────────
 * oo_scheduler_get_state() and oo_scheduler_heartbeat() are pulled from
 * kernel-baremetal/build/*.o via OO_ORGAN_OBJS when that build exists.
 * When the build directory is absent, these stubs prevent PLT-stub crashes
 * (PLT section is not mapped in the EFI PE image, so any unresolved PLT
 * call runs off into unmapped zeros and eventually hits a #UD in .data).
 */
int oo_scheduler_get_state(void) { return 0; }   /* 0 = HOMEOSTASIS_NORMAL */

/* ── bot-baremetal stub ──────────────────────────────────────────────────
 * bot_get_threat_level() returns 0 = no threat detected.
 */
UINT8 bot_get_threat_level(void) { return 0; }
