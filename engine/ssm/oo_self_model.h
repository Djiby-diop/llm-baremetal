// oo_self_model.h — OO Self-Model: runtime introspection context (Phase E)
//
// OO continuously monitors its own execution state and generates a compact
// self-description string that can be prepended to the inference system prefix.
// This lets the LLM "know" its hardware situation during generation.
//
// State captured:
//   - Free RAM per arena (WEIGHTS/KV/SCRATCH/ZONE_C) in MB
//   - CPU thermal estimate from ConscienceEngine
//   - Tokens/sec from MetabionEngine
//   - Warden pressure level (0=idle 1=low 2=high 3=critical)
//   - DNA generation count
//   - Active engine count (how many are in non-IDLE mode)
//   - RDRAND availability + quantum seed count
//   - Boot time (tokens generated so far this session)
//
// Freestanding C11 — no libc, no malloc. Header-only (inline/static).

#pragma once

#include "../../core/llmk_zones.h"
#include "conscience-engine/core/conscience.h"
#include "metabion-engine/core/metabion.h"
#include "../ssm/soma_warden.h"
#include "../ssm/soma_dna.h"
#include "oo_quantum_rng.h"

// ============================================================
// OoSelfModel — snapshot of OO's own state
// ============================================================

typedef struct {
    // Memory (MB)
    unsigned int free_weights_mb;
    unsigned int free_kv_mb;
    unsigned int free_scratch_mb;
    unsigned int free_zone_c_mb;

    // Performance
    unsigned int tokens_per_sec;     // from metabion.last.tokens_per_sec
    unsigned int total_tokens_gen;   // tokens generated this boot

    // Thermal / health
    int      cpu_temp_celsius;       // from conscience (0 if unknown)
    int      warden_pressure;        // 0..3

    // Cognitive state
    unsigned int dna_generation;     // from SomaDNA.generation
    unsigned int active_engines;     // engines NOT in mode==0 (idle)

    // Entropy
    int      rdrand_ok;
    unsigned int quantum_seeds;

    // Context prefix built from above (max 256 bytes, null-terminated)
    char     prefix[256];
    int      prefix_valid;
} OoSelfModel;

// ============================================================
// Helpers
// ============================================================

static inline unsigned int _osm_mb(UINT64 bytes) {
    return (unsigned int)(bytes >> 20);
}

static inline void _osm_append(char *buf, int cap, int *pos, const char *s) {
    while (*s && *pos < cap - 1) buf[(*pos)++] = *s++;
    buf[*pos] = '\0';
}

static inline void _osm_append_uint(char *buf, int cap, int *pos, unsigned int v) {
    char tmp[12]; int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    else { while (v) { tmp[n++] = '0' + (v % 10); v /= 10; } }
    for (int i = n-1; i >= 0 && *pos < cap-1; i--) buf[(*pos)++] = tmp[i];
    buf[*pos] = '\0';
}

// ============================================================
// oo_self_model_update — capture current state into model
//
// Parameters come from global engine structs already in scope.
// Pass NULL for unavailable sources (they'll be zeroed).
// ============================================================

static inline void oo_self_model_update(
    OoSelfModel             *sm,
    const LlmkZones         *zones,
    const ConscienceEngine  *conscience,
    const MetabionEngine    *metabion,
    const SomaWardenCtx     *warden,
    const SomaDNA           *dna,
    unsigned int             total_tokens_gen)
{
    if (!sm) return;

    // Memory
    if (zones) {
        sm->free_weights_mb  = _osm_mb(llmk_arena_remaining_bytes(zones, LLMK_ARENA_WEIGHTS));
        sm->free_kv_mb       = _osm_mb(llmk_arena_remaining_bytes(zones, LLMK_ARENA_KV_CACHE));
        sm->free_scratch_mb  = _osm_mb(llmk_arena_remaining_bytes(zones, LLMK_ARENA_SCRATCH));
        sm->free_zone_c_mb   = _osm_mb(llmk_arena_remaining_bytes(zones, LLMK_ARENA_ZONE_C));
    }

    // Performance
    if (metabion && metabion->samples_count > 0)
        sm->tokens_per_sec = (unsigned int)metabion->last.tokens_per_sec;
    sm->total_tokens_gen = total_tokens_gen;

    // Thermal
    if (conscience && conscience->samples_taken > 0)
        sm->cpu_temp_celsius = (int)conscience->last.temp_celsius;

    // Safety
    if (warden)
        sm->warden_pressure = warden->pressure_level;

    // DNA
    if (dna)
        sm->dna_generation = (unsigned int)dna->generation;

    // Entropy
    sm->rdrand_ok      = (g_quantum_rng.rdrand_available == 1);
    sm->quantum_seeds  = g_quantum_rng.quantum_seeds;

    sm->prefix_valid = 0;
}

