/* soma_modules.h — God File Module Map
 *
 * OO Kernel C source is split into 5 modules compiled as a unity build.
 * All modules share the preamble (includes/globals) from llama2_efi_final.c
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  llama2_efi_final.c  (unity master — 269 lines)                 │
 * │   preamble: includes + typedefs + global vars (L1-251)          │
 * │   #include "soma_mind.c"                                        │
 * │   #include "soma_loader.c"                                      │
 * │   #include "soma_repl.c"                                        │
 * │   #include "soma_inference.c"                                   │
 * │   #include "soma_boot.c"                                        │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Module responsibilities:
 *
 *  soma_mind.c (2428 lines)
 *    - llmk_mind_* family: policy state machine, routing decisions
 *    - Halting engine: sigmoid, halting_eval, halting_decide, halting_sweep
 *    - Sidecar/attach/backbone binding (core model management)
 *    - Audit printers: print_doctor, print_status, print_diag
 *    - D+ halt policy: persist/load/apply (file: HALT_POLICY.BIN)
 *    - Bootstrap: llmk_mind_bootstrap_v1()
 *
 *  soma_loader.c (5410 lines)
 *    - REPL config loaders: diopion, djibion, djiblas, snap, boot, engines
 *    - OO state: llmk_oo_load_state, llmk_oo_load_recovery
 *    - KV config: llmk_repl_cfg_set_kv, llmk_repl_cfg_read_ctx_seq
 *    - Model format detection and loading setup
 *
 *  soma_repl.c (4494 lines)
 *    - llmk_repl_no_model_loop(): REPL when no model loaded
 *    - All /cmd handlers: /ssm_load, /ssm_infer, /soma_status, etc.
 *    - Command help table (g_llmk_cmd_help[])
 *    - llmk_cmd_matches_filter(), llmk_cmd_common_prefix_len()
 *    - Full interactive REPL with command dispatch
 *
 *  soma_inference.c (7484 lines)
 *    - llmk_infermini_*: LCG, randf, fill, token mapping (fast path)
 *    - SomaMind routing: reflex/internal/external dispatch
 *    - All inference phases A-Z (SSM, GGUF, OO-native, dual-core)
 *    - Speculative decoding, DNA sampling, swarm coordination
 *    - Per-token halt head evaluation during generation
 *
 *  soma_boot.c (7560 lines)
 *    - efi_main(): full EFI entry, memory init (26 zones), GPU setup
 *    - Phase A-Z boot sequence execution
 *    - QEMU simulation path + hardware detection
 *    - Diagnostics, benchmarks, self-test
 *
 * Build (unchanged from before):
 *   gcc ... llama2_efi_final.c -o llmk.efi
 *
 * To edit a specific subsystem:
 *   vim engine/llama2/soma_mind.c      # policy/halting
 *   vim engine/llama2/soma_repl.c      # REPL/commands
 *   vim engine/llama2/soma_inference.c # inference loop
 *   vim engine/llama2/soma_boot.c      # boot/efi_main
 *   vim engine/llama2/soma_loader.c    # loaders/config
 */
#pragma once
