#include "layout_plan_support.h"

#include <string.h>

void zero_layout_plan(Llama2LayoutPlanSummary *plan) {
    if (!plan) return;
    memset(plan, 0, sizeof(*plan));
}

void finalize_layout_plan(
    Llama2LayoutPlanSummary *layout,
    const GgufSummary *summary,
    const TensorPlanSummary *tensor_plan
) {
    uint64_t dim = 0;
    uint64_t hidden = 0;
    uint64_t layers = 0;
    uint64_t heads = 0;
    uint64_t kv_heads = 0;
    uint64_t vocab = 0;
    uint64_t seq = 0;
    uint64_t kv_dim = 0;
    uint64_t head_size = 0;
    uint64_t weights_floats = 0;
    uint64_t kv_cache_floats = 0;
    uint64_t freq_cis_floats = 0;

    if (!layout || !summary || !tensor_plan) return;
    zero_layout_plan(layout);

    dim = summary->embedding_length;
    hidden = summary->embedding_length ? (summary->embedding_length * 8u) / 3u : 0;
    layers = summary->block_count;
    heads = summary->head_count;
    kv_heads = summary->head_count_kv ? summary->head_count_kv : summary->head_count;
    vocab = summary->vocab_size;
    seq = summary->context_length;

    if (dim > 0 && hidden > 0 && layers > 0 && heads > 0 && kv_heads > 0 && vocab > 0 && seq > 0) {
        layout->hyperparams_complete = 1;
        layout->shared_classifier = tensor_plan->output_present ? 0u : 1u;
        kv_dim = (dim * kv_heads) / heads;
        head_size = dim / heads;
        weights_floats =
            (vocab * dim) +
            (layers * dim) +
            (layers * dim * dim) +
            (layers * dim * kv_dim) +
            (layers * dim * kv_dim) +
            (layers * dim * dim) +
            (layers * dim) +
            (layers * dim * hidden) +
            (layers * hidden * dim) +
            (layers * dim * hidden) +
            dim;

        freq_cis_floats = seq * head_size;
        if (!layout->shared_classifier) {
            weights_floats += vocab * dim;
        }
        kv_cache_floats = layers * seq * kv_dim * 2u;

        layout->dim = dim;
        layout->hidden_dim = hidden;
        layout->n_layers = layers;
        layout->n_heads = heads;
        layout->n_kv_heads = kv_heads;
        layout->vocab_size = vocab;
        layout->seq_len = seq;
        layout->kv_dim = kv_dim;
        layout->head_size = head_size;
        layout->float_weights_bytes = weights_floats * sizeof(float);
        layout->float_kv_cache_bytes = kv_cache_floats * sizeof(float);
        layout->float_total_bytes = (weights_floats + kv_cache_floats + freq_cis_floats) * sizeof(float);
        layout->float_layout_compatible = tensor_plan->fully_mapped_llama;
        layout->q8_candidate =
            tensor_plan->fully_mapped_llama &&
            tensor_plan->q8_0_count >= (1u + (layout->shared_classifier ? 0u : 1u) + (uint32_t)(layers * 7u));
    }
}
