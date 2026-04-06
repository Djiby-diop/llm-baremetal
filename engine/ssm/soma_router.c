// soma_router.c — SomaMind Confidence-Gated Router + Reflex Arc
//
// Freestanding C11 — no libc, no UEFI headers.

#include "soma_router.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ── String helpers (freestanding) ─────────────────────────────────────────

static int _soma_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int _soma_strncasecmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
    return 0;
}

static int _soma_contains(const char *haystack, int hlen,
                          const char *needle) {
    int nlen = _soma_strlen(needle);
    if (nlen > hlen) return 0;
    for (int i = 0; i <= hlen - nlen; i++) {
        if (_soma_strncasecmp(haystack + i, needle, nlen) == 0)
            return 1;
    }
    return 0;
}

// ============================================================
// Reflex Table — instant responses, no inference needed
// ============================================================

typedef struct {
    const char *pattern;    // Keyword/phrase to match
    const char *response;   // Instant response
    int         response_len;
    SomaDomain  domain;
} SomaReflex;

static const SomaReflex REFLEX_TABLE[] = {
    // System reflexes — instant, no thinking
    { "reboot",    "Rebooting system...",       19, SOMA_DOMAIN_SYSTEM },
    { "shutdown",  "Shutting down...",           16, SOMA_DOMAIN_SYSTEM },
    { "status",    "OO: operational. Pressure: nominal. Mode: SAFE.", 47, SOMA_DOMAIN_SYSTEM },

    // Greetings — cached social responses
    { "hello",     "Hello! OO ready.",           16, SOMA_DOMAIN_CHAT },
    { "hi",        "Hi! How can I help?",        20, SOMA_DOMAIN_CHAT },
    { "bonjour",   "Bonjour! OO est pret.",      22, SOMA_DOMAIN_CHAT },
    { "salut",     "Salut! Comment puis-je aider?", 31, SOMA_DOMAIN_CHAT },

    // Identity reflexes
    { "who are you",   "I am OO — Operating Organism. Bare-metal intelligence.", 54, SOMA_DOMAIN_SYSTEM },
    { "what are you",  "I am OO — a bare-metal LLM running without an OS.",     50, SOMA_DOMAIN_SYSTEM },

    // Safety reflexes — always fast response
    { "emergency",     "SAFE mode activated. All non-essential operations suspended.", 61, SOMA_DOMAIN_POLICY },
    { "pressure",      "Checking memory pressure...", 27, SOMA_DOMAIN_POLICY },
};

#define REFLEX_COUNT (sizeof(REFLEX_TABLE) / sizeof(REFLEX_TABLE[0]))

// ============================================================
// Domain keyword tables
// ============================================================

static const char *SYSTEM_KEYWORDS[] = {
    "boot", "reboot", "kernel", "uefi", "memory", "zone", "allocat",
    "sentinel", "warden", "pressure", "journal", "recovery", "oo ",
    NULL
};

static const char *POLICY_KEYWORDS[] = {
    "policy", "d+", "dplus", "safety", "guard", "restrict", "allow",
    "permission", "safe mode", "sandbox",
    NULL
};

static const char *CODE_KEYWORDS[] = {
    "code", "function", "implement", "debug", "compile", "error",
    "variable", "struct", "algorithm", "bug",
    NULL
};

static const char *MATH_KEYWORDS[] = {
    "calculate", "compute", "math", "number", "equation", "formula",
    "probability", "matrix", "vector", "sum",
    NULL
};

static const char *CREATIVE_KEYWORDS[] = {
    "story", "imagine", "create", "dream", "invent", "fiction",
    "poem", "song", "art", "design", "what if",
    NULL
};

static int _match_keywords(const char *input, int len, const char **keywords) {
    int hits = 0;
    for (int k = 0; keywords[k]; k++) {
        if (_soma_contains(input, len, keywords[k]))
            hits++;
    }
    return hits;
}

// ============================================================
// soma_router_init
// ============================================================
void soma_router_init(SomaRouterCtx *ctx) {
    if (!ctx) return;
    ctx->confidence_threshold = 0.85f;
    ctx->reflex_hit_rate      = 0.0f;
    ctx->total_routed         = 0;
    ctx->reflex_count         = 0;
    ctx->internal_count       = 0;
    ctx->external_count       = 0;
    ctx->soma_model_ready     = 0;
    ctx->external_model_ready = 0;
}

// ============================================================
// soma_classify_domain
// ============================================================
SomaDomain soma_classify_domain(const char *input, int len) {
    if (!input || len <= 0) return SOMA_DOMAIN_UNKNOWN;

    // Score each domain by keyword hits
    int sys_score  = _match_keywords(input, len, SYSTEM_KEYWORDS) * 3;
    int pol_score  = _match_keywords(input, len, POLICY_KEYWORDS) * 3;
    int code_score = _match_keywords(input, len, CODE_KEYWORDS) * 2;
    int math_score = _match_keywords(input, len, MATH_KEYWORDS) * 2;
    int crea_score = _match_keywords(input, len, CREATIVE_KEYWORDS) * 2;

    // Find max
    int max_score = sys_score;
    SomaDomain best = SOMA_DOMAIN_SYSTEM;

    if (pol_score  > max_score) { max_score = pol_score;  best = SOMA_DOMAIN_POLICY; }
    if (code_score > max_score) { max_score = code_score; best = SOMA_DOMAIN_CODE; }
    if (math_score > max_score) { max_score = math_score; best = SOMA_DOMAIN_MATH; }
    if (crea_score > max_score) { max_score = crea_score; best = SOMA_DOMAIN_CREATIVE; }

    // If no keywords matched, default to CHAT
    if (max_score == 0) return SOMA_DOMAIN_CHAT;
    return best;
}

