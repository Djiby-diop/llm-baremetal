// BPE Tokenizer — freestanding bare-metal implementation
// No libc: no strcmp, no malloc, no qsort. Works in UEFI.

#include "bpe_tokenizer.h"

// ============================================================
// Freestanding string helpers
// ============================================================
static int bpe_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int bpe_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void bpe_memcpy(void *dst, const void *src, int n) {
    char *d = (char*)dst;
    const char *s = (const char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static void bpe_strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// ============================================================
// Binary reader helpers
// ============================================================
static uint32_t bpe_read_u32(const unsigned char *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float bpe_read_f32(const unsigned char *p) {
    uint32_t u = bpe_read_u32(p);
    float f;
    bpe_memcpy(&f, &u, 4);
    return f;
}

// ============================================================
// Load tokenizer from tokenizer.bin (llama.c format)
// Format: [vocab_size:i32][for each: score:f32, len:i32, str:len bytes]
// ============================================================
BpeStatus bpe_load(
    BpeTokenizer  *tok,
    BpeVocabEntry *vocab_buf,
    int            vocab_buf_size,
    const void    *data,
    uint64_t       data_len)
{
    if (!tok || !vocab_buf || !data || data_len < 4) return BPE_ERR_BADFMT;

    const unsigned char *p = (const unsigned char *)data;
    const unsigned char *end = p + data_len;

    // Read max_token_length (first int in llama.c tokenizer.bin)
    int max_token_len = (int)bpe_read_u32(p); p += 4;
    (void)max_token_len;

    // Infer vocab_size from remaining data (llama.c doesn't store it directly)
    // We try to read until buffer exhausted or vocab_buf full
    int vs = 0;
    const unsigned char *scan = p;
    while (scan < end && vs < vocab_buf_size) {
        if (scan + 8 > end) break;
        float score = bpe_read_f32(scan); scan += 4;
        int len = (int)bpe_read_u32(scan); scan += 4;
        if (len <= 0 || len > BPE_MAX_TOKEN_LEN || scan + len > end) break;
        vocab_buf[vs].score = score;
        vocab_buf[vs].id    = vs;
        bpe_strncpy(vocab_buf[vs].str, (const char*)scan, len + 1);
        vocab_buf[vs].str[len] = 0;
        scan += len;
        vs++;
    }

    if (vs == 0) return BPE_ERR_BADFMT;
    if (vs > vocab_buf_size) return BPE_ERR_NOMEM;

    tok->vocab      = vocab_buf;
    tok->vocab_size = vs;
    tok->bos_id     = 1;
    tok->eos_id     = 2;
    tok->unk_id     = 0;
    tok->pad_id     = -1;
    tok->byte_offset = 3; // <0x00> starts at id 3 in llama tokenizer
    tok->initialized = 1;

    return BPE_OK;
}

// ============================================================
// Vocabulary lookup (linear scan — no hash needed for inference)
// ============================================================
static int bpe_find_token(const BpeTokenizer *tok, const char *str, int len) {
    for (int i = 0; i < tok->vocab_size; i++) {
        const char *t = tok->vocab[i].str;
        int tl = bpe_strlen(t);
        if (tl != len) continue;
        int match = 1;
        for (int j = 0; j < len; j++) if (t[j] != str[j]) { match = 0; break; }
        if (match) return tok->vocab[i].id;
    }
    return tok->unk_id;
}

// ============================================================
// BPE encode — greedy merge algorithm (no sorting needed)
// Works by iteratively finding the highest-score mergeable pair
// ============================================================

// Temporary token buffer for merge operations
typedef struct {
    int  id;
    char str[BPE_MAX_TOKEN_LEN];
    int  str_len;
} BpeTok;

static BpeTok _bpe_tmp[BPE_MAX_TOKENS * 2]; // static scratch

int bpe_encode(
    const BpeTokenizer *tok,
    const char         *text,
    int                 add_bos,
    int                *out_ids,
    int                 max_out)
{
    if (!tok || !tok->initialized || !text || !out_ids) return BPE_ERR_BADFMT;

    int n = 0;

    // BOS
    if (add_bos && n < max_out) out_ids[n++] = tok->bos_id;

    // Tokenize char by char initially (byte-level fallback)
    int text_len = bpe_strlen(text);
    int tmp_n = 0;

    // Add space prefix if non-empty (llama SentencePiece convention)
    char prefixed[BPE_MAX_INPUT_LEN + 2];
    prefixed[0] = ' ';
    int pi = 1;
    for (int i = 0; i < text_len && pi < BPE_MAX_INPUT_LEN; i++) {
        prefixed[pi++] = text[i];
    }
    prefixed[pi] = 0;
    text = prefixed;
    text_len = pi;

    // Initialize with single chars
    for (int i = 0; i < text_len && tmp_n < BPE_MAX_TOKENS * 2 - 1; i++) {
        char cbuf[2] = { text[i], 0 };
        int id = bpe_find_token(tok, cbuf, 1);
        _bpe_tmp[tmp_n].id = id;
        _bpe_tmp[tmp_n].str[0] = text[i];
        _bpe_tmp[tmp_n].str[1] = 0;
        _bpe_tmp[tmp_n].str_len = 1;
        tmp_n++;
    }

    // Greedy BPE merges: repeatedly find best merge pair
    while (1) {
        float best_score = -1e38f;
        int   best_i     = -1;
        char  best_str[BPE_MAX_TOKEN_LEN];
        int   best_id    = -1;

        for (int i = 0; i < tmp_n - 1; i++) {
            // Concatenate tokens i and i+1
            int la = _bpe_tmp[i].str_len;
            int lb = _bpe_tmp[i+1].str_len;
            if (la + lb >= BPE_MAX_TOKEN_LEN) continue;

            char merged[BPE_MAX_TOKEN_LEN];
            bpe_memcpy(merged, _bpe_tmp[i].str, la);
            bpe_memcpy(merged + la, _bpe_tmp[i+1].str, lb);
            merged[la + lb] = 0;

            // Find this merged string in vocab
            int id = bpe_find_token(tok, merged, la + lb);
            if (id == tok->unk_id) continue;

            float score = tok->vocab[id].score;
            if (score > best_score) {
                best_score = score;
                best_i     = i;
                best_id    = id;
                bpe_strncpy(best_str, merged, BPE_MAX_TOKEN_LEN);
            }
        }

        if (best_i < 0) break; // no more merges possible

        // Apply merge: replace tokens[best_i] and [best_i+1] with merged
        int ml = bpe_strlen(best_str);
        _bpe_tmp[best_i].id = best_id;
        bpe_strncpy(_bpe_tmp[best_i].str, best_str, BPE_MAX_TOKEN_LEN);
        _bpe_tmp[best_i].str_len = ml;

        // Shift remaining tokens left
        for (int i = best_i + 1; i < tmp_n - 1; i++) {
            _bpe_tmp[i] = _bpe_tmp[i + 1];
        }
        tmp_n--;
    }

    // Write output
    for (int i = 0; i < tmp_n && n < max_out; i++) {
        out_ids[n++] = _bpe_tmp[i].id;
    }

    return n;
}

// ============================================================
// Decode single token
// ============================================================
int bpe_decode_token(
    const BpeTokenizer *tok,
    int                 token_id,
    char               *buf,
    int                 buf_size)
{
    if (!tok || !tok->initialized || !buf || buf_size <= 0) return 0;
    if (token_id < 0 || token_id >= tok->vocab_size) {
        buf[0] = 0;
        return 0;
    }

    const char *s = tok->vocab[token_id].str;
    int len = bpe_strlen(s);

    // Handle <0xNN> byte tokens (llama byte fallback)
    if (len == 6 && s[0] == '<' && s[1] == '0' && s[2] == 'x' && s[5] == '>') {
        // Decode hex byte
        char hi = s[3], lo = s[4];
        int hv = (hi >= '0' && hi <= '9') ? hi-'0' : (hi >= 'a' && hi <= 'f') ? hi-'a'+10 : hi-'A'+10;
        int lv = (lo >= '0' && lo <= '9') ? lo-'0' : (lo >= 'a' && lo <= 'f') ? lo-'a'+10 : lo-'A'+10;
        buf[0] = (char)((hv << 4) | lv);
        if (buf_size > 1) buf[1] = 0;
        return 1;
    }

    // Replace leading space (▁ or ' ') with actual space
    int out = 0;
    for (int i = 0; i < len && out < buf_size - 1; i++) {
        buf[out++] = s[i];
    }
    buf[out] = 0;
    return out;
}

// ============================================================
// Decode sequence
// ============================================================
int bpe_decode(
    const BpeTokenizer *tok,
    const int          *ids,
    int                 n_ids,
    char               *out,
    int                 out_size)
{
    int total = 0;
    for (int i = 0; i < n_ids && total < out_size - 1; i++) {
        int written = bpe_decode_token(tok, ids[i], out + total, out_size - total);
        total += written;
    }
    out[total] = 0;
    return total;
}
