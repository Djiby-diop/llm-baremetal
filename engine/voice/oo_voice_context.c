// oo_voice_context.c — Multi-Turn Dialogue Context Manager (Implementation)
//
// Freestanding C11 — no libc, no malloc, no external deps.

#include "oo_voice_context.h"

// ── Freestanding string helpers ──────────────────────────────────────────────

static int _vc_len(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}

static void _vc_cpy(char *dst, const char *src, int cap) {
    if (!dst || !src || cap <= 0) return;
    int i = 0;
    while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void _vc_cat(char *dst, const char *src, int cap) {
    if (!dst || !src) return;
    int i = _vc_len(dst);
    while (i < cap - 1 && *src) dst[i++] = *src++;
    dst[i] = '\0';
}

static int _vc_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static int _vc_has(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    int hn = _vc_len(needle);
    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (j < hn && haystack[i+j] && haystack[i+j] == needle[j]) j++;
        if (j == hn) return 1;
    }
    return 0;
}

static int _vc_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

// normalize input: lowercase, replace punctuation with space
static void _vc_normalize(char *out, int cap, const char *in) {
    int i = 0;
    for (int j = 0; in[j] && i < cap - 1; j++) {
        int c = _vc_tolower((unsigned char)in[j]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '\'' || c == '-') {
            out[i++] = (char)c;
        } else {
            if (i > 0 && out[i-1] != ' ') out[i++] = ' ';
        }
    }
    out[i] = '\0';
}

// ── Init ─────────────────────────────────────────────────────────────────────

void ovc_init(OvcContext *ctx) {
    if (!ctx) return;
    // Zero everything
    for (int i = 0; i < OVC_MAX_TURNS; i++) {
        ctx->turns[i].speaker    = OVC_SPEAKER_HUMAN;
        ctx->turns[i].text[0]    = '\0';
        ctx->turns[i].cmd[0]     = '\0';
        ctx->turns[i].intent[0]  = '\0';
        ctx->turns[i].score      = 0;
        ctx->turns[i].tick       = 0;
    }
    for (int i = 0; i < OVC_ENTITIES_MAX; i++) {
        ctx->entities[i].key[0]   = '\0';
        ctx->entities[i].value[0] = '\0';
        ctx->entities[i].last_updated = 0;
    }
    ctx->turn_count          = 0;
    ctx->head                = 0;
    ctx->entity_count        = 0;
    ctx->last_cmd[0]         = '\0';
    ctx->last_intent[0]      = '\0';
    ctx->last_response[0]    = '\0';
    ctx->active_task[0]      = '\0';
    ctx->awaiting_confirm    = 0;
    ctx->total_turns         = 0;
    ctx->total_commands_executed = 0;
    _vc_cpy(ctx->active_task, "none", sizeof(ctx->active_task));
}

// ── Turn management ───────────────────────────────────────────────────────────

void ovc_push_human(OvcContext *ctx, const char *text,
                    const char *cmd, const char *intent, int score,
                    uint32_t tick) {
    if (!ctx) return;
    int idx = ctx->head % OVC_MAX_TURNS;
    ctx->turns[idx].speaker = OVC_SPEAKER_HUMAN;
    _vc_cpy(ctx->turns[idx].text,   text   ? text   : "", OVC_TEXT_LEN);
    _vc_cpy(ctx->turns[idx].cmd,    cmd    ? cmd    : "", OVC_CMD_LEN);
    _vc_cpy(ctx->turns[idx].intent, intent ? intent : "", 48);
    ctx->turns[idx].score = score;
    ctx->turns[idx].tick  = tick;
    ctx->head++;
    if (ctx->turn_count < OVC_MAX_TURNS) ctx->turn_count++;
    ctx->total_turns++;
    if (cmd && cmd[0]) {
        _vc_cpy(ctx->last_cmd, cmd, OVC_CMD_LEN);
        ctx->total_commands_executed++;
    }
    if (intent && intent[0])
        _vc_cpy(ctx->last_intent, intent, sizeof(ctx->last_intent));
}

