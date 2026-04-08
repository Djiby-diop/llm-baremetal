/*
 * trophion.c — Computational Hunger Engine implementation
 */
#include "trophion.h"

static void _clamp_int(int *v, int lo, int hi) {
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;
}

static TrophionState _compute_state(const TrophionEngine *e) {
    if (e->hunger_level >= 900)           return TROPHION_STATE_STARVED;
    if (e->hunger_level >= e->params.hungry_below)  return TROPHION_STATE_HUNGRY;
    if (e->tokens_this_epoch >= e->params.gorged_above) return TROPHION_STATE_GORGED;
    return TROPHION_STATE_SATIATED;
}

static void _update_effects(TrophionEngine *e) {
    switch (e->state) {
        case TROPHION_STATE_STARVED:
            e->token_budget_delta = -128;
            e->verbosity_hint     = 0;
            e->defer_heavy        = 1;
            break;
        case TROPHION_STATE_HUNGRY:
            e->token_budget_delta = +64 + (e->hunger_level / 10);
            e->verbosity_hint     = 80;
            e->defer_heavy        = 0;
            break;
        case TROPHION_STATE_SATIATED:
            e->token_budget_delta = 0;
            e->verbosity_hint     = 50;
            e->defer_heavy        = 0;
            break;
        case TROPHION_STATE_GORGED:
            e->token_budget_delta = -32;
            e->verbosity_hint     = 20;
            e->defer_heavy        = 1;
            break;
    }
}

void trophion_init(TrophionEngine *e) {
    e->enabled              = 1;
    e->state                = TROPHION_STATE_HUNGRY;
    e->params.starved_below = 5;
    e->params.hungry_below  = 200;
    e->params.gorged_above  = 500;
    e->params.hunger_per_idle   = 2;
    e->params.digest_per_token  = 1;
    e->hunger_level         = 300;  /* start slightly hungry */
    e->tokens_this_epoch    = 0;
    e->epoch_steps          = 0;
    e->epoch_count          = 0;
    e->token_budget_delta   = 0;
    e->verbosity_hint       = 50;
    e->defer_heavy          = 0;
    e->total_tokens_consumed= 0;
    e->gorge_events         = 0;
    e->starve_events        = 0;
    _update_effects(e);
}

void trophion_feed(TrophionEngine *e, int tokens) {
    if (!e->enabled || tokens <= 0) return;
    e->hunger_level      -= tokens * e->params.digest_per_token;
    e->tokens_this_epoch += tokens;
    e->total_tokens_consumed += (unsigned int)tokens;
    _clamp_int(&e->hunger_level, 0, 1000);
    TrophionState prev = e->state;
    e->state = _compute_state(e);
    if (e->state == TROPHION_STATE_GORGED && prev != TROPHION_STATE_GORGED)
        e->gorge_events++;
    _update_effects(e);
}

void trophion_idle(TrophionEngine *e) {
    if (!e->enabled) return;
    e->hunger_level += e->params.hunger_per_idle;
    e->epoch_steps++;
    _clamp_int(&e->hunger_level, 0, 1000);
    TrophionState prev = e->state;
    e->state = _compute_state(e);
    if (e->state == TROPHION_STATE_STARVED && prev != TROPHION_STATE_STARVED)
        e->starve_events++;
    _update_effects(e);
}

void trophion_epoch_reset(TrophionEngine *e) {
    if (!e->enabled) return;
    e->tokens_this_epoch = 0;
    e->epoch_steps       = 0;
    e->epoch_count++;
    e->state = _compute_state(e);
    _update_effects(e);
}

TrophionState trophion_get_state(const TrophionEngine *e) {
    return e->state;
}

static const char *_state_name(TrophionState s) {
    switch (s) {
        case TROPHION_STATE_STARVED:  return "STARVED";
        case TROPHION_STATE_HUNGRY:   return "HUNGRY";
        case TROPHION_STATE_SATIATED: return "SATIATED";
        case TROPHION_STATE_GORGED:   return "GORGED";
    }
    return "?";
}

void trophion_format_context(const TrophionEngine *e, char *buf, int buf_size) {
    if (!e->enabled || buf_size < 28) return;
    const char *name = _state_name(e->state);
    int i = 0;
    const char *p = "[HUNGER:";
    while (*p && i < buf_size-1) buf[i++] = *p++;
    while (*name && i < buf_size-1) buf[i++] = *name++;
    buf[i++] = ']'; buf[i] = '\0';
}

void trophion_print(const TrophionEngine *e) { (void)e; }
