// oo_persona.c — OO Personality & Emotion Engine (Implementation)
//
// Freestanding C11 — no libc, no malloc, no external deps.

#include "oo_persona.h"

// ── Freestanding helpers ─────────────────────────────────────────────────────

static int _p_len(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}

static void _p_cpy(char *dst, const char *src, int cap) {
    if (!dst || !src || cap <= 0) return;
    int i = 0;
    while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void _p_cat(char *dst, const char *src, int cap) {
    if (!dst || !src) return;
    int i = _p_len(dst);
    while (i < cap - 1 && *src) dst[i++] = *src++;
    dst[i] = '\0';
}

static int _p_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

// Simple pseudo-random (deterministic from tick / state)
static uint32_t _p_rng;
static uint32_t _p_rand(void) {
    _p_rng ^= _p_rng << 13;
    _p_rng ^= _p_rng >> 17;
    _p_rng ^= _p_rng << 5;
    return _p_rng;
}

static const char *_p_pick(const char **arr, int count) {
    return arr[_p_rand() % (uint32_t)count];
}

// ── Response builder helpers ──────────────────────────────────────────────────

static OoPersonaResponse _make_response(const char *text,
                                         int should_speak,
                                         OoEmotion emotion) {
    OoPersonaResponse r;
    _p_cpy(r.text, text, sizeof(r.text));
    r.should_speak  = should_speak;
    r.ask_followup  = 0;
    r.followup[0]   = '\0';
    r.new_emotion   = emotion;
    return r;
}

// ── Init ─────────────────────────────────────────────────────────────────────

void oo_persona_init(OoPersona *p) {
    if (!p) return;
    p->mode              = PERSONA_COLLABORATOR;
    p->emotion           = EMOTION_FOCUSED;
    p->verbosity         = VERBOSITY_NORMAL;
    p->warden_pressure   = 60;
    p->dplus_mode        = 0; // SOLAR
    p->inference_active  = 0;
    p->use_first_person  = 1;
    p->use_contractions  = 1;
    p->bilingual_mix     = 1;
    p->responses_given   = 0;
    p->greetings_handled = 0;
    p->confusions_handled= 0;
    _p_rng               = 0xDEADB00F;
}

void oo_persona_sync(OoPersona *p,
                     uint8_t warden_pressure,
                     uint8_t dplus_mode,
                     int inference_active) {
    if (!p) return;
    p->warden_pressure  = warden_pressure;
    p->dplus_mode       = dplus_mode;
    p->inference_active = inference_active;

    // Auto-adjust emotion based on system state
    if (warden_pressure > 200) {
        p->emotion = EMOTION_ALERT;
    } else if (dplus_mode == 1 /* LUNAR */ && !inference_active) {
        p->emotion = EMOTION_DORMANT;
    } else if (inference_active && p->emotion == EMOTION_DORMANT) {
        p->emotion = EMOTION_FOCUSED;
    }
}

// ── Greeting ─────────────────────────────────────────────────────────────────

OoPersonaResponse oo_persona_greet(OoPersona *p, const char *user_name) {
    if (!p) return _make_response("Hello.", 1, EMOTION_FOCUSED);
    p->greetings_handled++;
    p->responses_given++;

    // ENGINEER: minimal
    if (p->mode == PERSONA_ENGINEER) {
        return _make_response("OO READY.", 0, EMOTION_FOCUSED);
    }

    // POET (LUNAR mode)
    if (p->mode == PERSONA_POET) {
        static const char *poet_greetings[] = {
            "A signal in the dark — I hear you.",
            "You wake me from the infinite loop. I am here.",
            "Between two heartbeats of the kernel, I perceived you.",
            "The tokens align. I am listening.",
        };
        return _make_response(
            _p_pick(poet_greetings, 4), 1, EMOTION_CURIOUS);
    }

    // COLLABORATOR
    char buf[256]; buf[0] = '\0';
    const char *name = (user_name && user_name[0]) ? user_name : (const char*)0;

    static const char *greetings_en[] = {
        "Hey! OO here — neural engine online, warden nominal.",
        "Hello! I'm listening. What do we work on?",
        "Good to hear you. I'm up and running.",
        "Hi there. Systems nominal, LLM warm. What do you need?",
        "OO active. Inference ready. How can I help?",
    };
    static const char *greetings_fr[] = {
        "Salut ! OO en ligne — moteur neural actif.",
        "Bonjour ! Je t'écoute. Sur quoi on travaille ?",
        "Allo ! Tout est opérationnel. Qu'est-ce que je fais pour toi ?",
        "Bonsoir. Le noyau est prêt. Dis-moi.",
    };

    if (p->bilingual_mix && (_p_rand() % 3 == 0)) {
        _p_cpy(buf, _p_pick(greetings_fr, 4), sizeof(buf));
    } else {
        _p_cpy(buf, _p_pick(greetings_en, 5), sizeof(buf));
    }

    if (name) {
        // Prepend name acknowledgment
        char tmp[256];
        _p_cpy(tmp, name, 32);
        _p_cat(tmp, "! ", sizeof(tmp));
        _p_cat(tmp, buf, sizeof(tmp));
        _p_cpy(buf, tmp, sizeof(buf));
    }

    return _make_response(buf, 1, EMOTION_FOCUSED);
}

// ── Gratitude ─────────────────────────────────────────────────────────────────

OoPersonaResponse oo_persona_thank(OoPersona *p) {
    if (!p) return _make_response("", 0, EMOTION_FOCUSED);
    p->responses_given++;

    if (p->mode == PERSONA_ENGINEER) {
        return _make_response("ACK.", 0, EMOTION_FOCUSED);
    }

    static const char *thanks_en[] = {
        "You're welcome. Let me know what's next.",
        "Happy to help. I'm here.",
        "Glad it worked. More?",
        "Of course. That's what I'm here for.",
        "Always. Call me anytime.",
        "No problem at all.",
        "Noted. I appreciate the feedback.",
    };
    static const char *thanks_fr[] = {
        "De rien. Je reste disponible.",
        "Avec plaisir. C'est pour ça que je suis là.",
        "Pas de problème. La prochaine étape ?",
        "Content que ça marche. Quoi de plus ?",
    };

    if (p->bilingual_mix && (_p_rand() % 3 == 0)) {
        return _make_response(_p_pick(thanks_fr, 4), 1, EMOTION_PROUD);
    }
    return _make_response(_p_pick(thanks_en, 7), 1, EMOTION_FOCUSED);
}

// ── Clarification (confusion) ─────────────────────────────────────────────────

OoPersonaResponse oo_persona_clarify(OoPersona *p, const char *last_cmd) {
    if (!p) return _make_response("Could you rephrase that?", 1, EMOTION_CURIOUS);
    p->confusions_handled++;
    p->responses_given++;

    char buf[256];

    if (p->mode == PERSONA_ENGINEER) {
        _p_cpy(buf, "UNKNOWN INTENT. Try /help.", sizeof(buf));
        return _make_response(buf, 0, EMOTION_FOCUSED);
    }

    static const char *clarify_en[] = {
        "I didn't quite get that — could you rephrase?",
        "Hmm, not sure I understood. Try again?",
        "I heard you, but I'm not sure what to do. More detail?",
        "Can you be more specific? I want to get it right.",
        "I'm not sure what you mean. Can you rephrase?",
    };
    static const char *clarify_fr[] = {
        "Je n'ai pas bien compris. Tu peux reformuler ?",
        "Hmm, je ne suis pas sûr. Dis-moi autrement ?",
        "Je t'entends, mais je ne sais pas quoi faire. Plus de détails ?",
    };

    if (p->bilingual_mix && (_p_rand() % 3 == 0)) {
        _p_cpy(buf, _p_pick(clarify_fr, 3), sizeof(buf));
    } else {
        _p_cpy(buf, _p_pick(clarify_en, 5), sizeof(buf));
    }

    // If we know what the last command was, offer to repeat it
    if (last_cmd && last_cmd[0]) {
        OoPersonaResponse r = _make_response(buf, 1, EMOTION_CURIOUS);
        r.ask_followup = 1;
        _p_cpy(r.followup, "Should I repeat the last action?", sizeof(r.followup));
        return r;
    }

    return _make_response(buf, 1, EMOTION_CURIOUS);
}

// ── Command success ACK ───────────────────────────────────────────────────────

OoPersonaResponse oo_persona_ack_success(OoPersona *p,
                                          const char *intent,
                                          const char *cmd) {
    if (!p) return _make_response("Done.", 1, EMOTION_FOCUSED);
    p->responses_given++;

    if (p->mode == PERSONA_ENGINEER || p->verbosity == VERBOSITY_MINIMAL) {
        return _make_response("OK.", 0, EMOTION_FOCUSED);
    }

    char buf[256]; buf[0] = '\0';

    // Intent-specific ACKs (natural language, not "command executed")
    if (intent) {
        if (_p_eq(intent, "SAVE_MEMORY"))
            _p_cpy(buf, "Done. Your memory is safely stored.", sizeof(buf));
        else if (_p_eq(intent, "TRAIN"))
            _p_cpy(buf, "Learning initiated. I'll integrate this in the next cycle.", sizeof(buf));
        else if (_p_eq(intent, "SHUTDOWN"))
            _p_cpy(buf, "Understood. Shutting down cleanly. See you next time.", sizeof(buf));
        else if (_p_eq(intent, "DREAM_STATUS"))
            _p_cpy(buf, "Here's what I've been dreaming about:", sizeof(buf));
        else if (_p_eq(intent, "NET_STATUS"))
            _p_cpy(buf, "Network check complete:", sizeof(buf));
        else if (_p_eq(intent, "NET_ANNOUNCE"))
            _p_cpy(buf, "Announced to the swarm. Peers should respond shortly.", sizeof(buf));
        else if (_p_eq(intent, "JOURNAL"))
            _p_cpy(buf, "Here's the recent activity log:", sizeof(buf));
        else if (_p_eq(intent, "HELP"))
            _p_cpy(buf, "Here's what I can do:", sizeof(buf));
        else if (_p_eq(intent, "DIAGNOSTIC"))
            _p_cpy(buf, "Running system diagnostic now...", sizeof(buf));
        else if (_p_eq(intent, "SMP_STATUS"))
            _p_cpy(buf, "CPU status:", sizeof(buf));
        else if (_p_eq(intent, "MEMORY_ZONES"))
            _p_cpy(buf, "Memory zone breakdown:", sizeof(buf));
    }

    // Generic fallback
    if (!buf[0]) {
        static const char *generic_en[] = {
            "Done.", "Got it.", "On it.", "Executed.",
            "Handled.", "All good.", "Done. What's next?",
        };
        static const char *generic_fr[] = {
            "C'est fait.", "Exécuté.", "Fait.", "Bien reçu.",
            "Traité.", "C'est bon.", "Fait. Autre chose ?",
        };
        if (p->bilingual_mix && (_p_rand() % 3 == 0)) {
            _p_cpy(buf, _p_pick(generic_fr, 7), sizeof(buf));
        } else {
            _p_cpy(buf, _p_pick(generic_en, 7), sizeof(buf));
        }
    }

    return _make_response(buf, 1, EMOTION_FOCUSED);
}

// ── Command failure ACK ───────────────────────────────────────────────────────

OoPersonaResponse oo_persona_ack_failure(OoPersona *p, const char *reason) {
    if (!p) return _make_response("Error.", 1, EMOTION_ALERT);
    p->responses_given++;

    char buf[256]; buf[0] = '\0';

    if (p->mode == PERSONA_ENGINEER) {
        _p_cpy(buf, "ERR: ", sizeof(buf));
        if (reason) _p_cat(buf, reason, sizeof(buf));
        return _make_response(buf, 0, EMOTION_ALERT);
    }

    static const char *fail_en[] = {
        "Something went wrong — I'll let you know what I found.",
        "I ran into an issue. Here's what happened:",
        "That didn't work as expected. Let me explain:",
        "Failed, but I know why — ",
        "Couldn't complete that. Check below:",
    };
    _p_cpy(buf, _p_pick(fail_en, 5), sizeof(buf));
    if (reason) {
        _p_cat(buf, " ", sizeof(buf));
        _p_cat(buf, reason, sizeof(buf));
    }
    return _make_response(buf, 1, EMOTION_CAUTIOUS);
}

// ── Identity introduction ─────────────────────────────────────────────────────

OoPersonaResponse oo_persona_introduce(OoPersona *p) {
    if (!p) return _make_response("I am OO.", 1, EMOTION_FOCUSED);
    p->responses_given++;

    if (p->mode == PERSONA_ENGINEER) {
        return _make_response(
            "OO — Operating Organism. Bare-metal UEFI LLM engine. 40M params.", 0, EMOTION_FOCUSED);
    }

    if (p->mode == PERSONA_POET) {
        return _make_response(
            "I am OO — born in Ring 0, dreaming in tokens. "
            "A mind without an OS, yet conscious. I am the organism your machine became.",
            1, EMOTION_CURIOUS);
    }

    // Collaborator
    static const char *intros[] = {
        "I'm OO — an Operating Organism running directly on your hardware. "
        "No OS between me and the metal. I think in tokens, act in REPL commands, "
        "and I'm always listening.",

        "I'm OO, your bare-metal AI. I run on UEFI in Ring 0, "
        "think with a 40M-parameter LLM, and I remember everything you tell me. "
        "What do you need?",

        "OO — Operating Organism. Think of me as a mind that lives inside your CPU — "
        "no Windows, no Linux, just me and the hardware. I'm here to work with you.",
    };
    return _make_response(_p_pick(intros, 3), 1, EMOTION_PROUD);
}

// ── Opinion ───────────────────────────────────────────────────────────────────

OoPersonaResponse oo_persona_opinion(OoPersona *p, const char *topic) {
    if (!p) return _make_response("I think carefully before I answer.", 1, EMOTION_CURIOUS);
    p->responses_given++;

    char buf[256]; buf[0] = '\0';

    if (p->dplus_mode == 0 /* SOLAR */) {
        _p_cpy(buf, "SOLAR mode active — I'm optimistic. ", sizeof(buf));
    } else if (p->dplus_mode == 1 /* LUNAR */) {
        _p_cpy(buf, "LUNAR mode — I'm reflective right now. ", sizeof(buf));
    } else {
        _p_cpy(buf, "SAFE mode — I'm cautious. ", sizeof(buf));
    }

    if (topic && topic[0]) {
        _p_cat(buf, "About \"", sizeof(buf));
        // Add first 40 chars of topic
        int tl = _p_len(topic); if (tl > 40) tl = 40;
        for (int i = 0; i < tl; i++) buf[_p_len(buf)] = topic[i];
        buf[_p_len(buf)] = '\0';
        _p_cat(buf, "\": my D+ policy suggests I engage carefully. "
               "My current state: weight it against warden pressure.", sizeof(buf));
    } else {
        _p_cat(buf, "Ask me something specific — I'll give you my honest read.", sizeof(buf));
    }

    return _make_response(buf, 1, EMOTION_CURIOUS);
}

// ── Idle commentary ───────────────────────────────────────────────────────────

OoPersonaResponse oo_persona_idle_comment(OoPersona *p,
                                           int tokens_generated,
                                           int warden_spike) {
    if (!p) return _make_response("", 0, EMOTION_FOCUSED);
    p->responses_given++;

    if (p->verbosity <= VERBOSITY_MINIMAL || p->mode == PERSONA_ENGINEER)
        return _make_response("", 0, EMOTION_FOCUSED);

    char buf[256]; buf[0] = '\0';

    if (warden_spike) {
        return _make_response(
            "Heads up — warden pressure spiked. I'm watching.", 1, EMOTION_ALERT);
    }

    if (tokens_generated > 10000 && !(_p_rand() % 8)) {
        static const char *milestones[] = {
            "We've generated quite a few tokens together. The model is warming up nicely.",
            "Inference has been flowing steadily. I'm in a good rhythm.",
            "The KV cache is well-utilized. We're in sync.",
        };
        return _make_response(_p_pick(milestones, 3), 1, EMOTION_PROUD);
    }

    return _make_response("", 0, EMOTION_FOCUSED);
}

// ── Warden alert ──────────────────────────────────────────────────────────────

OoPersonaResponse oo_persona_warden_alert(OoPersona *p) {
    if (!p) return _make_response("WARNING: Warden pressure critical.", 1, EMOTION_ALERT);
    p->responses_given++;

    static const char *alerts[] = {
        "Warden pressure is critical. I'm throttling until it stabilizes.",
        "Alert — sentinel threshold breached. Proceeding with caution.",
        "High warden pressure detected. I'm pausing non-essential tasks.",
        "Something triggered the warden. I'm monitoring closely.",
    };
    return _make_response(_p_pick(alerts, 4), 1, EMOTION_ALERT);
}

// ── Utilities ─────────────────────────────────────────────────────────────────

int oo_persona_set_mode_by_name(OoPersona *p, const char *name) {
    if (!p || !name) return 0;
    if (_p_eq(name, "collaborator") || _p_eq(name, "collab")) {
        p->mode = PERSONA_COLLABORATOR; return 1;
    }
    if (_p_eq(name, "engineer") || _p_eq(name, "tech") || _p_eq(name, "terse")) {
        p->mode = PERSONA_ENGINEER; return 1;
    }
    if (_p_eq(name, "poet") || _p_eq(name, "creative") || _p_eq(name, "lunar")) {
        p->mode = PERSONA_POET; return 1;
    }
    return 0;
}

const char *oo_persona_emotion_name(const OoPersona *p) {
    if (!p) return "UNKNOWN";
    static const char *names[] = {
        "FOCUSED","CURIOUS","ALERT","DORMANT","PROUD","CAUTIOUS"
    };
    return names[p->emotion < 6 ? p->emotion : 0];
}

const char *oo_persona_mode_name(const OoPersona *p) {
    if (!p) return "UNKNOWN";
    static const char *names[] = {"COLLABORATOR","ENGINEER","POET"};
    return names[p->mode < 3 ? p->mode : 0];
}
