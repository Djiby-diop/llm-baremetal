// oo_voice_context.h — Multi-Turn Dialogue Context Manager
//
// Maintains conversation history, speaker state, and active task context
// across multiple voice/text interactions.
//
// Enables natural multi-turn dialogue:
//   User: "save my memory"
//   OO:   "Done. Your memory has been saved."
//   User: "do it again"       ← references previous command
//   OO:   "Saving memory again..."
//
//   User: "remember that my name is Djiby"
//   User: "what's my name?"   ← context recall
//
// Freestanding C11 — no libc, no malloc. All storage is static.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Limits ───────────────────────────────────────────────────────────────────
#define OVC_MAX_TURNS        8    // rolling conversation history
#define OVC_TEXT_LEN       192    // max chars per turn text
#define OVC_CMD_LEN        128    // max chars per turn command
#define OVC_ENTITY_LEN      64    // max chars per named entity
#define OVC_ENTITIES_MAX    16    // number of named entities tracked
#define OVC_RESPONSE_LEN   256    // max chars per OO response

// ── Turn speaker ─────────────────────────────────────────────────────────────
typedef enum {
    OVC_SPEAKER_HUMAN = 0,
    OVC_SPEAKER_OO    = 1,
} OvcSpeaker;

// ── A single conversation turn ────────────────────────────────────────────────
typedef struct {
    OvcSpeaker speaker;
    char       text[OVC_TEXT_LEN];     // raw input or response
    char       cmd[OVC_CMD_LEN];       // REPL command that was generated (if any)
    char       intent[48];             // intent label that was matched
    int        score;                  // match confidence score
    uint32_t   tick;                   // system tick at time of turn
} OvcTurn;

// ── Named entity slot ────────────────────────────────────────────────────────
typedef struct {
    char key[32];               // "user.name", "project", "filename", etc.
    char value[OVC_ENTITY_LEN]; // extracted value
    uint32_t last_updated;      // tick when last set
} OvcEntity;

// ── Context engine ────────────────────────────────────────────────────────────
typedef struct {
    // Rolling conversation history
    OvcTurn  turns[OVC_MAX_TURNS];
    int      turn_count;     // total turns inserted (wraps at OVC_MAX_TURNS)
    int      head;           // index of most recent turn in circular buffer

    // Named entity memory (short-term session memory)
    OvcEntity entities[OVC_ENTITIES_MAX];
    int       entity_count;

    // Repeat/reference tracking
    char      last_cmd[OVC_CMD_LEN];   // last REPL command executed
    char      last_intent[48];         // last intent matched
    char      last_response[OVC_RESPONSE_LEN]; // last OO response

    // Active task state
    char      active_task[64];   // "none", "inference", "training", "saving"...
    int       awaiting_confirm;  // 1 = OO asked a question, waiting for yes/no

    // Session stats
    uint32_t  total_turns;
    uint32_t  total_commands_executed;
} OvcContext;

// ── Public API ────────────────────────────────────────────────────────────────

// Initialize context (call once at boot)
void ovc_init(OvcContext *ctx);

// Add a human turn (after voice/text input)
void ovc_push_human(OvcContext *ctx, const char *text,
                    const char *cmd, const char *intent, int score,
                    uint32_t tick);

// Add an OO response turn
void ovc_push_oo(OvcContext *ctx, const char *response, uint32_t tick);

// Get the last human command (for "do it again")
const char *ovc_last_cmd(const OvcContext *ctx);

// Get the last human input text (for "what did I say?")
const char *ovc_last_human_text(const OvcContext *ctx);

// Get a named entity value (returns NULL if not found)
const char *ovc_get_entity(const OvcContext *ctx, const char *key);

// Set a named entity
void ovc_set_entity(OvcContext *ctx, const char *key, const char *value,
                    uint32_t tick);

// Detect if user is referencing the previous interaction
// Returns 1 if input matches: "again","encore","redo","redis","refais","same","meme"
int ovc_is_repeat_request(const char *input, int len);

// Detect confirmation (yes/non/oui/nope/sure/absolument...)
// Returns: 1=yes, -1=no, 0=unclear
int ovc_parse_confirmation(const char *input);

// Detect if user is asking "what did I say" / "last command"
int ovc_is_recall_request(const char *input);

// Detect greeting (bonjour, salut, hello, hi, hey...)
int ovc_is_greeting(const char *input, int len);

// Detect gratitude (merci, thanks, great, parfait, bravo, nicely done...)
int ovc_is_gratitude(const char *input, int len);

// Detect frustration or confusion (quoi, what, je comprends pas, huh, pardon...)
int ovc_is_confusion(const char *input, int len);

// Simplified push: push a human+OO pair in one call
void ovc_push_turn(OvcContext *ctx, const char *human_text, int human_len,
                   const char *oo_response, int oo_len);

// Retrieve last OO response
void ovc_last_oo_response(const OvcContext *ctx, char *out, int cap);

// Get recent context as a flat string for LLM prompt injection
// Writes at most cap bytes into out (for LLM-backed fallback)
void ovc_context_summary(const OvcContext *ctx, char *out, int cap);

#ifdef __cplusplus
}
#endif
