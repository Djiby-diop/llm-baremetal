// oo_insitu_train.c — In-Situ Self-Training Engine (Implementation)
//
// OO trains itself entirely from within its bare-metal environment.
// No external OS, no reflash, no reboot required.
//
// Freestanding C11 — no libc, no malloc.

#include "oo_insitu_train.h"
#include "../ssm/oo_neuralfs2.h"

// ── Freestanding helpers ─────────────────────────────────────────────────

static void oit_memset(void *dst, int c, int n) {
    char *p = (char *)dst;
    while (n--) *p++ = (char)c;
}

static int oit_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static void oit_strcpy(char *d, const char *s, int cap) {
    int i = 0;
    while (i < cap - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static void oit_strcat(char *d, const char *s, int cap) {
    int i = oit_strlen(d);
    while (i < cap - 1 && *s) d[i++] = *s++;
    d[i] = '\0';
}

static int oit_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int oit_strhas_ci(const char *hay, const char *needle) {
    int hn = oit_strlen(needle);
    if (!hn) return 1;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (j < hn && oit_tolower((unsigned char)hay[i+j]) ==
                          oit_tolower((unsigned char)needle[j])) j++;
        if (j == hn) return 1;
    }
    return 0;
}

static int oit_itoa(char *buf, int cap, int v) {
    if (cap <= 0) return 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    int neg = (v < 0); if (neg) v = -v;
    char tmp[24]; int n = 0;
    while (v > 0 && n < 23) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    if (neg && n < 23) tmp[n++] = '-';
    int i = 0;
    while (n > 0 && i < cap - 1) buf[i++] = tmp[--n];
    buf[i] = '\0';
    return i;
}

// Simple square root approximation (Newton-Raphson, 8 iterations)
static float oit_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float s = x > 1.0f ? x * 0.5f : 1.0f;
    for (int i = 0; i < 8; i++) s = 0.5f * (s + x / s);
    return s;
}

// ── LoRA initialisation ───────────────────────────────────────────────────

void oit_init(OitEngine *e, int hidden_dim) {
    if (!e) return;
    oit_memset(e, 0, sizeof(*e));
    e->enabled      = 1;
    e->rag_enabled  = 1;
    e->lora_enabled = 1;
    e->verbose      = 0;

    int dim = hidden_dim < OIT_HIDDEN_MAX ? hidden_dim : OIT_HIDDEN_MAX;
    e->lora.rank        = OIT_LORA_RANK;
    e->lora.hidden_dim  = dim;
    e->lora.scale       = 1.0f;  // alpha/rank = 4/4 = 1
    e->lora.initialized = 1;

    // Initialise A with small random-ish values via a deterministic LCG
    uint32_t lcg = 0xCAFEBEEF;
    for (int r = 0; r < OIT_LORA_RANK; r++) {
        for (int d = 0; d < dim; d++) {
            lcg = lcg * 1664525u + 1013904223u;
            // Map uint32 to float in [-0.02, 0.02]
            e->lora.A[r][d] = ((float)(int)(lcg >> 16) / 32768.0f) * 0.02f;
        }
    }
    // B initialised to 0 (so delta = 0 at start, model unchanged)

    // Watchdog
    e->watchdog.lines_diop_last  = 0;
    e->watchdog.lines_dream_last = 0;
    e->watchdog.total_train_cycles = 0;
}

// ── JSONL line counter (reads EFI file) ───────────────────────────────────
// Returns number of '\n' chars in the file, or 0 on error.
// Declared extern — implemented in soma_mind.c (EFI context).
extern uint32_t oit_count_jsonl_lines(const void *root_dir, const unsigned short *path16);

// ── Watchdog ──────────────────────────────────────────────────────────────

