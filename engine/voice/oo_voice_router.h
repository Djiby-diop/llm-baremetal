// oo_voice_router.h — Natural Language → REPL Command Router
//
// Converts free-form user text (FR or EN) into OO REPL commands.
// No LLM required — pure keyword scoring, fully freestanding.
//
// Philosophy:
//   The user talks to OO like a person. OO understands intent.
//   "save my memory" → /nfs_save
//   "how many cores do you have?" → /smp_status
//   "remember that my name is Djiby" → /nfs_set oo.user.name Djiby
//   "apprends tout seul" → /oo_train
//   "tu reves de quoi ?" → /dream_status
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Limits ──────────────────────────────────────────────────────────────
#define OVR_MAX_TOKENS       48    // max words in one input
#define OVR_TOKEN_LEN        32    // max chars per token
#define OVR_CMD_MAX         256    // max chars in output command
#define OVR_INTENTS_MAX      24    // total intents defined
#define OVR_KEYWORDS_MAX     12    // max keywords per intent slot

// ── Match results ────────────────────────────────────────────────────────
typedef enum {
    OVR_NO_MATCH     = 0,
    OVR_WEAK_MATCH   = 1,   // score 20-39: suggestion only
    OVR_STRONG_MATCH = 2,   // score >= 40: auto-execute
} OvrMatchLevel;

typedef struct {
    OvrMatchLevel level;
    int           score;       // 0-100
    char          cmd[OVR_CMD_MAX]; // REPL command to inject (may be empty)
    char          label[64];   // human-readable intent name for feedback
} OvrResult;

// ── Engine ───────────────────────────────────────────────────────────────
typedef struct {
    int  threshold_weak;   // default: 20
    int  threshold_strong; // default: 40
    int  echo_intent;      // 1 = print "[OO understood: ...]" before executing
    uint32_t queries_routed;
    uint32_t queries_auto_executed;
} OvrEngine;

// ── Public API ───────────────────────────────────────────────────────────
void    ovr_init(OvrEngine *e);
OvrResult ovr_route(OvrEngine *e, const char *input);

// Helper: check if input looks like a REPL command (starts with '/')
static inline int ovr_is_command(const char *s) {
    return s && s[0] == '/';
}

// Helper: check if router should skip routing (empty, command, etc.)
static inline int ovr_should_skip(const char *s) {
    if (!s || s[0] == '\0') return 1;
    if (s[0] == '/')        return 1;  // already a command
    if (s[0] == '#')        return 1;  // comment
    return 0;
}

#ifdef __cplusplus
}
#endif
