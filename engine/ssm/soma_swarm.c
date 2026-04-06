// soma_swarm.c — SomaMind Swarm Intelligence implementation
// Freestanding C11, no libc, no malloc.

#include "soma_swarm.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ── Freestanding helpers ───────────────────────────────────────────────────

static float _sw_expf(float x) {
    if (x < -88.0f) return 0.0f;
    if (x >  88.0f) return 3.4e38f;
    union { float f; int i; } u;
    u.i = (int)(12102203.0f * x) + 1065353216;
    return u.f;
}

static uint32_t _sw_xorshift(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x; return x;
}

static float _sw_randf(uint32_t *rng) {
    return (float)(_sw_xorshift(rng) >> 8) * (1.0f / (float)(1u << 24));
}

static void _sw_memcpy_f32(float *dst, const float *src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

// Softmax in-place
static void _sw_softmax(float *x, int n) {
    float mx = x[0];
    for (int i = 1; i < n; i++) if (x[i] > mx) mx = x[i];
    float sum = 0.0f;
    for (int i = 0; i < n; i++) { x[i] = _sw_expf(x[i] - mx); sum += x[i]; }
    float inv = (sum > 0.0f) ? 1.0f / sum : 1.0f;
    for (int i = 0; i < n; i++) x[i] *= inv;
}

static int _sw_argmax(const float *x, int n) {
    int best = 0;
    for (int i = 1; i < n; i++) if (x[i] > x[best]) best = i;
    return best;
}

// Max-softmax confidence without modifying buffer
static float _sw_confidence(const float *logits, int n) {
    float mx = logits[0];
    for (int i = 1; i < n; i++) if (logits[i] > mx) mx = logits[i];
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += _sw_expf(logits[i] - mx);
    return (sum > 0.0f) ? 1.0f / sum : 0.0f;
}

// Temperature-scaled top-p sampling using work_buf
static int _sw_sample(const float *raw_logits, int n,
                      float temperature, float top_p,
                      float *work_buf, uint32_t *rng,
                      float *prob_out) {
    _sw_memcpy_f32(work_buf, raw_logits, n);
    if (temperature <= 0.0f) {
        int tok = _sw_argmax(work_buf, n);
        if (prob_out) *prob_out = 1.0f;
        return tok;
    }
    float inv_t = 1.0f / temperature;
    for (int i = 0; i < n; i++) work_buf[i] *= inv_t;
    _sw_softmax(work_buf, n);
    if (prob_out) {
        float mp = 0.0f;
        for (int i = 0; i < n; i++) if (work_buf[i] > mp) mp = work_buf[i];
        *prob_out = mp;
    }
    // Sort-free nucleus
    int best = _sw_argmax(work_buf, n);
    if (top_p <= 0.0f) return best;
    float thresh = work_buf[best] * 0.005f;
    float nucleus = 0.0f;
    for (int i = 0; i < n; i++) if (work_buf[i] >= thresh) nucleus += work_buf[i];
    float cutoff = (nucleus < top_p) ? nucleus : top_p;
    float target = _sw_randf(rng) * cutoff;
    float cumul = 0.0f;
    for (int i = 0; i < n; i++) {
        if (work_buf[i] >= thresh) {
            cumul += work_buf[i];
            if (cumul >= target) return i;
        }
    }
    return best;
}

// Simple strncpy (freestanding)
static void _sw_strcpy(char *dst, const char *src, int maxn) {
    int i = 0;
    while (i < maxn - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// ============================================================
// soma_swarm_init
// ============================================================
void soma_swarm_init(SomaSwarmCtx *ctx,
                     const SomaDNA *base_dna,
                     ssm_f32 *work_buf,
                     int vocab_size) {
    if (!ctx) return;

    ctx->mode       = SOMA_SWARM_WEIGHTED;
    ctx->enabled    = 0;
    ctx->rng        = (base_dna ? soma_dna_hash(base_dna) : 0xDEADBEEFu) ^ 0x5A5A5A5Au;
    ctx->work_buf   = work_buf;
    ctx->vocab_size = vocab_size;
    ctx->ready      = (work_buf != NULL && vocab_size > 0) ? 1 : 0;
    ctx->total_votes       = 0;
    ctx->unanimous_votes   = 0;
    ctx->split_votes       = 0;
    ctx->evolution_steps   = 0;

    // ── Agent 0: BASE — clone of base DNA ──────────────────────────
    if (base_dna) ctx->agents[0].dna = *base_dna;
    else soma_dna_init_default(&ctx->agents[0].dna);
    _sw_strcpy(ctx->agents[0].name, "BASE", 8);

    // ── Agent 1: SOLAR — low temp, tight nucleus ────────────────────
    ctx->agents[1].dna = ctx->agents[0].dna;
    ctx->agents[1].dna.temperature_solar = 0.15f;
    ctx->agents[1].dna.temperature_lunar = 0.40f;
    ctx->agents[1].dna.top_p_solar       = 0.40f;
    ctx->agents[1].dna.cognition_bias    = 0.15f;  // Logic-heavy
    _sw_strcpy(ctx->agents[1].name, "SOLAR", 8);

    // ── Agent 2: LUNAR — high temp, wide nucleus ────────────────────
    ctx->agents[2].dna = ctx->agents[0].dna;
    ctx->agents[2].dna.temperature_solar = 0.80f;
    ctx->agents[2].dna.temperature_lunar = 1.50f;
    ctx->agents[2].dna.top_p_lunar       = 0.98f;
    ctx->agents[2].dna.cognition_bias    = 0.85f;  // Creative-heavy
    _sw_strcpy(ctx->agents[2].name, "LUNAR", 8);

    // ── Agent 3: XPLR — random bias, medium temp ───────────────────
    ctx->agents[3].dna = ctx->agents[0].dna;
    ctx->agents[3].dna.temperature_solar = 0.55f;
    ctx->agents[3].dna.temperature_lunar = 0.90f;
    ctx->agents[3].dna.top_p_solar       = 0.75f;
    ctx->agents[3].dna.cognition_bias    = _sw_randf(&ctx->rng);  // Random
    _sw_strcpy(ctx->agents[3].name, "XPLR", 8);

    // Init agent stats
    for (int i = 0; i < SOMA_SWARM_AGENTS; i++) {
        ctx->agents[i].fitness     = 0.5f;   // Start neutral
        ctx->agents[i].votes_cast  = 0;
        ctx->agents[i].votes_won   = 0;
        ctx->agents[i].agreements  = 0;
    }
}

// ============================================================
// soma_swarm_vote
// ============================================================
SomaSwarmResult soma_swarm_vote(SomaSwarmCtx *ctx,
                                const ssm_f32 *raw_logits,
                                int vocab_size) {
    SomaSwarmResult res;
    res.selected_token  = 0;
    res.winning_agent   = 0;
    res.unanimous       = 0;
    res.vote_spread     = 1;
    res.mode_used       = ctx ? ctx->mode : SOMA_SWARM_MAJORITY;
    for (int i = 0; i < SOMA_SWARM_AGENTS; i++) {
        res.agent_votes[i]      = 0;
        res.agent_confidence[i] = 0.0f;
    }

    if (!ctx || !ctx->ready || !raw_logits) return res;

    // ── Each agent samples independently ────────────────────────────
    for (int a = 0; a < SOMA_SWARM_AGENTS; a++) {
        SomaSwarmAgent *ag = &ctx->agents[a];
        uint32_t ag_rng = ctx->rng ^ ((uint32_t)(a + 1) * 2654435761u);

        // Solar pass for this agent (simpler: one-pass with agent's solar params)
        float prob = 0.0f;
        res.agent_votes[a] = _sw_sample(
            raw_logits, vocab_size,
            ag->dna.temperature_solar, ag->dna.top_p_solar,
            ctx->work_buf, &ag_rng, &prob);
        res.agent_confidence[a] = _sw_confidence(raw_logits, vocab_size);
        ag->votes_cast++;
    }

    // Advance swarm RNG (use agent 0's final rng state proxy)
    _sw_xorshift(&ctx->rng);

    // ── Consensus based on mode ──────────────────────────────────────
    if (ctx->mode == SOMA_SWARM_CONFIDENT) {
        // Winning agent = highest confidence
        int best = 0;
        for (int a = 1; a < SOMA_SWARM_AGENTS; a++)
            if (res.agent_confidence[a] > res.agent_confidence[best]) best = a;
        res.selected_token = res.agent_votes[best];
        res.winning_agent  = best;

    } else if (ctx->mode == SOMA_SWARM_WEIGHTED) {
        // Weighted vote: accumulate fitness-weighted count per token
        // Use small stack table for tokens (max SOMA_SWARM_AGENTS unique)
        int   tok_ids[SOMA_SWARM_AGENTS];
        float tok_weights[SOMA_SWARM_AGENTS];
        int   tok_n = 0;

        for (int a = 0; a < SOMA_SWARM_AGENTS; a++) {
            int tok = res.agent_votes[a];
            float w = ctx->agents[a].fitness > 0.0f
                      ? ctx->agents[a].fitness : 0.1f;
            // Find existing slot
            int found = -1;
            for (int t = 0; t < tok_n; t++)
                if (tok_ids[t] == tok) { found = t; break; }
            if (found >= 0) {
                tok_weights[found] += w;
            } else if (tok_n < SOMA_SWARM_AGENTS) {
                tok_ids[tok_n]     = tok;
                tok_weights[tok_n] = w;
                tok_n++;
            }
        }
        // Best weighted token
        int best_t = 0;
        for (int t = 1; t < tok_n; t++)
            if (tok_weights[t] > tok_weights[best_t]) best_t = t;
        res.selected_token = tok_ids[best_t];
        // Find which agent proposed it
        for (int a = 0; a < SOMA_SWARM_AGENTS; a++)
            if (res.agent_votes[a] == res.selected_token) { res.winning_agent = a; break; }
        res.vote_spread = tok_n;

    } else {
        // MAJORITY: simple count
        int   tok_ids[SOMA_SWARM_AGENTS];
        int   tok_cnt[SOMA_SWARM_AGENTS];
        int   tok_n = 0;
        for (int a = 0; a < SOMA_SWARM_AGENTS; a++) {
            int tok = res.agent_votes[a];
            int found = -1;
            for (int t = 0; t < tok_n; t++)
                if (tok_ids[t] == tok) { found = t; break; }
            if (found >= 0) tok_cnt[found]++;
            else if (tok_n < SOMA_SWARM_AGENTS) {
                tok_ids[tok_n] = tok; tok_cnt[tok_n] = 1; tok_n++;
            }
        }
        int best_t = 0;
        for (int t = 1; t < tok_n; t++)
            if (tok_cnt[t] > tok_cnt[best_t]) best_t = t;
        res.selected_token = tok_ids[best_t];
        for (int a = 0; a < SOMA_SWARM_AGENTS; a++)
            if (res.agent_votes[a] == res.selected_token) { res.winning_agent = a; break; }
        res.vote_spread = tok_n;
    }

    // ── Update winning agent ─────────────────────────────────────────
    ctx->agents[res.winning_agent].votes_won++;
    for (int a = 0; a < SOMA_SWARM_AGENTS; a++)
        if (res.agent_votes[a] == res.selected_token)
            ctx->agents[a].agreements++;

    res.unanimous = (res.vote_spread == 1) ? 1 : 0;
    ctx->total_votes++;
    if (res.unanimous) ctx->unanimous_votes++;
    else               ctx->split_votes++;

    return res;
}

// ============================================================
// soma_swarm_update_fitness
// ============================================================
void soma_swarm_update_fitness(SomaSwarmCtx *ctx,
                               const SomaSwarmResult *last_result,
                               float observed_quality) {
    if (!ctx || !last_result) return;
    // Reward agents that voted for the selected token
    for (int a = 0; a < SOMA_SWARM_AGENTS; a++) {
        float reward = (last_result->agent_votes[a] == last_result->selected_token)
                       ? observed_quality * 0.1f    // Small positive
                       : -0.02f;                     // Small penalty
        ctx->agents[a].fitness += reward;
        if (ctx->agents[a].fitness < 0.05f) ctx->agents[a].fitness = 0.05f;
        if (ctx->agents[a].fitness > 1.00f) ctx->agents[a].fitness = 1.00f;
    }
}

// ============================================================
// soma_swarm_evolve — re-diversify low-fitness agents
// ============================================================
void soma_swarm_evolve(SomaSwarmCtx *ctx) {
    if (!ctx) return;

    // Find best and worst agents
    int best = 0, worst = 0;
    for (int a = 1; a < SOMA_SWARM_AGENTS; a++) {
        if (ctx->agents[a].fitness > ctx->agents[best].fitness)  best  = a;
        if (ctx->agents[a].fitness < ctx->agents[worst].fitness) worst = a;
    }
    if (best == worst) return;

    // Nudge worst agent's DNA toward best agent (blend 50/50 + small mutation)
    SomaDNA *bd = &ctx->agents[best].dna;
    SomaDNA *wd = &ctx->agents[worst].dna;

    wd->temperature_solar = (wd->temperature_solar + bd->temperature_solar) * 0.5f;
    wd->temperature_lunar = (wd->temperature_lunar + bd->temperature_lunar) * 0.5f;
    wd->top_p_solar       = (wd->top_p_solar       + bd->top_p_solar)       * 0.5f;
    wd->top_p_lunar       = (wd->top_p_lunar       + bd->top_p_lunar)       * 0.5f;
    wd->cognition_bias    = (wd->cognition_bias     + bd->cognition_bias)    * 0.5f;

    // Small mutation to maintain diversity
    soma_dna_mutate(wd, &ctx->rng, 0.03f);
    ctx->agents[worst].fitness = 0.3f;   // Partial reset

    ctx->evolution_steps++;
}

// ============================================================
// soma_swarm_print_stats
// ============================================================
static void _spr_int(char *buf, int v) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int neg = (v < 0); if (neg) v = -v;
    char t[12]; int n = 0;
    while (v > 0) { t[n++] = '0' + (v % 10); v /= 10; }
    int s = 0; if (neg) buf[s++] = '-';
    for (int i = n - 1; i >= 0; i--) buf[s++] = t[i];
    buf[s] = 0;
}

void soma_swarm_print_stats(const SomaSwarmCtx *ctx, SomaSwarmPrintFn fn) {
    if (!ctx || !fn) return;
    fn("[SomaMind Swarm]\n");
    char num[16];
    _spr_int(num, ctx->total_votes);   fn("  total_votes    : "); fn(num); fn("\n");
    _spr_int(num, ctx->unanimous_votes); fn("  unanimous      : "); fn(num); fn("\n");
    _spr_int(num, ctx->split_votes);   fn("  split          : "); fn(num); fn("\n");
    _spr_int(num, ctx->evolution_steps); fn("  evolutions     : "); fn(num); fn("\n");
    fn("  agents:\n");
    for (int a = 0; a < SOMA_SWARM_AGENTS; a++) {
        const SomaSwarmAgent *ag = &ctx->agents[a];
        fn("    ["); _spr_int(num, a); fn(num); fn("] ");
        fn(ag->name); fn(" fit=");
        _spr_int(num, (int)(ag->fitness * 100)); fn(num); fn("%");
        fn(" won="); _spr_int(num, ag->votes_won); fn(num);
        fn("/"); _spr_int(num, ag->votes_cast); fn(num); fn("\n");
    }
}
