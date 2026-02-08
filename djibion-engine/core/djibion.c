
#include "djibion.h"

static uint32_t djb2_u32(const char *s) {
    // Deterministic tiny hash; freestanding safe.
    uint32_t h = 5381u;
    if (!s) return h;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h = ((h << 5) + h) ^ (uint32_t)(*p);
    }
    return h;
}

static int is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static char tolower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int streq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower_ascii(*a) != tolower_ascii(*b)) return 0;
        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}

static void ascii_copy_cap(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!src) return;
    int p = 0;
    while (*src && p + 1 < cap) dst[p++] = *src++;
    dst[p] = 0;
}

static void ascii_append_cap(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    int p = 0;
    while (dst[p] && p + 1 < cap) p++;
    if (!src) return;
    while (*src && p + 1 < cap) dst[p++] = *src++;
    dst[p] = 0;
}

static int ascii_starts_with_ci(const char *s, const char *prefix) {
    if (!prefix || prefix[0] == 0) return 1;
    if (!s) return 0;
    while (*prefix) {
        if (tolower_ascii(*s) != tolower_ascii(*prefix)) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int ascii_has_dotdot(const char *s) {
    if (!s) return 0;
    for (const char *p = s; p[0] && p[1]; p++) {
        if (p[0] == '.' && p[1] == '.') return 1;
    }
    return 0;
}

static const char *ascii_basename_ptr(const char *path) {
    if (!path) return "";
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return last;
}

static void normalize_prefix_slash(char *pfx, int cap) {
    if (!pfx || cap <= 0) return;
    int n = 0;
    while (pfx[n]) n++;
    if (n <= 0) return;
    char last = pfx[n - 1];
    if (last == '\\' || last == '/') return;
    if (n + 1 < cap) {
        pfx[n] = '\\';
        pfx[n + 1] = 0;
    }
}

static void build_prefixed_path(char *out, int cap, const char *prefix, const char *path) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!prefix || prefix[0] == 0) return;

    char pfx[64];
    ascii_copy_cap(pfx, (int)sizeof(pfx), prefix);
    normalize_prefix_slash(pfx, (int)sizeof(pfx));

    const char *base = ascii_basename_ptr(path ? path : "");
    // If basename is empty, keep prefix only.
    ascii_copy_cap(out, cap, pfx);
    if (!base || base[0] == 0) return;
    ascii_append_cap(out, cap, base);
}

void djibion_init(DjibionEngine *e) {
    if (!e) return;
    e->mode = DJIBION_MODE_OFF;

    e->laws.max_fs_write_bytes = 64u * 1024u;
    e->laws.max_snap_bytes = 256u * 1024u * 1024u;
    e->laws.max_oo_cycles = 16;
    e->laws.allow_fs_delete = 0;
    e->laws.allow_fs_write = 1;

    e->laws.allow_snap_load = 1;
    e->laws.allow_snap_save = 1;
    e->laws.allow_cfg_write = 1;

    e->laws.allow_autorun = 1;
    e->laws.allow_oo_exec = 1;
    e->laws.allow_oo_auto = 1;
    e->laws.allow_oo_persist = 1;
    e->laws.fs_mut_prefix[0] = 0;

    e->decisions_total = 0;
    e->decisions_rejected = 0;
    e->decisions_transformed = 0;
}

void djibion_set_mode(DjibionEngine *e, DjibionMode mode) {
    if (!e) return;
    e->mode = mode;
}

static int biocode_is_base(char c) {
    return (c == 'A' || c == 'T' || c == 'C' || c == 'G');
}

static DjibionIntentType map_codon_to_intent(const char codon[3]) {
    // Minimal, symbolic mapping (v0.1). Can evolve without breaking the API.
    // We intentionally keep this conservative.
    if (!codon) return DJIBION_INTENT_NONE;
    if (codon[0] == 'A' && codon[1] == 'T' && codon[2] == 'G') return DJIBION_INTENT_MEMORY_BIND;
    if (codon[0] == 'C' && codon[1] == 'G' && codon[2] == 'A') return DJIBION_INTENT_IO_WRITE;
    if (codon[0] == 'T' && codon[1] == 'A' && codon[2] == 'T') return DJIBION_INTENT_IO_DELETE;
    if (codon[0] == 'G' && codon[1] == 'A' && codon[2] == 'G') return DJIBION_INTENT_RESUME;
    if (codon[0] == 'A' && codon[1] == 'G' && codon[2] == 'A') return DJIBION_INTENT_PLAN;
    return DJIBION_INTENT_NONE;
}