int oit_watchdog_tick(OitEngine *e, void *root_dir) {
    if (!e || !e->enabled || !root_dir) return 0;
    if (e->watchdog.training_active) return 0;

    // Count new lines accumulated since last check
    e->watchdog.lines_diop_now  = oit_count_jsonl_lines(root_dir, (const unsigned short *)L"DIOP_EXP.JSONL");
    e->watchdog.lines_dream_now = oit_count_jsonl_lines(root_dir, (const unsigned short *)L"OO_DREAM.JSONL");

    uint32_t new_diop  = e->watchdog.lines_diop_now  > e->watchdog.lines_diop_last
                         ? e->watchdog.lines_diop_now  - e->watchdog.lines_diop_last : 0;
    uint32_t new_dream = e->watchdog.lines_dream_now > e->watchdog.lines_dream_last
                         ? e->watchdog.lines_dream_now - e->watchdog.lines_dream_last : 0;

    if (new_diop + new_dream >= OIT_TRIGGER_LINES) {
        // Trigger autonomous training cycle
        e->watchdog.training_active = 1;
        int n = oit_train_from_jsonl(e, root_dir);
        e->watchdog.training_active = 0;

        // Update baselines
        e->watchdog.lines_diop_last  = e->watchdog.lines_diop_now;
        e->watchdog.lines_dream_last = e->watchdog.lines_dream_now;
        e->watchdog.total_train_cycles++;
        e->watchdog.total_pairs_processed += (uint32_t)n;
        return 1;
    }
    return 0;
}

// ── RAG: Context Injection ────────────────────────────────────────────────

