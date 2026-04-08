#pragma once
/*
 * mirrorion-engine — Self-Introspection Engine
 * ===============================================
 * NOVEL: During idle cycles, OO generates Q/A pairs ABOUT ITSELF.
 * The model trains on its own introspection — recursive self-improvement
 * from bare-metal signals, no external reward, no human intervention.
 *
 * Self-knowledge ring → OO_MIRROR.JSONL → training pipeline
 *
 * Output format:
 *   {"role":"self","q":"What is my DNA?","a":"0x932DF0EA","tick":N,"dna":"0xHEX"}
 */
#ifndef MIRRORION_H
#define MIRRORION_H

#include <stdint.h>

#define MIRRORION_RING_SIZE        64
#define MIRRORION_CONTEXT_SIZE     256
#define MIRRORION_MAX_QUESTIONS    32
#define MIRRORION_IDLE_THRESHOLD   100  /* idle ticks between questions */

/* ── Single Q/A entry ─────────────────────────────────────────────── */
typedef struct {
    char     question[MIRRORION_CONTEXT_SIZE / 2];
    char     answer[MIRRORION_CONTEXT_SIZE / 2];
    uint64_t tick;
    uint32_t dna_hash;
} MirrorionEntry;

/* ── Caller-provided system state snapshot ───────────────────────── */
typedef struct {
    uint64_t step_count;
    uint64_t boot_count;
    uint32_t dna_hash;
    int      mem_pressure;   /* 0-100 */
    float    last_halt_prob; /* 0.0-1.0 */
} MirrorionState;

/* ── Engine context ──────────────────────────────────────────────── */
typedef struct {
    int      enabled;
    uint64_t idle_tick_threshold;
    uint64_t idle_ticks;
    uint64_t total_questions;
    uint64_t total_answers;
    uint32_t flush_count;
    char     pending_question[MIRRORION_CONTEXT_SIZE];
    char     pending_context[MIRRORION_CONTEXT_SIZE];   /* formatted prefix for inference */
    int      has_pending;
} MirrorionEngine;

/* ── API ─────────────────────────────────────────────────────────── */

void mirrorion_init(MirrorionEngine *e);

/**
 * mirrorion_trigger() — call every idle tick
 * Returns 1 if a question is ready in e->pending_context (pass to SSM infer)
 */
int  mirrorion_trigger(MirrorionEngine *e, const MirrorionState *state);

/**
 * mirrorion_record_answer() — store the inference answer for the pending question
 */
void mirrorion_record_answer(MirrorionEngine *e, const char *answer,
                              const MirrorionState *state);

/**
 * mirrorion_flush_jsonl() — dump ring to buf as JSONL
 * Returns bytes written (ring is cleared after flush)
 */
int  mirrorion_flush_jsonl(MirrorionEngine *e, char *buf, int buf_size);

/**
 * mirrorion_status() — debug string
 */
void mirrorion_status(const MirrorionEngine *e, char *buf, int buf_size);

#endif /* MIRRORION_H */
