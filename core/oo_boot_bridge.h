/*
 * oo_boot_bridge.h — OO Boot Bridge for llm-baremetal
 *
 * Reads OOBOOT.CFG from the EFI/OO directory at kernel startup.
 * Populates OoBridgeConfig which drives LLM inference parameters.
 *
 * Format: key=value lines, # comments ignored, max 32 pairs.
 *
 * Expected keys (written by oo-host boot):
 *   oo_organism_id       — persistent identity string
 *   oo_continuity_epoch  — reboot counter (u64)
 *   oo_boot_count        — total boot count (u64)
 *   oo_mode              — runtime mode: normal|safe|degraded|experimental
 *   oo_goals_pending     — number of pending goals
 *   oo_goals_doing       — number of active goals
 *   oo_ts                — unix epoch when config was written
 *   oo_journal_path      — path to JOURNAL.JSONL on EFI volume
 *   oo_kv_path           — path to KV.BIN on EFI volume
 *
 * Optional keys (from repl.cfg / meta_patch):
 *   llm.max_tokens       — override max decode tokens
 *   llm.temperature      — override sampling temperature (float * 1000)
 *   llm.top_k            — override top-k sampling
 */

#ifndef OO_BOOT_BRIDGE_H
#define OO_BOOT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#define OO_BRIDGE_MAX_PAIRS   32
#define OO_BRIDGE_KEY_LEN     64
#define OO_BRIDGE_VAL_LEN    256
#define OO_BRIDGE_CFG_NAME   "EFI/OO/OOBOOT.CFG"

/* Runtime modes */
typedef enum {
    OO_MODE_NORMAL       = 0,
    OO_MODE_SAFE         = 1,
    OO_MODE_DEGRADED     = 2,
    OO_MODE_EXPERIMENTAL = 3,
} OoRuntimeMode;

/* Parsed boot config */
typedef struct {
    /* Raw key-value store */
    char keys  [OO_BRIDGE_MAX_PAIRS][OO_BRIDGE_KEY_LEN];
    char values[OO_BRIDGE_MAX_PAIRS][OO_BRIDGE_VAL_LEN];
    int  pair_count;

    /* Decoded well-known fields */
    char          organism_id[OO_BRIDGE_KEY_LEN];
    unsigned long continuity_epoch;
    unsigned long boot_count;
    OoRuntimeMode mode;
    int           goals_pending;
    int           goals_doing;
    unsigned long ts;
    char          journal_path[OO_BRIDGE_VAL_LEN];
    char          kv_path     [OO_BRIDGE_VAL_LEN];

    /* LLM overrides (0 = use default from repl.cfg) */
    int   max_tokens;      /* 0 = no override */
    float temperature;     /* 0.0 = no override */
    int   top_k;           /* 0 = no override */

    /* Meta */
    int   loaded;          /* 1 if successfully loaded */
    char  load_error[128]; /* error message if loaded == 0 */
} OoBridgeConfig;

/*
 * Load OOBOOT.CFG from the given root path.
 * Returns 1 on success, 0 on failure (cfg->load_error is set).
 * On failure the returned config has loaded=0 and safe defaults.
 */
int oo_bridge_load(OoBridgeConfig *cfg, const char *root_path);

/*
 * Get a value by key from the raw KV store.
 * Returns NULL if key not found.
 */
const char *oo_bridge_get(const OoBridgeConfig *cfg, const char *key);

/*
 * Apply LLM overrides from the bridge config to the REPL params.
 * Pass pointers to max_tokens, temperature, top_k — they are updated
 * only if the bridge has a non-zero override.
 */
void oo_bridge_apply_llm_overrides(
    const OoBridgeConfig *cfg,
    int   *max_tokens,
    float *temperature,
    int   *top_k
);

/*
 * Print the loaded config to serial (debug).
 */
void oo_bridge_print(const OoBridgeConfig *cfg);

/*
 * Write a minimal status line to the journal path (plain append, no JSON lib).
 * Format: {"kind":"boot_bridge","organism_id":"...","mode":"...","ts":...}
 */
int oo_bridge_journal_boot(const OoBridgeConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* OO_BOOT_BRIDGE_H */
