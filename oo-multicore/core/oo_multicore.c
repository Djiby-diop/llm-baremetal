/*
 * oo-multicore — SMP Bare-Metal for OO
 * Implémentation basée sur EFI_MP_SERVICES_PROTOCOL
 */

#include "oo_multicore.h"

#ifdef UEFI_BUILD
#include <efi.h>
#include <efilib.h>
#else
#include "efi_compat.h"
#endif

// Définition du protocole MP_SERVICES
#define EFI_MP_SERVICES_PROTOCOL_GUID \
    { 0x3fdda605, 0xa76e, 0x4f46, { 0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08 } }

typedef struct _EFI_MP_SERVICES_PROTOCOL EFI_MP_SERVICES_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_GET_NUMBER_OF_PROCESSORS)(
    EFI_MP_SERVICES_PROTOCOL  *This,
    UINTN                     *NumberOfProcessors,
    UINTN                     *NumberOfEnabledProcessors
);

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_GET_PROCESSOR_INFO)(
    EFI_MP_SERVICES_PROTOCOL  *This,
    UINTN                     ProcessorNumber,
    void                      *ProcessorInfoBuffer
);

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_STARTUP_ALL_APS)(
    EFI_MP_SERVICES_PROTOCOL  *This,
    void                      *Procedure,
    BOOLEAN                   SingleThread,
    EFI_EVENT                 WaitEvent,
    UINTN                     TimeoutInMicroSeconds,
    void                      *ProcedureArgument,
    UINTN                     **FailedCPUList
);

typedef EFI_STATUS (EFIAPI *EFI_MP_SERVICES_STARTUP_THIS_AP)(
    EFI_MP_SERVICES_PROTOCOL  *This,
    void                      *Procedure,
    UINTN                     ProcessorNumber,
    EFI_EVENT                 WaitEvent,
    UINTN                     TimeoutInMicroseconds,
    void                      *ProcedureArgument,
    BOOLEAN                   *Finished
);

struct _EFI_MP_SERVICES_PROTOCOL {
    EFI_MP_SERVICES_GET_NUMBER_OF_PROCESSORS GetNumberOfProcessors;
    EFI_MP_SERVICES_GET_PROCESSOR_INFO       GetProcessorInfo;
    EFI_MP_SERVICES_STARTUP_ALL_APS          StartupAllAPs;
    EFI_MP_SERVICES_STARTUP_THIS_AP          StartupThisAP;
    void                                     *SwitchBSP;
    void                                     *EnableDisableAP;
    void                                     *WhoAmI;
};

static EFI_MP_SERVICES_PROTOCOL *g_mp_services = NULL;
static OoMulticoreCtx *g_oo_mc_ctx = NULL;

/* ── Ticketlock Helper (Baremetal Atomics) ───────────────────────── */
static inline uint32_t atomic_fetch_add(volatile uint32_t *ptr, uint32_t val) {
    uint32_t ret;
    __asm__ __volatile__ (
        "lock xaddl %0, %1"
        : "=r" (ret), "+m" (*ptr)
        : "0" (val)
        : "memory", "cc"
    );
    return ret;
}

static void lock_mailbox(OoCoreDescriptor *core) {
    uint32_t ticket = atomic_fetch_add(&core->ticket_next, 1);
    while (core->ticket_now != ticket) {
        __asm__ __volatile__("pause" ::: "memory");
    }
}

static void unlock_mailbox(OoCoreDescriptor *core) {
    atomic_fetch_add(&core->ticket_now, 1);
}

/* ── AP Wrapper ──────────────────────────────────────────────────── */
typedef struct {
    int core_idx;
    void (*entry_fn)(void);
} ApStartArg;

static void EFIAPI ap_wrapper_entry(void *Arg) {
    ApStartArg *ap_arg = (ApStartArg *)Arg;
    if (!ap_arg || !g_oo_mc_ctx) return;
    
    int idx = ap_arg->core_idx;
    OoCoreDescriptor *core = &g_oo_mc_ctx->cores[idx];
    
    core->state = OO_CORE_STATE_RUNNING;
    
    /* On exécute la payload de l'Organisme sur ce cœur */
    if (ap_arg->entry_fn) {
        ap_arg->entry_fn();
    }
    
    core->state = OO_CORE_STATE_HALTED;
}

