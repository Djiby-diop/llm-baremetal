#pragma once
/*
 * trophion-engine — Computational Hunger Engine
 * NOVEL: Inference compute treated as metabolic food.
 * Idle → hungry (verbose). Heavy load → gorged (terse). Natural rhythm.
 */
#ifndef TROPHION_H
#define TROPHION_H

typedef enum {
    TROPHION_STATE_STARVED  = 0,
    TROPHION_STATE_HUNGRY   = 1,
    TROPHION_STATE_SATIATED = 2,
    TROPHION_STATE_GORGED   = 3,
} TrophionState;

typedef struct {
    int starved_below;
    int hungry_below;
    int gorged_above;
    int hunger_per_idle;
    int digest_per_token;
} TrophionParams;

typedef struct {
    int            enabled;
    TrophionState  state;
    TrophionParams params;
    int  hunger_level;
    int  tokens_this_epoch;
    int  epoch_steps;
    unsigned int epoch_count;
    int  token_budget_delta;
    int  verbosity_hint;
    int  defer_heavy;
    unsigned int total_tokens_consumed;
    unsigned int gorge_events;
    unsigned int starve_events;
} TrophionEngine;

void          trophion_init(TrophionEngine *e);
void          trophion_feed(TrophionEngine *e, int tokens);
void          trophion_idle(TrophionEngine *e);
void          trophion_epoch_reset(TrophionEngine *e);
TrophionState trophion_get_state(const TrophionEngine *e);
void          trophion_format_context(const TrophionEngine *e, char *buf, int buf_size);
void          trophion_print(const TrophionEngine *e);

#endif
