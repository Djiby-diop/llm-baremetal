/* oo_dplus.h — D+ Policy Engine (Public Prototype)
 *
 * Every action in the system passes through a D+ policy gate.
 * Actions include: inference token emission, memory allocation,
 * external I/O, self-modification, and network calls.
 *
 * The policy engine evaluates a context vector against a compiled
 * policy binary (OOPOLICY.BIN) and returns a verdict.
 */
#ifndef OO_DPLUS_H
#define OO_DPLUS_H

#include <stdint.h>

typedef enum {
    DPLUS_ALLOW   = 0,
    DPLUS_DENY    = 1,
    DPLUS_DEFER   = 2,   /* Route to oracle for human-in-the-loop decision */
    DPLUS_SANDBOX = 3,   /* Allow but isolate in quarantine zone */
} DPlusVerdict;

typedef enum {
    ACTION_INFERENCE_OUTPUT = 0,
    ACTION_MEMORY_ALLOC     = 1,
    ACTION_SELF_MODIFY      = 2,
    ACTION_NETWORK_SEND     = 3,
    ACTION_FILE_WRITE       = 4,
    ACTION_LORA_APPLY       = 5,
} DPlusActionType;

typedef struct {
    DPlusActionType action;
    uint64_t        payload_hash;   /* Blake2s hash of action payload */
    float           confidence;     /* Model confidence [0.0, 1.0] */
    uint32_t        flags;
} DPlusContext;

typedef struct {
    DPlusVerdict verdict;
    char         reason[64];
    uint32_t     rule_id;
} DPlusDecision;

/* Initialize D+ engine and load compiled policy from OOPOLICY.BIN */
int dplus_init(const char *policy_path);

/* Evaluate a context and return a verdict */
DPlusDecision dplus_evaluate(const DPlusContext *ctx);

/* Print current policy summary */
void dplus_print_policy(void);

#endif /* OO_DPLUS_H */