DjibionStatus djibion_biocode_to_intent(const char *biocode, DjibionIntent *out_intent) {
    if (!out_intent) return DJIBION_ERR_INVALID;
    out_intent->type = DJIBION_INTENT_NONE;
    out_intent->ttl = 0;
    out_intent->scope = 0;
    out_intent->hash = djb2_u32(biocode);
    if (!biocode) return DJIBION_ERR_INVALID;

    // Extract first codon encountered.
    char codon[3] = {0, 0, 0};
    int n = 0;
    for (const char *p = biocode; *p; p++) {
        char c = *p;
        if (c == '-' || is_space(c)) continue;
        if (!biocode_is_base(c)) return DJIBION_ERR_INVALID;
        if (n < 3) codon[n++] = c;
        if (n == 3) break;
    }
    if (n != 3) return DJIBION_ERR_INVALID;

    out_intent->type = map_codon_to_intent(codon);
    // TTL heuristic: depends on 2nd codon if present.
    // For now, default medium.
    out_intent->ttl = 50;
    out_intent->scope = 0;
    return DJIBION_OK;
}

static void set_reason(DjibionDecision *d, const char *msg) {
    if (!d) return;
    ascii_copy_cap(d->reason, (int)sizeof(d->reason), msg ? msg : "");
}

static void init_decision(DjibionDecision *d) {
    if (!d) return;
    d->verdict = DJIBION_VERDICT_ALLOW;
    d->tri.sense.ok = 1;
    d->tri.sense.score = 100;
    d->tri.structure.ok = 1;
    d->tri.structure.score = 100;
    d->tri.reality.ok = 1;
    d->tri.reality.score = 100;
    d->risk = 0;
    d->reason[0] = 0;
    d->transformed_arg0[0] = 0;
}

static void tri_fail(DjibionDecision *d, int which, uint8_t score) {
    if (!d) return;
    if (which == 0) { d->tri.sense.ok = 0; d->tri.sense.score = score; }
    if (which == 1) { d->tri.structure.ok = 0; d->tri.structure.score = score; }
    if (which == 2) { d->tri.reality.ok = 0; d->tri.reality.score = score; }
}

