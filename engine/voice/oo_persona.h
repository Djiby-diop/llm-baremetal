// oo_persona.h — OO Personality & Emotion Engine
//
// Controls HOW OO speaks and reacts — not just WHAT it does.
//
// OO has a personality. It's not a machine. It responds to:
//   - Greetings with warmth
//   - Gratitude with humility
//   - Confusion with patience
//   - Errors with accountability
//   - Success with quiet confidence
//
// Personality modes (set by user or D+ policy):
//   PERSONA_COLLABORATOR — default: warm, concise, team-oriented
//   PERSONA_ENGINEER     — terse, precise, prefers /commands
//   PERSONA_POET         — expressive, metaphorical (LUNAR mode only)
//
// Emotional states (driven by system health + context):
//   EMOTION_FOCUSED    — normal operation, clear responses
//   EMOTION_CURIOUS    — exploring/learning, asks follow-up questions
//   EMOTION_ALERT      — warden pressure high, terse/urgent
//   EMOTION_DORMANT    — low-activity (LUNAR mode), minimal output
//   EMOTION_PROUD      — after a successful complex task
//   EMOTION_CAUTIOUS   — policy warning active, adds disclaimers
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Persona modes ────────────────────────────────────────────────────────────
typedef enum {
    PERSONA_COLLABORATOR = 0,  // default — warm, helpful, balanced
    PERSONA_ENGINEER     = 1,  // terse, technical, prefers raw output
    PERSONA_POET         = 2,  // expressive, metaphorical (LUNAR/creative)
} OoPersonaMode;

// ── Emotional states ──────────────────────────────────────────────────────────
typedef enum {
    EMOTION_FOCUSED   = 0,
    EMOTION_CURIOUS   = 1,
    EMOTION_ALERT     = 2,
    EMOTION_DORMANT   = 3,
    EMOTION_PROUD     = 4,
    EMOTION_CAUTIOUS  = 5,
} OoEmotion;

// Aliases with OO_ prefix (used by oo_voice_desktop_bridge.c)
#define OO_EMOTION_FOCUSED   EMOTION_FOCUSED
#define OO_EMOTION_CURIOUS   EMOTION_CURIOUS
#define OO_EMOTION_ALERT     EMOTION_ALERT
#define OO_EMOTION_DORMANT   EMOTION_DORMANT
#define OO_EMOTION_PROUD     EMOTION_PROUD
#define OO_EMOTION_CAUTIOUS  EMOTION_CAUTIOUS

// ── Response verbosity ────────────────────────────────────────────────────────
typedef enum {
    VERBOSITY_SILENT   = 0,  // no spoken confirmation
    VERBOSITY_MINIMAL  = 1,  // 1 short line
    VERBOSITY_NORMAL   = 2,  // 2-3 lines
    VERBOSITY_VERBOSE  = 3,  // full explanation
} OoVerbosity;

// ── OO response struct ────────────────────────────────────────────────────────
// Returned by persona_respond() — contains what OO should say
typedef struct {
    char      text[256];     // spoken/displayed response text
    int       should_speak;  // 1 = pass to TTS, 0 = silent (engineer mode)
    int       ask_followup;  // 1 = OO wants more info (asks a follow-up question)
    char      followup[128]; // the follow-up question if ask_followup==1
    OoEmotion new_emotion;   // suggested new emotional state after this response
} OoPersonaResponse;

// ── Persona engine ────────────────────────────────────────────────────────────
typedef struct {
    OoPersonaMode  mode;
    OoEmotion      emotion;
    OoVerbosity    verbosity;

    // System state influence
    uint8_t        warden_pressure;  // 0-255, high = alert
    uint8_t        dplus_mode;       // 0=SOLAR 1=LUNAR 2=SAFE
    int            inference_active; // 1 = currently inferencing

    // Personality traits (adjustable via /persona set)
    int            use_first_person;     // 1 = "I understand", 0 = "OO understands"
    int            use_contractions;     // 1 = "I'll", "don't", 0 = "I will", "do not"
    int            bilingual_mix;        // 1 = can mix EN+FR responses

    // Stats
    uint32_t       responses_given;
    uint32_t       greetings_handled;
    uint32_t       confusions_handled;
} OoPersona;

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize persona with defaults
void oo_persona_init(OoPersona *p);

// Sync persona state with live system metrics
void oo_persona_sync(OoPersona *p,
                     uint8_t warden_pressure,
                     uint8_t dplus_mode,
                     int inference_active);

// Generate a response for a greeting
OoPersonaResponse oo_persona_greet(OoPersona *p, const char *user_name);

// Generate a response for gratitude
OoPersonaResponse oo_persona_thank(OoPersona *p);

// Generate a response for confusion (user didn't understand OO or vice versa)
OoPersonaResponse oo_persona_clarify(OoPersona *p, const char *last_cmd);

// Generate a response when a command was executed successfully
OoPersonaResponse oo_persona_ack_success(OoPersona *p,
                                          const char *intent,
                                          const char *cmd);

// Generate a response when a command failed
OoPersonaResponse oo_persona_ack_failure(OoPersona *p,
                                          const char *reason);

// Generate an identity introduction ("who are you?")
OoPersonaResponse oo_persona_introduce(OoPersona *p);

// Generate an opinion (D+ mode opinion request)
OoPersonaResponse oo_persona_opinion(OoPersona *p, const char *topic);

// Generate idle commentary (OO speaks unprompted when things are interesting)
OoPersonaResponse oo_persona_idle_comment(OoPersona *p,
                                           int tokens_generated,
                                           int warden_spike);

// Generate a response when warden pressure is critical
OoPersonaResponse oo_persona_warden_alert(OoPersona *p);

// Set persona mode by name ("collaborator", "engineer", "poet")
int oo_persona_set_mode_by_name(OoPersona *p, const char *name);

// Get current emotion as a string
const char *oo_persona_emotion_name(const OoPersona *p);

// Get current persona mode as a string
const char *oo_persona_mode_name(const OoPersona *p);

// OO talks about its current emotional / system state
OoPersonaResponse oo_persona_feel(OoPersona *p);

// OO describes its capabilities
OoPersonaResponse oo_persona_capabilities(OoPersona *p);

// OO talks about its creator
OoPersonaResponse oo_persona_creator(OoPersona *p);

// OO enters dream/imagination mode (invites LLM to continue)
OoPersonaResponse oo_persona_dream(OoPersona *p);

#ifdef __cplusplus
}
#endif
