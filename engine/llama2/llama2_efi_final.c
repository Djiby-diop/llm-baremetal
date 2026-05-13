/* llama2_efi_final.c — OO kernel unity build master
 * Split into 5 modules. DO NOT add code here.
 * Compile: gcc llama2_efi_final.c (unchanged Makefile)
 *
 * Modules:
 *   soma_mind.c      — Policy, routing, halting engine (llmk_mind_*)
 *   soma_loader.c    — Model loader, OO state, REPL config
 *   soma_repl.c      — REPL loop, command handlers (/ssm_infer etc)
 *   soma_inference.c — Inference engine, all phases A-Z
 *   soma_boot.c      — efi_main, EFI entry, memory init
 */
// REPL V3 - Full Interactive Chat Loop
// Type "quit" or "exit" to stop

#include <efi.h>
#include <efilib.h>
#include <efinet.h>
#include <stdint.h>

// GOP types + GUID are provided by gnu-efi headers (efiprot.h / efilib.h).

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#include <immintrin.h>
#endif

// djiblas optimized matmul
// DJIBLAS_DISABLE_CPUID is set via CFLAGS (-DDJIBLAS_DISABLE_CPUID=1) to force SSE2 on QEMU/baremetal.
// Do NOT hardcode 0 here — it would override the Makefile flag.
#ifndef DJIBLAS_DISABLE_CPUID
#define DJIBLAS_DISABLE_CPUID 0
#endif
#include "djiblas.h"

// LLM-Kernel primitives (zones + sentinel + post-mortem log)
#include "llmk_zones.h"
#include "llmk_log.h"
#include "llmk_sentinel.h"

// LLM-OO runtime (organism-oriented entities)
#include "llmk_oo.h"

// Djibion meta-engine (laws + triangulation + intent gating)
#include "djibion-engine/core/djibion.h"
#include "diopion-engine/core/diopion.h"
#include "diagnostion-engine/core/diagnostion.h"
#include "memorion-engine/core/memorion.h"
#include "orchestrion-engine/core/orchestrion.h"
#include "calibrion-engine/core/calibrion.h"
#include "compatibilion-engine/core/compatibilion.h"
#include "evolvion-engine/core/evolvion.h"
#include "evolvion-engine/core/oo_driver_probe.h"
#include "ghost-engine/core/oo_net_packet.h"
#include "synaption-engine/core/synaption.h"
#include "conscience-engine/core/conscience.h"
#include "neuralfs-engine/core/neuralfs.h"
#include "ghost-engine/core/ghost.h"
#include "immunion-engine/core/immunion.h"
#include "dreamion-engine/core/dreamion.h"
#include "symbion-engine/core/symbion.h"
#include "collectivion-engine/core/collectivion.h"
#include "metabion-engine/core/metabion.h"
#include "cellion-engine/core/cellion.h"
#include "morphion-engine/core/morphion.h"
#include "pheromion-engine/core/pheromion.h"

// Phase 5: build-time metabolism profile defaults (generated; repl.cfg can override)
#include "metabion_profile.h"

// DjibMark - Omnipresent execution tracing
#include "djibmark.h"
#include "interface.h"      // Simple loading overlay (off by default)

// OO Network Stack (Ethernet + DHCP + UDP + BootSwarm)
#include "../network/oo_net_core.h"
#include "../network/oo_net_core.c"

// OO WiFi Driver (EFI WiFi2 Protocol + USB SNP fallback)
#include "../network/oo_net_wifi.h"
#include "../network/oo_net_wifi.c"

// OO Hardware Drivers (Phase Z)
#include "../drivers/oo_audio_hda.h"
#include "../drivers/oo_audio_hda.c"
#include "../drivers/oo_nvme.h"
#include "../drivers/oo_nvme.c"

// OO USB HID keyboard (Phase Z2)
#include "../drivers/oo_usb_hid.h"
#include "../drivers/oo_usb_hid.c"

// OO WiFi firmware loader (Phase Z3)
#include "../drivers/oo_wifi_fw.h"
#include "../drivers/oo_wifi_fw.c"

// OO Multicore SMP (UEFI MP Services)
#include "../../oo-multicore/core/oo_multicore.h"
#include "../../oo-multicore/core/oo_multicore.c"


// GGUF support
#include "gguf_loader.h"
#include "gguf_infer.h"

// OOSI v2 + OO inference bridge
#include "oosi_loader.h"
#include "oosi_infer.h"
#include "llmk_oo_infer.h"

