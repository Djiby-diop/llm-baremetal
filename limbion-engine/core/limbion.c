/*
 * limbion.c — Emotional State Engine implementation
 */
#include "limbion.h"

static void _clamp(int *v, int lo, int hi) {
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;
}

static const char *_quad_name(LimbionQuadrant q) {
    switch (q) {
        case LIMBION_QUAD_CALM:      return "CALM";
        case LIMBION_QUAD_EXCITED:   return "EXCITED";
        case LIMBION_QUAD_TENSE:     return "TENSE";
        case LIMBION_QUAD_DEPRESSED: return "DEPRESSED";
    }
    return "UNKNOWN";
}

static LimbionQuadrant _compute_quad(int valence, int arousal) {
    if (valence >= 0 && arousal < 40) return LIMBION_QUAD_CALM;
    if (valence >= 0 && arousal >= 40) return LIMBION_QUAD_EXCITED;
    if (valence < 0  && arousal >= 40) return LIMBION_QUAD_TENSE;
    return LIMBION_QUAD_DEPRESSED;
}

static void _update_affect(LimbionAffect *a) {
    a->quadrant = _compute_quad(a->valence, a->arousal);

    /* temperature delta: arousal raises temp, negative valence lowers it */
    a->temperature_delta = (a->arousal / 5) - ((-a->valence) / 10);
    _clamp(&a->temperature_delta, -50, +80);

    /* token budget: hunger (arousal) = more tokens, fear (neg val) = fewer */
    a->token_budget_delta = (a->arousal / 4) + (a->valence / 8);
    _clamp(&a->token_budget_delta, -64, +128);

    /* D+ defer if very negative valence */
    a->defer_threshold = (a->valence < -60) ? 1 : 0;

    /* mood string */
    const char *s = _quad_name(a->quadrant);
    int i = 0;
    while (s[i] && i < 15) { a->mood_ascii[i] = s[i]; i++; }
    a->mood_ascii[i] = '\0';
}

void limbion_init(LimbionEngine *e) {
    int i;
    e->enabled = 1;
    e->affect.valence = 20;    /* slight positivity at boot */
    e->affect.arousal = 30;
    e->valence_decay_per_step = 1;
    e->arousal_decay_per_step = 1;
    e->events_processed = 0;
    e->quadrant_changes = 0;
    e->history_head = 0;
    for (i = 0; i < 8; i++) e->history[i] = LIMBION_QUAD_CALM;
    _update_affect(&e->affect);
}

void limbion_trigger(LimbionEngine *e, LimbionTrigger t, int intensity) {
    if (!e->enabled) return;
    _clamp(&intensity, 0, 100);

    switch (t) {
        case LIMBION_TRIGGER_BOOT_OK:
            e->affect.valence += intensity / 3;
            e->affect.arousal += intensity / 5;
            break;
        case LIMBION_TRIGGER_BOOT_FAIL:
            e->affect.valence -= intensity / 2;
            e->affect.arousal += intensity / 2;
            break;
        case LIMBION_TRIGGER_MEM_PRESSURE:
            e->affect.valence -= intensity / 3;
            e->affect.arousal += intensity / 3;
            break;
        case LIMBION_TRIGGER_GOOD_INFERENCE:
            e->affect.valence += intensity / 4;
            break;
        case LIMBION_TRIGGER_HALT_FIRED:
            e->affect.arousal -= intensity / 4;
            break;
        case LIMBION_TRIGGER_IDLE_LONG:
            e->affect.arousal -= intensity / 5;
            e->affect.valence -= intensity / 10;
            break;
        case LIMBION_TRIGGER_DNA_MISMATCH:
            e->affect.valence -= intensity / 2;
            e->affect.arousal += intensity / 2;
            break;
        case LIMBION_TRIGGER_TRAIN_SAMPLE:
            e->affect.valence += intensity / 5;
            break;
        case LIMBION_TRIGGER_DPLUS_DENY:
            e->affect.valence -= intensity / 4;
            e->affect.arousal += intensity / 6;
            break;
        case LIMBION_TRIGGER_DREAM_COMPLETE:
            e->affect.valence += intensity / 6;
            e->affect.arousal -= intensity / 5;
            break;
    }

    _clamp(&e->affect.valence, -100, 100);
    _clamp(&e->affect.arousal, 0, 100);

    LimbionQuadrant old_quad = e->affect.quadrant;
    _update_affect(&e->affect);
    if (e->affect.quadrant != old_quad) {
        e->quadrant_changes++;
        e->history[e->history_head & 7] = e->affect.quadrant;
        e->history_head++;
    }
    e->events_processed++;
}

void limbion_decay(LimbionEngine *e) {
    if (!e->enabled) return;
    /* drift toward neutral */
    if (e->affect.valence > 0)  e->affect.valence -= e->valence_decay_per_step;
    else if (e->affect.valence < 0) e->affect.valence += e->valence_decay_per_step;
    if (e->affect.arousal > 20) e->affect.arousal -= e->arousal_decay_per_step;
    _clamp(&e->affect.valence, -100, 100);
    _clamp(&e->affect.arousal, 0, 100);
    _update_affect(&e->affect);
}

const LimbionAffect *limbion_get_affect(const LimbionEngine *e) {
    return &e->affect;
}

void limbion_format_context(const LimbionEngine *e, char *buf, int buf_size) {
    if (!e->enabled || buf_size < 32) return;
    /* simple snprintf replacement — no stdlib */
    const char *mood = e->affect.mood_ascii;
    int v = e->affect.valence;
    int a = e->affect.arousal;
    /* build string manually */
    char tmp[64];
    int i = 0;
    const char *prefix = "[MOOD:";
    while (*prefix) tmp[i++] = *prefix++;
    int j = 0; while (mood[j]) tmp[i++] = mood[j++];
    tmp[i++] = ' '; tmp[i++] = 'V'; tmp[i++] = ':';
    if (v >= 0) tmp[i++] = '+';
    /* write valence digits */
    if (v < 0) { tmp[i++] = '-'; v = -v; }
    if (v >= 100) { tmp[i++] = '1'; tmp[i++] = '0'; tmp[i++] = '0'; }
    else if (v >= 10) { tmp[i++] = '0' + v/10; tmp[i++] = '0' + v%10; }
    else { tmp[i++] = '0' + v; }
    tmp[i++] = ' '; tmp[i++] = 'A'; tmp[i++] = ':';
    if (a >= 100) { tmp[i++] = '1'; tmp[i++] = '0'; tmp[i++] = '0'; }
    else if (a >= 10) { tmp[i++] = '0' + a/10; tmp[i++] = '0' + a%10; }
    else { tmp[i++] = '0' + a; }
    tmp[i++] = ']'; tmp[i++] = '\0';
    j = 0;
    while (tmp[j] && j < buf_size - 1) { buf[j] = tmp[j]; j++; }
    buf[j] = '\0';
}

void limbion_print(const LimbionEngine *e) {
    (void)e; /* EFI console print — platform-specific, stub */
}
