#pragma once
/*
 * mirrorion-engine — Self-Introspection Engine
 * NOVEL: During idle, AI generates questions about itself and answers them.
 * Self-knowledge ring becomes training data (OO_MIRROR.JSONL).
 * Recursive self-improvement from bare-metal signals, no external reward.
 */
#ifndef MIRRORION_H
#define MIRRORION_H

#define MIRRORION_MAX_KNOWLEDGE  64
#define MIRRORION_Q_LEN         128
#define MIRRORION_A_LEN         256

typedef struct {
    char question[MIRRORION_Q_LEN];
    char answer[MIRRORION_A_LEN];
    int  confidence;
    int  source_signal;
    unsigned int step_stamp;
} MirrorionEntry;

typedef enum {
    MIRROR_TRIGGER_HIGH_HALT_PROB = 0,
    MIRROR_TRIGGER_MEM_PRESSURE   = 1,
    MIRROR_TRIGGER_SLOW_INFERENCE = 2,
    MIRROR_TRIGGER_DNA_CHANGED    = 3,
    MIRROR_TRIGGER_BOOT_NEW       = 4,
    MIRROR_TRIGGER_IDLE_LONG      = 5,
    MIRROR_TRIGGER_DPLUS_DENY     = 6,
    MIRROR_TRIGGER_MODULE_FAIL    = 7,
} MirrorionTrigger;

typedef struct {
    int             enabled;
    int             mode;  /* 0=off 1=passive 2=active */
    MirrorionEntry  ring[MIRRORION_MAX_KNOWLEDGE];
    int             ring_head;
    int             ring_count;
    int             generating;
    MirrorionTrigger current_trigger;
    char            pending_question[MIRRORION_Q_LEN];
    unsigned int    reflections_done;
    unsigned int    reflections_saved;
} MirrorionEngine;

void        mirrorion_init(MirrorionEngine *e);
void        mirrorion_set_mode(MirrorionEngine *e, int mode);
const char *mirrorion_trigger(MirrorionEngine *e, MirrorionTrigger t, const char *context);
void        mirrorion_record_answer(MirrorionEngine *e, const char *answer, int confidence);
void        mirrorion_get_context(const MirrorionEngine *e, const char *query_hint,
                                   char *buf, int buf_size);
int         mirrorion_flush_jsonl(const MirrorionEngine *e, void *efi_root);
void        mirrorion_print(const MirrorionEngine *e);

#endif
