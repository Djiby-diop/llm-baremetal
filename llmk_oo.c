#include "llmk_oo.h"

// ============================================================================
// LLM-OO (Organism-Oriented) minimal runtime
// - Entities are long-lived intentions with energy + lifecycle.
// - Cooperative: execution advances only when the user calls /oo_step or /oo_run.
// ============================================================================

typedef enum {
    LLMK_OO_IDLE = 0,
    LLMK_OO_RUNNING = 1,
    LLMK_OO_DONE = 2,
    LLMK_OO_KILLED = 3,
} LlmkOoStatus;

typedef struct {
    int used;
    int id;
    LlmkOoStatus status;
    int energy;
    int ticks;
    char goal[160];
    char notes[1024];
    int notes_len;
    int notes_truncated;
    char digest[256];

    // Small agenda board (ordered list)
    // state: 0=todo, 1=doing, 2=done
    struct {
        char text[96];
        int state;
        int prio;
    } agenda[8];
    int agenda_count;
} LlmkOoEntity;

#define LLMK_OO_AGENDA_MAX 8
#define LLMK_OO_AGENDA_ITEM_CAP 96

#define LLMK_OO_ACTION_TODO  0
#define LLMK_OO_ACTION_DOING 1
#define LLMK_OO_ACTION_DONE  2

#define LLMK_OO_MAX_ENTITIES 16
static LlmkOoEntity g_oo_entities[LLMK_OO_MAX_ENTITIES];
static int g_oo_next_id = 1;
static LlmkOoOnStep g_on_step = NULL;

static void llmk_oo_copy_ascii(char *dst, int dst_cap, const char *src);

static const CHAR16 *llmk_oo_status_name(LlmkOoStatus st) {
    switch (st) {
        case LLMK_OO_IDLE: return L"idle";
        case LLMK_OO_RUNNING: return L"running";
        case LLMK_OO_DONE: return L"done";
        case LLMK_OO_KILLED: return L"killed";
        default: return L"?";
    }
}

static void ascii_to_char16_local(CHAR16 *dst, const char *src, int max_len) {
    if (!dst || max_len <= 0) return;
    int i = 0;
    if (!src) {
        dst[0] = 0;
        return;
    }
    for (; i < max_len - 1 && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x20 || c > 0x7E) dst[i] = L'_';
        else dst[i] = (CHAR16)c;
    }
    dst[i] = 0;
}

static void llmk_oo_print_ascii_with_newlines(const char *src, int max_chars) {
    if (!src || max_chars <= 0) return;
    int n = 0;
    for (const char *p = src; *p && n < max_chars; p++, n++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n') {
            Print(L"\r\n");
            continue;
        }
        if (c == '\r') {
            // ignore (we normalize to \n elsewhere)
            continue;
        }
        if (c == '\t') c = ' ';
        if (c < 0x20 || c > 0x7E) c = '_';
        Print(L"%c", (CHAR16)c);
    }
}

static void llmk_oo_sanitize_ascii_inplace(char *buf) {
    if (!buf) return;
    for (int i = 0; buf[i]; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == '\r') buf[i] = '\n';
        else if (c == '\n') {
            // keep
        } else if (c == '\t') buf[i] = ' ';
        else if (c < 0x20 || c > 0x7E) buf[i] = ' ';
    }
}

static void llmk_oo_sanitize_agenda_inplace(char *buf) {
    if (!buf) return;
    llmk_oo_sanitize_ascii_inplace(buf);
    // Agenda items should be single-line.
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '\n') buf[i] = ' ';
    }
    // Trim spaces (both ends)
    int end = 0;
    while (buf[end]) end++;
    while (end > 0 && buf[end - 1] == ' ') buf[--end] = 0;
    int start = 0;
    while (buf[start] == ' ') start++;
    if (start > 0) {
        int p = 0;
        while (buf[start]) buf[p++] = buf[start++];
        buf[p] = 0;
    }
}

static void llmk_oo_agenda_compact_entity(LlmkOoEntity *e) {
    if (!e || !e->used) return;
    if (e->agenda_count < 0) e->agenda_count = 0;
    if (e->agenda_count > LLMK_OO_AGENDA_MAX) e->agenda_count = LLMK_OO_AGENDA_MAX;

    int w = 0;
    for (int r = 0; r < e->agenda_count; r++) {
        if (e->agenda[r].text[0] == 0) continue;
        if (e->agenda[r].state == LLMK_OO_ACTION_DONE) continue;
        if (w != r) e->agenda[w] = e->agenda[r];
        if (e->agenda[w].state < LLMK_OO_ACTION_TODO || e->agenda[w].state > LLMK_OO_ACTION_DONE) {
            e->agenda[w].state = LLMK_OO_ACTION_TODO;
        }
        w++;
    }

    for (int i = w; i < LLMK_OO_AGENDA_MAX; i++) {
        e->agenda[i].text[0] = 0;
        e->agenda[i].state = LLMK_OO_ACTION_TODO;
        e->agenda[i].prio = 0;
    }
    e->agenda_count = w;
}

static int llmk_oo_agenda_add_entity_ex(LlmkOoEntity *e, const char *action, int prio, int state) {
    if (!e || !e->used || !action || !action[0]) return 0;
    llmk_oo_agenda_compact_entity(e);
    if (e->agenda_count >= LLMK_OO_AGENDA_MAX) return 0;

    char tmp[LLMK_OO_AGENDA_ITEM_CAP];
    llmk_oo_copy_ascii(tmp, (int)sizeof(tmp), action);
    llmk_oo_sanitize_agenda_inplace(tmp);
    if (!tmp[0]) return 0;

    int idx = e->agenda_count;
    llmk_oo_copy_ascii(e->agenda[idx].text, (int)sizeof(e->agenda[idx].text), tmp);
    llmk_oo_sanitize_agenda_inplace(e->agenda[idx].text);
    e->agenda[idx].prio = prio;
    e->agenda[idx].state = state;
    if (e->agenda[idx].state < LLMK_OO_ACTION_TODO || e->agenda[idx].state > LLMK_OO_ACTION_DONE) {
        e->agenda[idx].state = LLMK_OO_ACTION_TODO;
    }
    e->agenda_count++;
    return 1;
}

