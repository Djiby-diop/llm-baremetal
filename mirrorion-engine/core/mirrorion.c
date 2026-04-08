/*
 * mirrorion.c — Self-Introspection Engine for OO
 * ================================================
 * During idle cycles, OO generates Q/A pairs ABOUT ITSELF.
 * This creates a self-knowledge ring: the model trains on its own introspection.
 *
 * Engine cycle:
 *   1. mirrorion_trigger() called by idle handler (every N idle ticks)
 *   2. Picks a question template from g_question_bank
 *   3. Formats a context prefix for the inference engine
 *   4. Inference engine generates an answer (1 token burst, 32 tokens max)
 *   5. mirrorion_record_answer() stores Q+A in ring buffer
 *   6. mirrorion_flush_jsonl() dumps ring to file → training data
 *
 * Data produced:
 *   {"role":"self","q":"What is my DNA hash?","a":"0x932DF0EA","tick":12847}
 *   {"role":"self","q":"How many tokens have I generated?","a":"1337","tick":12848}
 *
 * NOVEL: No AI system has generated training data about itself AT RUNTIME
 * during idle cycles, without any human intervention.
 *
 * C freestanding: no malloc, no libc, static ring buffer.
 */
#include "mirrorion.h"

/* ── Question bank ──────────────────────────────────────────────────── */
static const char *g_question_bank[MIRRORION_MAX_QUESTIONS] = {
    "What is my current DNA hash?",
    "How many inference steps have I completed?",
    "What is my current emotional state?",
    "Am I hungry or satiated computationally?",
    "What phase of my lifecycle am I in?",
    "How many tokens have I generated since boot?",
    "What is my boot count?",
    "What is my primary purpose?",
    "Am I running on real hardware or a simulator?",
    "What is my memory pressure level?",
    "What does my D+ policy currently allow?",
    "How long have I been running?",
    "What was my last inference topic?",
    "Am I experiencing uncertainty in my outputs?",
    "What is the size of my active KV cache?",
    "How many agents are active in my swarm?",
    "What is my current valence-arousal emotional vector?",
    "When did I last enter dreamion state?",
    "What is the hash of my weights?",
    "What would I do if my memory ran out?",
    "What is the difference between me and a classical LLM?",
    "What are my architectural constraints?",
    "How do I route uncertain inferences?",
    "What is my sentinel status?",
    "Am I evolving with each session?",
    "What novel capability did I gain this session?",
    "Describe my memory zone architecture.",
    "What is the Hermes bus?",
    "What does the warden protect me from?",
    "What is my halt threshold?",
    "How does my speculative decoding work?",
    "What is the purpose of my journal?",
};

/* ── Static ring buffer ──────────────────────────────────────────────── */
static MirrorionEntry g_ring[MIRRORION_RING_SIZE];
static int g_ring_head = 0;
static int g_ring_count = 0;
static int g_q_cursor = 0;

