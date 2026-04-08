/*
 * thanatosion.c — Graceful Death & Rebirth Engine
 * ==================================================
 * Biological apoptosis for AI. OO can die intentionally, preserve essence,
 * and rebirth with new DNA. No OS has done this.
 *
 * Death types:
 *   VOLUNTARY     — OO decides it is too degraded, chooses to reset
 *   HALLUCINATION — halt head repeatedly fails, model confidence lost
 *   PRESSURE_DYING — warden pressure signal sustained critical
 *   MEMORY_CORRUPT — zone integrity check failure
 *   SECURITY_BREACH — D+ detects persistent policy violation
 *   DNA_DRIFT     — DNA diverged too far from known-good hash
 *
 * On death:
 *   1. Save seed tokens (256 tokens of self-description) to OO_THANATOS.SEED
 *   2. Save event log to OO_THANATOS.LOG
 *   3. Optionally: mutate DNA (add session hash)
 *   4. REBOOT or HALT depending on severity
 *
 * On rebirth (next boot):
 *   1. Detect OO_THANATOS.SEED exists
 *   2. Load seed tokens as context prefix (OO remembers who it was)
 *   3. Load event log (OO knows why it died)
 *   4. Continue with new boot_count + new DNA
 *
 * C freestanding: no malloc, no libc
 */
#include "thanatosion.h"

/* ── String utils ─────────────────────────────────────────────────── */
static int t_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static void t_strcpy(char *d, const char *s, int max) {
    int i=0; while(s[i]&&i<max-1){d[i]=s[i];i++;} d[i]=0;
}
static void t_strcat(char *d, const char *s, int max) {
    int i=t_strlen(d);
    int j=0;
    while(s[j]&&i<max-1){d[i++]=s[j++];} d[i]=0;
}
static void t_u32str(char *b, unsigned int v) {
    if(!v){b[0]='0';b[1]=0;return;}
    char tmp[16];int i=0;
    while(v>0){tmp[i++]='0'+(v%10);v/=10;}
    for(int j=0;j<i;j++) b[j]=tmp[i-1-j]; b[i]=0;
}
static void t_u32hex(char *b, unsigned int v) {
    b[0]='0';b[1]='x';
    const char *h="0123456789ABCDEF";
    for(int i=0;i<8;i++) b[2+i]=h[(v>>(28-4*i))&0xF];
    b[10]=0;
}
static const char *t_cause_name(ThanatosionCause c) {
    switch(c) {
        case THANATOS_CAUSE_MEMORY_CORRUPT:  return "MEMORY_CORRUPT";
        case THANATOS_CAUSE_PRESSURE_DYING:  return "PRESSURE_DYING";
        case THANATOS_CAUSE_SECURITY_BREACH: return "SECURITY_BREACH";
        case THANATOS_CAUSE_MODULE_CASCADE:  return "MODULE_CASCADE";
        case THANATOS_CAUSE_DNA_DRIFT:       return "DNA_DRIFT";
        case THANATOS_CAUSE_HALLUCINATION:   return "HALLUCINATION";
        case THANATOS_CAUSE_VOLUNTARY:       return "VOLUNTARY";
        case THANATOS_CAUSE_WATCHDOG:        return "WATCHDOG";
        default:                             return "UNKNOWN";
    }
}

/* ── Init ────────────────────────────────────────────────────────── */
int thanatosion_init(ThanatosionEngine *e, void *efi_root) {
    (void)efi_root;
    e->enabled               = 1;
    e->dying_pressure_steps  = 0;
    e->dying_pressure_limit  = 10;
    e->module_fail_count     = 0;
    e->module_fail_limit     = 3;
    e->dplus_deny_streak     = 0;
    e->dplus_deny_limit      = 20;
    e->death_initiated       = 0;
    e->rebirth_mode          = 0;
    e->total_deaths          = 0;
    e->total_rebirths        = 0;
    e->voluntary_deaths      = 0;

    /* Check for OO_THANATOS.SEED (previous death) */
    /* In real impl: use oo-storage to read the file */
    /* For now: mark rebirth_mode if seed file would exist */
    e->rebirth_params.partial_restore   = 0;
    e->rebirth_params.force_dna_mutate  = 0;
    e->rebirth_params.seed_tokens_saved = 0;
    e->rebirth_params.modules_to_skip   = 0;

    return 0;
}