static int llmk_oo_agenda_pick_best_index(const LlmkOoEntity *e) {
    if (!e || !e->used) return -1;
    if (e->agenda_count <= 0) return -1;

    // Prefer DOING, then TODO. Higher prio wins; stable by index.
    for (int pass = 0; pass < 2; pass++) {
        int want = (pass == 0) ? LLMK_OO_ACTION_DOING : LLMK_OO_ACTION_TODO;
        int best = -1;
        int best_prio = -2147483647;
        for (int i = 0; i < e->agenda_count; i++) {
            if (e->agenda[i].text[0] == 0) continue;
            if (e->agenda[i].state != want) continue;
            int p = e->agenda[i].prio;
            if (best < 0 || p > best_prio) {
                best = i;
                best_prio = p;
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}

static int llmk_oo_agenda_peek_entity_ex(const LlmkOoEntity *e, int *out_k, char *out, int out_cap, int *out_state, int *out_prio) {
    if (out && out_cap > 0) out[0] = 0;
    if (out_k) *out_k = 0;
    if (out_state) *out_state = 0;
    if (out_prio) *out_prio = 0;
    if (!e || !e->used) return 0;
    if (!out || out_cap <= 0) return 0;

    int idx = llmk_oo_agenda_pick_best_index(e);
    if (idx < 0) return 0;
    llmk_oo_copy_ascii(out, out_cap, e->agenda[idx].text);
    llmk_oo_sanitize_agenda_inplace(out);
    if (out_k) *out_k = idx + 1;
    if (out_state) *out_state = e->agenda[idx].state;
    if (out_prio) *out_prio = e->agenda[idx].prio;
    return out[0] != 0;
}

static void llmk_oo_copy_ascii(char *dst, int dst_cap, const char *src) {
    if (!dst || dst_cap <= 0) return;
    int p = 0;
    if (!src) {
        dst[0] = 0;
        return;
    }
    for (const char *s = src; *s && p + 1 < dst_cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\r' || c == '\n') {
            c = '\n';
        } else if (c < 0x20 || c > 0x7E) {
            c = ' ';
        }
        dst[p++] = (char)c;
    }
    dst[p] = 0;
}

static int llmk_oo_find_index_by_id(int id) {
    for (int i = 0; i < LLMK_OO_MAX_ENTITIES; i++) {
        if (g_oo_entities[i].used && g_oo_entities[i].id == id) return i;
    }
    return -1;
}

static int llmk_oo_ascii_is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static int llmk_oo_ascii_starts_with(const char *s, const char *pfx) {
    if (!s || !pfx) return 0;
    for (; *pfx; pfx++, s++) {
        if (*s != *pfx) return 0;
    }
    return 1;
}

static int llmk_oo_append_bytes(char *out, int cap, int pos, const char *src, int n) {
    if (!out || cap <= 0) return -1;
    if (pos < 0) pos = 0;
    if (!src) n = 0;
    if (n < 0) n = 0;
    if (pos + n >= cap) return -1;
    for (int i = 0; i < n; i++) out[pos++] = src[i];
    out[pos] = 0;
    return pos;
}

static int llmk_oo_append_cstr(char *out, int cap, int pos, const char *s) {
    if (!s) s = "";
    int n = 0;
    while (s[n]) n++;
    return llmk_oo_append_bytes(out, cap, pos, s, n);
}

static int llmk_oo_append_u32(char *out, int cap, int pos, UINT32 v) {
    if (!out || cap <= 0) return -1;
    char tmp[16];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        if (pos + 1 >= cap) return -1;
        out[pos++] = tmp[i];
    }
    out[pos] = 0;
    return pos;
}

static const char *llmk_oo_read_line(const char *p, const char *end, const char **line_out, int *len_out) {
    if (line_out) *line_out = NULL;
    if (len_out) *len_out = 0;
    if (!p || !end || p >= end) return NULL;
    const char *s = p;
    while (p < end && *p != '\n') p++;
    int n = (int)(p - s);
    if (line_out) *line_out = s;
    if (len_out) *len_out = n;
    if (p < end && *p == '\n') p++;
    return p;
}

static int llmk_oo_parse_kv_u32(const char *line, int len, const char *key, UINT32 *out) {
    if (!line || len <= 0 || !key || !out) return 0;
    int klen = 0;
    while (key[klen]) klen++;
    if (len < klen + 1) return 0;
    for (int i = 0; i < klen; i++) if (line[i] != key[i]) return 0;
    if (line[klen] != '=') return 0;
    UINT32 v = 0;
    int i = klen + 1;
    if (i >= len) return 0;
    for (; i < len; i++) {
        char c = line[i];
        if (!llmk_oo_ascii_is_digit(c)) return 0;
        v = v * 10U + (UINT32)(c - '0');
    }
    *out = v;
    return 1;
}

static int llmk_oo_parse_kv_i32(const char *line, int len, const char *key, int *out) {
    if (!line || len <= 0 || !key || !out) return 0;
    int klen = 0;
    while (key[klen]) klen++;
    if (len < klen + 1) return 0;
    for (int i = 0; i < klen; i++) if (line[i] != key[i]) return 0;
    if (line[klen] != '=') return 0;

    int i = klen + 1;
    if (i >= len) return 0;
    int sign = 1;
    if (line[i] == '-') {
        sign = -1;
        i++;
    } else if (line[i] == '+') {
        i++;
    }
    if (i >= len) return 0;

    int v = 0;
    for (; i < len; i++) {
        char c = line[i];
        if (!llmk_oo_ascii_is_digit(c)) return 0;
        v = v * 10 + (c - '0');
    }
    *out = v * sign;
    return 1;
}

static UINT32 llmk_oo_crc32_update(UINT32 crc, const unsigned char *buf, int len) {
    // Standard CRC-32 (IEEE 802.3) reflected polynomial 0xEDB88320
    crc = ~crc;
    for (int i = 0; i < len; i++) {
        crc ^= (UINT32)buf[i];
        for (int j = 0; j < 8; j++) {
            UINT32 m = (UINT32)(-(int)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & m);
        }
    }
    return ~crc;
}

static UINT32 llmk_oo_crc32(const void *data, int len) {
    if (!data || len <= 0) return 0;
    return llmk_oo_crc32_update(0U, (const unsigned char *)data, len);
}

static int llmk_oo_append_hex8(char *out, int out_cap, int pos, UINT32 v) {
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        unsigned int nib = (v >> (i * 4)) & 0xF;
        if (pos + 1 >= out_cap) return -1;
        out[pos++] = hex[nib];
    }
    return pos;
}

