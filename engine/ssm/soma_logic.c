// soma_logic.c — SomaMind Phase G: Logical Syllogism Reflex
// Freestanding C11, no libc, no malloc.

#include "soma_logic.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// ── Freestanding string helpers ────────────────────────────────────────────

static int _lg_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int _lg_isspace(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }
static int _lg_isalpha(char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z');
}
static char _lg_tolower(char c) {
    return (c>='A'&&c<='Z') ? (char)(c+32) : c;
}
static void _lg_lower_copy(char *dst, const char *src, int max) {
    int i=0;
    while(i<max-1 && src[i]) { dst[i]=_lg_tolower(src[i]); i++; }
    dst[i]=0;
}
static void _lg_strcpy(char *dst, const char *src, int max) {
    int i=0;
    while(i<max-1 && src[i]) { dst[i]=src[i]; i++; }
    dst[i]=0;
}
static int _lg_strncmp_ci(const char *a, const char *b, int n) {
    for(int i=0;i<n;i++) {
        char ca=_lg_tolower(a[i]), cb=_lg_tolower(b[i]);
        if(ca!=cb) return (unsigned char)ca-(unsigned char)cb;
        if(!ca) return 0;
    }
    return 0;
}
static int _lg_streq_ci(const char *a, const char *b) {
    while(*a&&*b) {
        if(_lg_tolower(*a)!=_lg_tolower(*b)) return 0;
        a++; b++;
    }
    return _lg_tolower(*a)==_lg_tolower(*b);
}
// Case-insensitive substring search. Returns pointer or NULL.
static const char *_lg_strstr_ci(const char *hay, const char *needle) {
    int nl = _lg_strlen(needle);
    int hl = _lg_strlen(hay);
    for(int i=0; i<=hl-nl; i++)
        if(_lg_strncmp_ci(hay+i, needle, nl)==0) return hay+i;
    return NULL;
}
static int _lg_append(char *buf, int *pos, int max, const char *s) {
    while(*s && *pos<max-1) buf[(*pos)++]=*s++;
    buf[*pos]=0;
    return *pos;
}
// Trim leading/trailing whitespace into dst
static void _lg_trim(char *dst, const char *src, int max) {
    while(_lg_isspace(*src)) src++;
    int len=_lg_strlen(src);
    while(len>0 && _lg_isspace(src[len-1])) len--;
    if(len>=max) len=max-1;
    for(int i=0;i<len;i++) dst[i]=src[i];
    dst[len]=0;
}

// ── Fact table helpers ─────────────────────────────────────────────────────

// Find fact by text (case-insensitive). Returns index or -1.
static int _lg_find_fact(const SomaLogicResult *r, const char *text) {
    for(int i=0;i<r->fact_count;i++)
        if(_lg_streq_ci(r->facts[i].text, text)) return i;
    return -1;
}

// Add or update a fact. Returns index.
static int _lg_set_fact(SomaLogicResult *r, const char *text,
                        SomaFactStatus status, int derived) {
    int idx = _lg_find_fact(r, text);
    if(idx < 0) {
        if(r->fact_count >= SOMA_LOGIC_MAX_FACTS) return -1;
        idx = r->fact_count++;
        _lg_strcpy(r->facts[idx].text, text, SOMA_LOGIC_FACT_LEN);
        r->facts[idx].derived = derived;
    }
    r->facts[idx].status  = status;
    return idx;
}

// ── Sentence splitter ─────────────────────────────────────────────────────
// Splits prompt on '.', '!', '\n', ';' into segments (max 32).
#define MAX_SEGMENTS 32
static int _lg_split_sentences(const char *prompt,
                                char segs[][128], int max_segs) {
    int n=0, slen=_lg_strlen(prompt), pos=0;
    while(pos<slen && n<max_segs) {
        while(pos<slen && _lg_isspace(prompt[pos])) pos++;
        if(pos>=slen) break;
        int start=pos, len=0;
        while(pos<slen && prompt[pos]!='.' && prompt[pos]!='!'
              && prompt[pos]!='\n' && prompt[pos]!=';') {
            if(len<127) segs[n][len++]=prompt[pos];
            pos++;
        }
        segs[n][len]=0;
        if(pos<slen) pos++; // skip delimiter
        if(len>2) n++;
    }
    return n;
}

