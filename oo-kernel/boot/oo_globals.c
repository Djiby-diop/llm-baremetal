/*
 * oo_globals.c — Global singleton instances for OO subsystems.
 *
 * These are the one-and-only definitions of global state objects that are
 * declared as `extern` in cmd_*.c and repl_ctx.c throughout the codebase.
 * Centralizing them here avoids ODR violations and makes lifetime clear.
 */

#include <efi.h>
#include <efilib.h>

#include "oo-kernel/hal/hw_dna.h"
#include "oo-engine/blas/hebbian.h"
#include "oo-engine/splitbrain/splitbrain.h"
#include "oo-engine/wasm/oo_wasm.h"
#include "oo-warden/sentinel/pressure.h"
#include "oo-engine/ssm/oo_somamind_v1.h"

/* Hardware DNA fingerprint — computed once at boot by hw_dna_verify() */
HwDna g_hw_dna = {0};

/* Hebbian learning table — updated by inference loop */
HebbianTable g_hebbian = {0};

/* Split-brain dual inference context */
SplitbrainCtx g_splitbrain = {0};

/* WASM sandbox state */
OoWasmSandbox g_wasm_sandbox = {0};
OoWasmMod g_wasm_mod = {0};

/* Memory pressure signal — updated by pressure_update() */
OoPressureSignal g_pressure = {0};

/* SomaMind V1 cognitive bridge — Phase SM */
SomaMindV1 g_somamind = {0};