static int llmk_oo_import_entity(UINT32 id, UINT32 energy, UINT32 ticks, UINT32 status,
                                 const char *goal, UINT32 goal_len,
                                 const char *digest, UINT32 digest_len,
                                 const char *notes, UINT32 notes_len,
                                 UINT32 agenda_count, const char **agenda_items, const UINT32 *agenda_lens,
                                 const UINT32 *agenda_states, const int *agenda_prios) {
    if (id == 0) return 0;
    // Find free slot
    int slot = -1;
    for (int i = 0; i < LLMK_OO_MAX_ENTITIES; i++) {
        if (!g_oo_entities[i].used) { slot = i; break; }
    }
    if (slot < 0) return 0;

    LlmkOoEntity *e = &g_oo_entities[slot];
    e->used = 1;
    e->id = (int)id;
    e->status = (LlmkOoStatus)status;
    e->energy = (int)energy;
    e->ticks = (int)ticks;

    // goal
    UINT32 gl = goal_len;
    if (gl > (UINT32)sizeof(e->goal) - 1U) gl = (UINT32)sizeof(e->goal) - 1U;
    for (UINT32 i = 0; i < gl; i++) e->goal[i] = goal ? goal[i] : 0;
    e->goal[gl] = 0;
    llmk_oo_sanitize_ascii_inplace(e->goal);

    // digest
    UINT32 dl = digest_len;
    if (dl > (UINT32)sizeof(e->digest) - 1U) dl = (UINT32)sizeof(e->digest) - 1U;
    for (UINT32 i = 0; i < dl; i++) e->digest[i] = digest ? digest[i] : 0;
    e->digest[dl] = 0;
    llmk_oo_sanitize_ascii_inplace(e->digest);

    // notes
    e->notes_truncated = 0;
    UINT32 nl = notes_len;
    if (nl > (UINT32)sizeof(e->notes) - 1U) {
        nl = (UINT32)sizeof(e->notes) - 1U;
        e->notes_truncated = 1;
    }
    for (UINT32 i = 0; i < nl; i++) e->notes[i] = notes ? notes[i] : 0;
    e->notes[nl] = 0;
    e->notes_len = (int)nl;
    llmk_oo_sanitize_ascii_inplace(e->notes);

    // agenda
    e->agenda_count = 0;
    for (int i = 0; i < LLMK_OO_AGENDA_MAX; i++) {
        e->agenda[i].text[0] = 0;
        e->agenda[i].state = LLMK_OO_ACTION_TODO;
        e->agenda[i].prio = 0;
    }
    if (agenda_count > 0 && agenda_items && agenda_lens) {
        UINT32 n = agenda_count;
        if (n > (UINT32)LLMK_OO_AGENDA_MAX) n = (UINT32)LLMK_OO_AGENDA_MAX;
        for (UINT32 i = 0; i < n; i++) {
            char tmp[LLMK_OO_AGENDA_ITEM_CAP];
            int cap = (int)sizeof(tmp);
            int p = 0;
            UINT32 al = agenda_lens[i];
            if (al > (UINT32)(cap - 1)) al = (UINT32)(cap - 1);
            for (UINT32 k = 0; k < al; k++) {
                tmp[p++] = agenda_items[i] ? agenda_items[i][k] : 0;
            }
            tmp[p] = 0;
            llmk_oo_sanitize_agenda_inplace(tmp);
            if (!tmp[0]) continue;

            int pr = agenda_prios ? agenda_prios[i] : 0;
            int st = LLMK_OO_ACTION_TODO;
            if (agenda_states) {
                UINT32 s = agenda_states[i];
                if (s == 1) st = LLMK_OO_ACTION_DOING;
                else if (s == 2) st = LLMK_OO_ACTION_DONE;
                else st = LLMK_OO_ACTION_TODO;
            }
            llmk_oo_agenda_add_entity_ex(e, tmp, pr, st);
        }
    }
    llmk_oo_agenda_compact_entity(e);

    // Clamp status
    if (e->status < LLMK_OO_IDLE || e->status > LLMK_OO_KILLED) e->status = LLMK_OO_IDLE;
    if (e->energy < 0) e->energy = 0;
    if (e->ticks < 0) e->ticks = 0;
    return 1;
}

static void llmk_oo_step_index(int idx) {
    if (idx < 0 || idx >= LLMK_OO_MAX_ENTITIES) return;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return;
    if (e->status == LLMK_OO_DONE || e->status == LLMK_OO_KILLED) return;

    e->status = LLMK_OO_RUNNING;
    e->ticks++;
    if (e->energy > 0) e->energy--;
    if (e->energy <= 0) e->status = LLMK_OO_DONE;
    else e->status = LLMK_OO_IDLE;

    if (g_on_step) {
        g_on_step(e->id, e->ticks, e->energy);
    }
}

void llmk_oo_init(void) {
    for (int i = 0; i < LLMK_OO_MAX_ENTITIES; i++) {
        g_oo_entities[i].used = 0;
        g_oo_entities[i].id = 0;
        g_oo_entities[i].status = LLMK_OO_IDLE;
        g_oo_entities[i].energy = 0;
        g_oo_entities[i].ticks = 0;
        g_oo_entities[i].goal[0] = 0;
        g_oo_entities[i].notes[0] = 0;
        g_oo_entities[i].notes_len = 0;
        g_oo_entities[i].notes_truncated = 0;
        g_oo_entities[i].digest[0] = 0;
        g_oo_entities[i].agenda_count = 0;
        for (int j = 0; j < LLMK_OO_AGENDA_MAX; j++) {
            g_oo_entities[i].agenda[j].text[0] = 0;
            g_oo_entities[i].agenda[j].state = LLMK_OO_ACTION_TODO;
            g_oo_entities[i].agenda[j].prio = 0;
        }
    }
    g_oo_next_id = 1;
    g_on_step = NULL;
}

void llmk_oo_set_on_step(LlmkOoOnStep cb) {
    g_on_step = cb;
}

