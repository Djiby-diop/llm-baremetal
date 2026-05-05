// oo_voice_nlp.c — Moteur NLP LLM-backed (Implémentation)
//
// Architecture: deux passes LLM légères
//   Passe 1 (classification): prompt court + 1 token de sortie = intent
//   Passe 2 (génération): réponse naturelle courte (32-64 tokens max)
//
// Freestanding C11 — no libc, no malloc.

#include "oo_voice_nlp.h"
#include "oo_voice_router.h"
#include "oo_voice_context.h"

// ── Helpers string freestanding ───────────────────────────────────────────────

static int _nlp_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static void _nlp_memset(void *d, int v, int n) {
    char *p = d; for (int i = 0; i < n; i++) p[i] = (char)v;
}

static void _nlp_strcpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s[i] && i < cap-1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static void _nlp_strcat(char *d, const char *s, int cap) {
    int i = _nlp_strlen(d);
    int j = 0;
    while (s[j] && i < cap-1) { d[i++] = s[j++]; }
    d[i] = '\0';
}

static int _nlp_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static int _nlp_strchr(const char *s, char c) {
    for (int i = 0; s[i]; i++) if (s[i] == c) return i;
    return -1;
}

// Conversion minuscule ASCII
static char _nlp_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

// ── Table intent name → OVR_INTENT_* ─────────────────────────────────────────

typedef struct { const char *name; int id; } NlpIntentMap;

static const NlpIntentMap _intent_map[] = {
    { "GREETING",       OVR_INTENT_GREETING       },
    { "THANKS",         OVR_INTENT_THANKS         },
    { "ASK_STATE",      OVR_INTENT_ASK_STATE      },
    { "STOP",           OVR_INTENT_STOP           },
    { "REPEAT",         OVR_INTENT_REPEAT         },
    { "RUN_INFER",      OVR_INTENT_INFER          },
    { "BENCH",          OVR_INTENT_BENCH          },
    { "REBOOT",         OVR_INTENT_REBOOT         },
    { "WARDEN_STATUS",  OVR_INTENT_WARDEN_STATUS  },
    { "SWARM_STATUS",   OVR_INTENT_SWARM_STATUS   },
    { "LOAD_MODEL",     OVR_INTENT_LOAD_MODEL     },
    { "DISPLAY_SOMA",   OVR_INTENT_DISPLAY_SOMA   },
    { "VOICE_MODE",     OVR_INTENT_VOICE_MODE     },
    { "CALIBRATE",      OVR_INTENT_CALIBRATE      },
    { "WRITE_CODE",     OVR_INTENT_WRITE_CODE     },
    { "ANALYZE",        OVR_INTENT_ANALYZE        },
    { "DREAM",          OVR_INTENT_DREAM          },
    { "UNKNOWN",        OVR_INTENT_UNKNOWN        },
    { (void*)0, -1 }
};

static int _name_to_intent(const char *name) {
    for (int i = 0; _intent_map[i].name; i++) {
        if (_nlp_strncmp(name, _intent_map[i].name, 32) == 0)
            return _intent_map[i].id;
    }
    return OVR_INTENT_UNKNOWN;
}

static const char *_intent_to_name(int id) {
    for (int i = 0; _intent_map[i].name; i++) {
        if (_intent_map[i].id == id) return _intent_map[i].name;
    }
    return "UNKNOWN";
}

// ── Extraction d'entités par règles (pas de LLM pour ça) ─────────────────────

// Détecte les extensions fichiers courantes
static int _looks_like_file(const char *tok, int len) {
    const char *exts[] = { ".gguf", ".bin", ".efi", ".sh", ".c", ".h",
                           ".cfg", ".txt", ".json", ".py", (void*)0 };
    for (int i = 0; exts[i]; i++) {
        int el = _nlp_strlen(exts[i]);
        if (len >= el && _nlp_strncmp(tok + len - el, exts[i], el) == 0)
            return 1;
    }
    return 0;
}

