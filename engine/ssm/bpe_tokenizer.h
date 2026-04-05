#ifndef BPE_TOKENIZER_H
#define BPE_TOKENIZER_H

// BPE Tokenizer — freestanding bare-metal implementation
// No libc (no strcmp, no malloc). Works in UEFI environment.
// Supports: encode string → token ids, decode token id → string

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Tokenizer config
// ============================================================
#define BPE_MAX_VOCAB       52000
#define BPE_MAX_TOKEN_LEN   256    // max bytes per token string
#define BPE_MAX_MERGE_LEN   512    // max bytes per merge rule
#define BPE_MAX_INPUT_LEN   2048   // max input string length
#define BPE_MAX_TOKENS      2048   // max output tokens

// ============================================================
// Vocabulary entry
// ============================================================
typedef struct {
    char    str[BPE_MAX_TOKEN_LEN]; // token string (UTF-8)
    float   score;                  // merge priority score
    int     id;                     // token id
} BpeVocabEntry;

// ============================================================
// Tokenizer state — no heap, caller provides storage
// ============================================================
typedef struct {
    BpeVocabEntry *vocab;      // [vocab_size] vocabulary table
    int            vocab_size;

    // Special tokens
    int bos_id;   // beginning of sequence (typically 1)
    int eos_id;   // end of sequence (typically 2)
    int unk_id;   // unknown token (typically 0)
    int pad_id;   // padding (-1 if none)

    // Byte fallback: single byte tokens at fixed offsets
    int byte_offset; // id of token "<0x00>", next is <0x01>, etc.

    int initialized;
} BpeTokenizer;

// ============================================================
// Load tokenizer from raw binary blob
// Format: [vocab_size:u32][for each token: score:f32, len:u32, str:len bytes]
// This matches the llama.c tokenizer.bin format.
// ============================================================
typedef enum {
    BPE_OK           =  0,
    BPE_ERR_NOMEM    = -1,
    BPE_ERR_BADFMT   = -2,
    BPE_ERR_OVERFLOW = -3,
} BpeStatus;

BpeStatus bpe_load(
    BpeTokenizer  *tok,
    BpeVocabEntry *vocab_buf,  // caller-allocated [vocab_size]
    int            vocab_buf_size,
    const void    *data,       // raw tokenizer.bin bytes
    uint64_t       data_len
);

// ============================================================
// Encode: string → token ids
// Returns number of tokens written, or negative on error.
// Adds BOS token at start if add_bos != 0.
// ============================================================
int bpe_encode(
    const BpeTokenizer *tok,
    const char         *text,      // input UTF-8 string
    int                 add_bos,   // prepend BOS token?
    int                *out_ids,   // [BPE_MAX_TOKENS] output
    int                 max_out
);

// ============================================================
// Decode: token id → string
// Writes into buf[0..buf_size]. Returns bytes written.
// ============================================================
int bpe_decode_token(
    const BpeTokenizer *tok,
    int                 token_id,
    char               *buf,
    int                 buf_size
);

// ============================================================
// Decode sequence → string (concatenate all tokens)
// ============================================================
int bpe_decode(
    const BpeTokenizer *tok,
    const int          *ids,
    int                 n_ids,
    char               *out,
    int                 out_size
);

// ============================================================
// Buffer size helper
// ============================================================
#define BPE_VOCAB_BUF_SIZE(n) ((n) * sizeof(BpeVocabEntry))

#ifdef __cplusplus
}
#endif

#endif // BPE_TOKENIZER_H