int llmk_oo_new(const char *goal) {
    if (!goal || !goal[0]) return -1;

    for (int i = 0; i < LLMK_OO_MAX_ENTITIES; i++) {
        if (g_oo_entities[i].used) continue;

        g_oo_entities[i].used = 1;
        g_oo_entities[i].id = g_oo_next_id++;
        g_oo_entities[i].status = LLMK_OO_IDLE;
        g_oo_entities[i].energy = 100;
        g_oo_entities[i].ticks = 0;

        int p = 0;
        for (const char *s = goal; *s && p + 1 < (int)sizeof(g_oo_entities[i].goal); s++) {
            unsigned char c = (unsigned char)*s;
            if (c < 0x20 || c > 0x7E) c = ' ';
            g_oo_entities[i].goal[p++] = (char)c;
        }
        g_oo_entities[i].goal[p] = 0;

        g_oo_entities[i].notes[0] = 0;
        g_oo_entities[i].notes_len = 0;
        g_oo_entities[i].notes_truncated = 0;

        g_oo_entities[i].agenda_count = 0;
        for (int j = 0; j < LLMK_OO_AGENDA_MAX; j++) {
            g_oo_entities[i].agenda[j].text[0] = 0;
            g_oo_entities[i].agenda[j].state = LLMK_OO_ACTION_TODO;
            g_oo_entities[i].agenda[j].prio = 0;
        }
        g_oo_entities[i].digest[0] = 0;

        return g_oo_entities[i].id;
    }

    return -1;
}

int llmk_oo_note(int id, const char *text) {
    if (!text || !text[0]) return 0;
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;

    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;

    // Copy note text into a temp buffer to sanitize.
    // Keep this reasonably large so /oo_think can store useful snippets.
    char tmp[512];
    llmk_oo_copy_ascii(tmp, (int)sizeof(tmp), text);
    llmk_oo_sanitize_ascii_inplace(tmp);

    // Append with newline.
    int avail = (int)sizeof(e->notes) - 1 - e->notes_len;
    if (avail <= 0) {
        e->notes_truncated = 1;
        return 1;
    }

    int need_nl = (e->notes_len > 0 && e->notes[e->notes_len - 1] != '\n');
    if (need_nl) {
        if (avail <= 1) {
            e->notes_truncated = 1;
            return 1;
        }
        e->notes[e->notes_len++] = '\n';
        e->notes[e->notes_len] = 0;
        avail--;
    }

    int wrote = 0;
    for (int i = 0; tmp[i] && wrote + 1 < avail; i++) {
        e->notes[e->notes_len++] = tmp[i];
        wrote++;
    }
    if (tmp[wrote] != 0) e->notes_truncated = 1;

    if (e->notes_len + 1 < (int)sizeof(e->notes)) {
        e->notes[e->notes_len++] = '\n';
    } else {
        e->notes_truncated = 1;
    }
    if (e->notes_len >= (int)sizeof(e->notes)) e->notes_len = (int)sizeof(e->notes) - 1;
    e->notes[e->notes_len] = 0;
    return 1;
}

int llmk_oo_get_brief(int id, char *goal_out, int goal_cap, char *digest_out, int digest_cap) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;

    if (goal_out && goal_cap > 0) {
        llmk_oo_copy_ascii(goal_out, goal_cap, e->goal);
        llmk_oo_sanitize_ascii_inplace(goal_out);
    }
    if (digest_out && digest_cap > 0) {
        llmk_oo_copy_ascii(digest_out, digest_cap, e->digest);
        llmk_oo_sanitize_ascii_inplace(digest_out);
    }
    return 1;
}

int llmk_oo_get_notes_tail(int id, char *out, int out_cap, int max_tail_chars) {
    if (!out || out_cap <= 0) return 0;
    out[0] = 0;

    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;
    if (e->notes_len <= 0) return 1;

    int tail = max_tail_chars;
    if (tail < 32) tail = 32;
    if (tail > 800) tail = 800;

    int start = e->notes_len - tail;
    if (start < 0) start = 0;
    while (start > 0 && e->notes[start] != '\n') start--; // align to line
    if (e->notes[start] == '\n') start++;

    llmk_oo_copy_ascii(out, out_cap, e->notes + start);
    llmk_oo_sanitize_ascii_inplace(out);

    // squash newlines into spaces for prompt-compactness
    for (int i = 0; out[i]; i++) {
        if (out[i] == '\n') out[i] = ' ';
    }

    return 1;
}

int llmk_oo_agenda_add(int id, const char *action) {
    return llmk_oo_agenda_add_ex(id, action, 0);
}

int llmk_oo_agenda_add_ex(int id, const char *action, int prio) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;
    return llmk_oo_agenda_add_entity_ex(e, action, prio, LLMK_OO_ACTION_TODO);
}

int llmk_oo_agenda_peek(int id, char *out, int out_cap) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    const LlmkOoEntity *e = &g_oo_entities[idx];
    return llmk_oo_agenda_peek_entity_ex(e, NULL, out, out_cap, NULL, NULL);
}

int llmk_oo_agenda_next(int id, char *out, int out_cap) {
    return llmk_oo_agenda_next_ex(id, NULL, out, out_cap);
}

int llmk_oo_agenda_next_ex(int id, int *out_k, char *out, int out_cap) {
    if (out_k) *out_k = 0;
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;
    llmk_oo_agenda_compact_entity(e);
    int pick = llmk_oo_agenda_pick_best_index(e);
    if (pick < 0) return 0;
    if (pick >= e->agenda_count) return 0;
    if (out && out_cap > 0) {
        llmk_oo_copy_ascii(out, out_cap, e->agenda[pick].text);
        llmk_oo_sanitize_agenda_inplace(out);
    }
    e->agenda[pick].state = LLMK_OO_ACTION_DOING;
    if (out_k) *out_k = pick + 1;
    return (out && out_cap > 0) ? (out[0] != 0) : 1;
}

int llmk_oo_agenda_count(int id) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;
    llmk_oo_agenda_compact_entity(e);
    return e->agenda_count;
}