// Détecte un nombre (entier ou décimal)
static int _looks_like_number(const char *tok, int len) {
    int dots = 0;
    for (int i = 0; i < len; i++) {
        char c = tok[i];
        if (c == '.') { dots++; if (dots > 1) return 0; continue; }
        if (c >= '0' && c <= '9') continue;
        if (i == len-1 && (c == 'M' || c == 'K' || c == 'G' || c == 'B')) continue;
        return 0;
    }
    return len > 0 ? 1 : 0;
}

// Détecte une commande REPL (/infer, /bench...)
static int _looks_like_cmd(const char *tok, int len) {
    return (len > 1 && tok[0] == '/') ? 1 : 0;
}

// Références anaphoriques (résolution de contexte)
static const char *_anaphora_fr[] = { "ça", "cela", "le", "la", "ce", "eux",
                                       "lui", "il", "elle", "précédent", (void*)0 };
static const char *_anaphora_en[] = { "it", "that", "this", "the", "previous",
                                       "last", "them", "he", "she", (void*)0 };

static int _looks_like_memory_ref(const char *tok, int len) {
    for (int i = 0; _anaphora_fr[i]; i++) {
        int al = _nlp_strlen(_anaphora_fr[i]);
        if (len == al && _nlp_strncmp(tok, _anaphora_fr[i], al) == 0) return 1;
    }
    for (int i = 0; _anaphora_en[i]; i++) {
        int al = _nlp_strlen(_anaphora_en[i]);
        if (len == al && _nlp_strncmp(tok, _anaphora_en[i], al) == 0) return 1;
    }
    return 0;
}

int oo_nlp_extract_entities(const char *input, int len,
                              OoNlpEntity *entities, int max_entities) {
    if (!input || !entities || max_entities <= 0) return 0;

    int count = 0;
    int i = 0;
    char tok[64];
    int  tok_start = 0;

    while (i <= len && count < max_entities) {
        char c = (i < len) ? input[i] : ' ';
        int delim = (c == ' ' || c == '\t' || c == '\n' || c == ',' ||
                     c == '.' || c == '?' || c == '!' || c == 0);

        if (!delim) {
            i++; continue;
        }

        int tlen = i - tok_start;
        if (tlen > 0 && tlen < 64) {
            // Copie token en minuscule
            for (int j = 0; j < tlen; j++) tok[j] = _nlp_lower(input[tok_start + j]);
            tok[tlen] = '\0';

            OoNlpEntityType t = OO_ENTITY_NONE;
            if (_looks_like_file(tok, tlen))        t = OO_ENTITY_FILE;
            else if (_looks_like_cmd(tok, tlen))    t = OO_ENTITY_COMMAND;
            else if (_looks_like_number(tok, tlen)) t = OO_ENTITY_NUMBER;
            else if (_looks_like_memory_ref(tok, tlen)) t = OO_ENTITY_MEMORY;

            if (t != OO_ENTITY_NONE) {
                entities[count].type  = t;
                entities[count].start = tok_start;
                entities[count].len   = tlen;
                entities[count].confidence = 0.85f;
                _nlp_strcpy(entities[count].value, tok, 64);
                count++;
            }
        }
        tok_start = i + 1;
        i++;
    }
    return count;
}

// ── Stub LLM inference ────────────────────────────────────────────────────────
//
// Interface vers oo_engine/llm/infer.h
// Déclarations minimales pour éviter la dépendance circulaire en header.
// Le lien se fait au link-time via llm-baremetal.efi.

extern int  oo_llm_infer_classify(void *ctx, const char *prompt, int plen,
                                   char *out_token, int out_cap,
                                   float *confidence);
extern int  oo_llm_infer_generate(void *ctx, const char *prompt, int plen,
                                   char *out_text, int out_cap,
                                   int max_tokens, float temperature);

// ── Implémentation publique ───────────────────────────────────────────────────

int oo_nlp_init(OoNlpConfig *cfg) {
    if (!cfg) return -1;
    if (!cfg->llm_ctx) return -1;
    if (cfg->ambiguity_threshold <= 0.0f) cfg->ambiguity_threshold = 0.55f;
    if (!cfg->lang) cfg->lang = "fr";
    return 0;
}