// ============================================================
// oo_self_model_to_prefix — build compact ASCII self-description
//
// Format (fits in 256 bytes):
//   [OO mem=Ww/Kk/Ss/Cc MB tok/s=T temp=X°C warden=N dna=G]
//
// Returns pointer to sm->prefix (always null-terminated).
// ============================================================

static inline const char *oo_self_model_to_prefix(OoSelfModel *sm) {
    if (!sm) return "";
    if (sm->prefix_valid) return sm->prefix;

    char *buf = sm->prefix;
    int   cap = 255;
    int   pos = 0;

    _osm_append(buf, cap, &pos, "[OO mem=");
    _osm_append_uint(buf, cap, &pos, sm->free_weights_mb);
    _osm_append(buf, cap, &pos, "/");
    _osm_append_uint(buf, cap, &pos, sm->free_kv_mb);
    _osm_append(buf, cap, &pos, "/");
    _osm_append_uint(buf, cap, &pos, sm->free_scratch_mb);
    _osm_append(buf, cap, &pos, " MB tok/s=");
    _osm_append_uint(buf, cap, &pos, sm->tokens_per_sec);
    if (sm->cpu_temp_celsius > 0) {
        _osm_append(buf, cap, &pos, " temp=");
        _osm_append_uint(buf, cap, &pos, (unsigned int)sm->cpu_temp_celsius);
        _osm_append(buf, cap, &pos, "C");
    }
    _osm_append(buf, cap, &pos, " warden=");
    buf[pos++] = '0' + (sm->warden_pressure & 3);
    _osm_append(buf, cap, &pos, " dna=");
    _osm_append_uint(buf, cap, &pos, sm->dna_generation);
    _osm_append(buf, cap, &pos, " rng=");
    _osm_append(buf, cap, &pos, sm->rdrand_ok ? "hw" : "sw");
    _osm_append(buf, cap, &pos, " tok=");
    _osm_append_uint(buf, cap, &pos, sm->total_tokens_gen);
    _osm_append(buf, cap, &pos, "] ");

    buf[pos] = '\0';
    sm->prefix_valid = 1;
    return buf;
}

// ============================================================
// oo_self_model_print — REPL diagnostic display (CHAR16 via Print)
// ============================================================

static inline void oo_self_model_print(const OoSelfModel *sm) {
    if (!sm) return;
    Print(L"\r\n[OO-SelfModel] Runtime Introspection\r\n");
    Print(L"  Memory free  : weights=%uMB  kv=%uMB  scratch=%uMB  zone_c=%uMB\r\n",
          sm->free_weights_mb, sm->free_kv_mb, sm->free_scratch_mb, sm->free_zone_c_mb);
    Print(L"  Performance  : tok/s=%u  total_tok=%u\r\n",
          sm->tokens_per_sec, sm->total_tokens_gen);
    Print(L"  Thermal      : cpu_temp=%d C\r\n", sm->cpu_temp_celsius);
    Print(L"  Warden       : pressure=%d  (0=idle 1=low 2=high 3=critical)\r\n",
          sm->warden_pressure);
    Print(L"  DNA          : generation=%u\r\n", sm->dna_generation);
    Print(L"  Entropy      : rdrand=%s  quantum_seeds=%u\r\n",
          sm->rdrand_ok ? L"hw" : L"sw", sm->quantum_seeds);
    Print(L"  Prefix       : ");
    if (sm->prefix_valid) {
        CHAR16 wbuf[256]; // SAFE: bounded buffer for self-model prefix display (prefix is max 256 bytes)
        int i = 0;
        while (sm->prefix[i] && i < 255) {
            wbuf[i] = (CHAR16)(unsigned char)sm->prefix[i];
            i++;
        }
        wbuf[i] = 0;
        Print(L"%s", wbuf);
    } else {
        Print(L"(call oo_self_model_to_prefix first)");
    }
    Print(L"\r\n\r\n");
}
