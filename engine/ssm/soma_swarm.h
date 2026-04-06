// soma_swarm.h — SomaMind Swarm Intelligence (Phase E)
//
// N local agents, each with its own DNA variant, vote on each token
// using the same logit vector. No second forward pass — swarm operates
// at sampling level only.
//
// Agent diversity:
//   Agent 0 = base DNA (logical anchor)
//   Agent 1 = Solar specialist (low temp)
//   Agent 2 = Lunar specialist (high temp)
//   Agent 3 = Explorer (randomized bias)
//
// Consensus modes:
//   MAJORITY  — token with most votes wins
//   WEIGHTED  — votes weighted by per-agent fitness score
//   CONFIDENT — use agent with highest per-token confidence
//
// Each agent tracks its own mini-stats (agreements with final decision,
// unique contributions). Over time, low-fitness agents get their DNA
// nudged toward the best-performing agent.
//
// Freestanding C11 — no libc, no malloc.

#pragma once

#include "soma_dna.h"
#include "soma_dual.h"    // soma_dual_sample, SomaDualResult, SomaCoreUsed
#include "ssm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================
#define SOMA_SWARM_AGENTS     4    // Number of local agents
#define SOMA_SWARM_MAX_VOCAB  512  // Max tracked vote buckets per token step

// ============================================================
// Consensus strategy
// ============================================================
typedef enum {
    SOMA_SWARM_MAJORITY   = 0,  // Token with most votes
    SOMA_SWARM_WEIGHTED   = 1,  // Votes weighted by agent fitness
    SOMA_SWARM_CONFIDENT  = 2,  // Agent with highest confidence decides
} SomaSwarmMode;

// ============================================================
// Per-agent state
// ============================================================
typedef struct {
    SomaDNA     dna;            // This agent's DNA variant
    float       fitness;        // Running fitness [0, 1]
    int         votes_cast;     // Total votes this agent has cast
    int         votes_won;      // Times this agent's token was selected
    int         agreements;     // Times this agent agreed with consensus
    char        name[8];        // Short label: "BASE","SOLAR","LUNAR","XPLR"
} SomaSwarmAgent;

// ============================================================
// Per-token vote result
// ============================================================
typedef struct {
    int     selected_token;           // Final consensus token
    int     agent_votes[SOMA_SWARM_AGENTS]; // Each agent's token proposal
    float   agent_confidence[SOMA_SWARM_AGENTS]; // Each agent's confidence
    int     winning_agent;            // Index of agent whose token won
    int     unanimous;                // 1 if all agents agreed
    int     vote_spread;              // Number of unique tokens proposed
    SomaSwarmMode mode_used;
} SomaSwarmResult;

// ============================================================
// Swarm context
// ============================================================
typedef struct {
    SomaSwarmAgent  agents[SOMA_SWARM_AGENTS];
    SomaSwarmMode   mode;           // Active consensus mode
    int             enabled;        // 0 = off, 1 = on
    uint32_t        rng;            // Swarm-level RNG (diverged from main)

    // Work buffer: one per agent for dual sampling
    ssm_f32        *work_buf;       // [vocab_size] — shared scratch (agents run sequentially)
    int             vocab_size;
    int             ready;

    // Session stats
    int     total_votes;
    int     unanimous_votes;        // All agents agreed
    int     split_votes;            // Agents disagreed
    int     evolution_steps;        // Times agents were re-diversified
} SomaSwarmCtx;

// ============================================================
// API
// ============================================================

// Initialize swarm: diversify agent DNAs from base DNA.
void soma_swarm_init(SomaSwarmCtx *ctx,
                     const SomaDNA *base_dna,
                     ssm_f32 *work_buf,
                     int vocab_size);

// Cast votes: each agent samples from raw_logits with its own DNA.
// Returns consensus token + full vote breakdown.
SomaSwarmResult soma_swarm_vote(SomaSwarmCtx *ctx,
                                const ssm_f32 *raw_logits,
                                int vocab_size);

// After each inference turn: update agent fitness based on SMB confidence.
// Agents whose picks diverged from the final output get lower fitness.
void soma_swarm_update_fitness(SomaSwarmCtx *ctx,
                               const SomaSwarmResult *last_result,
                               float observed_quality);

// Re-diversify: nudge low-fitness agents toward best agent's DNA.
// Call periodically (e.g., every 16 turns).
void soma_swarm_evolve(SomaSwarmCtx *ctx);

// Print swarm stats
typedef void (*SomaSwarmPrintFn)(const char *msg);
void soma_swarm_print_stats(const SomaSwarmCtx *ctx, SomaSwarmPrintFn fn);

#ifdef __cplusplus
}
#endif