int oo_nlp_classify(OoNlpConfig *cfg,
                    const char *input, int input_len,
                    int *intent_out, float *confidence_out) {
    if (!cfg || !cfg->llm_ctx || !input || !intent_out) return -1;

    // Construction du prompt de classification
    char prompt[OO_NLP_PROMPT_CAP];
    _nlp_memset(prompt, 0, OO_NLP_PROMPT_CAP);

    int is_fr = (_nlp_strncmp(cfg->lang, "fr", 2) == 0);
    _nlp_strcpy(prompt,
        is_fr ? OO_NLP_SYSTEM_PROMPT_FR : OO_NLP_SYSTEM_PROMPT_EN,
        OO_NLP_PROMPT_CAP);
    _nlp_strcat(prompt, "Demande: ", OO_NLP_PROMPT_CAP);
    _nlp_strcat(prompt, input, OO_NLP_PROMPT_CAP);
    _nlp_strcat(prompt, "\nIntent: ", OO_NLP_PROMPT_CAP);

    char token[32];
    float conf = 0.0f;
    int r = oo_llm_infer_classify(cfg->llm_ctx, prompt, _nlp_strlen(prompt),
                                   token, 32, &conf);
    if (r < 0) {
        *intent_out = OVR_INTENT_UNKNOWN;
        if (confidence_out) *confidence_out = 0.0f;
        return -1;
    }

    // Nettoie le token (retire \n, espaces)
    char clean[32];
    int ci = 0;
    for (int i = 0; token[i] && ci < 31; i++) {
        char c = token[i];
        if (c >= 'A' && c <= 'Z') clean[ci++] = c;
        else if (c >= 'a' && c <= 'z') clean[ci++] = c - 32;
        else if (c == '_')             clean[ci++] = '_';
    }
    clean[ci] = '\0';

    *intent_out = _name_to_intent(clean);
    if (confidence_out) *confidence_out = conf;
    return 0;
}

int oo_nlp_generate_response(OoNlpConfig *cfg,
                              int intent,
                              const char *extra_context,
                              char *out_buf, int out_cap) {
    if (!cfg || !cfg->llm_ctx || !out_buf || out_cap <= 0) return -1;

    char prompt[OO_NLP_PROMPT_CAP];
    _nlp_memset(prompt, 0, OO_NLP_PROMPT_CAP);

    int is_fr = (_nlp_strncmp(cfg->lang, "fr", 2) == 0);
    _nlp_strcpy(prompt,
        is_fr ? OO_NLP_RESPONSE_PROMPT_FR : OO_NLP_RESPONSE_PROMPT_EN,
        OO_NLP_PROMPT_CAP);

    // Ajoute le contexte d'intent
    _nlp_strcat(prompt, "Action: ", OO_NLP_PROMPT_CAP);
    _nlp_strcat(prompt, _intent_to_name(intent), OO_NLP_PROMPT_CAP);
    if (extra_context && _nlp_strlen(extra_context) > 0) {
        _nlp_strcat(prompt, "\nContexte: ", OO_NLP_PROMPT_CAP);
        _nlp_strcat(prompt, extra_context, OO_NLP_PROMPT_CAP);
    }
    _nlp_strcat(prompt, "\nReponse: ", OO_NLP_PROMPT_CAP);

    return oo_llm_infer_generate(cfg->llm_ctx, prompt, _nlp_strlen(prompt),
                                  out_buf, out_cap, OO_NLP_RESP_BUDGET, 0.7f);
}

int oo_nlp_clarify(OoNlpConfig *cfg,
                   const OoNlpResult *res,
                   char *out_buf, int out_cap) {
    if (!cfg || !res || !out_buf || out_cap <= 0) return -1;
    if (!res->ambiguous) { out_buf[0] = '\0'; return 0; }

    // Prompt de clarification
    char prompt[OO_NLP_PROMPT_CAP];
    _nlp_memset(prompt, 0, OO_NLP_PROMPT_CAP);

    int is_fr = (_nlp_strncmp(cfg->lang, "fr", 2) == 0);
    if (is_fr) {
        _nlp_strcpy(prompt, "Tu es OO. La demande est ambiguë. "
                    "Pose UNE question courte pour clarifier: ",
                    OO_NLP_PROMPT_CAP);
    } else {
        _nlp_strcpy(prompt, "You are OO. The request is ambiguous. "
                    "Ask ONE short question to clarify: ",
                    OO_NLP_PROMPT_CAP);
    }
    _nlp_strcat(prompt, res->clarification, OO_NLP_PROMPT_CAP);

    return oo_llm_infer_generate(cfg->llm_ctx, prompt, _nlp_strlen(prompt),
                                  out_buf, out_cap, 48, 0.5f);
}

