#include "soma_distill.h"
#include "dreamion-engine/core/dreamion.h"
#include <efi.h>
#include <efilib.h>

extern DreamionEngine g_dreamion;
static SomaDistill    g_distill_global;

void distill_init(SomaDistill *d) {
    if (!d) return;
    for (int i = 0; i < SOMA_ADAPTER_SIZE; i++) d->weights[i] = 0.0f;
    d->learning_rate = 0.001f;
    d->total_steps = 0;
    d->active = 1;
}

/* Apply learned biases to logits during inference */
void distill_apply(SomaDistill *d, float *logits, int vocab_size) {
    if (!d || !d->active) return;
    for (int i = 0; i < vocab_size; i++) {
        logits[i] += d->weights[i % SOMA_ADAPTER_SIZE] * 0.1f;
    }
}

/* Background worker for AP4 (DISTILL core) */
void ap_distill_worker(void) {
    distill_init(&g_distill_global);
    
    while (1) {
        /* Self-training loop: consume dreams from Dreamion */
        if (g_distill_global.active && g_dreamion.memory_count > 4) {
            /* Pick a random memory from the ring buffer */
            static uint32_t seed = 0x1337BEEF;
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            int idx = seed % g_dreamion.memory_count;
            
            DreamionMemory *m = &g_dreamion.memory[idx];
            if (m->valid && m->output_len > 0) {
                /* Apply a tiny gradient step based on dream quality and confidence */
                float quality = m->confidence * (1.0f - m->halt_prob);
                float lr = g_distill_global.learning_rate * quality;
                
                for (int i = 0; i < m->output_len; i++) {
                    uint16_t tid = m->output_tokens[i];
                    /* Update adapter weights (hash-based mapping to small space) */
                    g_distill_global.weights[tid % SOMA_ADAPTER_SIZE] += lr;
                }
                g_distill_global.total_steps++;
            }
        }
        
        /* Memory barrier + power saving */
        __asm__ __volatile__("pause" ::: "memory");
    }
}

/* Bridge for inference engine to call */
void soma_distill_apply_to_logits(float *logits, int vocab_size) {
    distill_apply(&g_distill_global, logits, vocab_size);
}
