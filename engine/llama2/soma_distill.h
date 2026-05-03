#pragma once
#include <stdint.h>

/* soma_distill.h — Autonomous In-Situ Training Engine 
 * Trains a lightweight adapter using dreams on a secondary core.
 */

#define SOMA_ADAPTER_SIZE 512   /* Small hidden adapter vector */

typedef struct {
    float   weights[SOMA_ADAPTER_SIZE];
    float   learning_rate;
    int     total_steps;
    int     active;
} SomaDistill;

void distill_init(SomaDistill *d);
void distill_apply(SomaDistill *d, float *logits, int vocab_size);
void distill_update(SomaDistill *d, const uint16_t *prompt, int p_len, const uint16_t *completion, int c_len);
void ap_distill_worker(void);