void ovc_push_oo(OvcContext *ctx, const char *response, uint32_t tick) {
    if (!ctx) return;
    int idx = ctx->head % OVC_MAX_TURNS;
    ctx->turns[idx].speaker  = OVC_SPEAKER_OO;
    _vc_cpy(ctx->turns[idx].text, response ? response : "", OVC_TEXT_LEN);
    ctx->turns[idx].cmd[0]    = '\0';
    ctx->turns[idx].intent[0] = '\0';
    ctx->turns[idx].score     = 0;
    ctx->turns[idx].tick      = tick;
    ctx->head++;
    if (ctx->turn_count < OVC_MAX_TURNS) ctx->turn_count++;
    ctx->total_turns++;
    if (response)
        _vc_cpy(ctx->last_response, response, OVC_RESPONSE_LEN);
}

// ── Entity memory ─────────────────────────────────────────────────────────────

const char *ovc_get_entity(const OvcContext *ctx, const char *key) {
    if (!ctx || !key) return (const char*)0;
    for (int i = 0; i < ctx->entity_count; i++) {
        if (_vc_eq(ctx->entities[i].key, key))
            return ctx->entities[i].value;
    }
    return (const char*)0;
}

void ovc_set_entity(OvcContext *ctx, const char *key, const char *value,
                    uint32_t tick) {
    if (!ctx || !key || !value) return;
    // Update existing
    for (int i = 0; i < ctx->entity_count; i++) {
        if (_vc_eq(ctx->entities[i].key, key)) {
            _vc_cpy(ctx->entities[i].value, value, OVC_ENTITY_LEN);
            ctx->entities[i].last_updated = tick;
            return;
        }
    }
    // Insert new (evict oldest if full — LRU by index)
    int slot = ctx->entity_count < OVC_ENTITIES_MAX
               ? ctx->entity_count++
               : 0; // simple evict first slot when full
    _vc_cpy(ctx->entities[slot].key,   key,   32);
    _vc_cpy(ctx->entities[slot].value, value, OVC_ENTITY_LEN);
    ctx->entities[slot].last_updated = tick;
}

// ── Last turn accessors ───────────────────────────────────────────────────────

const char *ovc_last_cmd(const OvcContext *ctx) {
    return ctx ? ctx->last_cmd : (const char*)0;
}

const char *ovc_last_human_text(const OvcContext *ctx) {
    if (!ctx || ctx->turn_count == 0) return (const char*)0;
    // Walk backwards to find last human turn
    for (int i = 1; i <= ctx->turn_count; i++) {
        int idx = (ctx->head - i + OVC_MAX_TURNS * 32) % OVC_MAX_TURNS;
        if (ctx->turns[idx].speaker == OVC_SPEAKER_HUMAN)
            return ctx->turns[idx].text;
    }
    return (const char*)0;
}

// ── Pattern detectors ────────────────────────────────────────────────────────

int ovc_is_repeat_request(const char *input, int len) {
    (void)len;
    if (!input) return 0;
    char buf[256];
    _vc_normalize(buf, sizeof(buf), input);
    static const char *repeat_kw[] = {
        "again","encore","redo","refais","redis","meme chose","same","rejoue",
        "une autre fois","replay","repeat","do it again","recommence",
        "retente","retry","bis","bisrepeat", (const char*)0
    };
    for (int i = 0; repeat_kw[i]; i++)
        if (_vc_has(buf, repeat_kw[i])) return 1;
    return 0;
}

int ovc_parse_confirmation(const char *input) {
    if (!input) return 0;
    char buf[128];
    _vc_normalize(buf, sizeof(buf), input);
    // Yes
    static const char *yes_kw[] = {
        "yes","oui","yep","yup","ok","okay","sure","absolument","bien sur",
        "vas-y","vas y","go","let s go","let's go","affirm","affirmatif",
        "positif","correct","exactement","nickel","parfait","agree","d accord",
        "d'accord","confirm","confirme", (const char*)0
    };
    for (int i = 0; yes_kw[i]; i++)
        if (_vc_has(buf, yes_kw[i])) return 1;
    // No
    static const char *no_kw[] = {
        "no","non","nope","nah","negative","negatif","stop","annule","cancel",
        "pas maintenant","not now","laisse","laisse tomber","nevermind",
        "oublie","abort","abandonne", (const char*)0
    };
    for (int i = 0; no_kw[i]; i++)
        if (_vc_has(buf, no_kw[i])) return -1;
    return 0;
}

