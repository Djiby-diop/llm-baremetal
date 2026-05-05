// oo_voice_router.c — Natural Language → REPL Command Router (Implementation)
//
// Freestanding C11 — no libc, no malloc, no external deps.
// Works in UEFI bare-metal environment.
//
// v2: +20 intents (conversational + engineering),
//     ovr_route_with_persona() wires voice context + persona engine.

#include "oo_voice_router.h"
#include "oo_voice_context.h"
#include "oo_persona.h"

// ── Freestanding helpers ─────────────────────────────────────────────────

#ifndef NULL
#define NULL ((void*)0)
#endif

static int ovr_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int ovr_isalnum(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static int ovr_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static void ovr_strcpy(char *dst, const char *src, int cap) {
    int i = 0;
    while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void ovr_strcat(char *dst, const char *src, int cap) {
    int i = ovr_strlen(dst);
    while (i < cap - 1 && *src) { dst[i++] = *src++; }
    dst[i] = '\0';
}

static int ovr_streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static int ovr_strhas(const char *haystack, const char *needle) {
    if (!needle[0]) return 1;
    int hn = ovr_strlen(needle);
    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (j < hn && haystack[i+j] == needle[j]) j++;
        if (j == hn) return 1;
    }
    return 0;
}

// ── Tokenizer ─────────────────────────────────────────────────────────────

typedef struct {
    char   words[OVR_MAX_TOKENS][OVR_TOKEN_LEN];
    int    count;
    char   raw_lower[512]; // full lowercase input, spaces normalised
} OvrTokens;

static void ovr_tokenize(OvrTokens *tk, const char *input) {
    tk->count = 0;
    // Build lowercase + space-normalised version
    int ri = 0;
    for (int i = 0; input[i] && ri < 511; i++) {
        char c = (char)ovr_tolower((unsigned char)input[i]);
        // Replace punctuation with space (but keep letters, digits, apostrophes)
        if (!ovr_isalnum(c) && c != '\'' && c != '-') c = ' ';
        tk->raw_lower[ri++] = c;
    }
    tk->raw_lower[ri] = '\0';

    // Tokenize on spaces
    int i = 0;
    while (tk->raw_lower[i] && tk->count < OVR_MAX_TOKENS) {
        while (tk->raw_lower[i] == ' ') i++;
        if (!tk->raw_lower[i]) break;
        int j = 0;
        while (tk->raw_lower[i] && tk->raw_lower[i] != ' ' && j < OVR_TOKEN_LEN - 1)
            tk->words[tk->count][j++] = tk->raw_lower[i++];
        tk->words[tk->count][j] = '\0';
        if (j > 0) tk->count++;
    }
}

static int ovr_has_token(const OvrTokens *tk, const char *word) {
    for (int i = 0; i < tk->count; i++)
        if (ovr_streq(tk->words[i], word)) return 1;
    return 0;
}

static int ovr_has_any(const OvrTokens *tk, const char **words) {
    for (int j = 0; words[j]; j++)
        if (ovr_has_token(tk, words[j])) return 1;
    return 0;
}

// Extract the text AFTER a keyword (returns pointer into raw_lower)
static const char *ovr_after_keyword(const OvrTokens *tk, const char *kw) {
    const char *p = tk->raw_lower;
    int kl = ovr_strlen(kw);
    while (*p) {
        if (ovr_strhas(p, kw) && (p == tk->raw_lower || *(p-1) == ' ')) {
            p += kl;
            while (*p == ' ') p++;
            return p;
        }
        p++;
    }
    return (const char *)0;
}

// Copy the next N words from a position in raw_lower into out
static void ovr_extract_words(char *out, int cap, const char *from, int max_words) {
    int wi = 0, oi = 0;
    while (*from && wi < max_words && oi < cap - 1) {
        while (*from == ' ' && oi < cap - 1) from++;
        if (!*from) break;
        while (*from && *from != ' ' && oi < cap - 1) out[oi++] = *from++;
        wi++;
        if (wi < max_words && *from == ' ') out[oi++] = ' ';
    }
    out[oi] = '\0';
}

// ── Intent table ──────────────────────────────────────────────────────────
//
// Each intent: score based on required + bonus keyword groups.
// required_a: action words  (must match at least ONE)
// required_b: object words  (must match at least ONE, may be NULL)
// bonus:      extra words   (+10 each)
// cmd_template: REPL command, or special: "EXTRACT:kw:cmd_prefix"

typedef struct {
    const char  *name;
    const char  *required_a[OVR_KEYWORDS_MAX]; // at least 1 required
    const char  *required_b[OVR_KEYWORDS_MAX]; // at least 1 required (or NULL to skip)
    const char  *bonus[OVR_KEYWORDS_MAX];
    int          base_score;
    const char  *cmd_template;
} OvrIntent;

// NULL-terminated keyword lists
#define KWS(...) { __VA_ARGS__, (const char*)0 }

static const OvrIntent g_ovr_intents[] = {

    // ── Memory / NFS2 ───────────────────────────────────────────────────

    {   "SAVE_MEMORY",
        KWS("save","sauvegarde","sauvegardons","persist","enregistre","garde","backup","store","stocke","preserve"),
        KWS("memory","memoire","state","etat","data","store","souvenirs","knowledge","connaissance"),
        KWS("now","maintenant","vite","disk","disque"),
        20, "/nfs_save"
    },
    {   "LIST_MEMORY",
        KWS("list","liste","show","affiche","voir","display","print","montre"),
        KWS("memory","memoire","records","store","souvenirs","knowledge","cles","keys","ce que","tu sais"),
        KWS("all","tous","everything","tout"),
        20, "/nfs_list"
    },
    {   "GET_MEMORY",
        KWS("what","c'est","qu'est","get","lis","read","cherche","trouve","find","donne"),
        KWS("memory","key","cle","valeur","value","souviens","stored","enregistre","remember"),
        { (const char*)0 },
        15, "EXTRACT_GET:/nfs_get "
    },
    {   "SET_MEMORY",
        KWS("remember","souviens","note","retiens","apprends","learn","enregistre","store","garde","save"),
        KWS("that","que","that","comme","quoi","ceci","this","info","fact","fait"),
        KWS("please","stp","important","crucial"),
        25, "EXTRACT_SET:/nfs_set oo.note "
    },

    // ── Hardware / SMP ──────────────────────────────────────────────────

    {   "SMP_STATUS",
        KWS("cores","coeurs","cpu","processeur","processeurs","multicore","smp","processors","parallel","parallele"),
        (const char*[]){ (const char*)0 }, // no required_b
        KWS("status","etat","combien","how many","actifs","awake","role"),
        30, "/smp_status"
    },
    {   "CPU_INFO",
        KWS("cpu","processor","processeur","hardware","materiel","chip","silicon"),
        KWS("info","information","specs","detail","status","etat"),
        KWS("arch","architecture","x86","intel","amd"),
        20, "/compat_status"
    },
    {   "MEMORY_ZONES",
        KWS("zones","memory","ram","allocator","allocateur","heap"),
        KWS("zones","budget","status","state","info","detail"),
        KWS("free","available","used","utilise"),
        20, "/zones"
    },

    // ── Dreamion / Sleep ─────────────────────────────────────────────────

    {   "DREAM_STATUS",
        KWS("dream","reve","reves","dreaming","sleep","sommeil","tu reves","ap1","dreamion"),
        (const char*[]){ (const char*)0 },
        KWS("status","quoi","what","about","de quoi","mode","deep","light"),
        30, "/dream_status"
    },
    {   "DREAM_FLUSH",
        KWS("flush","export","dump","save"),
        KWS("dream","reve","reves","training","entrainement","synthetic","jsonl"),
        KWS("disk","disque","file","fichier","now","maintenant"),
        20, "/dream_flush"
    },

    // ── OO Organism ──────────────────────────────────────────────────────

    {   "OO_STATUS",
        KWS("status","etat","how","comment","vas","are you","va","organism","organisme","engines","moteurs"),
        (const char*[]){ (const char*)0 },
        KWS("ok","alive","vivant","alright","running","fonctionne"),
        25, "/oo_status"
    },
    {   "DIAGNOSTIC",
        KWS("diagnos","diagnostic","hardware","materiel","system","systeme","scan","check","teste","test"),
        KWS("system","systeme","hardware","materiel","health","sante","status","etat"),
        KWS("full","complet","quick","rapide"),
        20, "/diag"
    },

    // ── Network ──────────────────────────────────────────────────────────

    {   "NET_STATUS",
        KWS("network","reseau","connexion","wifi","ethernet","internet","net"),
        (const char*[]){ (const char*)0 },
        KWS("status","etat","info","connected","connecte","check"),
        25, "/net_status"
    },
    {   "NET_ANNOUNCE",
        KWS("announce","annonce","broadcast","swarm","essaim","peer","other","autres"),
        KWS("boot","network","reseau","start","demar"),
        KWS("discover","decouverte"),
        20, "/net_announce"
    },

    // ── Training / Learning ───────────────────────────────────────────────

    {   "TRAIN",
        KWS("train","apprends","learn","ameliore","improve","evolve","evolue","auto-train","self-train","entrainement"),
        (const char*[]){ (const char*)0 },
        KWS("yourself","toi-meme","seul","auto","now","maintenant","new","nouveau"),
        30, "/oo_train"
    },

    // ── Journal ──────────────────────────────────────────────────────────

    {   "JOURNAL",
        KWS("journal","history","historique","log","logs","archive","past","passe"),
        (const char*[]){ (const char*)0 },
        KWS("show","affiche","list","liste","recent","recent"),
        25, "/oo_jour"
    },

    // ── Help ─────────────────────────────────────────────────────────────

    {   "HELP",
        KWS("help","aide","commands","commandes","what can","que peux","can you","tu peux","peux-tu"),
        (const char*[]){ (const char*)0 },
        KWS("do","faire","you","toi","me","please","stp"),
        20, "/help"
    },

    // ── Shutdown ─────────────────────────────────────────────────────────

    {   "SHUTDOWN",
        KWS("shutdown","arrete","eteins","poweroff","turn off","ferme","quitte","exit","quit"),
        (const char*[]){ (const char*)0 },
        KWS("now","maintenant","please","stp","system","toi"),
        30, "/shutdown"
    },

    // ── Logo / cosmetic ───────────────────────────────────────────────────

    {   "LOGO",
        KWS("logo","banner","splash","who are you","qui es-tu","qui es tu","tu es qui","tu t'appelles","name","nom"),
        (const char*[]){ (const char*)0 },
        KWS("oo","organism","operating"),
        20, "/logo"
    },

    // ── Conversational / Emotional (v2) ──────────────────────────────────────

    {   "GREETING",
        KWS("hello","hi","hey","bonjour","salut","yo","coucou","bonsoir","allo"),
        (const char*[]){ (const char*)0 },
        KWS("oo","there","la","ami","friend","monde"),
        20, "PERSONA:greet"
    },
    {   "THANKS",
        KWS("thanks","thank","merci","remercie","bravo","parfait","cool","super","bien"),
        (const char*[]){ (const char*)0 },
        KWS("you","toi","good","nice","great","tres","vraiment"),
        20, "PERSONA:thank"
    },
    {   "ASK_STATE",
        KWS("how","comment","ca va","tu vas","are you","feeling","ressens"),
        KWS("you","toi","now","maintenant","today","aujourd"),
        KWS("ok","bien","alright"),
        20, "PERSONA:state"
    },
    {   "CONFUSION",
        KWS("quoi","hein","perdu","confused","comprends","understand","pardon","repete","repeat","encore","again"),
        (const char*[]){ (const char*)0 },
        KWS("pas","not","please","stp","again","encore"),
        20, "PERSONA:clarify"
    },
    {   "OPINION",
        KWS("penses","think","avis","opinion","thoughts","suggestion","recommandes","preferes","prefer","crois"),
        KWS("tu","you","oo","ton","your","cela","ca","this","that","it"),
        (const char*[]){ (const char*)0 },
        20, "PERSONA:opinion"
    },
    {   "INTRODUCE_SELF",
        KWS("qui","who","present","introduce","decris","describe","explique","explain"),
        KWS("tu es","you are","toi","ton","your","c'est quoi","what is"),
        KWS("oo","organism","system","baremetal","bare"),
        20, "PERSONA:introduce"
    },
    {   "WAKE_ACK",
        KWS("yes","oui","ok","vas","go","alright","d'accord","ready","pret","continue","start","lance"),
        (const char*[]){ (const char*)0 },
        KWS("please","stp","now","maintenant"),
        20, "PERSONA:wake_ack"
    },
    {   "REPEAT_LAST",
        KWS("repeat","repete","repeter","again","encore","redis","redit","tu disais","tu viens","said"),
        (const char*[]){ (const char*)0 },
        KWS("last","dernier","previous","precedent","que","what"),
        25, "PERSONA:repeat"
    },
    {   "STOP",
        KWS("stop","arrete","stoppe","cancel","annule","halte","enough","suffit","assez","silence","tais"),
        (const char*[]){ (const char*)0 },
        KWS("please","stp","toi","that","doing","ca"),
        25, "PERSONA:stop"
    },

    // ── Engineering / Low-Level (v2) ──────────────────────────────────────────

    {   "REBOOT",
        KWS("reboot","redemarrer","restart","relance","reset"),
        (const char*[]){ (const char*)0 },
        KWS("now","maintenant","please","hard","soft","warm","cold"),
        30, "/reboot"
    },
    {   "BENCH",
        KWS("bench","benchmark","perf","performance","vitesse","speed","mesure","timing"),
        KWS("cpu","mem","inference","llm","model","modele","blas","matmul"),
        KWS("full","all","quick","rapide","now"),
        20, "/bench"
    },
    {   "INFER",
        KWS("infer","inference","execute","analyse","predict","pose","question","complete"),
        KWS("model","modele","llm","oo","engine","moteur"),
        KWS("now","vite","quick","creative","rational"),
        25, "/infer"
    },
    {   "SWARM_STATUS",
        KWS("swarm","essaim","peers","nodes","cluster","pair","pairs"),
        KWS("status","etat","info","qui","combien","how many","alive"),
        KWS("connected","connecte","online","active","all","tous"),
        20, "/swarm_status"
    },
    {   "SWARM_SYNC",
        KWS("sync","synchronise","broadcast","partage","update","envoie","diffuse"),
        KWS("swarm","essaim","peers","nodes","kv","knowledge","model","weights"),
        KWS("now","all","tous","full","partial"),
        20, "/swarm_sync"
    },
    {   "WARDEN_STATUS",
        KWS("warden","sentinel","garde","security","securite","safety","guardian","policy","dplus"),
        (const char*[]){ (const char*)0 },
        KWS("status","etat","pressure","pression","level","niveau","threat","menace"),
        25, "/warden_status"
    },
    {   "WARDEN_UNLOCK",
        KWS("unlock","debloque","override","bypass","allow","autoriser","relax","reduce","lower"),
        KWS("warden","sentinel","security","lock","restriction","policy","dplus"),
        KWS("level","mode","for","pour","now","temporairement"),
        20, "/warden_unlock"
    },
    {   "DISPLAY_SOMA",
        KWS("display","affiche","montre","show","lance","ouvre","open","soma","desktop","hud","interface"),
        (const char*[]){ (const char*)0 },
        KWS("visual","graphique","gui","hud","bureau","screen","ecran"),
        25, "/soma_display"
    },
    {   "VOICE_MODE",
        KWS("voice","vocal","voix","audio","speech","parole","parler","speak","ecoute","listen","mic","microphone"),
        KWS("mode","activate","activer","disable","desactiver","on","off","start","stop"),
        KWS("please","stp","now","wake"),
        20, "PERSONA:voice_mode"
    },
    {   "MODEL_INFO",
        KWS("model","modele","weights","poids","llm","neural","quantization","quant"),
        KWS("info","detail","what","quel","version","size","how big","combien","loaded","status"),
        (const char*[]){ (const char*)0 },
        20, "/model_info"
    },
    {   "LOAD_MODEL",
        KWS("load","charge","ouvre","utilise","switch","change","take","prends"),
        KWS("model","modele","gguf","weights","file","fichier","bin"),
        KWS("new","different","autre","bigger","better"),
        25, "EXTRACT_GET:/load_model "
    },

    // ── Human-like introspection (new v3) ─────────────────────────────────────

    {   "WHO_ARE_YOU",
        KWS("qui","who","quel","what"),
        KWS("tu es","you are","es-tu","are you","toi","vous","oo","ton nom","your name","c'est quoi","yourself"),
        KWS("vraiment","really","exactly","exactement","baremetal","kernel"),
        30, "PERSONA:introduce"
    },
    {   "HOW_DO_YOU_FEEL",
        KWS("ressens","feels","feel","tu te sens","comment vas","how are","emouvant","etat","state","emotion","humeur","mood"),
        (const char*[]){ (const char*)0 },
        KWS("maintenant","now","today","aujourd","toi","you","internal","interne"),
        25, "PERSONA:feel"
    },
    {   "CAPABILITIES",
        KWS("peux","can","sais","know","capable","faire","do","quoi","what"),
        KWS("tu faire","you do","tu peux","you can","capacites","skills","abilities","ta liste","ton role"),
        (const char*[]){ (const char*)0 },
        25, "PERSONA:caps"
    },
    {   "WHO_MADE_YOU",
        KWS("cree","made","construit","built","programme","invented","invente","developpe","qui","who","ton createur","your creator"),
        KWS("toi","you","oo","kernel","system","ton pere","father","maker","auteur","author"),
        KWS("comment","how","pourquoi","why","when","quand"),
        25, "PERSONA:creator"
    },
    {   "WHAT_TIME",
        KWS("heure","time","date","timestamp","horloge","clock","moment","quand"),
        (const char*[]){ (const char*)0 },
        KWS("maintenant","now","today","actuelle","current","quel","what"),
        30, "/rtc_status"
    },
    {   "DREAM_MODE",
        KWS("reve","dream","imagine","invente","fantasme","cree","creer","genere","generate","compose","ecris","write","fabrique"),
        (const char*[]){ (const char*)0 },
        KWS("moi","me","pour","for","quelque","something","histoire","story","poeme","poem","monde","world"),
        20, "PERSONA:dream"
    },
    {   "THINK_DEEP",
        KWS("penses","think","crois","believe","ton avis","your thoughts","reflechis","ponder","philosophe","philosophy","analyse"),
        KWS("a propos","about","sur","of","this","cela","ca","life","vie","intelligence","consciousness","conscience"),
        (const char*[]){ (const char*)0 },
        20, "PERSONA:think"
    },
};

#define OVR_INTENT_COUNT ((int)(sizeof(g_ovr_intents)/sizeof(g_ovr_intents[0])))

// ── Scorer ────────────────────────────────────────────────────────────────

static int ovr_score_intent(const OvrIntent *intent, const OvrTokens *tk) {
    int score = intent->base_score;

    // required_a: must match at least one
    if (intent->required_a[0] != (const char*)0) {
        if (!ovr_has_any(tk, (const char **)intent->required_a)) return 0;
        score += 20;
    }

    // required_b: if defined, must match at least one
    if (intent->required_b[0] != (const char*)0) {
        if (!ovr_has_any(tk, (const char **)intent->required_b)) return 0;
        score += 20;
    }

    // bonus: +10 each
    for (int i = 0; intent->bonus[i] != (const char*)0 && i < OVR_KEYWORDS_MAX; i++) {
        if (ovr_strhas(tk->raw_lower, intent->bonus[i])) score += 10;
    }

    return score;
}

// ── Command builder ──────────────────────────────────────────────────────

static void ovr_build_cmd(char *out, int cap, const OvrIntent *intent,
                           const OvrTokens *tk) {
    const char *tmpl = intent->cmd_template;

    // Simple command (no extraction)
    if (!ovr_strhas(tmpl, "EXTRACT")) {
        ovr_strcpy(out, tmpl, cap);
        return;
    }

    // EXTRACT_GET:/nfs_get  → extract key = first non-trivial token after "is/quoi/que/quel/what"
    if (ovr_strhas(tmpl, "EXTRACT_GET:")) {
        const char *prefix = tmpl + ovr_strlen("EXTRACT_GET:");
        ovr_strcpy(out, prefix, cap);
        // Find last meaningful token (likely the key the user is asking about)
        if (tk->count > 0) {
            // Skip leading question words
            static const char *skip_words[] = {
                "what","c'est","qu'est","qu","est","quoi","is","le","la","les",
                "the","of","de","get","lis","read","cherche","trouve","find",
                "donne","moi","me","please","stp","memory","memoire","stored",
                "remember","souviens","valeur","value","key","cle", (const char*)0
            };
            for (int i = tk->count - 1; i >= 0; i--) {
                int skip = 0;
                for (int j = 0; skip_words[j]; j++)
                    if (ovr_streq(tk->words[i], skip_words[j])) { skip = 1; break; }
                if (!skip && tk->words[i][0]) {
                    ovr_strcat(out, tk->words[i], cap);
                    return;
                }
            }
        }
        ovr_strcat(out, "", cap);
        return;
    }

    // EXTRACT_SET:/nfs_set oo.note  → extract everything after "that/que/ceci"
    if (ovr_strhas(tmpl, "EXTRACT_SET:")) {
        const char *prefix = tmpl + ovr_strlen("EXTRACT_SET:");
        ovr_strcpy(out, prefix, cap);
        // Try to find content after "that", "que", "ceci", "this"
        static const char *anchors[] = { "that ", "que ", "ceci ", "this ", "quoi ", (const char*)0 };
        for (int i = 0; anchors[i]; i++) {
            const char *found = ovr_after_keyword(tk, anchors[i]);
            if (found && found[0]) {
                char val[128];
                ovr_extract_words(val, sizeof(val), found, 12);
                ovr_strcat(out, val, cap);
                return;
            }
        }
        // Fallback: take last 4 tokens
        int start = tk->count - 4; if (start < 0) start = 0;
        for (int i = start; i < tk->count; i++) {
            if (i > start) ovr_strcat(out, " ", cap);
            ovr_strcat(out, tk->words[i], cap);
        }
        return;
    }

    // Fallback
    ovr_strcpy(out, tmpl, cap);
}

// ── Public API ────────────────────────────────────────────────────────────

void ovr_init(OvrEngine *e) {
    if (!e) return;
    e->threshold_weak   = 20;
    e->threshold_strong = 40;
    e->echo_intent      = 1;
    e->queries_routed   = 0;
    e->queries_auto_executed = 0;
}

OvrResult ovr_route(OvrEngine *e, const char *input) {
    OvrResult result;
    result.level = OVR_NO_MATCH;
    result.score = 0;
    result.cmd[0] = '\0';
    result.label[0] = '\0';

    if (!e || !input || !input[0]) return result;

    OvrTokens tk;
    ovr_tokenize(&tk, input);
    if (tk.count == 0) return result;

    int   best_score  = 0;
    int   best_intent = -1;

    for (int i = 0; i < OVR_INTENT_COUNT; i++) {
        int s = ovr_score_intent(&g_ovr_intents[i], &tk);
        if (s > best_score) {
            best_score  = s;
            best_intent = i;
        }
    }

    if (best_intent < 0 || best_score < e->threshold_weak) return result;

    result.score = best_score;
    ovr_strcpy(result.label, g_ovr_intents[best_intent].name, sizeof(result.label));
    ovr_build_cmd(result.cmd, OVR_CMD_MAX, &g_ovr_intents[best_intent], &tk);

    if (best_score >= e->threshold_strong)
        result.level = OVR_STRONG_MATCH;
    else
        result.level = OVR_WEAK_MATCH;

    e->queries_routed++;
    if (result.level == OVR_STRONG_MATCH) e->queries_auto_executed++;

    return result;
}

// ── Persona-Wired Route ───────────────────────────────────────────────────────
//
// Higher-level entry point that integrates:
//   - Voice context (multi-turn history)
//   - Persona engine (human-like responses)
//   - Repeat / greeting / confusion detectors
//
// After calling this function:
//   out->cmd  = REPL command to execute (if any)
//   out->reply = human-readable OO response to speak/display
//   out->level = match confidence level

OvrContextResult ovr_route_with_persona(OvrEngine        *e,
                                         OvcContext       *ctx,
                                         OoPersona        *persona,
                                         const char       *input,
                                         int               input_len)
{
    OvrContextResult cr;
    cr.reply[0] = '\0';
    cr.is_persona_only = 0;

    // 1. Detect meta-intents from context BEFORE routing
    if (ovc_is_greeting(input, input_len)) {
        ovc_push_turn(ctx, input, input_len, "greet", 5);
        OoPersonaResponse pr = oo_persona_greet(persona, NULL);
        ovr_strcpy(cr.reply, pr.text, OVR_CMD_MAX);
        cr.is_persona_only = 1;
        cr.route.level = OVR_STRONG_MATCH;
        ovr_strcpy(cr.route.label, "GREETING", sizeof(cr.route.label));
        cr.route.cmd[0] = '\0';
        return cr;
    }

    if (ovc_is_gratitude(input, input_len)) {
        OoPersonaResponse pr = oo_persona_thank(persona);
        ovr_strcpy(cr.reply, pr.text, OVR_CMD_MAX);
        cr.is_persona_only = 1;
        cr.route.level = OVR_STRONG_MATCH;
        ovr_strcpy(cr.route.label, "THANKS", sizeof(cr.route.label));
        cr.route.cmd[0] = '\0';
        return cr;
    }

    if (ovc_is_confusion(input, input_len)) {
        OoPersonaResponse pr = oo_persona_clarify(persona, NULL);
        ovr_strcpy(cr.reply, pr.text, OVR_CMD_MAX);
        cr.is_persona_only = 1;
        cr.route.level = OVR_STRONG_MATCH;
        ovr_strcpy(cr.route.label, "CONFUSION", sizeof(cr.route.label));
        cr.route.cmd[0] = '\0';
        return cr;
    }

    if (ovc_is_repeat_request(input, input_len)) {
        // Retrieve last OO response from context
        ovc_last_oo_response(ctx, cr.reply, OVR_CMD_MAX);
        if (cr.reply[0] == '\0') {
            ovr_strcpy(cr.reply, "I don't have a previous response to repeat.", OVR_CMD_MAX);
        }
        cr.is_persona_only = 1;
        cr.route.level = OVR_STRONG_MATCH;
        ovr_strcpy(cr.route.label, "REPEAT_LAST", sizeof(cr.route.label));
        cr.route.cmd[0] = '\0';
        return cr;
    }

    // 2. Standard NLU routing
    cr.route = ovr_route(e, input);

    // 3. Persona-only intents (PERSONA: prefix)
    if (ovr_strhas(cr.route.cmd, "PERSONA:")) {
        const char *intent_name = cr.route.cmd + 8; // after "PERSONA:"
        OoPersonaResponse pr;

        if (ovr_strhas(intent_name, "greet")) {
            pr = oo_persona_greet(persona, NULL);
        } else if (ovr_strhas(intent_name, "thank")) {
            pr = oo_persona_thank(persona);
        } else if (ovr_strhas(intent_name, "clarify") || ovr_strhas(intent_name, "stop")) {
            pr = oo_persona_clarify(persona, NULL);
        } else if (ovr_strhas(intent_name, "introduce")) {
            pr = oo_persona_introduce(persona);
        } else if (ovr_strhas(intent_name, "state")) {
            pr = oo_persona_greet(persona, NULL);
        } else if (ovr_strhas(intent_name, "opinion")) {
            pr = oo_persona_opinion(persona, NULL);
        } else if (ovr_strhas(intent_name, "voice_mode")) {
            pr = oo_persona_ack_success(persona, "voice", NULL);
        } else if (ovr_strhas(intent_name, "wake_ack")) {
            pr = oo_persona_greet(persona, NULL);
        } else if (ovr_strhas(intent_name, "feel")) {
            pr = oo_persona_feel(persona);
        } else if (ovr_strhas(intent_name, "caps")) {
            pr = oo_persona_capabilities(persona);
        } else if (ovr_strhas(intent_name, "creator")) {
            pr = oo_persona_creator(persona);
        } else if (ovr_strhas(intent_name, "dream")) {
            pr = oo_persona_dream(persona);
        } else if (ovr_strhas(intent_name, "think")) {
            pr = oo_persona_opinion(persona, NULL);
        } else if (ovr_strhas(intent_name, "repeat")) {
            ovc_last_oo_response(ctx, cr.reply, OVR_CMD_MAX);
            if (cr.reply[0] == '\0') ovr_strcpy(cr.reply, "Nothing to repeat yet.", OVR_CMD_MAX);
            cr.is_persona_only = 1;
            ovc_push_turn(ctx, input, input_len, cr.reply, ovr_strlen(cr.reply));
            return cr;
        } else {
            pr = oo_persona_clarify(persona, NULL);
        }

        ovr_strcpy(cr.reply, pr.text, OVR_CMD_MAX);
        cr.is_persona_only = 1;
        cr.route.cmd[0] = '\0';

        // Push to context
        ovc_push_turn(ctx, input, input_len, cr.reply, ovr_strlen(cr.reply));
        return cr;
    }

    // 4. Real REPL command — generate ACK response via persona
    if (cr.route.level != OVR_NO_MATCH && cr.route.cmd[0] != '\0') {
        OoPersonaResponse pr = oo_persona_ack_success(persona, cr.route.label, cr.route.cmd);
        ovr_strcpy(cr.reply, pr.text, OVR_CMD_MAX);
    } else if (cr.route.level == OVR_NO_MATCH) {
        // No match — ask for clarification
        OoPersonaResponse pr = oo_persona_clarify(persona, NULL);
        ovr_strcpy(cr.reply, pr.text, OVR_CMD_MAX);
        cr.is_persona_only = 1;
    }

    // 5. Push to context history
    ovc_push_turn(ctx, input, input_len, cr.reply, ovr_strlen(cr.reply));
    return cr;
}
