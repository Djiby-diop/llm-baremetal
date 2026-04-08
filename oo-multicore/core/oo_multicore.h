#pragma once
/*
 * oo-multicore — SMP Bare-Metal for OO
 * =====================================
 * x86-64 UEFI starts on a single BSP (Boot Strap Processor).
 * Other CPUs (APs) are parked. OO can wake them for parallel inference.
 *
 * Architecture:
 *  - BSP: runs REPL, Hermes bus, D+ policy, main inference
 *  - AP0: runs RATIONAL splitbrain instance
 *  - AP1: runs CREATIVE splitbrain instance
 *  - AP2: runs dreamion background tasks
 *  - AP3+: future swarm instances
 *
 * Synchronization: ticketlock (no stdlib, no OS spinlock)
 * Communication: shared arena regions + Hermes-like mailbox
 *
 * OO multicore is NOT like a thread scheduler.
 * Each AP runs a fixed role. No preemption. No context switches.
 * "Organic parallelism" — each core has a dedicated function.
 *
 * NOVEL: No AI system has used SMP to run split-brain instances
 * on separate physical cores, sharing weights read-only.
 */

#ifndef OO_MULTICORE_H
#define OO_MULTICORE_H

#include <stdint.h>

#define OO_MAX_CORES          16
#define OO_CORE_STACK_SIZE    (64 * 1024)  /* 64KB per AP */
#define OO_CORE_MAILBOX_SIZE  16           /* messages per core */

/* ── Core roles ────────────────────────────────────────────────────── */
typedef enum {
    OO_CORE_ROLE_BSP        = 0,  /* bootstrap processor — REPL + bus */
    OO_CORE_ROLE_RATIONAL   = 1,  /* splitbrain RATIONAL instance */
    OO_CORE_ROLE_CREATIVE   = 2,  /* splitbrain CREATIVE instance */
    OO_CORE_ROLE_DREAM      = 3,  /* dreamion background */
    OO_CORE_ROLE_SENTINEL   = 4,  /* pressure + watchdog monitor */
    OO_CORE_ROLE_IDLE       = 5,  /* parked, available */
} OoCoreRole;

/* ── Core state ────────────────────────────────────────────────────── */
typedef enum {
    OO_CORE_STATE_PARKED    = 0,
    OO_CORE_STATE_STARTING  = 1,
    OO_CORE_STATE_RUNNING   = 2,
    OO_CORE_STATE_HALTED    = 3,
    OO_CORE_STATE_ERROR     = 4,
} OoCoreState;

/* ── Mailbox message ──────────────────────────────────────────────── */
typedef struct {
    uint32_t  channel;    /* Hermes channel ID */
    uint64_t  payload[4]; /* 32 bytes of data */
    int       consumed;
} OoCoreMsg;

/* ── Per-core descriptor ──────────────────────────────────────────── */
typedef struct {
    uint32_t      apic_id;
    OoCoreRole    role;
    OoCoreState   state;
    uint64_t      stack_base;
    uint64_t      steps_executed;
    uint64_t      tokens_generated;
    /* mailbox ring */
    OoCoreMsg     mailbox[OO_CORE_MAILBOX_SIZE];
    int           mailbox_head;
    int           mailbox_tail;
    /* ticketlock for mailbox */
    volatile uint32_t ticket_now;
    volatile uint32_t ticket_next;
} OoCoreDescriptor;

/* ── Multicore context ────────────────────────────────────────────── */
typedef struct {
    int              enabled;
    int              core_count;       /* detected physical cores */
    int              active_aps;       /* woken APs */
    OoCoreDescriptor cores[OO_MAX_CORES];
    /* shared read-only weight base (BSP allocates, APs read) */
    uint64_t         shared_weights_base;
    uint64_t         shared_weights_size;
} OoMulticoreCtx;

/* ── API ───────────────────────────────────────────────────────────── */

/**
 * oo_multicore_init() — detect cores via ACPI MADT, init descriptors
 * Must be called after memory arenas are ready.
 */
int oo_multicore_init(OoMulticoreCtx *ctx);

/**
 * oo_multicore_wake_ap() — wake a parked AP and assign it a role
 * entry_fn: function pointer the AP will execute (must be freestanding)
 */
int oo_multicore_wake_ap(OoMulticoreCtx *ctx, int core_idx,
                          OoCoreRole role, void (*entry_fn)(void));

/**
 * oo_multicore_send() — send message to a core's mailbox
 */
int oo_multicore_send(OoMulticoreCtx *ctx, int core_idx,
                       uint32_t channel, const uint64_t payload[4]);

/**
 * oo_multicore_recv() — read next message from my mailbox (caller = this core)
 */
int oo_multicore_recv(OoMulticoreCtx *ctx, int my_core_idx, OoCoreMsg *out);

/**
 * oo_multicore_set_weights() — set shared weight region (BSP only)
 */
void oo_multicore_set_weights(OoMulticoreCtx *ctx,
                               uint64_t base, uint64_t size);

/**
 * oo_multicore_halt_ap() — park an AP back to IDLE
 */
void oo_multicore_halt_ap(OoMulticoreCtx *ctx, int core_idx);

/**
 * oo_multicore_print() — debug status
 */
void oo_multicore_print(const OoMulticoreCtx *ctx);

#endif /* OO_MULTICORE_H */
