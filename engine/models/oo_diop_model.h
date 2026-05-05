/* oo_diop_model.h — DIOP Model Integration  Phase 4D
 * =====================================================
 * Loads and runs Djiby-diop custom models from:
 *   - ESP filesystem (.bin / .gguf)
 *   - HTTP via oo_netboot_pull_model()
 *   - HuggingFace mirror via oracle proxy
 *
 * DIOP models are standard llama2-format .bin or GGUF files.
 * This layer wraps the existing llmk inference engine with
 * model-specific presets (quantization, temperature, system prompt).
 *
 * Pipeline:
 *   /diop_load <path|url> → loads weights into model_buf
 *   /diop_run  <prompt>   → runs inference with DIOP preset
 *   /diop_bench           → tokens/sec benchmark
 *   /diop_info            → show model metadata
 *   /diop_hf <repo>       → pull from HuggingFace via proxy
 *
 * Freestanding C11. No libc. Static pool.
 */
#pragma once
#include <efi.h>
#include <efilib.h>

#define OO_DIOP_NAME_MAX   128
#define OO_DIOP_PATH_MAX   256

/* Known DIOP model presets (from huggingface.co/djibydiop) */
typedef enum {
    DIOP_MODEL_UNKNOWN  = 0,
    DIOP_MODEL_BASE     = 1,   /* djibydiop/llm-baremetal base */
    DIOP_MODEL_CODE     = 2,   /* code understanding + generation */
    DIOP_MODEL_SYSTEMS  = 3,   /* low-level systems reasoning */
    DIOP_MODEL_ORACLE   = 4,   /* oracle-augmented reasoning */
} DiopModelVariant;

typedef enum {
    DIOP_FMT_UNKNOWN = 0,
    DIOP_FMT_BIN     = 1,
    DIOP_FMT_GGUF    = 2,
} DiopModelFmt;

typedef struct {
    int              loaded;
    DiopModelVariant variant;
    DiopModelFmt     fmt;
    CHAR8            name[OO_DIOP_NAME_MAX];
    CHAR8            path[OO_DIOP_PATH_MAX];
    void            *weights;          /* pointer into AllocatePages buffer */
    UINTN            weights_len;
    UINTN            weights_pages;    /* EFI page count */
    UINT32           vocab_size;
    UINT32           dim;
    UINT32           n_layers;
    UINT32           n_heads;
    /* DIOP preset parameters */
    float            temperature;
    float            top_p;
    int              max_tokens;
    CHAR8            system_prompt[512];
    /* Stats */
    UINT32           runs;
    UINT32           total_tokens;
} OoDiopModel;

/* Lifecycle */
void oo_diop_init(OoDiopModel *m);

/* Load from ESP file */
EFI_STATUS oo_diop_load_file(OoDiopModel *m, EFI_FILE_HANDLE Root,
                              const CHAR8 *path);

/* Load from URL via netboot HTTP */
EFI_STATUS oo_diop_load_url(OoDiopModel *m, const CHAR8 *url);

/* Detect format + extract metadata from loaded buffer */
int oo_diop_probe(OoDiopModel *m);

/* Run inference (integrates with llmk engine) */
int oo_diop_run(OoDiopModel *m, const CHAR8 *prompt,
                CHAR8 *out_buf, UINTN out_cap);

/* Benchmark: run 32-token warmup, return tokens/sec estimate */
float oo_diop_bench(OoDiopModel *m);

/* Unload weights, free EFI pages */
void oo_diop_unload(OoDiopModel *m);

/* Display */
void oo_diop_print_info(const OoDiopModel *m);

/* REPL dispatcher */
int oo_diop_repl_cmd(OoDiopModel *m, const char *cmd,
                     EFI_FILE_HANDLE Root);

/* Global singleton */
extern OoDiopModel g_diop;
