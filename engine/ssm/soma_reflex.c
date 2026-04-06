// soma_reflex.c — SomaMind Phase F: Symbolic Reflex Engine
// Freestanding C11, no libc, no malloc.

#include "soma_reflex.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ── Freestanding string helpers ────────────────────────────────────────────

static int _rf_isalpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static int _rf_isupper(char c) { return c >= 'A' && c <= 'Z'; }
static int _rf_islower(char c) { return c >= 'a' && c <= 'z'; }
static int _rf_isdigit(char c) { return c >= '0' && c <= '9'; }
static int _rf_isspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static char _rf_toupper(char c) { return (_rf_islower(c)) ? (char)(c - 32) : c; }

static int _rf_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static void _rf_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static int _rf_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
static int _rf_streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

// Append to fixed buffer
static int _rf_append(char *buf, int *pos, int max, const char *s) {
    while (*s && *pos < max - 1) { buf[(*pos)++] = *s++; }
    buf[*pos] = 0;
    return *pos;
}

// Float to string (simplified, no libc)
static void _rf_ftoa(float v, char *buf, int maxn) {
    if (maxn < 2) { buf[0] = 0; return; }
    int neg = (v < 0.0f);
    if (neg) v = -v;
    // Detect integer
    long long iv = (long long)(v + 0.5f);
    float diff = v - (float)iv;
    if (diff < 0.0f) diff = -diff;

    int pos = 0;
    if (neg && pos < maxn - 1) buf[pos++] = '-';

    if (diff < 0.005f) {
        // Integer representation
        if (iv == 0) { buf[pos++] = '0'; buf[pos] = 0; return; }
        char tmp[24]; int tn = 0;
        long long t = iv;
        while (t > 0 && tn < 20) { tmp[tn++] = '0' + (int)(t % 10); t /= 10; }
        for (int i = tn - 1; i >= 0 && pos < maxn - 1; i--) buf[pos++] = tmp[i];
        buf[pos] = 0;
    } else {
        // 2 decimal places
        long long int_part = (long long)v;
        int frac = (int)((v - (float)int_part) * 100.0f + 0.5f);
        if (frac >= 100) { int_part++; frac -= 100; }
        char tmp[24]; int tn = 0;
        long long t = int_part;
        if (t == 0) tmp[tn++] = '0';
        while (t > 0 && tn < 20) { tmp[tn++] = '0' + (int)(t % 10); t /= 10; }
        for (int i = tn - 1; i >= 0 && pos < maxn - 1; i--) buf[pos++] = tmp[i];
        if (pos < maxn - 1) buf[pos++] = '.';
        if (pos < maxn - 1) buf[pos++] = '0' + frac / 10;
        if (pos < maxn - 1) buf[pos++] = '0' + frac % 10;
        buf[pos] = 0;
    }
}

// ── Recursive descent expression evaluator ────────────────────────────────
// Grammar:
//   expr    = term   { ('+' | '-') term }
//   term    = factor { ('*' | '/' | '%') factor }
//   factor  = base   ['^' factor]       (right-assoc power)
//   base    = '(' expr ')' | '-' base | number | variable

typedef struct {
    const char         *s;      // Input string
    int                 pos;    // Current position
    const SomaReflexVar *vars;  // Variable table (for lookups)
    int                  nv;   // Number of variables
    int                  err;   // Non-zero on error
} ExprCtx;

static float _rf_parse_expr(ExprCtx *ctx);
static float _rf_parse_term(ExprCtx *ctx);
static float _rf_parse_factor(ExprCtx *ctx);
static float _rf_parse_base(ExprCtx *ctx);

static void _rf_skip_ws(ExprCtx *ctx) {
    while (_rf_isspace(ctx->s[ctx->pos])) ctx->pos++;
}

static float _rf_parse_number(ExprCtx *ctx) {
    _rf_skip_ws(ctx);
    float val = 0.0f;
    float frac = 0.0f;
    int has_frac = 0;
    float frac_div = 1.0f;
    while (_rf_isdigit(ctx->s[ctx->pos])) {
        val = val * 10.0f + (float)(ctx->s[ctx->pos] - '0');
        ctx->pos++;
    }
    if (ctx->s[ctx->pos] == '.') {
        ctx->pos++;
        has_frac = 1;
        while (_rf_isdigit(ctx->s[ctx->pos])) {
            frac = frac * 10.0f + (float)(ctx->s[ctx->pos] - '0');
            frac_div *= 10.0f;
            ctx->pos++;
        }
        (void)has_frac;
        val += frac / frac_div;
    }
    return val;
}