int ovc_is_recall_request(const char *input) {
    if (!input) return 0;
    char buf[256];
    _vc_normalize(buf, sizeof(buf), input);
    static const char *recall_kw[] = {
        "what did i say","qu est-ce que j ai dit","what was my","rappelle moi",
        "remind me","last command","derniere commande","what did you do",
        "qu as-tu fait","previous","precedent","avant","before", (const char*)0
    };
    for (int i = 0; recall_kw[i]; i++)
        if (_vc_has(buf, recall_kw[i])) return 1;
    return 0;
}

int ovc_is_greeting(const char *input, int len) {
    (void)len;
    if (!input) return 0;
    char buf[128];
    _vc_normalize(buf, sizeof(buf), input);
    static const char *greet_kw[] = {
        "hello","hi ","hey ","bonjour","salut","bonsoir","coucou","good morning",
        "good evening","bon matin","bonne nuit","howdy","hola","yo ","what's up",
        "ca va","comment va","how are you","tu vas bien", (const char*)0
    };
    for (int i = 0; greet_kw[i]; i++)
        if (_vc_has(buf, greet_kw[i])) return 1;
    return 0;
}

int ovc_is_gratitude(const char *input, int len) {
    (void)len;
    if (!input) return 0;
    char buf[128];
    _vc_normalize(buf, sizeof(buf), input);
    static const char *thanks_kw[] = {
        "merci","thanks","thank you","thanks a lot","merci beaucoup","nicely done",
        "great job","good job","bien joue","parfait","excellent","bravo","super",
        "impressive","impressionnant","awesome","amazing","genial","incroyable",
        "well done","beau travail","chapeau","kudos", (const char*)0
    };
    for (int i = 0; thanks_kw[i]; i++)
        if (_vc_has(buf, thanks_kw[i])) return 1;
    return 0;
}

int ovc_is_confusion(const char *input, int len) {
    (void)len;
    if (!input) return 0;
    char buf[128];
    _vc_normalize(buf, sizeof(buf), input);
    static const char *confuse_kw[] = {
        "quoi","what","pardon","huh","hein","je comprends pas","i don t understand",
        "i don't understand","i dont understand","explain","explique","clarify",
        "clarifie","what do you mean","que veux-tu dire","que veux tu dire",
        "je suis perdu","lost","confus","confused","refrase","rephrase",
        "repete","repeat that","dis autrement","say again", (const char*)0
    };
    for (int i = 0; confuse_kw[i]; i++)
        if (_vc_has(buf, confuse_kw[i])) return 1;
    return 0;
}

// ── Context summary for LLM prompt injection ─────────────────────────────────

void ovc_push_turn(OvcContext *ctx, const char *human_text, int human_len,
                   const char *oo_response, int oo_len) {
    (void)human_len; (void)oo_len;
    ovc_push_human(ctx, human_text, "", "", 0, 0);
    if (oo_response && oo_response[0])
        ovc_push_oo(ctx, oo_response, 0);
}

void ovc_last_oo_response(const OvcContext *ctx, char *out, int cap) {
    if (!ctx || !out || cap <= 0) { if (out) out[0] = '\0'; return; }
    _vc_cpy(out, ctx->last_response, cap);
}

void ovc_context_summary(const OvcContext *ctx, char *out, int cap) {
    if (!ctx || !out || cap <= 0) return;
    out[0] = '\0';
    _vc_cat(out, "[CONTEXT]\n", cap);

    // Named entities
    if (ctx->entity_count > 0) {
        _vc_cat(out, "Known: ", cap);
        for (int i = 0; i < ctx->entity_count && i < 6; i++) {
            _vc_cat(out, ctx->entities[i].key, cap);
            _vc_cat(out, "=", cap);
            _vc_cat(out, ctx->entities[i].value, cap);
            _vc_cat(out, " ", cap);
        }
        _vc_cat(out, "\n", cap);
    }

    // Last few turns
    int start = ctx->head - (ctx->turn_count < 4 ? ctx->turn_count : 4);
    for (int i = start; i < ctx->head; i++) {
        int idx = (i + OVC_MAX_TURNS * 32) % OVC_MAX_TURNS;
        const char *who = ctx->turns[idx].speaker == OVC_SPEAKER_HUMAN
                          ? "USER: " : "OO: ";
        _vc_cat(out, who, cap);
        _vc_cat(out, ctx->turns[idx].text, cap);
        _vc_cat(out, "\n", cap);
    }
}