// OOSI v3 — full standalone Mamba (all weights int8)
#include "../ssm/oosi_v3_loader.h"
#include "../ssm/oosi_v3_infer.h"
#include "../ssm/soma_router.h"
#include "../ssm/soma_dna.h"
#include "../ssm/soma_dual.h"
#include "../ssm/soma_smb.h"
#include "../ssm/soma_dream.h"
#include "../ssm/soma_meta.h"
#include "../ssm/soma_swarm.h"
#include "../ssm/soma_reflex.h"
#include "../ssm/soma_logic.h"
#include "../ssm/soma_memory.h"
#include "../ssm/oo_neuralfs2_persist.h"
#include "../ssm/oo_neuralfs2_persist.c"
#include "../ssm/soma_journal.h"
#include "../ssm/soma_cortex.h"
#include "../ssm/soma_export.h"
#include "../ssm/soma_warden.h"
#include "../ssm/soma_session.h"
#include "../ssm/soma_dna_persist.h"
#include "../ssm/soma_dna_sampler.h"
#include "../ssm/soma_spec.h"
#include "../ssm/soma_swarm_net.h"
#include "../ssm/oo_swarm_node.h"    /* Phase O: swarm node identity + state machine */
#include "../ssm/oo_swarm_sync.h"    /* Phase O: swarm sync protocol                 */
#include "../ssm/oo_quantum_rng.h"
#include "../ssm/oo_self_model.h"
#include "../ssm/oo_neuralfs2.h"
#include "../ssm/oo_shell.h"
#include "../ssm/soma_uart.h"

// Phase W: Natural Language → REPL Command Router
#include "../voice/oo_voice_router.h"
#include "../voice/oo_voice_router.c"

// Phase WW: Full Voice Pipeline (HDA capture → wakeword → NLP → TTS → HDA play)
// Headers only — implementations linked via REPL_OBJS (.o files)
#include "../voice/oo_wakeword.h"
#include "../voice/oo_voice_context.h"
#include "../voice/oo_persona.h"
#include "../voice/oo_tts_phoneme.h"
#include "../voice/oo_voice_desktop_bridge.h"
#include "../voice/oo_voice_nlp.h"
#include "../voice/oo_voice_state_writer.h"
#include "../voice/oo_voice_loop.h"

// Phase WD: HDA audio driver (headers only — .c linked via REPL_OBJS)
#include "../drivers/oo_audio_hda.h"

// Phase WI: IOAPIC + LAPIC (headers only — .c linked via REPL_OBJS)
#include "../drivers/oo_ioapic.h"

// Phase X: In-Situ Self-Training Engine (RAG + LoRA delta)
#include "../trainer/oo_insitu_train.h"
#include "../trainer/oo_insitu_train.c"

// Phase SM: SomaMind V1 — compact SSM + adaptive halting + tool-use
#include "../ssm/oo_somamind_v1.h"
#include "../ssm/oo_somamind_v1.c"

// Phase NB: OO Network Boot — HTTP model pull + oracle queries (GPT/Claude/Gemini)
#include "../network/oo_netboot.h"
#include "../network/oo_netboot.c"

// Phase 3: TLS abstraction layer (proxy mode / mbedTLS stub)
#include "../network/oo_tls.h"
#include "../network/oo_tls.c"

// Phase 3: DNS4 resolver (EFI_DNS4_PROTOCOL + static cache)
#include "../network/oo_dns.h"
#include "../network/oo_dns.c"

// Phase 4A: mbedTLS TCP4 transport glue + TLS stub
#include "../network/oo_mbedtls.h"
#include "../network/oo_mbedtls.c"// Phase 4D: DIOP custom model loader + inference bridge
#include "../models/oo_diop_model.h"
#include "../models/oo_diop_model.c"// Phase 5F: Model self-expansion engine
#ifndef LLMK_SKIP_EXPERIMENTAL_UNITY
#include "../models/oo_model_growth.h"
#include "../models/oo_model_growth.c"// Phase 5A: ExitBootServices + IDT + GDT (full CPU takeover)
#endif
#include "../kernel/oo_exit_boot.h"
#include "../kernel/oo_exit_boot.c"// Phase 5C: NVMe bare-metal PCI driver
#ifndef LLMK_SKIP_EXPERIMENTAL_UNITY
#include "../kernel/oo_nvme.h"
#include "../kernel/oo_nvme.c"// Phase 4E: Federation protocol — peer discovery + patch sharing
#endif
#include "../network/oo_federation.h"
#include "../network/oo_federation.c"// Phase 5B: MMU — 4-level page tables, higher-half kernel, huge pages
#include "../kernel/oo_mmu.h"
#include "../kernel/oo_mmu.c"