int oo_nlp_to_repl_cmd(const OoNlpResult *result, char *cmd_out, int cmd_cap) {
    if (!result || !cmd_out || cmd_cap <= 0) return -1;
    cmd_out[0] = '\0';

    // Table intent → commande REPL de base
    static const struct { int id; const char *base; } intent_cmd[] = {
        { OVR_INTENT_INFER,         "/infer"          },
        { OVR_INTENT_BENCH,         "/bench"          },
        { OVR_INTENT_REBOOT,        "/reboot"         },
        { OVR_INTENT_WARDEN_STATUS, "/warden_status"  },
        { OVR_INTENT_SWARM_STATUS,  "/swarm_status"   },
        { OVR_INTENT_LOAD_MODEL,    "/load_model"     },
        { OVR_INTENT_DISPLAY_SOMA,  "/display"        },
        { OVR_INTENT_VOICE_MODE,    "/voice"          },
        { OVR_INTENT_CALIBRATE,     "/calibrate"      },
        { OVR_INTENT_WRITE_CODE,    "/write_code"     },
        { OVR_INTENT_ANALYZE,       "/analyze"        },
        { OVR_INTENT_DREAM,         "/dream"          },
        { -1, (void*)0 }
    };

    const char *base = "/unknown";
    for (int i = 0; intent_cmd[i].id >= 0; i++) {
        if (intent_cmd[i].id == result->intent) {
            base = intent_cmd[i].base;
            break;
        }
    }

    _nlp_strcpy(cmd_out, base, cmd_cap);

    // Ajoute les entités comme paramètres
    for (int i = 0; i < result->entity_count; i++) {
        const OoNlpEntity *e = &result->entities[i];
        if (e->type == OO_ENTITY_FILE || e->type == OO_ENTITY_NUMBER) {
            _nlp_strcat(cmd_out, " ", cmd_cap);
            _nlp_strcat(cmd_out, e->value, cmd_cap);
        }
    }
    return 0;
}

int oo_nlp_analyze(OoNlpConfig *cfg,
                   const char *input, int input_len,
                   OoVoiceContext *ctx,
                   OoNlpResult *result) {
    if (!cfg || !cfg->llm_ctx || !input || !result) return -1;

    _nlp_memset(result, 0, sizeof(OoNlpResult));
    result->intent = OVR_INTENT_UNKNOWN;

    // Étape 1: Classification
    float conf = 0.0f;
    int intent = OVR_INTENT_UNKNOWN;
    int r = oo_nlp_classify(cfg, input, input_len, &intent, &conf);

    result->intent            = (r == 0) ? intent : OVR_INTENT_UNKNOWN;
    result->intent_confidence = conf;
    result->used_llm          = 1;

    // Étape 2: Extraction d'entités (règles, pas LLM)
    result->entity_count = oo_nlp_extract_entities(input, input_len,
                                                    result->entities,
                                                    OO_NLP_MAX_ENTITIES);

    // Résolution des références anaphoriques via contexte
    for (int i = 0; i < result->entity_count; i++) {
        if (result->entities[i].type == OO_ENTITY_MEMORY && ctx) {
            // Remplace par le dernier nom de fichier/commande cité en contexte
            const char *last = ovc_last_oo_response(ctx);
            if (last && _nlp_strlen(last) > 0) {
                _nlp_strcpy(result->entities[i].value, last, 64);
                result->entities[i].type = OO_ENTITY_MEMORY;
                result->entities[i].confidence = 0.7f;
            }
        }
    }

    // Étape 3: Ambiguïté
    if (conf < cfg->ambiguity_threshold && result->intent != OVR_INTENT_UNKNOWN) {
        result->ambiguous = 1;
        _nlp_strcpy(result->clarification,
                    (_nlp_strncmp(cfg->lang, "fr", 2) == 0)
                        ? "Pouvez-vous preciser votre demande?"
                        : "Can you clarify your request?",
                    128);
    }

    // Étape 4: Construction commande REPL
    oo_nlp_to_repl_cmd(result, result->cmd, 128);
    result->has_cmd = (result->cmd[0] == '/') ? 1 : 0;

    // Étape 5: Génération réponse naturelle (uniquement si non ambigu)
    if (!result->ambiguous && result->intent != OVR_INTENT_UNKNOWN) {
        oo_nlp_generate_response(cfg, result->intent, (void*)0,
                                  result->response, OO_NLP_MAX_RESPONSE);
        result->response_len = _nlp_strlen(result->response);
    }

    return 0;
}

