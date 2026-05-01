
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int dim;
    int hidden_dim;
    int n_layers;
    int n_heads;
    int vocab_size;
    int seq_len;
} DiopConfig;

typedef struct {
    float* tok_emb;      
    float* pos_emb;      
    float* weights;      // Buffer brut pour la gestion simplifiée
} DiopWeights;

typedef struct {
    float* x;            // [dim]
    float* xb;           // [dim]
    float* xb2;          // [dim]
    float* hb;           // [hidden_dim]
    float* q;            // [dim]
    float* k;            // [dim]
    float* v;            // [dim]
    float* att;          // [n_heads, seq_len]
    float* key_cache;    // [n_layers, seq_len, dim]
    float* value_cache;  // [n_layers, seq_len, dim]
} DiopState;

// Fonctions mathématiques optimisées
static void layer_norm(float* out, float* x, float* w, float* b, int dim) {
    float mean = 0.0f;
    for (int i = 0; i < dim; i++) mean += x[i];
    mean /= dim;
    float var = 0.0f;
    for (int i = 0; i < dim; i++) {
        float diff = x[i] - mean;
        var += diff * diff;
    }
    var /= dim;
    float inv_std = 1.0f / sqrtf(var + 1e-5f);
    for (int i = 0; i < dim; i++) {
        out[i] = (x[i] - mean) * inv_std * w[i] + (b ? b[i] : 0.0f);
    }
}

static void matmul(float* out, float* x, float* w, int n, int d) {
    for (int i = 0; i < d; i++) {
        float val = 0.0f;
        for (int j = 0; j < n; j++) {
            val += w[i * n + j] * x[j];
        }
        out[i] = val;
    }
}

static void softmax(float* x, int size) {
    float max_val = x[0];
    for (int i = 1; i < size; i++) if (x[i] > max_val) max_val = x[i];
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }
    for (int i = 0; i < size; i++) x[i] /= sum;
}

// Inférence complète d'un token
void diop_core_forward(DiopConfig* cfg, DiopWeights* w, DiopState* s, int token, int pos) {
    float* x = s->x;
    int dim = cfg->dim;
    int kv_dim = dim; // Simplifié pour l'instant
    int head_size = dim / cfg->n_heads;

    // 1. Embedding + Positional Embedding
    float* t_emb = w->tok_emb + token * dim;
    float* p_emb = w->pos_emb + pos * dim;
    for (int i = 0; i < dim; i++) x[i] = t_emb[i] + p_emb[i];

    // 2. Transformer Layers
    float* w_ptr = w->weights; // On part après les embeddings
    
    for (int l = 0; l < cfg->n_layers; l++) {
        // Attention Norm
        layer_norm(s->xb, x, w_ptr, NULL, dim); w_ptr += dim; 

        // QKV Projections (Layout depend du format d'export de trainer.py)
        matmul(s->q, s->xb, w_ptr, dim, dim); w_ptr += dim * dim;
        matmul(s->k, s->xb, w_ptr, dim, dim); w_ptr += dim * dim;
        matmul(s->v, s->xb, w_ptr, dim, dim); w_ptr += dim * dim;

        // KV Cache
        int loff = l * cfg->seq_len * dim;
        memcpy(s->key_cache + loff + pos * dim, s->k, dim * sizeof(float));
        memcpy(s->value_cache + loff + pos * dim, s->v, dim * sizeof(float));

        // Multi-head Attention
        for (int h = 0; h < cfg->n_heads; h++) {
            float* q_head = s->q + h * head_size;
            float* att_head = s->att + h * cfg->seq_len;
            
            for (int t = 0; t <= pos; t++) {
                float* k_head = s->key_cache + loff + t * dim + h * head_size;
                float score = 0.0f;
                for (int i = 0; i < head_size; i++) score += q_head[i] * k_head[i];
                att_head[t] = score / sqrtf(head_size);
            }
            softmax(att_head, pos + 1);
            
            float* xb_head = s->xb + h * head_size;
            memset(xb_head, 0, head_size * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                float* v_head = s->value_cache + loff + t * dim + h * head_size;
                float a = att_head[t];
                for (int i = 0; i < head_size; i++) xb_head[i] += a * v_head[i];
            }
        }

        // Output projection
        matmul(s->xb2, s->xb, w_ptr, dim, dim); w_ptr += dim * dim;
        for (int i = 0; i < dim; i++) x[i] += s->xb2[i]; // Residual

        // FFN Norm
        layer_norm(s->xb, x, w_ptr, NULL, dim); w_ptr += dim;

        // FFN Layers (w1, w2)
        matmul(s->hb, s->xb, w_ptr, dim, cfg->hidden_dim); w_ptr += dim * cfg->hidden_dim;
        // Activation GELU simplifiée (x * sigmoid(1.702 * x))
        for (int i = 0; i < cfg->hidden_dim; i++) {
            s->hb[i] = s->hb[i] * (1.0f / (1.0f + expf(-1.702f * s->hb[i])));
        }
        matmul(s->xb, s->hb, w_ptr, cfg->hidden_dim, dim); w_ptr += cfg->hidden_dim * dim;
        for (int i = 0; i < dim; i++) x[i] += s->xb[i]; // Residual
    }

    // 3. Final Norm
    layer_norm(x, x, w_ptr, NULL, dim);
}
