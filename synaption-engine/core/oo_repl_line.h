/* oo_repl_line.h — Synaptic REPL input layer
 * =============================================
 * Host-side (Linux/Windows) wrapper around the synapticion-line editor.
 * Provides REPL readline with persistent history, OO-command completion,
 * and llama-style brace-matching. Not used in UEFI bare-metal build.
 *
 * Architecture layer: synaption-engine / input reflex
 * Backed by: external/synapticion-line (bestline, ISC licence)
 *
 * Usage:
 *   OoReplLine rl;
 *   oo_repl_line_init(&rl, ".oo_history", oo_repl_cmds, OO_REPL_CMD_COUNT);
 *   char *line = oo_repl_line_read(&rl, "oo> ");
 *   if (line) { ... oo_repl_line_free(line); }
 *   oo_repl_line_shutdown(&rl);
 */

#ifndef EC736290_92F6_4D73_A60B_A57ED957A169
#define EC736290_92F6_4D73_A60B_A57ED957A169
#ifndef OO_REPL_LINE_H
#define OO_REPL_LINE_H

/* synapticion-line is host-only: skip in UEFI freestanding builds */
#ifndef UEFI_BUILD

#include "../../external/synapticion-line/bestline.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Context ─────────────────────────────────────────────────────── */
typedef struct {
    const char  *history_path;   /* file path for persistent history    */
    const char **cmds;           /* NULL-terminated list of /commands   */
    size_t       cmd_count;
    int          balance_mode;   /* 1 = bracket balancing (llama-style) */
} OoReplLine;

/* ── Lifecycle ───────────────────────────────────────────────────── */

/* Initialise the REPL line context and load history from disk.
 * cmds: array of command strings (e.g. "/diag", "/models", ...).
 * cmd_count: length of cmds array. */
static inline void oo_repl_line_init(OoReplLine *rl,
                                      const char *history_path,
                                      const char **cmds,
                                      size_t cmd_count)
{
    rl->history_path = history_path;
    rl->cmds         = cmds;
    rl->cmd_count    = cmd_count;
    rl->balance_mode = 1;
    if (history_path) bestlineHistoryLoad(history_path);
    if (rl->balance_mode) bestlineBalanceModeEnable();
    /* Enable llama-style multi-line with brace matching */
    bestlineLlamaMode(1);
}

/* Free resources and persist history. */
static inline void oo_repl_line_shutdown(OoReplLine *rl)
{
    if (rl && rl->history_path) bestlineHistorySave(rl->history_path);
    bestlineHistoryFree();
}

/* ── Completion hook ─────────────────────────────────────────────── */

/* Internal: used as bestlineCompletionCallback. User data passed via
 * the global below because bestline has no user-data pointer. */
static const OoReplLine *_oo_rl_ctx_for_completions = NULL;

static inline void _oo_repl_completion_cb(const char *buf, int len,
                                            bestlineCompletions *lc)
{
    if (!_oo_rl_ctx_for_completions) return;
    const OoReplLine *rl = _oo_rl_ctx_for_completions;
    for (size_t i = 0; i < rl->cmd_count; i++) {
        const char *cmd = rl->cmds[i];
        /* Match prefix: buf may be "/di" → complete to "/diag" */
        size_t blen = (size_t)len;
        size_t clen = 0;
        const char *p = cmd;
        while (*p) { clen++; p++; }
        if (blen <= clen) {
            int match = 1;
            for (size_t j = 0; j < blen; j++) {
                if (buf[j] != cmd[j]) { match = 0; break; }
            }
            if (match) bestlineAddCompletion(lc, cmd);
        }
    }
}

/* ── Read ────────────────────────────────────────────────────────── */

/* Read one line from the user with the given prompt.
 * Returns a heap-allocated string (call oo_repl_line_free when done),
 * or NULL on EOF/error. Adds non-empty lines to history automatically. */
static inline char *oo_repl_line_read(OoReplLine *rl, const char *prompt)
{
    _oo_rl_ctx_for_completions = rl;
    bestlineSetCompletionCallback(_oo_repl_completion_cb);

    char *line = bestline(prompt);
    if (!line) return NULL;

    /* Skip empty and whitespace-only lines for history */
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p) {
        bestlineHistoryAdd(line);
        if (rl->history_path) bestlineHistorySave(rl->history_path);
    }
    return line;
}

/* Release a line returned by oo_repl_line_read. */
static inline void oo_repl_line_free(char *line)
{
    bestlineFree(line);
}

#ifdef __cplusplus
}
#endif

#endif /* !UEFI_BUILD */
#endif /* OO_REPL_LINE_H */


#endif /* EC736290_92F6_4D73_A60B_A57ED957A169 */
