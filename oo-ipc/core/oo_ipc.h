#pragma once
/*
 * oo-ipc — Inter-Process/Inter-Instance Communication
 * =====================================================
 * Bridges OO bare-metal instances with external agents.
 *
 * Three modes:
 *  1. LOCAL: shared physical memory between SMP cores (oo-multicore)
 *  2. SWARM: UDP-like protocol over NIC (oo-net, when available)
 *  3. BRIDGE: named pipe / shared file for Batterfyl/Syrin integration
 *
 * Bridge mode (mode 3) enables:
 *  - OO bare-metal ↔ Syrin Python (Batterfyl's Mamba router)
 *  - OO exports OO_HANDOFF.TXT as IPC channel
 *  - Syrin reads HANDOFF, sends back response via HANDOFF_RESP.TXT
 *  - Three-tier routing: Mamba fast → OO sovereign → Gemini cloud
 *
 * Message format (binary, 128 bytes):
 *  [4B magic] [4B channel] [4B seq] [4B flags]
 *  [8B sender_dna] [8B receiver_dna]
 *  [84B payload] [8B hmac_truncated]
 *
 * Security: all IPC messages signed with oo-crypto Poly1305
 */

#ifndef OO_IPC_H
#define OO_IPC_H

#include <stdint.h>

#define OO_IPC_MAGIC         0x4F4F4950u  /* "OOIP" */
#define OO_IPC_MSG_SIZE      128
#define OO_IPC_PAYLOAD_SIZE  84
#define OO_IPC_MAX_QUEUE     32

/* ── IPC modes ──────────────────────────────────────────────────────── */
typedef enum {
    OO_IPC_MODE_OFF      = 0,
    OO_IPC_MODE_LOCAL    = 1,  /* SMP shared memory */
    OO_IPC_MODE_SWARM    = 2,  /* UDP over NIC */
    OO_IPC_MODE_BRIDGE   = 3,  /* file-based (HANDOFF.TXT) */
} OoIpcMode;

/* ── Message flags ──────────────────────────────────────────────────── */
#define OO_IPC_FLAG_REQUEST   0x01
#define OO_IPC_FLAG_RESPONSE  0x02
#define OO_IPC_FLAG_URGENT    0x04
#define OO_IPC_FLAG_SIGNED    0x08
#define OO_IPC_FLAG_HALT_REQ  0x10  /* request halt decision from remote */

/* ── IPC message ────────────────────────────────────────────────────── */
typedef struct {
    uint32_t magic;
    uint32_t channel;
    uint32_t seq;
    uint32_t flags;
    uint64_t sender_dna;
    uint64_t receiver_dna;   /* 0 = broadcast */
    uint8_t  payload[OO_IPC_PAYLOAD_SIZE];
    uint8_t  hmac[8];        /* truncated Poly1305 */
} __attribute__((packed)) OoIpcMsg;

/* ── Engine context ─────────────────────────────────────────────────── */
typedef struct {
    int        enabled;
    OoIpcMode  mode;
    uint64_t   my_dna;

    /* queue */
    OoIpcMsg   rx_queue[OO_IPC_MAX_QUEUE];
    int        rx_head, rx_tail;
    OoIpcMsg   tx_queue[OO_IPC_MAX_QUEUE];
    int        tx_head, tx_tail;

    /* bridge mode: file paths */
    char       handoff_path[64];  /* "OO_HANDOFF.TXT" */
    char       response_path[64]; /* "OO_HANDOFF_RESP.TXT" */

    /* stats */
    uint32_t msgs_sent;
    uint32_t msgs_recv;
    uint32_t msgs_dropped;
    uint32_t auth_failures;
} OoIpcCtx;

/* ── API ────────────────────────────────────────────────────────────── */

void oo_ipc_init(OoIpcCtx *ctx, OoIpcMode mode, uint64_t my_dna);
int  oo_ipc_send(OoIpcCtx *ctx, const OoIpcMsg *msg);
int  oo_ipc_recv(OoIpcCtx *ctx, OoIpcMsg *out);
int  oo_ipc_poll(OoIpcCtx *ctx);  /* check for new messages, returns count */

/* Bridge helpers for Batterfyl/Syrin integration */
int  oo_ipc_bridge_write_query(OoIpcCtx *ctx, const char *query,
                                int halt_flag, void *efi_root);
int  oo_ipc_bridge_read_response(OoIpcCtx *ctx, char *resp_buf,
                                  int buf_size, void *efi_root);

void oo_ipc_print(const OoIpcCtx *ctx);

#endif /* OO_IPC_H */