int oit_build_ctx_inject(OitEngine *e, const char *query,
                          const void *nfs2_store) {
    if (!e || !e->rag_enabled || !query || !nfs2_store) return 0;

    const Nfs2Store *s = (const Nfs2Store *)nfs2_store;
    e->ctx_inject.count = 0;

    // Quick keyword extraction from query (first 4 significant tokens)
    char kws[4][32]; int nkw = 0;
    static const char *skip_wds[] = {
        "the","a","an","is","are","was","what","how","can","do","you","i",
        "le","la","les","un","une","de","du","je","tu","il","me","te","se",
        "qui","que","quoi","comment","est","sont","peux","peut",(const char*)0
    };
    char tmp[32]; int ti = 0;
    for (int i = 0; query[i] && nkw < 4; i++) {
        char c = (char)oit_tolower((unsigned char)query[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (ti < 31) tmp[ti++] = c;
        } else if (ti > 0) {
            tmp[ti] = '\0';
            int skip = 0;
            for (int j = 0; skip_wds[j]; j++)
                if (oit_strlen(tmp) <= 3 ||
                    (tmp[0] == skip_wds[j][0] && oit_strhas_ci(skip_wds[j], tmp))) {
                    skip = 1; break;
                }
            if (!skip && ti >= 3) { oit_strcpy(kws[nkw++], tmp, 32); }
            ti = 0;
        }
    }
    if (ti > 0 && nkw < 4) { tmp[ti] = '\0'; oit_strcpy(kws[nkw++], tmp, 32); }

    if (nkw == 0) return 0;

    // Scan NFS2 for records whose name or data matches any keyword
    for (int i = 0; i < NFS2_MAX_RECORDS && e->ctx_inject.count < OIT_MAX_CTX_INJECT; i++) {
        if (!(s->records[i].flags & NFS2_FLAG_USED)) continue;
        // Skip internal/system keys (start with "oo.lora" or "oo.sys")
        if (oit_strhas_ci(s->records[i].name, "oo.lora")) continue;
        if (oit_strhas_ci(s->records[i].name, "oo.sys"))  continue;

        for (int k = 0; k < nkw; k++) {
            if (oit_strhas_ci(s->records[i].name, kws[k]) ||
                oit_strhas_ci(s->records[i].data, kws[k])) {
                // Build "key: value" string
                char entry[OIT_CTX_INJECT_MAXLEN];
                oit_strcpy(entry, s->records[i].name, OIT_CTX_INJECT_MAXLEN);
                oit_strcat(entry, ": ", OIT_CTX_INJECT_MAXLEN);
                oit_strcat(entry, s->records[i].data, OIT_CTX_INJECT_MAXLEN);
                oit_strcpy(e->ctx_inject.entries[e->ctx_inject.count],
                           entry, OIT_CTX_INJECT_MAXLEN);
                e->ctx_inject.count++;
                break;
            }
        }
    }
    return e->ctx_inject.count;
}

void oit_format_ctx_prefix(const OitEngine *e, char *out, int cap) {
    if (!e || !out || cap <= 0 || e->ctx_inject.count == 0) {
        if (out && cap > 0) out[0] = '\0';
        return;
    }
    oit_strcpy(out, "[Context from memory:\n", cap);
    for (int i = 0; i < e->ctx_inject.count; i++) {
        oit_strcat(out, "  - ", cap);
        oit_strcat(out, e->ctx_inject.entries[i], cap);
        oit_strcat(out, "\n", cap);
    }
    oit_strcat(out, "]\n", cap);
}

// ── LoRA forward pass (apply delta) ──────────────────────────────────────
//
// out_vec += scale * B * (A * in_vec)
// Complexity: O(rank * dim) — very fast for rank=4

void oit_lora_apply(const OitEngine *e,
                    float *out_vec, const float *in_vec, int dim) {
    if (!e || !e->lora_enabled || !e->lora.initialized) return;
    if (!out_vec || !in_vec) return;
    if (dim > e->lora.hidden_dim) dim = e->lora.hidden_dim;

    // intermediate = A * in_vec  (rank-dim → rank-1)
    float inter[OIT_LORA_RANK];
    for (int r = 0; r < OIT_LORA_RANK; r++) {
        float acc = 0.0f;
        for (int d = 0; d < dim; d++) acc += e->lora.A[r][d] * in_vec[d];
        inter[r] = acc;
    }

    // out_vec += scale * B * inter
    for (int d = 0; d < dim; d++) {
        float acc = 0.0f;
        for (int r = 0; r < OIT_LORA_RANK; r++) acc += e->lora.B[d][r] * inter[r];
        out_vec[d] += e->lora.scale * acc;
    }
}

// ── LoRA backward (one gradient step) ────────────────────────────────────
//
// Given input x, expected y, actual y_hat = W*x + B*A*x:
//   loss = ||y - y_hat||^2 / dim
//   dL/dB = 2 * (y_hat - y) * inter^T / dim   [dim x rank]
//   dL/dA = 2 * B^T * (y_hat - y) * x^T / dim [rank x dim]

static void oit_lora_backward(OitEngine *e,
                               const float *x, const float *y, const float *y_hat,
                               int dim) {
    if (!e || !e->lora.initialized || dim <= 0) return;
    if (dim > e->lora.hidden_dim) dim = e->lora.hidden_dim;

    // Compute error e = (y_hat - y)
    float err[OIT_HIDDEN_MAX];
    float loss = 0.0f;
    for (int d = 0; d < dim; d++) {
        err[d] = y_hat[d] - y[d];
        loss += err[d] * err[d];
    }
    loss /= (float)dim;
    (void)loss; // informational

    // Compute inter = A * x
    float inter[OIT_LORA_RANK];
    for (int r = 0; r < OIT_LORA_RANK; r++) {
        float acc = 0.0f;
        for (int d = 0; d < dim; d++) acc += e->lora.A[r][d] * x[d];
        inter[r] = acc;
    }

    float lr_scaled = OIT_TRAIN_LR / (float)dim;

    // Update B: B[d][r] -= lr * 2 * err[d] * inter[r] / dim
    for (int d = 0; d < dim; d++)
        for (int r = 0; r < OIT_LORA_RANK; r++)
            e->lora.B[d][r] -= lr_scaled * 2.0f * err[d] * inter[r];

    // Update A: A[r][d] -= lr * 2 * (B^T * err)[r] * x[d] / dim
    float bt_err[OIT_LORA_RANK];
    for (int r = 0; r < OIT_LORA_RANK; r++) {
        float acc = 0.0f;
        for (int d = 0; d < dim; d++) acc += e->lora.B[d][r] * err[d];
        bt_err[r] = acc;
    }
    for (int r = 0; r < OIT_LORA_RANK; r++)
        for (int d = 0; d < dim; d++)
            e->lora.A[r][d] -= lr_scaled * 2.0f * bt_err[r] * x[d];
}

void oit_train_batch(OitEngine *e, const OitPair *pairs, int count) {
    if (!e || !pairs || count <= 0) return;
    int dim = e->lora.hidden_dim;
    if (dim <= 0) return;

    // For each pair: use input token hash as x, output hash as y
    // (simplified: we can't do full forward pass without model weights)
    // Instead we use a simple "embedding fingerprint" to build x/y vectors.
    // This gives the LoRA a directional signal even without full model state.
    for (int p = 0; p < count; p++) {
        float x[OIT_HIDDEN_MAX], y[OIT_HIDDEN_MAX], y_hat[OIT_HIDDEN_MAX];
        oit_memset(x, 0, sizeof(float) * dim);
        oit_memset(y, 0, sizeof(float) * dim);

        // Build x from input string hash
        uint32_t h = 0x811c9dc5u;
        for (int i = 0; pairs[p].input[i]; i++) {
            h ^= (uint32_t)(unsigned char)pairs[p].input[i];
            h *= 0x01000193u;
            x[h % (uint32_t)dim] += 0.1f * pairs[p].quality;
        }

        // Build y from output string hash
        h = 0x811c9dc5u;
        for (int i = 0; pairs[p].output[i]; i++) {
            h ^= (uint32_t)(unsigned char)pairs[p].output[i];
            h *= 0x01000193u;
            y[h % (uint32_t)dim] += 0.1f * pairs[p].quality;
        }

        // y_hat = x + LoRA(x)
        for (int d = 0; d < dim; d++) y_hat[d] = x[d];
        oit_lora_apply(e, y_hat, x, dim);

        oit_lora_backward(e, x, y, y_hat, dim);
    }
}

// ── LoRA persistence (NFS2 binary-ish encoding) ───────────────────────────
// We store the B matrix as it changes (A is fixed init); float → int16 (×1000)

int oit_lora_save(OitEngine *e, void *nfs2_store) {
    if (!e || !nfs2_store) return -1;
    Nfs2Store *s = (Nfs2Store *)nfs2_store;
    int dim = e->lora.hidden_dim;
    if (dim > 48) dim = 48;  // store first 48 dims to fit in NFS2_DATA_MAX=384

    // Encode: rank*dim int16 values (2 bytes each, 4*48=192 values, 384 bytes max)
    char buf[NFS2_DATA_MAX];
    int  bi = 0;
    for (int r = 0; r < OIT_LORA_RANK && bi + 2 < NFS2_DATA_MAX; r++) {
        for (int d = 0; d < dim && bi + 2 < NFS2_DATA_MAX; d++) {
            int16_t v = (int16_t)(e->lora.B[d][r] * 10000.0f);
            buf[bi++] = (char)(v & 0xFF);
            buf[bi++] = (char)((v >> 8) & 0xFF);
        }
    }
    // NUL-terminate (NFS2 text mode will still handle it)
    if (bi < NFS2_DATA_MAX) buf[bi] = '\0';

    Nfs2Record *rec;
    int idx = nfs2_find(s, "oo.lora.delta_b");
    if (idx < 0) idx = nfs2_free_slot(s);
    if (idx < 0) return -1;
    rec = &s->records[idx];
    oit_strcpy(rec->name, "oo.lora.delta_b", NFS2_NAME_MAX);
    rec->flags    = NFS2_FLAG_USED | NFS2_FLAG_BINARY;
    rec->data_len = (unsigned int)bi;
    for (int i = 0; i < bi; i++) rec->data[i] = buf[i];
    rec->write_count++;
    s->total_writes++;
    if (!(rec->flags & NFS2_FLAG_USED)) s->record_count++;
    return 0;
}

int oit_lora_load(OitEngine *e, const void *nfs2_store) {
    if (!e || !nfs2_store) return -1;
    const Nfs2Store *s = (const Nfs2Store *)nfs2_store;
    int idx = nfs2_find(s, "oo.lora.delta_b");
    if (idx < 0) return 1;  // not found = fresh start

    int dim = e->lora.hidden_dim;
    if (dim > 48) dim = 48;
    const char *buf = s->records[idx].data;
    int bi = 0;
    for (int r = 0; r < OIT_LORA_RANK; r++) {
        for (int d = 0; d < dim; d++) {
            if (bi + 1 >= (int)s->records[idx].data_len) break;
            int16_t v = (int16_t)((unsigned char)buf[bi] | ((unsigned char)buf[bi+1] << 8));
            bi += 2;
            e->lora.B[d][r] = (float)v / 10000.0f;
        }
    }
    return 0;
}

// ── Training from JSONL file ──────────────────────────────────────────────
// Reads pairs from OO_DREAM.JSONL and DIOP_EXP.JSONL (EFI context).
// Returns number of pairs processed.
// Declared extern implementation: oit_read_jsonl_pairs() in soma_mind.c.
extern int oit_read_jsonl_pairs(void *root_dir, const unsigned short *path16,
                                 OitPair *pairs, int max_pairs);

int oit_train_from_jsonl(OitEngine *e, void *root_dir) {
    if (!e || !root_dir) return 0;

    OitPair pairs[OIT_TRAIN_BATCH];
    int total = 0;

    // Read from OO_DREAM.JSONL (high-quality synthetic pairs)
    int n = oit_read_jsonl_pairs(root_dir, (const unsigned short *)L"OO_DREAM.JSONL",
                                  pairs, OIT_TRAIN_BATCH);
    if (n > 0) {
        oit_train_batch(e, pairs, n);
        total += n;
    }

    // Read from DIOP_EXP.JSONL (real interaction pairs)
    n = oit_read_jsonl_pairs(root_dir, (const unsigned short *)L"DIOP_EXP.JSONL",
                              pairs, OIT_TRAIN_BATCH);
    if (n > 0) {
        oit_train_batch(e, pairs, n);
        total += n;
    }

    return total;
}

// ── Diagnostics ───────────────────────────────────────────────────────────

void oit_print_status(const OitEngine *e, void (*print_fn)(const char *)) {
    if (!e || !print_fn) return;
    char buf[128];

    print_fn("[OIT] In-Situ Self-Training Engine\r\n");

    oit_strcpy(buf, "  enabled=", sizeof(buf));
    buf[10] = (char)('0' + e->enabled);
    buf[11] = '  '; buf[11] = ' ';
    buf[12] = '\0';
    oit_strcat(buf, "rag=", sizeof(buf));
    oit_strcat(buf, e->rag_enabled ? "on" : "off", sizeof(buf));
    oit_strcat(buf, "  lora=", sizeof(buf));
    oit_strcat(buf, e->lora_enabled ? "on" : "off", sizeof(buf));
    oit_strcat(buf, "\r\n", sizeof(buf));
    print_fn(buf);

    oit_strcpy(buf, "  lora_rank=", sizeof(buf));
    char num[16]; oit_itoa(num, sizeof(num), e->lora.rank);
    oit_strcat(buf, num, sizeof(buf));
    oit_strcat(buf, "  hidden_dim=", sizeof(buf));
    oit_itoa(num, sizeof(num), e->lora.hidden_dim);
    oit_strcat(buf, num, sizeof(buf));
    oit_strcat(buf, "\r\n", sizeof(buf));
    print_fn(buf);

    oit_strcpy(buf, "  total_train_cycles=", sizeof(buf));
    oit_itoa(num, sizeof(num), (int)e->watchdog.total_train_cycles);
    oit_strcat(buf, num, sizeof(buf));
    oit_strcat(buf, "  total_pairs=", sizeof(buf));
    oit_itoa(num, sizeof(num), (int)e->watchdog.total_pairs_processed);
    oit_strcat(buf, num, sizeof(buf));
    oit_strcat(buf, "\r\n", sizeof(buf));
    print_fn(buf);

    oit_strcpy(buf, "  diop_lines=", sizeof(buf));
    oit_itoa(num, sizeof(num), (int)e->watchdog.lines_diop_now);
    oit_strcat(buf, num, sizeof(buf));
    oit_strcat(buf, "  dream_lines=", sizeof(buf));
    oit_itoa(num, sizeof(num), (int)e->watchdog.lines_dream_now);
    oit_strcat(buf, num, sizeof(buf));
    oit_strcat(buf, "  trigger_at=", sizeof(buf));
    oit_itoa(num, sizeof(num), OIT_TRIGGER_LINES);
    oit_strcat(buf, num, sizeof(buf));
    oit_strcat(buf, "\r\n", sizeof(buf));
    print_fn(buf);
}