// ── Route unifiée (keyword → NLP fallback) ────────────────────────────────────

int oo_nlp_route(OoNlpConfig *cfg,
                 const char *text, int len,
                 OoVoiceContext *ctx,
                 OoVoiceDecision *decision) {
    if (!decision) return -1;
    _nlp_memset(decision, 0, sizeof(OoVoiceDecision));

    // Passe 1: keyword scoring (rapide)
    OvrResult kw = ovr_route(text, len);

    if (kw.score >= 40) {
        // Confiant — exécution directe
        decision->intent      = kw.intent;
        decision->confidence  = (float)kw.score / 100.0f;
        _nlp_strcpy(decision->cmd, kw.cmd_template, 128);
        decision->needs_clarification = 0;
        return 0;
    }

    if (!cfg || !cfg->llm_ctx) {
        // Pas de LLM disponible → meilleur guess keyword
        decision->intent     = kw.intent;
        decision->confidence = (float)kw.score / 100.0f;
        _nlp_strcpy(decision->cmd, kw.cmd_template, 128);
        decision->needs_clarification = (kw.score < 20) ? 1 : 0;
        return 0;
    }

    // Passe 2: NLP LLM
    OoNlpResult nlp;
    oo_nlp_analyze(cfg, text, len, ctx, &nlp);

    decision->intent     = nlp.intent;
    decision->confidence = nlp.intent_confidence;
    _nlp_strcpy(decision->cmd,      nlp.cmd,      128);
    _nlp_strcpy(decision->response, nlp.response, 512);

    if (nlp.ambiguous) {
        decision->needs_clarification = 1;
        _nlp_strcpy(decision->clarification, nlp.clarification, 128);
    }

    return 0;
}

// ── Debug UART ────────────────────────────────────────────────────────────────

static void _nlp_putc(char c) {
    for (int i = 0; i < 10000; i++) {
        uint8_t s; __asm__ volatile ("inb $0x3F8+5,%0":"=a"(s));
        if (s & 0x20) break;
    }
    __asm__ volatile ("outb %0,$0x3F8"::"a"(c));
}

static void _nlp_print(const char *s) { while (*s) _nlp_putc(*s++); }

static void _nlp_print_int(int v) {
    char buf[16]; int i = 0;
    if (v < 0) { _nlp_putc('-'); v = -v; }
    if (v == 0) { _nlp_putc('0'); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) _nlp_putc(buf[--i]);
}

void oo_nlp_dump(const OoNlpResult *result) {
    if (!result) return;
    _nlp_print("[NLP] intent=");
    _nlp_print(_intent_to_name(result->intent));
    _nlp_print(" conf=");
    _nlp_print_int((int)(result->intent_confidence * 100));
    _nlp_print("% entities=");
    _nlp_print_int(result->entity_count);
    _nlp_print(" ambiguous=");
    _nlp_print(result->ambiguous ? "yes" : "no");
    _nlp_putc('\n');
    if (result->has_cmd) {
        _nlp_print("[NLP] cmd: ");
        _nlp_print(result->cmd);
        _nlp_putc('\n');
    }
}