void llmk_oo_agenda_print(int id) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return;

    llmk_oo_agenda_compact_entity(e);

    Print(L"\r\n  agenda:\r\n");
    if (e->agenda_count <= 0) {
        Print(L"  (empty)\r\n");
        return;
    }
    for (int i = 0; i < e->agenda_count && i < LLMK_OO_AGENDA_MAX; i++) {
        CHAR16 a16[LLMK_OO_AGENDA_ITEM_CAP + 8];
        ascii_to_char16_local(a16, e->agenda[i].text, (int)(sizeof(a16) / sizeof(a16[0])));
        const CHAR16 *st = L"[ ]";
        if (e->agenda[i].state == LLMK_OO_ACTION_DOING) st = L"[>]";
        else if (e->agenda[i].state == LLMK_OO_ACTION_DONE) st = L"[x]";
        Print(L"  %d %s p=%d  %s\r\n", i + 1, st, e->agenda[i].prio, a16);
    }
}

int llmk_oo_action_get(int id, int k, char *out, int out_cap, int *out_state, int *out_prio) {
    if (out && out_cap > 0) out[0] = 0;
    if (out_state) *out_state = 0;
    if (out_prio) *out_prio = 0;
    if (k <= 0) return 0;
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;
    llmk_oo_agenda_compact_entity(e);
    int i = k - 1;
    if (i < 0 || i >= e->agenda_count) return 0;
    if (out && out_cap > 0) {
        llmk_oo_copy_ascii(out, out_cap, e->agenda[i].text);
        llmk_oo_sanitize_agenda_inplace(out);
    }
    if (out_state) *out_state = e->agenda[i].state;
    if (out_prio) *out_prio = e->agenda[i].prio;
    return 1;
}

int llmk_oo_action_set_state(int id, int k, int state) {
    if (k <= 0) return 0;
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;
    llmk_oo_agenda_compact_entity(e);
    int i = k - 1;
    if (i < 0 || i >= e->agenda_count) return 0;
    if (state < LLMK_OO_ACTION_TODO || state > LLMK_OO_ACTION_DONE) state = LLMK_OO_ACTION_TODO;
    e->agenda[i].state = state;
    if (state == LLMK_OO_ACTION_DONE) llmk_oo_agenda_compact_entity(e);
    return 1;
}

int llmk_oo_action_set_prio(int id, int k, int prio) {
    if (k <= 0) return 0;
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;
    llmk_oo_agenda_compact_entity(e);
    int i = k - 1;
    if (i < 0 || i >= e->agenda_count) return 0;
    e->agenda[i].prio = prio;
    return 1;
}

int llmk_oo_action_edit(int id, int k, const char *new_text) {
    if (k <= 0 || !new_text) return 0;
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;
    llmk_oo_agenda_compact_entity(e);
    int i = k - 1;
    if (i < 0 || i >= e->agenda_count) return 0;
    llmk_oo_copy_ascii(e->agenda[i].text, (int)sizeof(e->agenda[i].text), new_text);
    llmk_oo_sanitize_agenda_inplace(e->agenda[i].text);
    if (!e->agenda[i].text[0]) return 0;
    return 1;
}

int llmk_oo_show_print(int id) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;

    CHAR16 goal16[168];
    ascii_to_char16_local(goal16, e->goal, (int)(sizeof(goal16) / sizeof(goal16[0])));

    Print(L"\r\nOO entity %d:\r\n", e->id);
    Print(L"  status=%s energy=%d ticks=%d\r\n", llmk_oo_status_name(e->status), e->energy, e->ticks);
    Print(L"  goal=%s\r\n", goal16);

    if (e->digest[0]) {
        CHAR16 dig16[260];
        ascii_to_char16_local(dig16, e->digest, (int)(sizeof(dig16) / sizeof(dig16[0])));
        Print(L"\r\n  digest:\r\n  %s\r\n", dig16);
    } else {
        Print(L"\r\n  digest: (none)\r\n");
    }

    // Print last part of notes to avoid flooding console.
    if (e->notes_len > 0) {
        int start = e->notes_len - 480;
        if (start < 0) start = 0;
        while (start > 0 && e->notes[start] != '\n') start--; // align to line
        if (e->notes[start] == '\n') start++;

        Print(L"\r\n  notes (tail):\r\n");
        llmk_oo_print_ascii_with_newlines(e->notes + start, 520);
        if (e->notes_truncated) Print(L"\r\n  (notes truncated)\r\n");
        Print(L"\r\n");
    } else {
        Print(L"\r\n  notes: (empty)\r\n\r\n");
    }

    // Agenda (small plan queue)
    llmk_oo_agenda_print(id);
    Print(L"\r\n");

    return 1;
}

