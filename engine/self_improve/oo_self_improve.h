/* oo_self_improve.h — OO Self-Improvement Engine
 * =================================================
 * OO reads its own source code, analyzes logs/crashes/REPL usage,
 * proposes code patches or architectural improvements, and waits
 * for human approval before applying any change.
 *
 * Pipeline:
 *   1. OBSERVE  — collect UART logs, REPL commands, inference patterns
 *   2. ANALYZE  — LLM (local or oracle) generates improvement proposals
 *   3. PROPOSE  — store proposals in journal with unique patch_id
 *   4. REVIEW   — human uses /patch_list, /patch_show, /patch_approve, /patch_reject
 *   5. APPLY    — approved patches written to persistent storage (EFI file)
 *   6. VERIFY   — next boot auto-tests the patch, reports result
 *
 * Safety:
 *   - No patch applied without explicit /patch_approve <id>
 *   - D+ policy checks each patch category before approval is allowed
 *   - All proposals logged to oo_patches.journal (append-only)
 *   - Rollback: /patch_rollback <id> restores previous version
 *
 * Freestanding C11. No libc. No malloc (static pool).
 */
#pragma once
#include <efi.h>
#include <efilib.h>

#define OO_PATCH_MAX        32
#define OO_PATCH_DESC_LEN   256
#define OO_PATCH_CODE_LEN   4096
#define OO_PATCH_ID_LEN     16

typedef enum {
    PATCH_CAT_PERF    = 0,
    PATCH_CAT_SAFETY  = 1,
    PATCH_CAT_FEATURE = 2,
    PATCH_CAT_BUGFIX  = 3,
    PATCH_CAT_MODEL   = 4,
    PATCH_CAT_CONFIG  = 5,
    PATCH_CAT_ARCH    = 6,
} PatchCategory;

typedef enum {
    PATCH_ST_PENDING     = 0,
    PATCH_ST_APPROVED    = 1,
    PATCH_ST_APPLIED     = 2,
    PATCH_ST_REJECTED    = 3,
    PATCH_ST_FAILED      = 4,
    PATCH_ST_ROLLED_BACK = 5,
} PatchStatus;

typedef enum {
    PATCH_SRC_LOCAL_LLM = 0,
    PATCH_SRC_ORACLE    = 1,
    PATCH_SRC_HUMAN     = 2,
    PATCH_SRC_CRASH     = 3,
    PATCH_SRC_EVOLUTION = 4,
} PatchSource;

typedef struct {
    CHAR8         id[OO_PATCH_ID_LEN];
    PatchCategory category;
    PatchStatus   status;
    PatchSource   source;
    CHAR8         description[OO_PATCH_DESC_LEN];
    CHAR8         target_file[128];
    CHAR8         code[OO_PATCH_CODE_LEN];
    UINT64        timestamp;
    UINT32        confidence_pct;
    UINT32        dplus_score;
    int           requires_reboot;
} OoPatch;

typedef struct {
    OoPatch patches[OO_PATCH_MAX];
    int     count;
    int     next_id;
    int     initialized;
} OoSelfImprove;

void oo_si_init(OoSelfImprove *si);
void oo_si_observe_session(OoSelfImprove *si, const CHAR8 *uart_log, UINTN log_len);
void oo_si_observe_crash(OoSelfImprove *si, const CHAR8 *crash_desc, UINT64 fault_addr);
int  oo_si_generate_proposals(OoSelfImprove *si, PatchSource source, const CHAR8 *context_prompt);
int  oo_si_add_proposal(OoSelfImprove *si, PatchCategory cat, PatchSource src,
                        const CHAR8 *description, const CHAR8 *target_file,
                        const CHAR8 *code, UINT32 confidence_pct);
int  oo_si_approve(OoSelfImprove *si, const CHAR8 *patch_id);
int  oo_si_reject(OoSelfImprove *si,  const CHAR8 *patch_id);
int  oo_si_rollback(OoSelfImprove *si, const CHAR8 *patch_id);
int  oo_si_apply_approved(OoSelfImprove *si, EFI_FILE_HANDLE Root);
int  oo_si_repl_cmd(OoSelfImprove *si, const char *cmd, EFI_FILE_HANDLE Root);
void oo_si_print_patch(const OoPatch *p);
void oo_si_print_list(OoSelfImprove *si);