static float _rf_parse_base(ExprCtx *ctx) {
    _rf_skip_ws(ctx);
    char c = ctx->s[ctx->pos];

    // Parenthesis
    if (c == '(') {
        ctx->pos++;
        float v = _rf_parse_expr(ctx);
        _rf_skip_ws(ctx);
        if (ctx->s[ctx->pos] == ')') ctx->pos++;
        else ctx->err = 1;
        return v;
    }

    // Unary minus
    if (c == '-') {
        ctx->pos++;
        return -_rf_parse_base(ctx);
    }

    // Number
    if (_rf_isdigit(c) || c == '.') {
        return _rf_parse_number(ctx);
    }

    // Variable (single uppercase letter or short name)
    if (_rf_isalpha(c)) {
        char vname[SOMA_REFLEX_VAR_NAME_LEN];
        int vn = 0;
        while (_rf_isalpha(ctx->s[ctx->pos]) && vn < SOMA_REFLEX_VAR_NAME_LEN - 1) {
            vname[vn++] = _rf_toupper(ctx->s[ctx->pos]);
            ctx->pos++;
        }
        vname[vn] = 0;
        // Lookup
        for (int i = 0; i < ctx->nv; i++) {
            if (_rf_streq(ctx->vars[i].name, vname) && ctx->vars[i].resolved)
                return ctx->vars[i].value;
        }
        ctx->err = 1;  // Unknown variable
        return 0.0f;
    }

    ctx->err = 1;
    return 0.0f;
}

static float _rf_ipow(float base, int exp) {
    if (exp < 0) return 0.0f;
    float r = 1.0f;
    for (int i = 0; i < exp; i++) r *= base;
    return r;
}

static float _rf_parse_factor(ExprCtx *ctx) {
    float v = _rf_parse_base(ctx);
    _rf_skip_ws(ctx);
    if (ctx->s[ctx->pos] == '^') {
        ctx->pos++;
        float exp = _rf_parse_factor(ctx);  // right-assoc
        int iexp = (int)(exp + 0.5f);
        v = _rf_ipow(v, iexp);
    }
    return v;
}

static float _rf_parse_term(ExprCtx *ctx) {
    float v = _rf_parse_factor(ctx);
    _rf_skip_ws(ctx);
    while (ctx->s[ctx->pos] == '*' || ctx->s[ctx->pos] == '/' || ctx->s[ctx->pos] == '%') {
        char op = ctx->s[ctx->pos++];
        float r = _rf_parse_factor(ctx);
        if      (op == '*') v *= r;
        else if (op == '/') { if (r != 0.0f) v /= r; else ctx->err = 1; }
        else if (op == '%') {
            int iv = (int)v, ir = (int)r;
            v = (ir != 0) ? (float)(iv % ir) : 0.0f;
        }
        _rf_skip_ws(ctx);
    }
    return v;
}

static float _rf_parse_expr(ExprCtx *ctx) {
    float v = _rf_parse_term(ctx);
    _rf_skip_ws(ctx);
    while (ctx->s[ctx->pos] == '+' || ctx->s[ctx->pos] == '-') {
        char op = ctx->s[ctx->pos++];
        float r = _rf_parse_term(ctx);
        v = (op == '+') ? v + r : v - r;
        _rf_skip_ws(ctx);
    }
    return v;
}

// Public single-expression evaluator
int soma_reflex_eval(const char *expr, float *out) {
    ExprCtx ctx;
    ctx.s   = expr;
    ctx.pos = 0;
    ctx.vars = NULL;
    ctx.nv   = 0;
    ctx.err  = 0;
    float v = _rf_parse_expr(&ctx);
    if (ctx.err || ctx.s[ctx.pos] != '\0') return 0;
    if (out) *out = v;
    return 1;
}

// Evaluator with variable table
static float _rf_eval_with_vars(const char *expr,
                                 const SomaReflexVar *vars, int nv,
                                 int *ok) {
    ExprCtx ctx;
    ctx.s   = expr;
    ctx.pos = 0;
    ctx.vars = vars;
    ctx.nv   = nv;
    ctx.err  = 0;
    float v = _rf_parse_expr(&ctx);
    if (ok) *ok = (!ctx.err);
    return v;
}

// ── Pattern scanner ───────────────────────────────────────────────────────