// ── Pattern matchers ──────────────────────────────────────────────────────

// Try to match "Si/If [antecedent] alors/then [consequent]"
// Fills rule. Returns 1 on success.
static int _lg_match_if_then(const char *seg,
                              char *ante_out, char *cons_out) {
    // French: "Si X alors Y"
    // English: "If X then Y"
    static const char * const if_kw[]   = {"si ", "if "};
    static const char * const then_kw[] = {"alors ", "then "};

    for(int i=0;i<2;i++) {
        const char *p = _lg_strstr_ci(seg, if_kw[i]);
        if(!p) continue;
        p += _lg_strlen(if_kw[i]);
        while(_lg_isspace(*p)) p++;

        const char *q = _lg_strstr_ci(p, then_kw[i]);
        if(!q) continue;

        // Antecedent: from p to q
        int alen = (int)(q - p);
        while(alen>0 && _lg_isspace(p[alen-1])) alen--;
        if(alen<=0 || alen>=SOMA_LOGIC_FACT_LEN) continue;
        for(int k=0;k<alen;k++) ante_out[k]=p[k];
        ante_out[alen]=0;

        // Consequent: from q+len(then_kw)
        q += _lg_strlen(then_kw[i]);
        while(_lg_isspace(*q)) q++;
        _lg_strcpy(cons_out, q, SOMA_LOGIC_FACT_LEN);
        // trim
        int clen=_lg_strlen(cons_out);
        while(clen>0 && _lg_isspace(cons_out[clen-1])) clen--;
        cons_out[clen]=0;

        if(_lg_strlen(ante_out)>2 && _lg_strlen(cons_out)>2) return 1;
    }
    return 0;
}

// Try to match "[fact] est vrai/est faux/is true/is false"
// Returns 1 on success, fills fact_out and status_out.
static int _lg_match_fact_status(const char *seg,
                                  char *fact_out, SomaFactStatus *st_out) {
    static const char * const true_kw[]  = {
        " est vrai", " is true", " est correct", " is correct",
        " est réel", " is real"
    };
    static const char * const false_kw[] = {
        " est faux", " is false", " est incorrect", " is incorrect",
        " n'est pas vrai", " is not true"
    };
    // TRUE patterns
    for(int i=0;i<6;i++) {
        const char *p = _lg_strstr_ci(seg, true_kw[i]);
        if(p) {
            int flen = (int)(p - seg);
            // skip leading spaces
            const char *fs = seg;
            while(_lg_isspace(*fs)) fs++; flen -= (int)(fs - seg);
            if(flen<=0 || flen>=SOMA_LOGIC_FACT_LEN) continue;
            for(int k=0;k<flen;k++) fact_out[k]=fs[k];
            fact_out[flen]=0;
            // trim
            int l=_lg_strlen(fact_out);
            while(l>0&&_lg_isspace(fact_out[l-1])) l--;
            fact_out[l]=0;
            if(l>1) { *st_out=SOMA_FACT_TRUE; return 1; }
        }
    }
    // FALSE patterns
    for(int i=0;i<6;i++) {
        const char *p = _lg_strstr_ci(seg, false_kw[i]);
        if(p) {
            int flen = (int)(p - seg);
            const char *fs = seg;
            while(_lg_isspace(*fs)) fs++; flen -= (int)(fs - seg);
            if(flen<=0 || flen>=SOMA_LOGIC_FACT_LEN) continue;
            for(int k=0;k<flen;k++) fact_out[k]=fs[k];
            fact_out[flen]=0;
            int l=_lg_strlen(fact_out);
            while(l>0&&_lg_isspace(fact_out[l-1])) l--;
            fact_out[l]=0;
            if(l>1) { *st_out=SOMA_FACT_FALSE; return 1; }
        }
    }
    return 0;
}