// Phase 5E: Cooperative scheduler (8 tasks, LAPIC preemption stub)
#include "../kernel/oo_scheduler.h"
#include "../kernel/oo_scheduler.c"

// Phase 5G: Self-coding engine (DIOP → C patches → D+ gate → apply)
#include "../self_improve/oo_self_coding.h"
#include "../self_improve/oo_self_coding.c"

// Phase 5H: GPU double-buffer (GOP back-buffer + VirtIO-GPU detection)
#include "../display/oo_gpu.h"
#include "../display/oo_gpu.c"

// Phase SI: OO Self-Improvement Engine — human-in-the-loop patch pipeline
#include "../self_improve/oo_self_improve.h"
#include "../self_improve/oo_self_improve.c"

// Phase 10A: WASM module loader + interpreter (bare-metal MVP)
#include "../wasm/oo_wasm.h"
#include "../wasm/oo_wasm.c"

// Phase 6A: PS/2 keyboard IRQ + 8259A PIC wiring (post-ExitBootServices)
#include "../kernel/oo_irq.h"
#include "../kernel/oo_irq.c"

// Phase 6B: CPU thermal monitoring via IA32_THERM_STATUS MSR
#include "../kernel/oo_thermal.h"
#include "../kernel/oo_thermal.c"

// Phase 6C: LoRA adapter self-improvement (closes the evolution loop)
#include "../self_improve/oo_lora.h"
#include "../self_improve/oo_lora.c"

// Phase 6E: Evolution-baremetal bridge (DNA-validated LoRA backward)
#include "../self_improve/oo_evolution_bridge.h"
#include "../self_improve/oo_evolution_bridge.c"

// Phase 6F: Organ bus — all biological organs wired to united_bus IPC
#include "../kernel/oo_organ_bus.h"
#include "../kernel/oo_organ_bus.c"

// Forward declarations for static helpers used before their definitions
static void ascii_to_char16(CHAR16 *dst, const char *src, int max_len);

typedef enum {
    LLMK_MODEL_FMT_UNKNOWN = 0,
    LLMK_MODEL_FMT_BIN = 1,
    LLMK_MODEL_FMT_GGUF = 2,
    LLMK_MODEL_FMT_OOSI3 = 3,  // OOSI v3: self-contained Mamba+HaltHead, magic "OOS3"
    LLMK_MODEL_FMT_OOSI2 = 4,  // OOSI v2: legacy OOSS magic
} LlmkModelFormat;

static LlmkModelFormat g_loaded_model_format = LLMK_MODEL_FMT_UNKNOWN;
static CHAR16 g_loaded_model_path16[160];
static volatile UINT32 g_loaded_model_path16_canary = 0xD1B1D1B1u;
static GgufSummary g_loaded_model_gguf;
static int g_loaded_model_gguf_valid = 0;

#define LLMK_MIND_RUNTIME_HALT_THRESHOLD 0.50f
static int g_mind_runtime_halt_enabled = 1;
static float g_mind_runtime_halt_threshold = LLMK_MIND_RUNTIME_HALT_THRESHOLD;
typedef enum {
    LLMK_MIND_HALT_APPLY_NEVER = 0,
    LLMK_MIND_HALT_APPLY_SAVED = 1,
    LLMK_MIND_HALT_APPLY_SAVED_IF_NEEDED = 2,
    LLMK_MIND_HALT_APPLY_SYNC = 3,
    LLMK_MIND_HALT_APPLY_SYNC_FORCE = 4,
} LlmkMindHaltApplyMode;

static int g_mind_runtime_halt_apply_seen = 0;
static int g_mind_runtime_halt_apply_changed_enabled = 0;
static int g_mind_runtime_halt_apply_changed_threshold = 0;
static LlmkMindHaltApplyMode g_mind_runtime_halt_apply_mode = LLMK_MIND_HALT_APPLY_NEVER;

typedef struct {
    float temperature;
    float top_p;
    float repetition_penalty;
    int max_tokens;
    int applied;
} LlmkAttachRoutePolicyState;

typedef struct {
    int temperature_milli;
    int top_p_milli;
    int repetition_penalty_milli;
    int max_tokens;
} LlmkAttachRoutePolicyConfig;