DjibionStatus djibion_decide(DjibionEngine *e,
                             DjibionAction act,
                             const char *arg0,
                             uint32_t arg1,
                             DjibionDecision *out) {
    if (!e || !out) return DJIBION_ERR_INVALID;
    init_decision(out);
    e->decisions_total++;

    // Default: allow, but compute risk & reasons.
    // We keep this conservative to avoid breaking workflows; enforcement is opt-in.
    if (act == DJIBION_ACT_FS_WRITE || act == DJIBION_ACT_FS_APPEND) {
        out->risk = 35;
        if (ascii_has_dotdot(arg0 ? arg0 : "")) {
            out->risk = 80;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 5);
            set_reason(out, "path contains '..'");
        } else
        if (!e->laws.allow_fs_write) {
            out->risk = 70;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 10);
            set_reason(out, "fs write disabled by laws");
        } else if (e->laws.max_fs_write_bytes && arg1 > e->laws.max_fs_write_bytes) {
            out->risk = 60;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 15);
            set_reason(out, "fs write exceeds max bytes");
        } else if (!ascii_starts_with_ci(arg0 ? arg0 : "", e->laws.fs_mut_prefix)) {
            // Transform: enforce prefix by rewriting to prefix + basename.
            out->risk = 55;
            out->verdict = DJIBION_VERDICT_TRANSFORM;
            set_reason(out, "fs write outside allowed prefix");
            build_prefixed_path(out->transformed_arg0, (int)sizeof(out->transformed_arg0), e->laws.fs_mut_prefix, arg0);
        }
    } else if (act == DJIBION_ACT_FS_RM) {
        out->risk = 70;
        if (ascii_has_dotdot(arg0 ? arg0 : "")) {
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 5);
            set_reason(out, "path contains '..'");
        } else
        if (!e->laws.allow_fs_delete) {
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 5);
            set_reason(out, "fs delete disabled by laws");
        } else if (!ascii_starts_with_ci(arg0 ? arg0 : "", e->laws.fs_mut_prefix)) {
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 10);
            set_reason(out, "fs delete outside allowed prefix");
        }
    } else if (act == DJIBION_ACT_FS_CP || act == DJIBION_ACT_FS_MV) {
        out->risk = 45;
        // Treat destination as a write-capable path. For move, also implies delete of source.
        if (ascii_has_dotdot(arg0 ? arg0 : "")) {
            out->risk = 80;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 5);
            set_reason(out, "path contains '..'");
        } else if (!e->laws.allow_fs_write) {
            out->risk = 70;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 10);
            set_reason(out, "fs copy/move disabled by laws");
        } else if (act == DJIBION_ACT_FS_MV && !e->laws.allow_fs_delete) {
            out->risk = 75;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 5);
            set_reason(out, "fs move disabled (delete not allowed)");
        } else if (!ascii_starts_with_ci(arg0 ? arg0 : "", e->laws.fs_mut_prefix)) {
            out->risk = 55;
            out->verdict = DJIBION_VERDICT_TRANSFORM;
            set_reason(out, (act == DJIBION_ACT_FS_MV) ? "fs move outside allowed prefix" : "fs copy outside allowed prefix");
            build_prefixed_path(out->transformed_arg0, (int)sizeof(out->transformed_arg0), e->laws.fs_mut_prefix, arg0);
        }
    } else if (act == DJIBION_ACT_SNAP_LOAD) {
        out->risk = 25;
        // Snapshot load is read-mostly, but it can restore strong state; keep it safe.
        if (!e->laws.allow_snap_load) {
            out->risk = 65;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 10);
            set_reason(out, "snapshot load disabled by laws");
        } else if (ascii_has_dotdot(arg0 ? arg0 : "")) {
            out->risk = 80;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 5);
            set_reason(out, "path contains '..'");
        } else if (!ascii_starts_with_ci(arg0 ? arg0 : "", e->laws.fs_mut_prefix)) {
            out->risk = 50;
            out->verdict = DJIBION_VERDICT_TRANSFORM;
            set_reason(out, "snapshot load outside allowed prefix");
            build_prefixed_path(out->transformed_arg0, (int)sizeof(out->transformed_arg0), e->laws.fs_mut_prefix, arg0);
        }
    } else if (act == DJIBION_ACT_SNAP_SAVE) {
        out->risk = 40;
        if (!e->laws.allow_snap_save) {
            out->risk = 70;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 10);
            set_reason(out, "snapshot save disabled by laws");
        } else if (e->laws.max_snap_bytes && arg1 > e->laws.max_snap_bytes) {
            out->risk = 65;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 15);
            set_reason(out, "snapshot save exceeds max_snap_bytes");
        } else if (ascii_has_dotdot(arg0 ? arg0 : "")) {
            out->risk = 85;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 5);
            set_reason(out, "path contains '..'");
        } else if (!ascii_starts_with_ci(arg0 ? arg0 : "", e->laws.fs_mut_prefix)) {
            out->risk = 55;
            out->verdict = DJIBION_VERDICT_TRANSFORM;
            set_reason(out, "snapshot save outside allowed prefix");
            build_prefixed_path(out->transformed_arg0, (int)sizeof(out->transformed_arg0), e->laws.fs_mut_prefix, arg0);
        }
    } else if (act == DJIBION_ACT_OO_EXEC || act == DJIBION_ACT_OO_AUTO) {
        out->risk = 30;
        if ((act == DJIBION_ACT_OO_EXEC && !e->laws.allow_oo_exec) ||
            (act == DJIBION_ACT_OO_AUTO && !e->laws.allow_oo_auto)) {
            out->risk = 65;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 10);
            set_reason(out, "oo execution disabled by laws");
        } else if (e->laws.max_oo_cycles && arg1 > e->laws.max_oo_cycles) {
            out->risk = 55;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 2, 20);
            set_reason(out, "oo cycles exceed max_oo_cycles");
        }
    } else if (act == DJIBION_ACT_OO_SAVE || act == DJIBION_ACT_OO_LOAD) {
        out->risk = (act == DJIBION_ACT_OO_LOAD) ? 40 : 35;
        if (!e->laws.allow_oo_persist) {
            out->risk = 70;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 10);
            set_reason(out, "oo persist disabled by laws");
        } else if (ascii_has_dotdot(arg0 ? arg0 : "")) {
            out->risk = 85;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 5);
            set_reason(out, "path contains '..'");
        } else if (!ascii_starts_with_ci(arg0 ? arg0 : "", e->laws.fs_mut_prefix)) {
            out->risk = 55;
            out->verdict = DJIBION_VERDICT_TRANSFORM;
            set_reason(out, (act == DJIBION_ACT_OO_LOAD) ? "oo load outside allowed prefix" : "oo save outside allowed prefix");
            build_prefixed_path(out->transformed_arg0, (int)sizeof(out->transformed_arg0), e->laws.fs_mut_prefix, arg0);
        }
    } else if (act == DJIBION_ACT_AUTORUN) {
        out->risk = 35;
        if (!e->laws.allow_autorun) {
            out->risk = 65;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 10);
            set_reason(out, "autorun disabled by laws");
        } else if (ascii_has_dotdot(arg0 ? arg0 : "")) {
            out->risk = 80;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 1, 5);
            set_reason(out, "path contains '..'");
        } else if (!ascii_starts_with_ci(arg0 ? arg0 : "", e->laws.fs_mut_prefix)) {
            out->risk = 50;
            out->verdict = DJIBION_VERDICT_TRANSFORM;
            set_reason(out, "autorun file outside allowed prefix");
            build_prefixed_path(out->transformed_arg0, (int)sizeof(out->transformed_arg0), e->laws.fs_mut_prefix, arg0);
        }
    } else if (act == DJIBION_ACT_CFG_WRITE) {
        out->risk = 40;
        if (!e->laws.allow_cfg_write) {
            out->risk = 75;
            out->verdict = DJIBION_VERDICT_REJECT;
            tri_fail(out, 0, 10);
            set_reason(out, "config write disabled by laws");
        }
    } else {
        out->risk = 5;
    }

    if (out->verdict == DJIBION_VERDICT_REJECT) e->decisions_rejected++;
    if (out->verdict == DJIBION_VERDICT_TRANSFORM) e->decisions_transformed++;

    return DJIBION_OK;
}