// Try to match "Tous les X sont Y" / "All X are Y"
// Fills category_out, property_out. Returns 1 on match.
static int _lg_match_all_are(const char *seg,
                              char *cat_out, char *prop_out) {
    static const char * const all_kw[]  = {"tous les ", "all "};
    static const char * const are_kw[]  = {" sont ", " are "};
    for(int i=0;i<2;i++) {
        const char *p = _lg_strstr_ci(seg, all_kw[i]);
        if(!p) continue;
        p += _lg_strlen(all_kw[i]);
        while(_lg_isspace(*p)) p++;
        const char *q = _lg_strstr_ci(p, are_kw[i]);
        if(!q) continue;
        int clen=(int)(q-p);
        while(clen>0&&_lg_isspace(p[clen-1])) clen--;
        if(clen<=0||clen>=SOMA_LOGIC_FACT_LEN) continue;
        for(int k=0;k<clen;k++) cat_out[k]=_lg_tolower(p[k]);
        cat_out[clen]=0;
        q += _lg_strlen(are_kw[i]);
        while(_lg_isspace(*q)) q++;
        _lg_strcpy(prop_out, q, SOMA_LOGIC_FACT_LEN);
        int pl=_lg_strlen(prop_out);
        while(pl>0&&_lg_isspace(prop_out[pl-1])) pl--;
        prop_out[pl]=0;
        if(_lg_strlen(cat_out)>1&&_lg_strlen(prop_out)>1) return 1;
    }
    return 0;
}

// Try to match "Z est un/une X" / "Z is a/an X"
// Fills instance_out, category_out. Returns 1 on match.
static int _lg_match_is_a(const char *seg,
                           char *inst_out, char *cat_out) {
    static const char * const is_kw[] = {
        " est un ", " est une ", " is a ", " is an "
    };
    for(int i=0;i<4;i++) {
        const char *p = _lg_strstr_ci(seg, is_kw[i]);
        if(!p) continue;
        // Instance: before the keyword
        const char *fs = seg;
        while(_lg_isspace(*fs)) fs++;
        int ilen=(int)(p-fs);
        while(ilen>0&&_lg_isspace(fs[ilen-1])) ilen--;
        if(ilen<=0||ilen>=SOMA_LOGIC_FACT_LEN) continue;
        for(int k=0;k<ilen;k++) inst_out[k]=_lg_tolower(fs[k]);
        inst_out[ilen]=0;
        // Category: after the keyword
        const char *q = p+_lg_strlen(is_kw[i]);
        while(_lg_isspace(*q)) q++;
        _lg_strcpy(cat_out, q, SOMA_LOGIC_FACT_LEN);
        // lowercase
        for(int k=0;cat_out[k];k++) cat_out[k]=_lg_tolower(cat_out[k]);
        int cl=_lg_strlen(cat_out);
        while(cl>0&&_lg_isspace(cat_out[cl-1])) cl--;
        cat_out[cl]=0;
        if(_lg_strlen(inst_out)>0&&_lg_strlen(cat_out)>0) return 1;
    }
    return 0;
}

// Add derived conclusion (deduplicate)
static void _lg_add_derived(SomaLogicResult *r, const char *text) {
    for(int i=0;i<r->derived_count;i++)
        if(_lg_streq_ci(r->derived[i], text)) return;
    if(r->derived_count < SOMA_LOGIC_MAX_DERIVED)
        _lg_strcpy(r->derived[r->derived_count++], text, SOMA_LOGIC_FACT_LEN);
}

