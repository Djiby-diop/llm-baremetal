#pragma once
/*
 * limbion-engine — Emotional State Engine
 * NOVEL: 2D affective state (valence × arousal) modulates inference.
 */
#ifndef LIMBION_H
#define LIMBION_H

typedef enum {
    LIMBION_QUAD_CALM       = 0,  /* +valence, low arousal  — steady */
    LIMBION_QUAD_EXCITED    = 1,  /* +valence, high arousal — creative */
    LIMBION_QUAD_TENSE      = 2,  /* -valence, high arousal — danger */
    LIMBION_QUAD_DEPRESSED  = 3,  /* -valence, low arousal  — minimal */
} LimbionQuadrant;

typedef enum {
    LIMBION_TRIGGER_BOOT_OK         = 0,
    LIMBION_TRIGGER_BOOT_FAIL       = 1,
    LIMBION_TRIGGER_MEM_PRESSURE    = 2,
    LIMBION_TRIGGER_GOOD_INFERENCE  = 3,
    LIMBION_TRIGGER_HALT_FIRED      = 4,
    LIMBION_TRIGGER_IDLE_LONG       = 5,
    LIMBION_TRIGGER_DNA_MISMATCH    = 6,
    LIMBION_TRIGGER_TRAIN_SAMPLE    = 7,
    LIMBION_TRIGGER_DPLUS_DENY      = 8,
    LIMBION_TRIGGER_DREAM_COMPLETE  = 9,
} LimbionTrigger;

typedef struct {
    int   valence;           /* [-100, +100] */
    int   arousal;           /* [0, 100] */
    LimbionQuadrant quadrant;
    int   temperature_delta; /* applied to sampling temperature x0.01 */
    int   token_budget_delta;
    int   defer_threshold;
    char  mood_ascii[16];
} LimbionAffect;

typedef struct {
    int           enabled;
    LimbionAffect affect;
    int           valence_decay_per_step;
    int           arousal_decay_per_step;
    unsigned int  events_processed;
    unsigned int  quadrant_changes;
    LimbionQuadrant history[8];
    int             history_head;
} LimbionEngine;

void limbion_init(LimbionEngine *e);
void limbion_trigger(LimbionEngine *e, LimbionTrigger t, int intensity);
void limbion_decay(LimbionEngine *e);
const LimbionAffect *limbion_get_affect(const LimbionEngine *e);
void limbion_format_context(const LimbionEngine *e, char *buf, int buf_size);
void limbion_print(const LimbionEngine *e);

#endif