typedef struct {
    int active;
    int applied;
    int temperature_milli;
    int top_p_milli;
    int repetition_penalty_milli;
    int max_tokens;
} LlmkAttachRoutePolicyPreview;

typedef enum {
    LLMK_ATTACH_POLICY_APPLY_NEVER = 0,
    LLMK_ATTACH_POLICY_APPLY_SYNC = 1,
    LLMK_ATTACH_POLICY_APPLY_SYNC_FORCE = 2,
} LlmkAttachPolicyApplyMode;

static LlmkAttachRoutePolicyConfig g_attach_policy_external_cfg = { 780, 890, 1120, 144 };
static LlmkAttachRoutePolicyConfig g_attach_policy_dual_cfg = { 840, 920, 1080, 176 };
static int g_attach_policy_apply_seen = 0;
static int g_attach_policy_apply_changed_external = 0;
static int g_attach_policy_apply_changed_dual = 0;
static LlmkAttachPolicyApplyMode g_attach_policy_apply_mode = LLMK_ATTACH_POLICY_APPLY_NEVER;

typedef struct {
    char core_path[192];
    char core_kind[32];
    int core_requested;
    int core_active;
    char sidecar_path[192];
    char sidecar_kind[32];
    int sidecar_requested;
    int sidecar_active;
    char attach_path[192];
    char attach_kind[32];
    char attach_format[16];
    int attach_requested;
    int attach_active;
    char attach_last_validation[96];
    UINT32 sidecar_version;
    UINT32 sidecar_d_model;
    UINT32 sidecar_n_layer;
    UINT32 sidecar_d_state;
    UINT32 sidecar_d_conv;
    UINT32 sidecar_expand;
    UINT32 sidecar_vocab_size;
    UINT32 sidecar_halting_d_input;
    int sidecar_header_valid;
    char sidecar_last_validation[96];
} LlmkMindRuntimeState;

static LlmkMindRuntimeState g_mind_runtime_state;
static void *g_mind_sidecar_blob = NULL;
static UINTN g_mind_sidecar_blob_len = 0;

typedef struct {
    int cfg_enabled;
    float cfg_threshold;
    int found_enabled;
    int found_threshold;
    int found_any;
    int in_sync;
    EFI_STATUS cfg_st;
    int core_ready;
    int halt_ready;
    int sidecar_ready;
    int ready;
    int bootstrap_can_help;
} LlmkMindRuntimeSnapshot;

typedef struct {
    UINT32 magic;
    UINT32 version;
    UINT32 d_model;
    UINT32 n_layer;
    UINT32 d_state;
    UINT32 d_conv;
    UINT32 expand;
    UINT32 vocab_size;
    UINT32 halting_head_d_input;
} LlmkOoSidecarHeader;

typedef struct {
    const float *layer0_weight;
    const float *layer0_bias;
    const float *layer2_weight;
    const float *layer2_bias;
    const float *layer4_weight;
    const float *layer4_bias;
    UINT32 d_input;
    UINT32 hidden0;
    UINT32 hidden1;
    UINT32 d_output;
    int ready;
} LlmkOoHaltingHeadView;

#define LLMK_OOSS_MAGIC 0x4F4F5353u
static LlmkOoHaltingHeadView g_mind_halting_view;

// Forward decl used by early GGUF summary printer.
static void llmk_print_ascii(const char *s);
float fast_exp(float x);

static void llmk_copy_ascii_bounded(char *dst, int dst_cap, const char *src) {
    if (!dst || dst_cap <= 0) return;
    int i = 0;
    if (src) {
        while (src[i] && i + 1 < dst_cap) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}


/* ── Unity includes (compiled as one translation unit) ───────────── */
#include "limbion-engine/core/limbion.h"
#include "chronion-engine/core/chronion.h"
#include "trophion-engine/core/trophion.h"
#include "mirrorion-engine/core/mirrorion.h"
#include "thanatosion-engine/core/thanatosion.h"
/* tentative forward definition — soma_loader.c provides the actual definition */
static SomaDNA g_soma_dna;
/* Phase 10A: WASM singleton — loaded module state */
static OoWasmMod g_wasm_mod;
#include "soma_mind.c"
#include "soma_loader.c"
#include "soma_repl.c"
#include "soma_inference.c"
#include "orchestrion_ci.c"
#include "soma_distill.c"
#include "soma_vitals.c"
#include "soma_boot.c"