/* ── API Implementation ──────────────────────────────────────────── */

int oo_multicore_init(OoMulticoreCtx *ctx) {
    if (!ctx) return 0;
    
    /* Zéroter le contexte */
    for (int i = 0; i < sizeof(*ctx); i++) ((uint8_t*)ctx)[i] = 0;
    
    EFI_GUID mp_guid = EFI_MP_SERVICES_PROTOCOL_GUID;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateProtocol, 3, &mp_guid, NULL, (void **)&g_mp_services);
    
    if (EFI_ERROR(st) || !g_mp_services) {
        Print(L"[SMP] MP Services introuvable. Mode Single-Core forcé.\r\n");
        ctx->enabled = 0;
        ctx->core_count = 1; /* Le BSP */
        ctx->cores[0].role = OO_CORE_ROLE_BSP;
        ctx->cores[0].state = OO_CORE_STATE_RUNNING;
        return 0;
    }
    
    UINTN num_cores = 0, num_enabled = 0;
    st = uefi_call_wrapper(g_mp_services->GetNumberOfProcessors, 3, g_mp_services, &num_cores, &num_enabled);
    if (EFI_ERROR(st)) return 0;
    
    ctx->enabled = 1;
    ctx->core_count = (int)num_cores;
    if (ctx->core_count > OO_MAX_CORES) ctx->core_count = OO_MAX_CORES;
    
    Print(L"[SMP] MP Services initialisé. CPU cores: %d (enabled: %d)\r\n", (int)num_cores, (int)num_enabled);
    
    for (int i = 0; i < ctx->core_count; i++) {
        ctx->cores[i].role = OO_CORE_ROLE_IDLE;
        ctx->cores[i].state = OO_CORE_STATE_PARKED;
        ctx->cores[i].ticket_now = 0;
        ctx->cores[i].ticket_next = 0;
        ctx->cores[i].mailbox_head = 0;
        ctx->cores[i].mailbox_tail = 0;
    }
    
    /* Identifier le BSP */
    UINTN my_apic = 0; // En vrai, récupéré par WhoAmI
    st = uefi_call_wrapper(g_mp_services->WhoAmI, 2, g_mp_services, &my_apic);
    if (!EFI_ERROR(st)) {
        ctx->cores[my_apic].role = OO_CORE_ROLE_BSP;
        ctx->cores[my_apic].state = OO_CORE_STATE_RUNNING;
    }
    
    g_oo_mc_ctx = ctx;
    return 1;
}

int oo_multicore_wake_ap(OoMulticoreCtx *ctx, int core_idx, OoCoreRole role, void (*entry_fn)(void)) {
    if (!ctx || !ctx->enabled || !g_mp_services) return 0;
    if (core_idx < 0 || core_idx >= ctx->core_count) return 0;
    
    OoCoreDescriptor *core = &ctx->cores[core_idx];
    if (core->state != OO_CORE_STATE_PARKED && core->state != OO_CORE_STATE_HALTED) {
        return 0; /* Déjà actif */
    }
    
    core->role = role;
    core->state = OO_CORE_STATE_STARTING;
    
    // On alloue l'argument via le BSP
    ApStartArg *arg = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, sizeof(ApStartArg), (void**)&arg);
    if (EFI_ERROR(st) || !arg) return 0;
    
    arg->core_idx = core_idx;
    arg->entry_fn = entry_fn;
    
    /* StartupThisAP en asynchrone (WaitEvent=NULL) */
    st = uefi_call_wrapper(g_mp_services->StartupThisAP, 7, 
        g_mp_services, 
        ap_wrapper_entry, 
        core_idx, 
        NULL, /* no wait event */
        0,    /* timeout infinity */
        arg, 
        NULL);
        
    if (EFI_ERROR(st)) {
        core->state = OO_CORE_STATE_ERROR;
        uefi_call_wrapper(BS->FreePool, 1, arg);
        return 0;
    }
    
    ctx->active_aps++;
    return 1;
}