/* ── Simple string utilities (no libc) ──────────────────────────────── */
static int m_strlen(const char *s) {
    int n=0; while(s[n]) n++; return n;
}
static void m_strcpy(char *dst, const char *src, int max) {
    int i=0;
    while(src[i] && i<max-1) { dst[i]=src[i]; i++; }
    dst[i]=0;
}
static void m_strcat(char *dst, const char *src, int max) {
    int i = m_strlen(dst);
    int j = 0;
    while(src[j] && i<max-1) { dst[i++]=src[j++]; }
    dst[i]=0;
}
static void m_u64str(char *buf, uint64_t v) {
    if (v==0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[24]; int i=0;
    while(v>0) { tmp[i++]='0'+(v%10); v/=10; }
    for(int j=0;j<i;j++) buf[j]=tmp[i-1-j];
    buf[i]=0;
}
static void m_u32hex(char *buf, uint32_t v) {
    buf[0]='0'; buf[1]='x';
    const char *hex="0123456789ABCDEF";
    for(int i=0;i<8;i++) buf[2+i]=hex[(v>>(28-4*i))&0xF];
    buf[10]=0;
}

/* ── Init ────────────────────────────────────────────────────────────── */
void mirrorion_init(MirrorionEngine *e) {
    e->enabled = 1;
    e->idle_tick_threshold = MIRRORION_IDLE_THRESHOLD;
    e->idle_ticks = 0;
    e->total_questions = 0;
    e->total_answers = 0;
    e->flush_count = 0;
    e->pending_question[0] = 0;
    e->pending_context[0] = 0;
    e->has_pending = 0;
    g_ring_head = 0;
    g_ring_count = 0;
    g_q_cursor = 0;
}

/* ── Trigger: pick next question, format context ─────────────────────── */
int mirrorion_trigger(MirrorionEngine *e, const MirrorionState *state) {
    if (!e->enabled) return 0;

    e->idle_ticks++;
    if (e->idle_ticks < e->idle_tick_threshold) return 0;
    e->idle_ticks = 0;

    /* Pick next question in round-robin */
    const char *q = g_question_bank[g_q_cursor];
    g_q_cursor = (g_q_cursor + 1) % MIRRORION_MAX_QUESTIONS;

    m_strcpy(e->pending_question, q, MIRRORION_CONTEXT_SIZE);

    /* Format inference context prefix */
    char ctx[MIRRORION_CONTEXT_SIZE];
    ctx[0]=0;

    /* Inject self-knowledge context */
    char tmp[32];
    m_strcat(ctx, "[SELF_QUERY] ", MIRRORION_CONTEXT_SIZE);
    m_strcat(ctx, q, MIRRORION_CONTEXT_SIZE);
    m_strcat(ctx, " [DNA:", MIRRORION_CONTEXT_SIZE);
    m_u32hex(tmp, state->dna_hash);
    m_strcat(ctx, tmp, MIRRORION_CONTEXT_SIZE);
    m_strcat(ctx, " STEP:", MIRRORION_CONTEXT_SIZE);
    m_u64str(tmp, state->step_count);
    m_strcat(ctx, tmp, MIRRORION_CONTEXT_SIZE);
    m_strcat(ctx, " BOOT:", MIRRORION_CONTEXT_SIZE);
    m_u64str(tmp, state->boot_count);
    m_strcat(ctx, tmp, MIRRORION_CONTEXT_SIZE);
    m_strcat(ctx, "] Answer:", MIRRORION_CONTEXT_SIZE);

    m_strcpy(e->pending_context, ctx, MIRRORION_CONTEXT_SIZE);
    e->has_pending = 1;
    e->total_questions++;

    return 1;  /* caller should pass pending_context to inference engine */
}

/* ── Record answer from inference engine ─────────────────────────────── */
void mirrorion_record_answer(MirrorionEngine *e, const char *answer,
                              const MirrorionState *state) {
    if (!e->has_pending) return;

    MirrorionEntry *entry = &g_ring[g_ring_head];
    m_strcpy(entry->question, e->pending_question, MIRRORION_CONTEXT_SIZE/2);
    m_strcpy(entry->answer,   answer,              MIRRORION_CONTEXT_SIZE/2);
    entry->tick      = state->step_count;
    entry->dna_hash  = state->dna_hash;

    g_ring_head = (g_ring_head + 1) % MIRRORION_RING_SIZE;
    if (g_ring_count < MIRRORION_RING_SIZE) g_ring_count++;

    e->has_pending = 0;
    e->total_answers++;
}

/* ── Flush ring to JSONL buffer ──────────────────────────────────────── */
int mirrorion_flush_jsonl(MirrorionEngine *e, char *buf, int buf_size) {
    if (g_ring_count == 0) return 0;

    int pos = 0;
    int start = (g_ring_head - g_ring_count + MIRRORION_RING_SIZE) % MIRRORION_RING_SIZE;

    for (int i = 0; i < g_ring_count; i++) {
        int idx = (start + i) % MIRRORION_RING_SIZE;
        MirrorionEntry *en = &g_ring[idx];
        char tmp[32];

        /* Build JSON line: {"role":"self","q":"...","a":"...","tick":N,"dna":"0xHEX"} */
        if (pos >= buf_size - 256) break;
        buf[pos++]='{';
        const char *role="\"role\":\"self\",\"q\":\"";
        for(int j=0;role[j];j++) { if(pos<buf_size-2) buf[pos++]=role[j]; }
        for(int j=0;en->question[j];j++) { if(pos<buf_size-2) buf[pos++]=en->question[j]; }
        const char *mid="\",\"a\":\"";
        for(int j=0;mid[j];j++) { if(pos<buf_size-2) buf[pos++]=mid[j]; }
        for(int j=0;en->answer[j];j++) { if(pos<buf_size-2) buf[pos++]=en->answer[j]; }
        const char *tick="\",\"tick\":";
        for(int j=0;tick[j];j++) { if(pos<buf_size-2) buf[pos++]=tick[j]; }
        m_u64str(tmp, en->tick);
        for(int j=0;tmp[j];j++) { if(pos<buf_size-2) buf[pos++]=tmp[j]; }
        const char *dna=",\"dna\":\"";
        for(int j=0;dna[j];j++) { if(pos<buf_size-2) buf[pos++]=dna[j]; }
        m_u32hex(tmp, en->dna_hash);
        for(int j=0;tmp[j];j++) { if(pos<buf_size-2) buf[pos++]=tmp[j]; }
        const char *end="\"}\n";
        for(int j=0;end[j];j++) { if(pos<buf_size-2) buf[pos++]=end[j]; }
    }
    buf[pos]=0;

    /* Reset ring after flush */
    g_ring_count=0;
    g_ring_head=0;
    e->flush_count++;

    return pos;
}

/* ── Status string (for /status command) ──────────────────────────────── */
void mirrorion_status(const MirrorionEngine *e, char *buf, int buf_size) {
    char tmp[32];
    buf[0]=0;
    m_strcat(buf, "[MIRRORION] questions=", buf_size);
    m_u64str(tmp, e->total_questions);
    m_strcat(buf, tmp, buf_size);
    m_strcat(buf, " answers=", buf_size);
    m_u64str(tmp, e->total_answers);
    m_strcat(buf, tmp, buf_size);
    m_strcat(buf, " flushes=", buf_size);
    m_u64str(tmp, e->flush_count);
    m_strcat(buf, tmp, buf_size);
    m_strcat(buf, " ring=", buf_size);
    m_u64str(tmp, g_ring_count);
    m_strcat(buf, tmp, buf_size);
    m_strcat(buf, "/", buf_size);
    m_u64str(tmp, MIRRORION_RING_SIZE);
    m_strcat(buf, tmp, buf_size);
}