int llmk_oo_digest(int id) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    LlmkOoEntity *e = &g_oo_entities[idx];
    if (!e->used) return 0;

    // Heuristic digest: goal/status/ticks/energy + agenda + last note excerpt.
    char last[96];
    last[0] = 0;
    if (e->notes_len > 0) {
        int s = e->notes_len - 80;
        if (s < 0) s = 0;
        llmk_oo_copy_ascii(last, (int)sizeof(last), e->notes + s);
        llmk_oo_sanitize_ascii_inplace(last);
        // squash newlines
        for (int i = 0; last[i]; i++) if (last[i] == '\n') last[i] = ' ';
    }

    // Compose digest.
    // Keep ASCII and bounded.
    int p = 0;
    char buf[256];
    buf[0] = 0;

    // Very small itoa.
    char tmp[32];
    tmp[0] = 0;

    // "goal: ..."
    const char *g = e->goal;
    for (const char *s = "goal: "; *s && p + 1 < (int)sizeof(buf); s++) buf[p++] = *s;
    for (int i = 0; g[i] && i < 48 && p + 1 < (int)sizeof(buf); i++) buf[p++] = g[i];

    for (const char *s = "; st="; *s && p + 1 < (int)sizeof(buf); s++) buf[p++] = *s;
    {
        const char *st = "idle";
        if (e->status == LLMK_OO_RUNNING) st = "running";
        else if (e->status == LLMK_OO_DONE) st = "done";
        else if (e->status == LLMK_OO_KILLED) st = "killed";
        for (int i = 0; st[i] && p + 1 < (int)sizeof(buf); i++) buf[p++] = st[i];
    }

    for (const char *s = "; ticks="; *s && p + 1 < (int)sizeof(buf); s++) buf[p++] = *s;

    // ticks
    {
        int v = e->ticks;
        int n = 0;
        if (v == 0) tmp[n++] = '0';
        else {
            int vv = v;
            char rev[16];
            int rn = 0;
            while (vv > 0 && rn < (int)sizeof(rev)) { rev[rn++] = (char)('0' + (vv % 10)); vv /= 10; }
            while (rn > 0) tmp[n++] = rev[--rn];
        }
        tmp[n] = 0;
        for (int i = 0; tmp[i] && p + 1 < (int)sizeof(buf); i++) buf[p++] = tmp[i];
    }

    for (const char *s = "; notes="; *s && p + 1 < (int)sizeof(buf); s++) buf[p++] = *s;
    {
        int v = e->notes_len;
        int n = 0;
        if (v == 0) tmp[n++] = '0';
        else {
            int vv = v;
            char rev[16];
            int rn = 0;
            while (vv > 0 && rn < (int)sizeof(rev)) { rev[rn++] = (char)('0' + (vv % 10)); vv /= 10; }
            while (rn > 0) tmp[n++] = rev[--rn];
        }
        tmp[n] = 0;
        for (int i = 0; tmp[i] && p + 1 < (int)sizeof(buf); i++) buf[p++] = tmp[i];
    }

    // Agenda summary (next + remaining)
    llmk_oo_agenda_compact_entity(e);
    if (e->agenda_count > 0) {
        char next[LLMK_OO_AGENDA_ITEM_CAP];
        next[0] = 0;
        llmk_oo_agenda_peek_entity_ex(e, NULL, next, (int)sizeof(next), NULL, NULL);
        for (const char *s = "; next="; *s && p + 1 < (int)sizeof(buf); s++) buf[p++] = *s;
        for (int i = 0; next[i] && i < 48 && p + 1 < (int)sizeof(buf); i++) buf[p++] = next[i];
        for (const char *s = "; todo="; *s && p + 1 < (int)sizeof(buf); s++) buf[p++] = *s;
        {
            int v = e->agenda_count;
            int n = 0;
            if (v == 0) tmp[n++] = '0';
            else {
                int vv = v;
                char rev[16];
                int rn = 0;
                while (vv > 0 && rn < (int)sizeof(rev)) { rev[rn++] = (char)('0' + (vv % 10)); vv /= 10; }
                while (rn > 0) tmp[n++] = rev[--rn];
            }
            tmp[n] = 0;
            for (int i = 0; tmp[i] && p + 1 < (int)sizeof(buf); i++) buf[p++] = tmp[i];
        }
    }

    if (last[0]) {
        for (const char *s = "; last="; *s && p + 1 < (int)sizeof(buf); s++) buf[p++] = *s;
        for (int i = 0; last[i] && p + 1 < (int)sizeof(buf); i++) buf[p++] = last[i];
    }

    buf[p] = 0;
    llmk_oo_copy_ascii(e->digest, (int)sizeof(e->digest), buf);
    llmk_oo_sanitize_ascii_inplace(e->digest);

    // Compression behavior: keep only a readable tail, with a marker.
    if (e->notes_len > 896) {
        const char *marker = "[...snip...]\n";
        int marker_len = 0;
        while (marker[marker_len]) marker_len++;

        int keep = 640;
        int start = e->notes_len - keep;
        if (start < 0) start = 0;
        while (start > 0 && e->notes[start] != '\n') start--; // align
        if (e->notes[start] == '\n') start++;

        char tmp_notes[1024];
        int pos2 = 0;
        for (int i = 0; i < marker_len && pos2 + 1 < (int)sizeof(tmp_notes); i++) tmp_notes[pos2++] = marker[i];
        for (int i = start; i < e->notes_len && pos2 + 1 < (int)sizeof(tmp_notes); i++) tmp_notes[pos2++] = e->notes[i];
        tmp_notes[pos2] = 0;
        llmk_oo_copy_ascii(e->notes, (int)sizeof(e->notes), tmp_notes);
        llmk_oo_sanitize_ascii_inplace(e->notes);
        e->notes_len = 0;
        while (e->notes[e->notes_len]) e->notes_len++;
        e->notes_truncated = 1;
    }

    return 1;
}

int llmk_oo_kill(int id) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    g_oo_entities[idx].status = LLMK_OO_KILLED;
    g_oo_entities[idx].used = 0;
    return 1;
}

void llmk_oo_list_print(void) {
    int any = 0;
    Print(L"\r\nOO entities:\r\n");
    Print(L"  id   status    energy ticks  goal\r\n");
    Print(L"  ---- --------- ------ ------ --------------------------------\r\n");

    for (int i = 0; i < LLMK_OO_MAX_ENTITIES; i++) {
        if (!g_oo_entities[i].used) continue;
        any = 1;

        CHAR16 goal16[168];
        ascii_to_char16_local(goal16, g_oo_entities[i].goal, (int)(sizeof(goal16) / sizeof(goal16[0])));

        Print(L"  %4d %-9s %6d %6d  %s\r\n",
              g_oo_entities[i].id,
              llmk_oo_status_name(g_oo_entities[i].status),
              g_oo_entities[i].energy,
              g_oo_entities[i].ticks,
              goal16);
    }

    if (!any) Print(L"  (none)\r\n");
    Print(L"\r\n");
}

int llmk_oo_step(int id) {
    int idx = llmk_oo_find_index_by_id(id);
    if (idx < 0) return 0;
    llmk_oo_step_index(idx);
    return 1;
}

int llmk_oo_run(int steps) {
    if (steps < 1) steps = 1;
    if (steps > 256) steps = 256;

    int ran = 0;
    for (int s = 0; s < steps; s++) {
        int picked = -1;
        int best_energy = -1;

        for (int j = 0; j < LLMK_OO_MAX_ENTITIES; j++) {
            if (!g_oo_entities[j].used) continue;
            if (g_oo_entities[j].status != LLMK_OO_IDLE) continue;
            if (g_oo_entities[j].energy <= 0) continue;
            if (g_oo_entities[j].energy > best_energy) {
                best_energy = g_oo_entities[j].energy;
                picked = j;
            }
        }

        if (picked < 0) break;
        llmk_oo_step_index(picked);
        ran++;
    }

    return ran;
}