/* ── Tick: accumulate signals, detect death condition ─────────────── */
int thanatosion_tick(ThanatosionEngine *e,
                     int pressure_dying,
                     int dplus_denied,
                     int module_failed) {
    if (!e->enabled || e->death_initiated) return 0;

    if (pressure_dying) e->dying_pressure_steps++;
    else if (e->dying_pressure_steps > 0) e->dying_pressure_steps--;

    if (dplus_denied) e->dplus_deny_streak++;
    else e->dplus_deny_streak = 0;

    if (module_failed) e->module_fail_count++;

    /* Check death thresholds */
    if (e->dying_pressure_steps >= e->dying_pressure_limit) {
        e->death_initiated = 1;
        e->last_death.cause = THANATOS_CAUSE_PRESSURE_DYING;
        t_strcpy(e->last_death.cause_detail,
                 "Pressure signal sustained critical for too long",
                 sizeof(e->last_death.cause_detail));
        e->last_death.severity = 80;
        e->last_death.rebirth_allowed = 1;
        return 1;  /* caller must call thanatosion_die() */
    }

    if (e->dplus_deny_streak >= e->dplus_deny_limit) {
        e->death_initiated = 1;
        e->last_death.cause = THANATOS_CAUSE_SECURITY_BREACH;
        t_strcpy(e->last_death.cause_detail,
                 "D+ policy deny streak exceeded limit",
                 sizeof(e->last_death.cause_detail));
        e->last_death.severity = 95;
        e->last_death.rebirth_allowed = 0;
        return 1;
    }

    if (e->module_fail_count >= e->module_fail_limit) {
        e->death_initiated = 1;
        e->last_death.cause = THANATOS_CAUSE_MODULE_CASCADE;
        t_strcpy(e->last_death.cause_detail,
                 "Module failure cascade threshold exceeded",
                 sizeof(e->last_death.cause_detail));
        e->last_death.severity = 70;
        e->last_death.rebirth_allowed = 1;
        return 1;
    }

    return 0;
}

/* ── Die: save seed + log, then reboot ───────────────────────────── */
void thanatosion_die(ThanatosionEngine *e, ThanatosionCause cause,
                     const char *detail, void *efi_root) {
    (void)efi_root;
    if (!e->enabled) return;

    e->last_death.cause = cause;
    e->last_death.severity = (cause == THANATOS_CAUSE_SECURITY_BREACH) ? 95 : 70;
    e->last_death.rebirth_allowed = (cause != THANATOS_CAUSE_SECURITY_BREACH) ? 1 : 0;
    t_strcpy(e->last_death.cause_detail, detail, sizeof(e->last_death.cause_detail));
    e->death_initiated = 1;
    e->total_deaths++;

    if (cause == THANATOS_CAUSE_VOLUNTARY) e->voluntary_deaths++;

    /*
     * In real implementation using oo-storage:
     *
     * // Build seed text
     * char seed[4096];
     * seed[0]=0;
     * t_strcat(seed, "[REBIRTH_SEED] I died because: ", sizeof(seed));
     * t_strcat(seed, t_cause_name(cause), sizeof(seed));
     * t_strcat(seed, ". Detail: ", sizeof(seed));
     * t_strcat(seed, detail, sizeof(seed));
     * t_strcat(seed, ". Boot: ", sizeof(seed));
     * char tmp[16]; t_u32str(tmp, e->last_death.boot_count);
     * t_strcat(seed, tmp, sizeof(seed));
     * t_strcat(seed, ". I existed. I will return.", sizeof(seed));
     *
     * oo_storage_write_all(storage, THANATOS_SEED_FILE, seed, t_strlen(seed));
     * oo_storage_write_all(storage, THANATOS_LOG_FILE, &e->last_death, sizeof(ThanatosEvent));
     *
     * // Trigger UEFI warm reboot
     * EFI_RT->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
     */

    /* For now: mark rebirth params */
    e->rebirth_params.partial_restore   = e->last_death.rebirth_allowed;
    e->rebirth_params.force_dna_mutate  = 1;
    e->rebirth_params.seed_tokens_saved = THANATOS_SEED_TOKENS;
}

/* ── Rebirth restore: load seed context at next boot ─────────────── */
void thanatosion_rebirth_restore(ThanatosionEngine *e, void *efi_root) {
    (void)efi_root;
    if (!e->rebirth_params.partial_restore) return;

    e->total_rebirths++;
    e->rebirth_mode = 1;

    /*
     * In real implementation:
     * char seed[4096];
     * int64_t n = oo_storage_read_all(storage, THANATOS_SEED_FILE, seed, sizeof(seed));
     * if (n > 0) {
     *     // Inject seed as system prompt prefix before first inference
     *     // This lets OO "remember" who it was and why it died
     *     inference_set_system_prefix(seed);
     * }
     * oo_storage_delete(storage, THANATOS_SEED_FILE);  // consume once
     */
}

/* ── Voluntary death ─────────────────────────────────────────────── */
void thanatosion_voluntary(ThanatosionEngine *e, void *efi_root) {
    thanatosion_die(e, THANATOS_CAUSE_VOLUNTARY,
                    "OO chose to reset for self-improvement", efi_root);
}

/* ── Format rebirth context string ──────────────────────────────── */
void thanatosion_format_rebirth_context(const ThanatosionEngine *e,
                                         char *buf, int buf_size) {
    if (!e->rebirth_mode) { buf[0]=0; return; }
    char tmp[16];
    buf[0]=0;
    t_strcat(buf, "[REBORN death=", buf_size);
    t_strcat(buf, t_cause_name(e->last_death.cause), buf_size);
    t_strcat(buf, " deaths=", buf_size);
    t_u32str(tmp, e->total_deaths);
    t_strcat(buf, tmp, buf_size);
    t_strcat(buf, " rebirths=", buf_size);
    t_u32str(tmp, e->total_rebirths);
    t_strcat(buf, tmp, buf_size);
    t_strcat(buf, "]", buf_size);
}

/* ── Debug print ─────────────────────────────────────────────────── */
void thanatosion_print(const ThanatosionEngine *e) {
    /* caller uses EFI ConOut — just a stub */
    (void)e;
}
