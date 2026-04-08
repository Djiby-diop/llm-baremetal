#pragma once
/*
 * chronion-engine — Temporal Self-Awareness Engine
 * NOVEL: AI has explicit knowledge of its own temporal position.
 * Boot count, step counter, DNA age, inference velocity — all tracked.
 */
#ifndef CHRONION_H
#define CHRONION_H

#define CHRONION_MAX_PHASE_STAMPS  32
#define CHRONION_MAX_BOOT_HISTORY   8

typedef struct {
    unsigned int boot_count;
    unsigned int steps_this_boot;
    unsigned int tokens_lifetime;
    unsigned int tokens_this_boot;
    unsigned int dna_generation;
    unsigned int idle_steps;
    unsigned int phase_bits;
} ChronionEpoch;

typedef struct {
    int tokens_per_100steps;
    int velocity_trend;
    int idle_fraction_pct;
} ChronionVelocity;

typedef struct {
    unsigned char phase_id;
    unsigned int  step_stamp;
} ChronionPhaseStamp;

typedef struct {
    unsigned int tokens_generated;
    unsigned int steps_total;
    unsigned int dna_hash;
} ChronionBootRecord;

typedef struct {
    int              enabled;
    ChronionEpoch    epoch;
    ChronionVelocity velocity;
    ChronionPhaseStamp phase_stamps[CHRONION_MAX_PHASE_STAMPS];
    int                phase_stamp_count;
    ChronionBootRecord boot_history[CHRONION_MAX_BOOT_HISTORY];
    int                boot_history_head;
    unsigned int step_window_start;
    unsigned int token_window_start;
} ChronionEngine;

void chronion_init(ChronionEngine *e, unsigned int boot_count,
                   unsigned int dna_gen, unsigned int tokens_lifetime);
void chronion_step(ChronionEngine *e, int tokens_this_step);
void chronion_idle(ChronionEngine *e);
void chronion_stamp_phase(ChronionEngine *e, unsigned char phase_id);
void chronion_format_context(const ChronionEngine *e, char *buf, int buf_size);
void chronion_age_summary(const ChronionEngine *e, char *buf, int buf_size);
int  chronion_save_epoch(const ChronionEngine *e, void *efi_root);
int  chronion_load_epoch(ChronionEngine *e, void *efi_root);
void chronion_print(const ChronionEngine *e);

#endif
