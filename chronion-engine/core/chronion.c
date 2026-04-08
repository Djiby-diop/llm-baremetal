/*
 * chronion.c — Temporal Self-Awareness Engine implementation
 */
#include "chronion.h"

void chronion_init(ChronionEngine *e, unsigned int boot_count,
                   unsigned int dna_gen, unsigned int tokens_lifetime) {
    int i;
    e->enabled = 1;
    e->epoch.boot_count       = boot_count;
    e->epoch.steps_this_boot  = 0;
    e->epoch.tokens_lifetime  = tokens_lifetime;
    e->epoch.tokens_this_boot = 0;
    e->epoch.dna_generation   = dna_gen;
    e->epoch.idle_steps       = 0;
    e->epoch.phase_bits       = 0;
    e->velocity.tokens_per_100steps = 0;
    e->velocity.velocity_trend      = 0;
    e->velocity.idle_fraction_pct   = 0;
    e->phase_stamp_count  = 0;
    e->boot_history_head  = 0;
    e->step_window_start  = 0;
    e->token_window_start = 0;
    for (i = 0; i < CHRONION_MAX_PHASE_STAMPS; i++) {
        e->phase_stamps[i].phase_id   = 0xFF;
        e->phase_stamps[i].step_stamp = 0;
    }
    for (i = 0; i < CHRONION_MAX_BOOT_HISTORY; i++) {
        e->boot_history[i].tokens_generated = 0;
        e->boot_history[i].steps_total      = 0;
        e->boot_history[i].dna_hash         = 0;
    }
}

void chronion_step(ChronionEngine *e, int tokens_this_step) {
    if (!e->enabled) return;
    e->epoch.steps_this_boot++;
    if (tokens_this_step > 0) {
        e->epoch.tokens_this_boot += (unsigned int)tokens_this_step;
        e->epoch.tokens_lifetime  += (unsigned int)tokens_this_step;
        e->epoch.idle_steps = 0;
    }
    /* update velocity every 100 steps */
    unsigned int window = e->epoch.steps_this_boot - e->step_window_start;
    if (window >= 100) {
        unsigned int tok_window = e->epoch.tokens_this_boot - e->token_window_start;
        int new_vel = (int)tok_window;
        e->velocity.velocity_trend = new_vel - e->velocity.tokens_per_100steps;
        e->velocity.tokens_per_100steps = new_vel;
        /* idle fraction */
        e->velocity.idle_fraction_pct =
            (int)(e->epoch.idle_steps * 100 / (window > 0 ? window : 1));
        e->step_window_start  = e->epoch.steps_this_boot;
        e->token_window_start = e->epoch.tokens_this_boot;
    }
}

void chronion_idle(ChronionEngine *e) {
    if (!e->enabled) return;
    e->epoch.idle_steps++;
    e->epoch.steps_this_boot++;
}

void chronion_stamp_phase(ChronionEngine *e, unsigned char phase_id) {
    if (!e->enabled) return;
    if (e->phase_stamp_count >= CHRONION_MAX_PHASE_STAMPS) return;
    e->phase_stamps[e->phase_stamp_count].phase_id   = phase_id;
    e->phase_stamps[e->phase_stamp_count].step_stamp = e->epoch.steps_this_boot;
    e->phase_stamp_count++;
    e->epoch.phase_bits |= (1u << (phase_id & 31));
}

static void _uint_to_str(unsigned int v, char *buf, int *i) {
    if (v == 0) { buf[(*i)++] = '0'; return; }
    char tmp[12]; int n = 0;
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    for (int k = n - 1; k >= 0; k--) buf[(*i)++] = tmp[k];
}

void chronion_format_context(const ChronionEngine *e, char *buf, int buf_size) {
    if (!e->enabled || buf_size < 32) return;
    int i = 0;
    const char *p = "[T:b";
    while (*p && i < buf_size-1) buf[i++] = *p++;
    _uint_to_str(e->epoch.boot_count, buf, &i);
    buf[i++] = ' '; buf[i++] = 's';
    _uint_to_str(e->epoch.steps_this_boot, buf, &i);
    buf[i++] = ' '; buf[i++] = 'g';
    _uint_to_str(e->epoch.dna_generation, buf, &i);
    buf[i++] = ']'; buf[i] = '\0';
}

void chronion_age_summary(const ChronionEngine *e, char *buf, int buf_size) {
    if (!e->enabled || buf_size < 48) return;
    int i = 0;
    const char *p = "boot #";
    while (*p && i < buf_size-1) buf[i++] = *p++;
    _uint_to_str(e->epoch.boot_count, buf, &i);
    p = " steps:";
    while (*p && i < buf_size-1) buf[i++] = *p++;
    _uint_to_str(e->epoch.steps_this_boot, buf, &i);
    p = " DNA gen:";
    while (*p && i < buf_size-1) buf[i++] = *p++;
    _uint_to_str(e->epoch.dna_generation, buf, &i);
    buf[i] = '\0';
}

int chronion_save_epoch(const ChronionEngine *e, void *efi_root) {
    (void)e; (void)efi_root;
    /* EFI file write — platform stub */
    return 0;
}

int chronion_load_epoch(ChronionEngine *e, void *efi_root) {
    (void)e; (void)efi_root;
    return 0;
}

void chronion_print(const ChronionEngine *e) {
    (void)e;
}
