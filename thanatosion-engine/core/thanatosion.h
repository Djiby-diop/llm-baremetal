#pragma once
/*
 * thanatosion-engine — Graceful Death & Rebirth Engine
 * NOVEL: Biological apoptosis for AI. Controlled death with partial
 * state preservation → guided rebirth with new DNA but preserved essence.
 * No OS, no AI system has ever implemented intentional self-termination
 * with continuity-preserving rebirth.
 */
#ifndef THANATOSION_H
#define THANATOSION_H

#define THANATOS_LOG_FILE     "OO_THANATOS.LOG"
#define THANATOS_SEED_FILE    "OO_THANATOS.SEED"
#define THANATOS_SEED_TOKENS  256

typedef enum {
    THANATOS_CAUSE_MEMORY_CORRUPT  = 0,
    THANATOS_CAUSE_PRESSURE_DYING  = 1,
    THANATOS_CAUSE_SECURITY_BREACH = 2,
    THANATOS_CAUSE_MODULE_CASCADE  = 3,
    THANATOS_CAUSE_DNA_DRIFT       = 4,
    THANATOS_CAUSE_HALLUCINATION   = 5,
    THANATOS_CAUSE_VOLUNTARY       = 6,
    THANATOS_CAUSE_WATCHDOG        = 7,
} ThanatosionCause;

typedef struct {
    unsigned int     boot_count;
    unsigned int     step_at_death;
    unsigned int     dna_hash;
    ThanatosionCause cause;
    char             cause_detail[64];
    int              severity;
    int              rebirth_allowed;
} ThanatosEvent;

typedef struct {
    int partial_restore;
    int force_dna_mutate;
    int seed_tokens_saved;
    int modules_to_skip;
} ThanatosRebirth;

typedef struct {
    int             enabled;
    int             dying_pressure_steps;
    int             dying_pressure_limit;  /* default: 10 */
    int             module_fail_count;
    int             module_fail_limit;     /* default: 3 */
    int             dplus_deny_streak;
    int             dplus_deny_limit;      /* default: 20 */
    int             death_initiated;
    int             rebirth_mode;
    ThanatosEvent   last_death;
    ThanatosRebirth rebirth_params;
    unsigned int    total_deaths;
    unsigned int    total_rebirths;
    unsigned int    voluntary_deaths;
} ThanatosionEngine;

int  thanatosion_init(ThanatosionEngine *e, void *efi_root);
int  thanatosion_tick(ThanatosionEngine *e, int pressure_dying,
                      int dplus_denied, int module_failed);
void thanatosion_die(ThanatosionEngine *e, ThanatosionCause cause,
                     const char *detail, void *efi_root);
void thanatosion_rebirth_restore(ThanatosionEngine *e, void *efi_root);
void thanatosion_voluntary(ThanatosionEngine *e, void *efi_root);
void thanatosion_format_rebirth_context(const ThanatosionEngine *e,
                                         char *buf, int buf_size);
void thanatosion_print(const ThanatosionEngine *e);

#endif