int oo_multicore_send(OoMulticoreCtx *ctx, int core_idx, uint32_t channel, const uint64_t payload[4]) {
    if (!ctx || core_idx < 0 || core_idx >= ctx->core_count) return 0;
    
    OoCoreDescriptor *core = &ctx->cores[core_idx];
    lock_mailbox(core);
    
    int next_head = (core->mailbox_head + 1) % OO_CORE_MAILBOX_SIZE;
    if (next_head == core->mailbox_tail) {
        unlock_mailbox(core);
        return 0; /* Queue pleine */
    }
    
    OoCoreMsg *msg = &core->mailbox[core->mailbox_head];
    msg->channel = channel;
    msg->consumed = 0;
    for (int i=0; i<4; i++) msg->payload[i] = payload[i];
    
    core->mailbox_head = next_head;
    
    unlock_mailbox(core);
    return 1;
}

int oo_multicore_recv(OoMulticoreCtx *ctx, int my_core_idx, OoCoreMsg *out) {
    if (!ctx || !out || my_core_idx < 0 || my_core_idx >= ctx->core_count) return 0;
    
    OoCoreDescriptor *core = &ctx->cores[my_core_idx];
    lock_mailbox(core);
    
    if (core->mailbox_head == core->mailbox_tail) {
        unlock_mailbox(core);
        return 0; /* Queue vide */
    }
    
    OoCoreMsg *msg = &core->mailbox[core->mailbox_tail];
    out->channel = msg->channel;
    out->consumed = 1;
    for (int i=0; i<4; i++) out->payload[i] = msg->payload[i];
    
    core->mailbox_tail = (core->mailbox_tail + 1) % OO_CORE_MAILBOX_SIZE;
    
    unlock_mailbox(core);
    return 1;
}

void oo_multicore_set_weights(OoMulticoreCtx *ctx, uint64_t base, uint64_t size) {
    if (ctx) {
        ctx->shared_weights_base = base;
        ctx->shared_weights_size = size;
    }
}

void oo_multicore_halt_ap(OoMulticoreCtx *ctx, int core_idx) {
    if (!ctx || core_idx < 0 || core_idx >= ctx->core_count) return;
    OoCoreDescriptor *core = &ctx->cores[core_idx];
    if (core->role != OO_CORE_ROLE_BSP) {
        core->state = OO_CORE_STATE_HALTED;
        core->role = OO_CORE_ROLE_IDLE;
    }
}

void oo_multicore_print(const OoMulticoreCtx *ctx) {
    if (!ctx) return;
    Print(L"\r\n[SMP] Multicore Status:\r\n");
    Print(L"  Enabled: %d | Cores: %d | Woken APs: %d\r\n", ctx->enabled, ctx->core_count, ctx->active_aps);
    Print(L"  Shared Weights: 0x%lx (%d MB)\r\n", ctx->shared_weights_base, (int)(ctx->shared_weights_size / (1024*1024)));
    
    for (int i = 0; i < ctx->core_count; i++) {
        const OoCoreDescriptor *c = &ctx->cores[i];
        const CHAR16 *r = L"IDLE";
        switch(c->role) {
            case OO_CORE_ROLE_BSP: r = L"BSP"; break;
            case OO_CORE_ROLE_RATIONAL: r = L"RATIONAL"; break;
            case OO_CORE_ROLE_CREATIVE: r = L"CREATIVE"; break;
            case OO_CORE_ROLE_DREAM: r = L"DREAM"; break;
            case OO_CORE_ROLE_DISTILL: r = L"DISTILL"; break;
            case OO_CORE_ROLE_SENTINEL: r = L"SENTINEL"; break;
            default: break;
        }
        
        const CHAR16 *s = L"???";
        switch(c->state) {
            case OO_CORE_STATE_PARKED: s = L"PARKED"; break;
            case OO_CORE_STATE_STARTING: s = L"STARTING"; break;
            case OO_CORE_STATE_RUNNING: s = L"RUNNING"; break;
            case OO_CORE_STATE_HALTED: s = L"HALTED"; break;
            case OO_CORE_STATE_ERROR: s = L"ERROR"; break;
        }
        
        Print(L"  Core %d: [%s] -> %s  (Steps: %lu)\r\n", i, r, s, c->steps_executed);
    }
    Print(L"\r\n");
}
