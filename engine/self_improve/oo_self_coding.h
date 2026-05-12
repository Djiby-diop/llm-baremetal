/* oo_self_coding.h — OO Self-Coding Engine  Phase 5G / Phase 7B
 * ==============================================================
 * DIOP model generates C patches for OO subsystems.
 * Pipeline: prompt → generate → diff → D+ gate → apply → NVMe persist.
 */
#pragma once
#include <efi.h>
/* NVMe persistence uses extern declarations in .c to avoid type conflicts */

#define OO_CODEGEN_MAX_CODE   8192
#define OO_CODEGEN_MAX_PROMPT 1024
#define OO_CODEGEN_MAX_LOG    16

typedef enum {
    OO_PATCH_PENDING   = 0,
    OO_PATCH_APPROVED  = 1,
    OO_PATCH_REJECTED  = 2,
    OO_PATCH_APPLIED   = 3,
    OO_PATCH_FAILED    = 4,
} OoPatchState;

typedef struct {
    char         id[16];
    char         target_file[128];
    char         description[256];
    char         code[OO_CODEGEN_MAX_CODE];
    OoPatchState state;
    UINT32       d_plus_score;  /* D+ policy score 0-100 */
    UINT64       created_tick;
    UINT64       applied_tick;
} OoCodePatch;

#define OO_CODEGEN_PATCH_SLOTS  8

typedef struct {
    int         initialized;
    OoCodePatch patches[OO_CODEGEN_PATCH_SLOTS];
    UINT32      patch_count;
    UINT32      patches_applied;
    UINT32      patches_rejected;
    UINT32      auto_approve_threshold; /* D+ score to auto-approve (0=always ask) */
    UINT64      gen_count;
    char        last_prompt[OO_CODEGEN_MAX_PROMPT];
} OoSelfCoding;

EFI_STATUS oo_coding_init(OoSelfCoding *ctx);
int        oo_coding_generate(OoSelfCoding *ctx, const char *prompt,
                               const char *target_file);
int        oo_coding_approve(OoSelfCoding *ctx, const char *patch_id);
int        oo_coding_reject(OoSelfCoding *ctx, const char *patch_id);
int        oo_coding_apply(OoSelfCoding *ctx, const char *patch_id);
void       oo_coding_list_patches(const OoSelfCoding *ctx);
void       oo_coding_print(const OoSelfCoding *ctx);
int        oo_coding_repl_cmd(OoSelfCoding *ctx, const char *cmd);

extern OoSelfCoding g_self_coding;