// ── Forward chaining (modus ponens / tollens) ─────────────────────────────
static void _lg_forward_chain(SomaLogicResult *r) {
    int changed = 1;
    int passes  = 0;
    while(changed && passes < SOMA_LOGIC_MAX_RULES) {
        changed = 0; passes++;
        for(int ri=0;ri<r->rule_count;ri++) {
            if(r->rules[ri].used) continue;
            SomaLogicRule *rule = &r->rules[ri];

            // Modus Ponens: if ante is TRUE → cons becomes TRUE
            int ai = _lg_find_fact(r, rule->antecedent);
            if(ai>=0 && r->facts[ai].status == SOMA_FACT_TRUE) {
                int ci = _lg_find_fact(r, rule->consequent);
                if(ci < 0 || r->facts[ci].status != SOMA_FACT_TRUE) {
                    // Derive consequent
                    char derived_text[SOMA_LOGIC_FACT_LEN+16];
                    int dp=0;
                    _lg_append(derived_text, &dp, sizeof(derived_text),
                               rule->consequent);
                    _lg_append(derived_text, &dp, sizeof(derived_text),
                               " est vrai");
                    _lg_set_fact(r, rule->consequent, SOMA_FACT_TRUE, 1);
                    _lg_add_derived(r, derived_text);
                    rule->used = 1;
                    changed = 1;
                }
            }

            // Modus Tollens: if cons is FALSE → ante becomes FALSE
            int ci2 = _lg_find_fact(r, rule->consequent);
            if(ci2>=0 && r->facts[ci2].status == SOMA_FACT_FALSE) {
                int ai2 = _lg_find_fact(r, rule->antecedent);
                if(ai2 < 0 || r->facts[ai2].status != SOMA_FACT_FALSE) {
                    char derived_text[SOMA_LOGIC_FACT_LEN+16];
                    int dp=0;
                    _lg_append(derived_text, &dp, sizeof(derived_text),
                               rule->antecedent);
                    _lg_append(derived_text, &dp, sizeof(derived_text),
                               " est faux");
                    _lg_set_fact(r, rule->antecedent, SOMA_FACT_FALSE, 1);
                    _lg_add_derived(r, derived_text);
                    rule->used = 1;
                    changed = 1;
                }
            }
        }
    }
}

// ── Barbara syllogism ─────────────────────────────────────────────────────
// Store "Tous les CAT sont PROP" pairs, then for each "Z est un CAT"
// derive "Z est PROP"
typedef struct { char cat[SOMA_LOGIC_FACT_LEN]; char prop[SOMA_LOGIC_FACT_LEN]; } AllArePair;
typedef struct { char inst[SOMA_LOGIC_FACT_LEN]; char cat[SOMA_LOGIC_FACT_LEN];  } IsAPair;

static void _lg_barbara(SomaLogicResult *r,
                         AllArePair *all_pairs, int np,
                         IsAPair   *is_pairs,  int ni) {
    for(int i=0;i<np;i++) {
        for(int j=0;j<ni;j++) {
            if(_lg_streq_ci(all_pairs[i].cat, is_pairs[j].cat)) {
                // Derive: inst est PROP
                char derived[SOMA_LOGIC_FACT_LEN*2+8];
                int dp=0;
                _lg_append(derived, &dp, sizeof(derived), is_pairs[j].inst);
                _lg_append(derived, &dp, sizeof(derived), " est ");
                _lg_append(derived, &dp, sizeof(derived), all_pairs[i].prop);
                if(r->barbara_count < SOMA_LOGIC_MAX_DERIVED) {
                    _lg_strcpy(r->barbara_derived[r->barbara_count++],
                               derived, SOMA_LOGIC_FACT_LEN);
                }
            }
        }
    }
}