int llmk_oo_export(char *out, int out_cap) {
    if (!out || out_cap <= 0) return -1;
    int pos = 0;

    // Header
    pos = llmk_oo_append_cstr(out, out_cap, pos, "OO4\n");
    if (pos < 0) return -1;

    const int crc_start = pos;

    for (int i = 0; i < LLMK_OO_MAX_ENTITIES; i++) {
        LlmkOoEntity *e = &g_oo_entities[i];
        if (!e->used) continue;

        pos = llmk_oo_append_cstr(out, out_cap, pos, "BEGIN\n");
        if (pos < 0) return -1;

        pos = llmk_oo_append_cstr(out, out_cap, pos, "id=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)e->id);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;

        pos = llmk_oo_append_cstr(out, out_cap, pos, "energy=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)e->energy);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;

        pos = llmk_oo_append_cstr(out, out_cap, pos, "ticks=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)e->ticks);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;

        pos = llmk_oo_append_cstr(out, out_cap, pos, "status=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)e->status);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;

        // goal bytes
        int goal_len = 0;
        while (e->goal[goal_len]) goal_len++;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "goal_len=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)goal_len);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;
        pos = llmk_oo_append_bytes(out, out_cap, pos, e->goal, goal_len);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;

        // digest bytes
        int dig_len = 0;
        while (e->digest[dig_len]) dig_len++;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "digest_len=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)dig_len);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;
        pos = llmk_oo_append_bytes(out, out_cap, pos, e->digest, dig_len);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;

        // notes bytes
        int notes_len = e->notes_len;
        if (notes_len < 0) notes_len = 0;
        if (notes_len > (int)sizeof(e->notes) - 1) notes_len = (int)sizeof(e->notes) - 1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "notes_len=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)notes_len);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;
        pos = llmk_oo_append_bytes(out, out_cap, pos, e->notes, notes_len);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;

        // agenda
        llmk_oo_agenda_compact_entity(e);
        UINT32 ac = (UINT32)e->agenda_count;
        if (ac > (UINT32)LLMK_OO_AGENDA_MAX) ac = (UINT32)LLMK_OO_AGENDA_MAX;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "agenda_count=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_u32(out, out_cap, pos, ac);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;
        for (UINT32 ai = 0; ai < ac; ai++) {
            int aidx = (int)ai;

            // agenda_state
            pos = llmk_oo_append_cstr(out, out_cap, pos, "agenda_state=");
            if (pos < 0) return -1;
            pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)e->agenda[aidx].state);
            if (pos < 0) return -1;
            pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
            if (pos < 0) return -1;

            // agenda_prio (signed)
            pos = llmk_oo_append_cstr(out, out_cap, pos, "agenda_prio=");
            if (pos < 0) return -1;
            {
                char tmp[16];
                int tp = 0;
                int v = e->agenda[aidx].prio;
                if (v < 0) {
                    tmp[tp++] = '-';
                    v = -v;
                }
                if (v == 0) {
                    tmp[tp++] = '0';
                } else {
                    char rev[16];
                    int rp = 0;
                    while (v > 0 && rp + 1 < (int)sizeof(rev)) {
                        rev[rp++] = (char)('0' + (v % 10));
                        v /= 10;
                    }
                    while (rp > 0) tmp[tp++] = rev[--rp];
                }
                tmp[tp] = 0;
                pos = llmk_oo_append_cstr(out, out_cap, pos, tmp);
                if (pos < 0) return -1;
            }
            pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
            if (pos < 0) return -1;

            int alen = 0;
            while (e->agenda[aidx].text[alen]) alen++;
            pos = llmk_oo_append_cstr(out, out_cap, pos, "agenda_len=");
            if (pos < 0) return -1;
            pos = llmk_oo_append_u32(out, out_cap, pos, (UINT32)alen);
            if (pos < 0) return -1;
            pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
            if (pos < 0) return -1;
            pos = llmk_oo_append_bytes(out, out_cap, pos, e->agenda[aidx].text, alen);
            if (pos < 0) return -1;
            pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
            if (pos < 0) return -1;
        }

        pos = llmk_oo_append_cstr(out, out_cap, pos, "END\n");
        if (pos < 0) return -1;
    }

    // Optional integrity line (CRC32 over payload excluding header+crc line)
    {
        UINT32 crc = llmk_oo_crc32(out + crc_start, pos - crc_start);
        pos = llmk_oo_append_cstr(out, out_cap, pos, "crc32=");
        if (pos < 0) return -1;
        pos = llmk_oo_append_hex8(out, out_cap, pos, crc);
        if (pos < 0) return -1;
        pos = llmk_oo_append_cstr(out, out_cap, pos, "\n");
        if (pos < 0) return -1;
    }

    pos = llmk_oo_append_cstr(out, out_cap, pos, "DONE\n");
    if (pos < 0) return -1;
    return pos;
}

int llmk_oo_import(const char *in, int in_len) {
    if (!in || in_len <= 0) return 0;
    const char *p = in;
    const char *end = in + in_len;

    // Require header line "OO1", "OO2", "OO3" or "OO4"
    const char *line = NULL;
    int len = 0;
    p = llmk_oo_read_line(p, end, &line, &len);
    if (!p || !line || len < 3) return -1;

    int version = 0;
    if (len == 3 && line[0] == 'O' && line[1] == 'O' && line[2] == '1') version = 1;
    else if (len == 3 && line[0] == 'O' && line[1] == 'O' && line[2] == '2') version = 2;
    else if (len == 3 && line[0] == 'O' && line[1] == 'O' && line[2] == '3') version = 3;
    else if (len == 3 && line[0] == 'O' && line[1] == 'O' && line[2] == '4') version = 4;
    else return -1;

    llmk_oo_init();

    int imported = 0;
    UINT32 max_id = 0;

    // Optional CRC check (OO3). If present, we verify bytes from after header up to the crc32 line.
    UINT32 expected_crc = 0;
    int have_crc = 0;
    const char *crc_line_ptr = NULL;

    while (p && p < end) {
        p = llmk_oo_read_line(p, end, &line, &len);
        if (!p || !line) break;
        if (len == 0) continue;

        if (len >= 6 && line[0] == 'c' && line[1] == 'r' && line[2] == 'c' && line[3] == '3' && line[4] == '2' && line[5] == '=') {
            // Parse 8 hex chars after '=' (lower/upper accepted)
            UINT32 v = 0;
            int ok = 1;
            if (len < 6 + 8) ok = 0;
            for (int i = 0; ok && i < 8; i++) {
                char c = line[6 + i];
                UINT32 n = 0;
                if (c >= '0' && c <= '9') n = (UINT32)(c - '0');
                else if (c >= 'a' && c <= 'f') n = (UINT32)(10 + (c - 'a'));
                else if (c >= 'A' && c <= 'F') n = (UINT32)(10 + (c - 'A'));
                else ok = 0;
                v = (v << 4) | n;
            }
            if (ok) {
                expected_crc = v;
                have_crc = 1;
                crc_line_ptr = line;
            }
            continue;
        }

        if (len == 4 && line[0] == 'D' && line[1] == 'O' && line[2] == 'N' && line[3] == 'E') {
            // Verify CRC if present
            if (version >= 3 && have_crc && crc_line_ptr) {
                const char *payload_start = in;
                // Skip first line (header) including newline.
                const char *q = in;
                while (q < end && *q != '\n') q++;
                if (q < end && *q == '\n') q++;
                payload_start = q;

                int payload_len = (int)(crc_line_ptr - payload_start);
                if (payload_len < 0) return -1;
                UINT32 got = llmk_oo_crc32(payload_start, payload_len);
                if (got != expected_crc) return -1;
            }
            break;
        }
        if (!(len == 5 && line[0] == 'B' && line[1] == 'E' && line[2] == 'G' && line[3] == 'I' && line[4] == 'N')) {
            // Skip unknown lines for forward compatibility
            continue;
        }

        UINT32 id = 0, energy = 0, ticks = 0, status = 0;
        UINT32 goal_len = 0, digest_len = 0, notes_len = 0;
        UINT32 agenda_count = 0;
        const char *agenda_items[LLMK_OO_AGENDA_MAX];
        UINT32 agenda_lens[LLMK_OO_AGENDA_MAX];
        UINT32 agenda_states[LLMK_OO_AGENDA_MAX];
        int agenda_prios[LLMK_OO_AGENDA_MAX];
        for (int z = 0; z < LLMK_OO_AGENDA_MAX; z++) {
            agenda_items[z] = NULL;
            agenda_lens[z] = 0;
            agenda_states[z] = 0;
            agenda_prios[z] = 0;
        }

        // id
        p = llmk_oo_read_line(p, end, &line, &len);
        if (!p || !llmk_oo_parse_kv_u32(line, len, "id", &id)) return -1;
        // energy
        p = llmk_oo_read_line(p, end, &line, &len);
        if (!p || !llmk_oo_parse_kv_u32(line, len, "energy", &energy)) return -1;
        // ticks
        p = llmk_oo_read_line(p, end, &line, &len);
        if (!p || !llmk_oo_parse_kv_u32(line, len, "ticks", &ticks)) return -1;
        // status
        p = llmk_oo_read_line(p, end, &line, &len);
        if (!p || !llmk_oo_parse_kv_u32(line, len, "status", &status)) return -1;

        // goal_len
        p = llmk_oo_read_line(p, end, &line, &len);
        if (!p || !llmk_oo_parse_kv_u32(line, len, "goal_len", &goal_len)) return -1;
        if (p + (int)goal_len > end) return -1;
        const char *goal = p;
        p += goal_len;
        if (p < end && *p == '\n') p++;

        // digest_len
        p = llmk_oo_read_line(p, end, &line, &len);
        if (!p || !llmk_oo_parse_kv_u32(line, len, "digest_len", &digest_len)) return -1;
        if (p + (int)digest_len > end) return -1;
        const char *digest = p;
        p += digest_len;
        if (p < end && *p == '\n') p++;

        // notes_len
        p = llmk_oo_read_line(p, end, &line, &len);
        if (!p || !llmk_oo_parse_kv_u32(line, len, "notes_len", &notes_len)) return -1;
        if (p + (int)notes_len > end) return -1;
        const char *notes = p;
        p += notes_len;
        if (p < end && *p == '\n') p++;

        int have_line = 0;

        // Optional agenda (OO2)
        if (version >= 2) {
            const char *l2 = NULL;
            int l2n = 0;
            const char *p2 = llmk_oo_read_line(p, end, &l2, &l2n);
            if (!p2 || !l2) return -1;

            if (llmk_oo_parse_kv_u32(l2, l2n, "agenda_count", &agenda_count)) {
                if (agenda_count > (UINT32)LLMK_OO_AGENDA_MAX) agenda_count = (UINT32)LLMK_OO_AGENDA_MAX;
                p = p2;
                for (UINT32 ai = 0; ai < agenda_count; ai++) {
                    if (version >= 4) {
                        // agenda_state
                        p = llmk_oo_read_line(p, end, &line, &len);
                        if (!p || !llmk_oo_parse_kv_u32(line, len, "agenda_state", &agenda_states[ai])) return -1;
                        // agenda_prio
                        p = llmk_oo_read_line(p, end, &line, &len);
                        if (!p || !llmk_oo_parse_kv_i32(line, len, "agenda_prio", &agenda_prios[ai])) return -1;
                    } else {
                        agenda_states[ai] = 0;
                        agenda_prios[ai] = 0;
                    }

                    // agenda_len
                    p = llmk_oo_read_line(p, end, &line, &len);
                    if (!p || !llmk_oo_parse_kv_u32(line, len, "agenda_len", &agenda_lens[ai])) return -1;
                    if (p + (int)agenda_lens[ai] > end) return -1;
                    agenda_items[ai] = p;
                    p += agenda_lens[ai];
                    if (p < end && *p == '\n') p++;
                }
            } else {
                // No agenda_count line; treat it as the next line (likely END).
                agenda_count = 0;
                line = l2;
                len = l2n;
                p = p2;
                have_line = 1;
            }
        }

        // END
        if (!have_line) {
            p = llmk_oo_read_line(p, end, &line, &len);
            if (!p || !line) return -1;
        }
        if (!(len == 3 && line[0] == 'E' && line[1] == 'N' && line[2] == 'D')) return -1;

        if (llmk_oo_import_entity(id, energy, ticks, status,
                                  goal, goal_len,
                                  digest, digest_len,
                                  notes, notes_len,
                                  agenda_count, agenda_items, agenda_lens,
                                  agenda_states, agenda_prios)) {
            imported++;
            if (id > max_id) max_id = id;
        }
    }

    if (max_id >= (UINT32)g_oo_next_id) g_oo_next_id = (int)(max_id + 1U);
    return imported;
}