// Try to extract VAR=expr patterns from a prompt segment.
// Returns number of vars found. vars[] must have capacity SOMA_REFLEX_MAX_VARS.
static int _rf_scan_assignments(const char *prompt,
                                 SomaReflexVar *vars) {
    int n = 0;
    int len = _rf_strlen(prompt);
    int i = 0;
    while (i < len && n < SOMA_REFLEX_MAX_VARS) {
        // Skip whitespace and punctuation
        while (i < len && !_rf_isalpha(prompt[i])) i++;
        if (i >= len) break;

        // Try to read a short variable name (1-4 chars, followed by '=')
        if (_rf_isupper(prompt[i]) || _rf_islower(prompt[i])) {
            int start = i;
            char vname[SOMA_REFLEX_VAR_NAME_LEN];
            int vn = 0;
            while (i < len && _rf_isalpha(prompt[i]) &&
                   vn < SOMA_REFLEX_VAR_NAME_LEN - 1) {
                vname[vn++] = _rf_toupper(prompt[i]);
                i++;
            }
            vname[vn] = 0;

            // Skip whitespace
            while (i < len && _rf_isspace(prompt[i])) i++;

            // Check for '=' (not '==' which would be comparison)
            if (i < len && prompt[i] == '=' && prompt[i + 1] != '=') {
                i++;  // skip '='
                while (i < len && _rf_isspace(prompt[i])) i++;

                // Read expression until space/comma/semicolon/newline/end
                char expr[SOMA_REFLEX_EXPR_LEN];
                int en = 0;
                int depth = 0;
                while (i < len && en < SOMA_REFLEX_EXPR_LEN - 1) {
                    char c = prompt[i];
                    if (c == '(') depth++;
                    if (c == ')') {
                        if (depth == 0) break;
                        depth--;
                    }
                    if (depth == 0 && (c == ',' || c == ';' || c == '\n' ||
                        c == ' ' || c == '\t'))
                        break;
                    expr[en++] = c;
                    i++;
                }
                expr[en] = 0;

                if (en > 0) {
                    // Check for duplicate var name
                    int dup = 0;
                    for (int d = 0; d < n; d++)
                        if (_rf_streq(vars[d].name, vname)) { dup = 1; break; }
                    if (!dup) {
                        _rf_strcpy(vars[n].name, vname, SOMA_REFLEX_VAR_NAME_LEN);
                        _rf_strcpy(vars[n].expr, expr, SOMA_REFLEX_EXPR_LEN);
                        vars[n].resolved = 0;
                        vars[n].value    = 0.0f;
                        vars[n].dep_mask = 0;
                        n++;
                    }
                }
            } else {
                i = start + 1;  // No '=' found — advance past this char
            }
        } else {
            i++;
        }
    }
    return n;
}

// Resolve dependencies: compute which vars each expr depends on
static void _rf_compute_deps(SomaReflexVar *vars, int n) {
    for (int i = 0; i < n; i++) {
        vars[i].dep_mask = 0;
        const char *e = vars[i].expr;
        while (*e) {
            if (_rf_isupper(*e) || _rf_islower(*e)) {
                char vname[SOMA_REFLEX_VAR_NAME_LEN];
                int vn = 0;
                while ((_rf_isupper(*e) || _rf_islower(*e)) && vn < SOMA_REFLEX_VAR_NAME_LEN - 1) {
                    vname[vn++] = _rf_toupper(*e);
                    e++;
                }
                vname[vn] = 0;
                for (int j = 0; j < n; j++) {
                    if (j != i && _rf_streq(vars[j].name, vname)) {
                        vars[i].dep_mask |= (1 << j);
                    }
                }
            } else {
                e++;
            }
        }
    }
}

// Topological resolution (up to n passes to handle chains)
static int _rf_resolve_all(SomaReflexVar *vars, int n) {
    int resolved_total = 0;
    for (int pass = 0; pass < n; pass++) {
        int progress = 0;
        for (int i = 0; i < n; i++) {
            if (vars[i].resolved) continue;
            // Check if all deps are resolved
            int deps_ok = 1;
            for (int j = 0; j < n; j++) {
                if ((vars[i].dep_mask & (1 << j)) && !vars[j].resolved) {
                    deps_ok = 0; break;
                }
            }
            if (deps_ok) {
                int ok = 0;
                float v = _rf_eval_with_vars(vars[i].expr, vars, n, &ok);
                if (ok) {
                    vars[i].value    = v;
                    vars[i].resolved = 1;
                    resolved_total++;
                    progress = 1;
                }
            }
        }
        if (!progress) break;
    }
    return resolved_total;
}