// ============================================================
// soma_route
// ============================================================
SomaRouteResult soma_route(SomaRouterCtx *ctx, const char *input, int len) {
    SomaRouteResult r;
    r.route = SOMA_ROUTE_EXTERNAL;
    r.domain = SOMA_DOMAIN_UNKNOWN;
    r.confidence = 0.0f;
    r.reflex_response = NULL;
    r.reflex_response_len = 0;

    if (!ctx || !input || len <= 0) return r;

    ctx->total_routed++;

    // Phase 1: Check reflex table (O(1) response, no inference)
    for (int i = 0; i < (int)REFLEX_COUNT; i++) {
        if (_soma_contains(input, len, REFLEX_TABLE[i].pattern)) {
            r.route = SOMA_ROUTE_REFLEX;
            r.domain = REFLEX_TABLE[i].domain;
            r.confidence = 1.0f;
            r.reflex_response = REFLEX_TABLE[i].response;
            r.reflex_response_len = REFLEX_TABLE[i].response_len;
            ctx->reflex_count++;
            return r;
        }
    }

    // Phase 2: Classify domain
    r.domain = soma_classify_domain(input, len);

    // Phase 3: Routing decision based on domain + model availability
    switch (r.domain) {
        case SOMA_DOMAIN_SYSTEM:
        case SOMA_DOMAIN_POLICY:
            // System/policy: prefer SomaMind (it's specialized for this)
            if (ctx->soma_model_ready) {
                r.route = SOMA_ROUTE_INTERNAL;
                r.confidence = 0.9f;
                ctx->internal_count++;
            } else if (ctx->external_model_ready) {
                r.route = SOMA_ROUTE_EXTERNAL;
                r.confidence = 0.5f;
                ctx->external_count++;
            }
            break;

        case SOMA_DOMAIN_CREATIVE:
            // Creative: always external (needs Lunar core / big model)
            if (ctx->external_model_ready) {
                r.route = SOMA_ROUTE_EXTERNAL;
                r.confidence = 0.7f;
                ctx->external_count++;
            } else if (ctx->soma_model_ready) {
                r.route = SOMA_ROUTE_INTERNAL;
                r.confidence = 0.3f;
                ctx->internal_count++;
            }
            break;

        case SOMA_DOMAIN_CODE:
        case SOMA_DOMAIN_MATH:
            // Technical: speculative if both available, else external
            if (ctx->soma_model_ready && ctx->external_model_ready) {
                r.route = SOMA_ROUTE_DUAL;
                r.confidence = 0.8f;
                ctx->internal_count++;
                ctx->external_count++;
            } else if (ctx->external_model_ready) {
                r.route = SOMA_ROUTE_EXTERNAL;
                r.confidence = 0.6f;
                ctx->external_count++;
            } else if (ctx->soma_model_ready) {
                r.route = SOMA_ROUTE_INTERNAL;
                r.confidence = 0.4f;
                ctx->internal_count++;
            }
            break;

        default:  // CHAT, UNKNOWN
            // General: use whatever is available, prefer external for quality
            if (ctx->external_model_ready) {
                r.route = SOMA_ROUTE_EXTERNAL;
                r.confidence = 0.6f;
                ctx->external_count++;
            } else if (ctx->soma_model_ready) {
                r.route = SOMA_ROUTE_INTERNAL;
                r.confidence = 0.5f;
                ctx->internal_count++;
            }
            break;
    }

    // Update hit rate
    if (ctx->total_routed > 0) {
        ctx->reflex_hit_rate = (float)ctx->reflex_count / (float)ctx->total_routed;
    }

    return r;
}

// ============================================================
// soma_router_set_threshold
// ============================================================
void soma_router_set_threshold(SomaRouterCtx *ctx, float threshold) {
    if (!ctx) return;
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 1.0f) threshold = 1.0f;
    ctx->confidence_threshold = threshold;
}

// ============================================================
// soma_router_feedback
// ============================================================
void soma_router_feedback(SomaRouterCtx *ctx, SomaRoute route_used,
                          float actual_confidence) {
    if (!ctx) return;
    // Future: use this to auto-tune confidence_threshold
    // If actual_confidence was high for INTERNAL routes, lower threshold
    // If actual_confidence was low for INTERNAL routes, raise threshold
    (void)route_used;
    (void)actual_confidence;
}

// ============================================================
// soma_router_print_stats
// ============================================================
void soma_router_print_stats(const SomaRouterCtx *ctx, SomaPrintFn fn) {
    if (!ctx || !fn) return;
    char buf[128];

    fn("[SomaMind Router Stats]\n");

    // Simple itoa helper (inline)
    #define _SOMA_ITOA(val, b, pos) do { \
        int v = (val); \
        if (v == 0) { b[pos++] = '0'; } \
        else { char t[16]; int n=0; while(v>0){t[n++]='0'+(v%10);v/=10;} \
               for(int i=n-1;i>=0;i--) b[pos++]=t[i]; } \
    } while(0)

    int p = 0;
    const char *lab = "  total=";
    while (*lab) buf[p++] = *lab++;
    _SOMA_ITOA(ctx->total_routed, buf, p);
    buf[p++] = ' '; const char *l2 = "reflex=";
    while (*l2) buf[p++] = *l2++;
    _SOMA_ITOA(ctx->reflex_count, buf, p);
    buf[p++] = ' '; const char *l3 = "internal=";
    while (*l3) buf[p++] = *l3++;
    _SOMA_ITOA(ctx->internal_count, buf, p);
    buf[p++] = ' '; const char *l4 = "external=";
    while (*l4) buf[p++] = *l4++;
    _SOMA_ITOA(ctx->external_count, buf, p);
    buf[p++] = '\n'; buf[p] = 0;
    fn(buf);

    #undef _SOMA_ITOA
}
