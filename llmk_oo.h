#pragma once

#include <efi.h>
#include <efilib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*LlmkOoOnStep)(int id, int tick, int energy);

void llmk_oo_init(void);
void llmk_oo_set_on_step(LlmkOoOnStep cb);

int llmk_oo_new(const char *goal);
int llmk_oo_kill(int id);

void llmk_oo_list_print(void);

int llmk_oo_step(int id);
int llmk_oo_run(int steps);

// Per-entity memory (stable, no LLM call)
int llmk_oo_note(int id, const char *text);
int llmk_oo_show_print(int id);
int llmk_oo_digest(int id);

// Per-entity agenda (small action queue)
// Agenda is a tiny action board with:
// - text (single-line)
// - state: 0=todo, 1=doing, 2=done
// - prio: small signed int (higher = sooner)
//
// Back-compat:
// - llmk_oo_agenda_add() adds with prio=0, state=todo
// - llmk_oo_agenda_next() selects best action, marks it doing, returns its text
int llmk_oo_agenda_add(int id, const char *action);
int llmk_oo_agenda_add_ex(int id, const char *action, int prio);
int llmk_oo_agenda_peek(int id, char *out, int out_cap);
int llmk_oo_agenda_next(int id, char *out, int out_cap);
int llmk_oo_agenda_next_ex(int id, int *out_k, char *out, int out_cap);
int llmk_oo_agenda_count(int id);
void llmk_oo_agenda_print(int id);

// Agenda item operations (k is 1-based index as shown by /oo_agenda)
int llmk_oo_action_get(int id, int k, char *out, int out_cap, int *out_state, int *out_prio);
int llmk_oo_action_set_state(int id, int k, int state);
int llmk_oo_action_set_prio(int id, int k, int prio);
int llmk_oo_action_edit(int id, int k, const char *new_text);

// Query helpers (stable, no LLM call)
// Returns 1 if entity exists, 0 otherwise.
int llmk_oo_get_brief(int id, char *goal_out, int goal_cap, char *digest_out, int digest_cap);
int llmk_oo_get_notes_tail(int id, char *out, int out_cap, int max_tail_chars);

// Persistence helpers (stable, no LLM call)
// Export current OO state into a binary-ish ASCII blob.
// Returns bytes written on success; -1 if out_cap is too small.
int llmk_oo_export(char *out, int out_cap);

// Import OO state from a blob created by llmk_oo_export.
// Clears existing entities.
// Returns number of entities imported; 0 if none; -1 on parse error.
int llmk_oo_import(const char *in, int in_len);

#ifdef __cplusplus
}
#endif