// Detect direct math question:
// e.g. "combien font 25+10" / "what is 2^8" / "calculate 100/4"
static int _rf_scan_direct_question(const char *prompt, char *expr_out, float *val_out) {
    // Trigger keywords
    static const char * const triggers[] = {
        "combien font ", "combien fait ", "combien vaut ",
        "what is ", "calculate ", "compute ", "eval ",
        "= ?", "=?",
        NULL
    };
    const char *found = NULL;
    int ti = 0;
    while (triggers[ti]) {
        int tlen = _rf_strlen(triggers[ti]);
        int plen = _rf_strlen(prompt);
        for (int i = 0; i + tlen <= plen; i++) {
            if (_rf_strncmp(prompt + i, triggers[ti], tlen) == 0) {
                found = prompt + i + tlen;
                break;
            }
        }
        if (found) break;
        ti++;
    }
    if (!found) return 0;

    // Skip spaces
    while (*found == ' ') found++;
    if (!*found) return 0;

    // Extract expression (until end or '?' or '"')
    char expr[SOMA_REFLEX_EXPR_LEN];
    int en = 0;
    while (*found && *found != '?' && *found != '"' && *found != '\n'
           && en < SOMA_REFLEX_EXPR_LEN - 1) {
        expr[en++] = *found++;
    }
    // Trim trailing spaces
    while (en > 0 && _rf_isspace(expr[en - 1])) en--;
    expr[en] = 0;
    if (en == 0) return 0;

    float v = 0.0f;
    if (!soma_reflex_eval(expr, &v)) return 0;

    _rf_strcpy(expr_out, expr, SOMA_REFLEX_EXPR_LEN);
    *val_out = v;
    return 1;
}

// ── Public API ────────────────────────────────────────────────────────────

void soma_reflex_init(SomaReflexCtx *ctx) {
    if (!ctx) return;
    ctx->enabled       = 1;
    ctx->total_scans   = 0;
    ctx->total_triggers = 0;
    ctx->total_resolved = 0;
    ctx->last_inject_len = 0.0f;
}

SomaReflexResult soma_reflex_scan(SomaReflexCtx *ctx, const char *prompt) {
    SomaReflexResult res;
    // Zero-initialize
    res.var_count        = 0;
    res.has_direct_result = 0;
    res.direct_result    = 0.0f;
    res.direct_expr[0]   = 0;
    res.injection[0]     = 0;
    res.injection_len    = 0;
    res.vars_resolved    = 0;
    res.vars_failed      = 0;
    res.triggered        = 0;
    for (int i = 0; i < SOMA_REFLEX_MAX_VARS; i++) {
        res.vars[i].name[0]     = 0;
        res.vars[i].expr[0]     = 0;
        res.vars[i].value       = 0.0f;
        res.vars[i].resolved    = 0;
        res.vars[i].dep_mask    = 0;
    }

    if (!ctx || !prompt) return res;
    ctx->total_scans++;

    // ── 1. Scan for VAR= assignments ──────────────────────────────────
    res.var_count = _rf_scan_assignments(prompt, res.vars);

    if (res.var_count > 0) {
        _rf_compute_deps(res.vars, res.var_count);
        res.vars_resolved = _rf_resolve_all(res.vars, res.var_count);
        res.vars_failed   = res.var_count - res.vars_resolved;
    }

    // ── 2. Scan for direct question ───────────────────────────────────
    {
        char dexpr[SOMA_REFLEX_EXPR_LEN];
        float dval = 0.0f;
        if (_rf_scan_direct_question(prompt, dexpr, &dval)) {
            res.has_direct_result = 1;
            res.direct_result     = dval;
            _rf_strcpy(res.direct_expr, dexpr, SOMA_REFLEX_EXPR_LEN);
        }
    }

    // ── 3. Build injection string ─────────────────────────────────────
    if (res.vars_resolved > 0 || res.has_direct_result) {
        res.triggered = 1;
        int pos = 0;
        _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, "[MATH:");

        // Resolved variables
        char vbuf[24];
        for (int i = 0; i < res.var_count; i++) {
            if (!res.vars[i].resolved) continue;
            _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, " ");
            _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, res.vars[i].name);
            _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, "=");
            _rf_ftoa(res.vars[i].value, vbuf, sizeof(vbuf));
            _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, vbuf);
        }

        // Direct result
        if (res.has_direct_result) {
            _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, " ");
            _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, res.direct_expr);
            _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, "=");
            _rf_ftoa(res.direct_result, vbuf, sizeof(vbuf));
            _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, vbuf);
        }

        _rf_append(res.injection, &pos, SOMA_REFLEX_INJECT_MAX, "]\n");
        res.injection_len = pos;

        ctx->total_triggers++;
        ctx->total_resolved += res.vars_resolved;
        ctx->last_inject_len = (float)pos;
    }

    return res;
}
