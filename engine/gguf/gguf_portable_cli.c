#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gguf_portable.h"

typedef struct {
    FILE *file;
    uint64_t file_size;
} HostFileReader;

static void print_usage(void) {
    printf("Usage: gguf_portable_cli <path-to-model.gguf>\n");
}

static size_t host_reader_read_at(void *user_data, uint64_t offset, void *dst, size_t nbytes) {
    HostFileReader *reader = (HostFileReader *)user_data;
    if (!reader || !reader->file || (!dst && nbytes > 0)) return 0;
    if (_fseeki64(reader->file, (long long)offset, SEEK_SET) != 0) return 0;
    return fread(dst, 1, nbytes, reader->file);
}

static uint64_t host_reader_size(void *user_data) {
    HostFileReader *reader = (HostFileReader *)user_data;
    if (!reader) return 0;
    return reader->file_size;
}

int main(int argc, char **argv) {
    const char *model_path = NULL;
    FILE *file = NULL;
    HostFileReader host_reader;
    LlmkPortableReader reader;
    LlmkPortableGgufSummary summary;
    LlmkPortableTensorPlan tensor_plan;
    LlmkPortableRuntimePlan runtime_plan;
    LlmkPortableStatus status;

    if (argc < 2 || !argv[1] || !argv[1][0]) {
        print_usage();
        return 1;
    }
    model_path = argv[1];

    file = fopen(model_path, "rb");
    if (!file) {
        printf("gguf_portable_cli: failed to open: %s\n", model_path);
        return 2;
    }
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        printf("gguf_portable_cli: failed to seek end\n");
        return 3;
    }
    host_reader.file = file;
    host_reader.file_size = (uint64_t)_ftelli64(file);

    reader.user_data = &host_reader;
    reader.read_at = host_reader_read_at;
    reader.size = host_reader_size;

    memset(&summary, 0, sizeof(summary));
    memset(&tensor_plan, 0, sizeof(tensor_plan));
    memset(&runtime_plan, 0, sizeof(runtime_plan));

    status = llmk_portable_gguf_build_runtime_plan(&reader, &summary, &tensor_plan, &runtime_plan);
    fclose(file);

    if (status != LLMK_PORTABLE_OK) {
        printf("gguf_portable_cli: portable core returned status=%d\n", (int)status);
        return 4;
    }

    printf("model_path=%s\n", model_path);
    printf("file_size=%llu\n", (unsigned long long)summary.file_size);
    printf("architecture=%s\n", summary.architecture[0] ? summary.architecture : "unknown");
    printf("name=%s\n", summary.name[0] ? summary.name : "unknown");
    printf("tokenizer_model=%s\n", summary.tokenizer_model[0] ? summary.tokenizer_model : "unknown");
    printf("tensor_count=%llu\n", (unsigned long long)summary.tensor_count);
    printf("kv_count=%llu\n", (unsigned long long)summary.kv_count);
    printf("context_length=%llu\n", (unsigned long long)summary.context_length);
    printf("embedding_length=%llu\n", (unsigned long long)summary.embedding_length);
    printf("block_count=%llu\n", (unsigned long long)summary.block_count);
    printf("head_count=%llu\n", (unsigned long long)summary.head_count);
    printf("head_count_kv=%llu\n", (unsigned long long)summary.head_count_kv);
    printf("vocab_size=%llu\n", (unsigned long long)summary.vocab_size);

    printf("recognized_tensor_count=%llu\n", (unsigned long long)tensor_plan.recognized_tensor_count);
    printf("detected_layers=%u\n", tensor_plan.detected_layers);
    printf("fully_mapped_llama=%u\n", tensor_plan.fully_mapped_llama);
    printf("q8_0_count=%u\n", tensor_plan.q8_0_count);

    printf("hyperparams_complete=%u\n", runtime_plan.hyperparams_complete);
    printf("float_layout_compatible=%u\n", runtime_plan.float_layout_compatible);
    printf("q8_candidate=%u\n", runtime_plan.q8_candidate);
    printf("float_total_bytes=%llu\n", (unsigned long long)runtime_plan.float_total_bytes);
    return 0;
}