// ── Contradiction check ───────────────────────────────────────────────────
static void _lg_check_contradictions(SomaLogicResult *r) {
    for(int i=0;i<r->fact_count;i++) {
        for(int j=i+1;j<r->fact_count;j++) {
            if(_lg_streq_ci(r->facts[i].text, r->facts[j].text)) {
                if(r->facts[i].status != SOMA_FACT_UNKNOWN &&
                   r->facts[j].status != SOMA_FACT_UNKNOWN &&
                   r->facts[i].status != r->facts[j].status) {
                    r->contradiction = 1;
                    _lg_strcpy(r->contradiction_fact,
                               r->facts[i].text, SOMA_LOGIC_FACT_LEN);
                    return;
                }
            }
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────

void soma_logic_init(SomaLogicCtx *ctx) {
    if(!ctx) return;
    ctx->enabled             = 1;
    ctx->total_scans         = 0;
    ctx->total_triggers      = 0;
    ctx->total_derived       = 0;
    ctx->total_contradictions = 0;
}

SomaLogicResult soma_logic_scan(SomaLogicCtx *ctx, const char *prompt) {
    SomaLogicResult r;
    // Zero-init
    r.fact_count        = 0;
    r.rule_count        = 0;
    r.derived_count     = 0;
    r.barbara_count     = 0;
    r.contradiction     = 0;
    r.contradiction_fact[0] = 0;
    r.injection[0]      = 0;
    r.injection_len     = 0;
    r.triggered         = 0;
    for(int i=0;i<SOMA_LOGIC_MAX_FACTS;i++) {
        r.facts[i].text[0]=0; r.facts[i].status=SOMA_FACT_UNKNOWN; r.facts[i].derived=0;
    }
    for(int i=0;i<SOMA_LOGIC_MAX_RULES;i++) {
        r.rules[i].antecedent[0]=0; r.rules[i].consequent[0]=0; r.rules[i].used=0;
    }
    for(int i=0;i<SOMA_LOGIC_MAX_DERIVED;i++) { r.derived[i][0]=0; r.barbara_derived[i][0]=0; }

    if(!ctx || !prompt) return r;
    ctx->total_scans++;

    // ── Split into sentences ──────────────────────────────────────────
    char segs[MAX_SEGMENTS][128];
    int  nseg = _lg_split_sentences(prompt, segs, MAX_SEGMENTS);

    // Temp storage for universal quantifiers
    AllArePair all_pairs[8]; int nall=0;
    IsAPair    is_pairs[8];  int nisa=0;

    // ── Pass 1: extract rules and stated facts ────────────────────────
    for(int s=0;s<nseg;s++) {
        char ante[SOMA_LOGIC_FACT_LEN], cons[SOMA_LOGIC_FACT_LEN];
        char fact[SOMA_LOGIC_FACT_LEN];
        char cat[SOMA_LOGIC_FACT_LEN], prop[SOMA_LOGIC_FACT_LEN];
        char inst[SOMA_LOGIC_FACT_LEN];
        SomaFactStatus st;

        // IF-THEN rule
        if(_lg_match_if_then(segs[s], ante, cons)) {
            if(r.rule_count < SOMA_LOGIC_MAX_RULES) {
                _lg_strcpy(r.rules[r.rule_count].antecedent, ante, SOMA_LOGIC_FACT_LEN);
                _lg_strcpy(r.rules[r.rule_count].consequent, cons, SOMA_LOGIC_FACT_LEN);
                r.rules[r.rule_count].used = 0;
                r.rule_count++;
            }
        }

        // Stated fact (true/false)
        if(_lg_match_fact_status(segs[s], fact, &st)) {
            _lg_set_fact(&r, fact, st, 0);
        }

        // Universal quantifier
        if(_lg_match_all_are(segs[s], cat, prop) && nall < 8) {
            _lg_strcpy(all_pairs[nall].cat,  cat,  SOMA_LOGIC_FACT_LEN);
            _lg_strcpy(all_pairs[nall].prop, prop, SOMA_LOGIC_FACT_LEN);
            nall++;
        }

        // Instance-category
        if(_lg_match_is_a(segs[s], inst, cat) && nisa < 8) {
            _lg_strcpy(is_pairs[nisa].inst, inst, SOMA_LOGIC_FACT_LEN);
            _lg_strcpy(is_pairs[nisa].cat,  cat,  SOMA_LOGIC_FACT_LEN);
            nisa++;
        }
    }

    // ── Pass 2: forward chaining ──────────────────────────────────────
    _lg_forward_chain(&r);

    // ── Pass 3: Barbara syllogisms ────────────────────────────────────
    if(nall > 0 && nisa > 0)
        _lg_barbara(&r, all_pairs, nall, is_pairs, nisa);

    // ── Pass 4: contradiction check ───────────────────────────────────
    _lg_check_contradictions(&r);

    // ── Build injection ───────────────────────────────────────────────
    int total_new = r.derived_count + r.barbara_count
                  + (r.contradiction ? 1 : 0);

    if(total_new > 0) {
        r.triggered = 1;
        int pos = 0;
        _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, "[LOGIC:");

        for(int i=0;i<r.derived_count;i++) {
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, " ");
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, r.derived[i]);
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, ";");
        }
        for(int i=0;i<r.barbara_count;i++) {
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, " ");
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, r.barbara_derived[i]);
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, ";");
        }
        if(r.contradiction) {
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, " CONTRADICTION:");
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, r.contradiction_fact);
            _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, ";");
        }
        _lg_append(r.injection, &pos, SOMA_LOGIC_INJECT_MAX, "]\n");
        r.injection_len = pos;

        ctx->total_triggers++;
        ctx->total_derived += r.derived_count + r.barbara_count;
        if(r.contradiction) ctx->total_contradictions++;
    }

    return r;
}
